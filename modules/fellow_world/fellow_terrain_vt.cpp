/**************************************************************************/
/*  fellow_terrain_vt.cpp                                                 */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#include "fellow_terrain_vt.h"

#include "fellow_terrain_vt.glsl.gen.h"

#include "core/config/engine.h"
#include "core/config/project_settings.h"
#include "core/object/callable_mp.h"
#include "core/os/mutex.h"
#include "core/os/os.h"
#include "scene/resources/texture_rd.h"
#include "servers/rendering/fellow_settings.h"
#include "servers/rendering/rendering_device.h"
#include "servers/rendering/rendering_device_binds.h"
#include "servers/rendering/rendering_server.h"

#include "modules/fellow_fluid/fellow_shared_shaders.h"

LocalVector<FellowTerrainVT::Slot> FellowTerrainVT::slots;
LocalVector<int> FellowTerrainVT::free_slots;
int FellowTerrainVT::slots_per_side = 0;
int FellowTerrainVT::atlas_texels = 0;
int FellowTerrainVT::users = 0;
uint64_t FellowTerrainVT::frame = UINT64_MAX;
int FellowTerrainVT::budget_left = 0;
int FellowTerrainVT::wanted_this_frame = 0;
int FellowTerrainVT::wanted_last_frame = 0;
float FellowTerrainVT::pressure_bias = 0.0f;
Ref<Texture2DRD> FellowTerrainVT::atlas_albedo;
Ref<Texture2DRD> FellowTerrainVT::atlas_normal;
uint64_t FellowTerrainVT::stat_baked = 0;
uint64_t FellowTerrainVT::stat_evicted = 0;
uint64_t FellowTerrainVT::stat_wanted_missing = 0;

// ---------------------------------------------------------------------------------------------------------------------
// The render thread: the atlas's textures, the kernel, the batches to dispatch.

namespace {

struct BakeBatch {
	uint64_t terrain = 0; // its FellowTerrainVT's id: the render thread's sets and buffer for it
	LocalVector<FellowTerrainVT::Job> jobs;
	FellowTerrainVT::BakeParams params;
	// The overview's levels too (its coarsest page's mip; -1: not this time).
	Ref<Texture2DRD> overview_albedo;
	Ref<Texture2DRD> overview_normal;
	int overview_mip = -1;
};

struct RenderState {
	RID albedo;
	RID normal;
	RID shader;
	RID pipeline;
	RID map_sampler;
	RID layer_sampler;
	RID white;
	RID flat_normal;
	bool failed = false;
};

// A terrain's kernel inputs on the render thread (milestone 71: made once, not per bake): the uniform sets writing the
// atlas and each overview level, the views of those levels, the buffer of its layers and jobs; made again when an
// input texture changes or a set is no longer valid (a resource it named was freed).
struct TerrainRT {
	static constexpr int INPUTS = 4 + 2 * FellowTerrainVT::LAYERS;
	RID inputs[INPUTS]; // the RenderingDevice textures the sets were made with
	RID overview[2]; // the overview's textures the views were made of
	RID views[2][FellowTerrainVT::OVERVIEW_MIPS];
	RID buffer;
	int buffer_jobs = 0; // its capacity
	RID set_atlas;
	RID set_levels[FellowTerrainVT::OVERVIEW_MIPS];
};

Mutex vt_mutex;
LocalVector<BakeBatch> vt_batches; // under vt_mutex
RenderState vt_rs; // render thread only
HashMap<uint64_t, TerrainRT> vt_terrains; // render thread only
uint64_t vt_next_id = 1; // main thread

struct BakePush {
	float map_texels[2];
	float world_offset[2];
	float spacing;
	float texels_per_metre;
	float height_blend;
	float macro_variation;
	int32_t job_base; // the first job of the dispatch (its z adds)
	int32_t out_size; // 0: pages into the atlas's slots; else an overview level of this size (no border)
	int32_t pad[2];
};
static_assert(sizeof(BakePush) == 48);

RID vt_texture(RD *p_rd, int p_size, const char *p_name, int p_mips = 1) {
	RD::TextureFormat tf;
	tf.width = p_size;
	tf.height = p_size;
	tf.mipmaps = p_mips;
	tf.format = RD::DATA_FORMAT_R8G8B8A8_UNORM;
	tf.usage_bits = RD::TEXTURE_USAGE_STORAGE_BIT | RD::TEXTURE_USAGE_SAMPLING_BIT | RD::TEXTURE_USAGE_CAN_COPY_FROM_BIT | RD::TEXTURE_USAGE_CAN_COPY_TO_BIT;
	const RID t = p_rd->texture_create(tf, RD::TextureView());
	if (t.is_valid()) {
		p_rd->set_resource_name(t, p_name);
		p_rd->texture_clear(t, Color(0.5f, 0.5f, 0.5f, 1.0f), 0, p_mips, 0, 1);
	}
	return t;
}

RID vt_constant_texture(RD *p_rd, uint32_t p_rgba) {
	RD::TextureFormat tf;
	tf.width = 4;
	tf.height = 4;
	tf.format = RD::DATA_FORMAT_R8G8B8A8_UNORM;
	tf.usage_bits = RD::TEXTURE_USAGE_SAMPLING_BIT | RD::TEXTURE_USAGE_CAN_UPDATE_BIT;
	Vector<uint8_t> data;
	data.resize(4 * 4 * 4);
	for (int i = 0; i < 16; i++) {
		memcpy(data.ptrw() + i * 4, &p_rgba, 4);
	}
	Vector<Vector<uint8_t>> layers;
	layers.push_back(data);
	return p_rd->texture_create(tf, RD::TextureView(), layers);
}

} // namespace

static void _vt_rt_create_atlas(int p_size, Ref<Texture2DRD> p_albedo, Ref<Texture2DRD> p_normal) {
	RD *rd = RenderingServer::get_singleton()->get_rendering_device();
	if (!rd) {
		return;
	}
	if (vt_rs.albedo.is_null()) {
		vt_rs.albedo = vt_texture(rd, p_size, "FellowTerrain VT albedo");
		vt_rs.normal = vt_texture(rd, p_size, "FellowTerrain VT normal");
	}
	if (vt_rs.albedo.is_valid() && p_albedo.is_valid()) {
		p_albedo->set_texture_rd_rid(vt_rs.albedo);
		p_normal->set_texture_rd_rid(vt_rs.normal);
	}
}

static void _vt_rt_create_overview(Ref<Texture2DRD> p_albedo, Ref<Texture2DRD> p_normal) {
	RD *rd = RenderingServer::get_singleton()->get_rendering_device();
	if (!rd) {
		return;
	}
	const RID a = vt_texture(rd, FellowTerrainVT::PAGE, "FellowTerrain VT overview albedo", FellowTerrainVT::OVERVIEW_MIPS);
	const RID n = vt_texture(rd, FellowTerrainVT::PAGE, "FellowTerrain VT overview normal", FellowTerrainVT::OVERVIEW_MIPS);
	if (a.is_valid() && n.is_valid()) {
		p_albedo->set_texture_rd_rid(a);
		p_normal->set_texture_rd_rid(n);
	}
}

static void _vt_rt_free_overview(Ref<Texture2DRD> p_albedo, Ref<Texture2DRD> p_normal) {
	RD *rd = RenderingServer::get_singleton()->get_rendering_device();
	if (!rd) {
		return;
	}
	for (const Ref<Texture2DRD> &t : { p_albedo, p_normal }) {
		const RID r = t->get_texture_rd_rid();
		if (r.is_valid() && rd->texture_is_valid(r)) {
			rd->free_rid(r);
		}
	}
}

static void _vt_rt_free_sets(RD *p_rd, TerrainRT &r_t) {
	for (RID *r : { &r_t.set_atlas }) {
		if (r->is_valid() && p_rd->uniform_set_is_valid(*r)) {
			p_rd->free_rid(*r);
		}
		*r = RID();
	}
	for (RID &r : r_t.set_levels) {
		if (r.is_valid() && p_rd->uniform_set_is_valid(r)) {
			p_rd->free_rid(r);
		}
		r = RID();
	}
}

static void _vt_rt_free_terrain(RD *p_rd, TerrainRT &r_t) {
	_vt_rt_free_sets(p_rd, r_t);
	for (int a = 0; a < 2; a++) {
		for (RID &v : r_t.views[a]) {
			if (v.is_valid() && p_rd->texture_is_valid(v)) {
				p_rd->free_rid(v);
			}
			v = RID();
		}
		r_t.overview[a] = RID();
	}
	if (r_t.buffer.is_valid()) {
		p_rd->free_rid(r_t.buffer);
		r_t.buffer = RID();
	}
	r_t.buffer_jobs = 0;
}

static void _vt_rt_forget(uint64_t p_terrain) {
	RD *rd = RenderingServer::get_singleton()->get_rendering_device();
	TerrainRT *t = vt_terrains.getptr(p_terrain);
	if (rd && t) {
		_vt_rt_free_terrain(rd, *t);
	}
	vt_terrains.erase(p_terrain);
}

static void _vt_rt_free_atlas() {
	RD *rd = RenderingServer::get_singleton()->get_rendering_device();
	if (!rd) {
		return;
	}
	MutexLock lock(vt_mutex);
	vt_batches.clear();
	for (KeyValue<uint64_t, TerrainRT> &E : vt_terrains) {
		_vt_rt_free_terrain(rd, E.value);
	}
	vt_terrains.clear();
	for (RID *r : { &vt_rs.albedo, &vt_rs.normal, &vt_rs.map_sampler, &vt_rs.layer_sampler, &vt_rs.white, &vt_rs.flat_normal }) {
		if (r->is_valid()) {
			rd->free_rid(*r);
			*r = RID();
		}
	}
	if (vt_rs.shader.is_valid()) {
		rd->free_rid(vt_rs.shader); // its pipeline with it
		vt_rs.shader = RID();
		vt_rs.pipeline = RID();
	}
}

static bool _vt_rt_ensure_kernel(RD *p_rd) {
	if (vt_rs.pipeline.is_valid()) {
		return true;
	}
	if (vt_rs.failed) {
		return false;
	}
	Ref<RDShaderFile> file = FellowSharedShaders::file(fellow_terrain_vt_shader_glsl, "fellow_terrain_vt");
	if (file.is_valid()) {
		vt_rs.shader = p_rd->shader_create_from_spirv(file->get_spirv_stages(), "FellowTerrain VT bake");
	}
	if (vt_rs.shader.is_null()) {
		vt_rs.failed = true;
		ERR_FAIL_V_MSG(false, "FellowTerrain3D: the virtual texture's bake kernel could not be made.");
	}
	vt_rs.pipeline = p_rd->compute_pipeline_create(vt_rs.shader);
	RD::SamplerState ss;
	ss.mag_filter = RD::SAMPLER_FILTER_LINEAR;
	ss.min_filter = RD::SAMPLER_FILTER_LINEAR;
	ss.mip_filter = RD::SAMPLER_FILTER_LINEAR;
	ss.repeat_u = RD::SAMPLER_REPEAT_MODE_CLAMP_TO_EDGE;
	ss.repeat_v = RD::SAMPLER_REPEAT_MODE_CLAMP_TO_EDGE;
	ss.max_lod = 1000.0f;
	vt_rs.map_sampler = p_rd->sampler_create(ss);
	ss.repeat_u = RD::SAMPLER_REPEAT_MODE_REPEAT;
	ss.repeat_v = RD::SAMPLER_REPEAT_MODE_REPEAT;
	vt_rs.layer_sampler = p_rd->sampler_create(ss);
	vt_rs.white = vt_constant_texture(p_rd, 0xFFFFFFFF);
	vt_rs.flat_normal = vt_constant_texture(p_rd, 0xFFFF8080); // (0.5, 0.5, 1, 1): no tilt
	return true;
}

static RID _vt_rt_texture(RID p_rs_texture, bool p_srgb, RID p_fallback) {
	if (p_rs_texture.is_null()) {
		return p_fallback;
	}
	const RID t = RenderingServer::get_singleton()->texture_get_rd_texture(p_rs_texture, p_srgb);
	return t.is_valid() ? t : p_fallback;
}

// The uniform set writing p_albedo / p_normal (the atlas, or an overview level's views) from a terrain's inputs.
static RID _vt_rt_make_set(RD *p_rd, const TerrainRT &p_t, RID p_albedo, RID p_normal) {
	const int L = FellowTerrainVT::LAYERS;
	Vector<RD::Uniform> u;
	u.push_back(RD::Uniform(RD::UNIFORM_TYPE_IMAGE, 0, p_albedo));
	u.push_back(RD::Uniform(RD::UNIFORM_TYPE_IMAGE, 1, p_normal));
	u.push_back(RD::Uniform(RD::UNIFORM_TYPE_SAMPLER, 2, vt_rs.map_sampler));
	u.push_back(RD::Uniform(RD::UNIFORM_TYPE_SAMPLER, 3, vt_rs.layer_sampler));
	for (int i = 0; i < 4; i++) {
		u.push_back(RD::Uniform(RD::UNIFORM_TYPE_TEXTURE, 4 + i, p_t.inputs[i]));
	}
	RD::Uniform ua;
	ua.uniform_type = RD::UNIFORM_TYPE_TEXTURE;
	ua.binding = 8;
	RD::Uniform un;
	un.uniform_type = RD::UNIFORM_TYPE_TEXTURE;
	un.binding = 9;
	for (int i = 0; i < L; i++) {
		ua.append_id(p_t.inputs[4 + i]);
		un.append_id(p_t.inputs[4 + L + i]);
	}
	u.push_back(ua);
	u.push_back(un);
	u.push_back(RD::Uniform(RD::UNIFORM_TYPE_STORAGE_BUFFER, 10, p_t.buffer));
	return p_rd->uniform_set_create(u, vt_rs.shader, 0);
}

// A batch's terrain ready to dispatch: its inputs current, its buffer large enough (holding this batch's data), its
// sets made; false if something could not be made.
static bool _vt_rt_prepare(RD *p_rd, const BakeBatch &p_b, RID p_ov_albedo, RID p_ov_normal, const Vector<uint8_t> &p_data, int p_jobs) {
	const FellowTerrainVT::BakeParams &p = p_b.params;
	const int L = FellowTerrainVT::LAYERS;
	TerrainRT &t = vt_terrains[p_b.terrain];
	RID inputs[TerrainRT::INPUTS];
	inputs[0] = _vt_rt_texture(p.heightmap, false, vt_rs.white);
	inputs[1] = _vt_rt_texture(p.normalmap, false, vt_rs.flat_normal);
	inputs[2] = _vt_rt_texture(p.splat[0], false, vt_rs.white);
	inputs[3] = _vt_rt_texture(p.splat[1], false, vt_rs.white);
	for (int i = 0; i < L; i++) {
		inputs[4 + i] = _vt_rt_texture(p.albedo[i], true, vt_rs.white);
		inputs[4 + L + i] = _vt_rt_texture(p.normal[i], false, vt_rs.flat_normal);
	}
	bool remake = false;
	for (int i = 0; i < TerrainRT::INPUTS; i++) {
		if (inputs[i] != t.inputs[i]) {
			t.inputs[i] = inputs[i];
			remake = true;
		}
	}
	// The buffer: the layers' parameters and up to this many jobs (the budget's and the overview's, grown if needed).
	const int needed = p_jobs;
	if (t.buffer.is_null() || t.buffer_jobs < needed) {
		if (t.buffer.is_valid()) {
			p_rd->free_rid(t.buffer);
		}
		t.buffer_jobs = MAX(needed, 3 * L + 64 + FellowTerrainVT::OVERVIEW_MIPS);
		Vector<uint8_t> init = p_data;
		init.resize(t.buffer_jobs * 16);
		t.buffer = p_rd->storage_buffer_create(t.buffer_jobs * 16, init);
		remake = true;
	} else {
		p_rd->buffer_update(t.buffer, 0, p_data.size(), p_data.ptr());
	}
	if (t.buffer.is_null()) {
		return false;
	}
	// The overview's level views (its textures are made once per terrain).
	if (p_ov_albedo.is_valid() && (t.overview[0] != p_ov_albedo || t.overview[1] != p_ov_normal)) {
		for (int a = 0; a < 2; a++) {
			for (RID &v : t.views[a]) {
				if (v.is_valid() && p_rd->texture_is_valid(v)) {
					p_rd->free_rid(v);
				}
				v = RID();
			}
		}
		t.overview[0] = p_ov_albedo;
		t.overview[1] = p_ov_normal;
		for (int k = 0; k < FellowTerrainVT::OVERVIEW_MIPS; k++) {
			t.views[0][k] = p_rd->texture_create_shared_from_slice(RD::TextureView(), p_ov_albedo, 0, k, 1, RD::TEXTURE_SLICE_2D);
			t.views[1][k] = p_rd->texture_create_shared_from_slice(RD::TextureView(), p_ov_normal, 0, k, 1, RD::TEXTURE_SLICE_2D);
		}
		remake = true;
	}
	if (!remake && t.set_atlas.is_valid() && !p_rd->uniform_set_is_valid(t.set_atlas)) {
		remake = true;
	}
	if (remake) {
		_vt_rt_free_sets(p_rd, t);
	}
	if (t.set_atlas.is_null()) {
		t.set_atlas = _vt_rt_make_set(p_rd, t, vt_rs.albedo, vt_rs.normal);
	}
	if (p_ov_albedo.is_valid()) {
		for (int k = 0; k < FellowTerrainVT::OVERVIEW_MIPS; k++) {
			if (t.set_levels[k].is_null() && t.views[0][k].is_valid() && t.views[1][k].is_valid()) {
				t.set_levels[k] = _vt_rt_make_set(p_rd, t, t.views[0][k], t.views[1][k]);
			}
		}
	}
	return t.set_atlas.is_valid();
}

static void _vt_rt_dispatch(RD *p_rd, const FellowTerrainVT::BakeParams &p_params, RID p_set, int p_job_base, int p_jobs, int p_out_size) {
	BakePush push = {};
	push.map_texels[0] = p_params.map_texels.x;
	push.map_texels[1] = p_params.map_texels.y;
	push.world_offset[0] = p_params.world_offset.x;
	push.world_offset[1] = p_params.world_offset.y;
	push.spacing = p_params.spacing;
	push.texels_per_metre = p_params.texels_per_metre;
	push.height_blend = p_params.height_blend;
	push.macro_variation = p_params.macro_variation;
	push.job_base = p_job_base;
	push.out_size = p_out_size;
	RD::ComputeListID cl = p_rd->compute_list_begin();
	p_rd->compute_list_bind_compute_pipeline(cl, vt_rs.pipeline);
	p_rd->compute_list_bind_uniform_set(cl, p_set, 0);
	p_rd->compute_list_set_push_constant(cl, &push, sizeof(push));
	const uint32_t groups = ((p_out_size > 0 ? p_out_size : FellowTerrainVT::PADDED) + 7) / 8;
	p_rd->compute_list_dispatch(cl, groups, groups, p_jobs);
	p_rd->compute_list_end();
}

static void _vt_rt_flush() {
	LocalVector<BakeBatch> batches;
	{
		MutexLock lock(vt_mutex);
		batches = vt_batches;
		vt_batches.clear();
	}
	RD *rd = RenderingServer::get_singleton()->get_rendering_device();
	if (batches.is_empty() || !rd || vt_rs.albedo.is_null() || !_vt_rt_ensure_kernel(rd)) {
		return;
	}
	// The bakes' GPU time in a timestamp capture (FellowBench --profile=1): the interval to the closing "<" marker.
	rd->capture_timestamp("FellowTerrain VT bake");
	for (const BakeBatch &b : batches) {
		const FellowTerrainVT::BakeParams &p = b.params;
		const uint64_t t0 = OS::get_singleton()->get_ticks_usec();
		// The overview's textures (made on this thread before the first batch that names them).
		const RID ov_albedo = b.overview_albedo.is_valid() ? b.overview_albedo->get_texture_rd_rid() : RID();
		const RID ov_normal = b.overview_normal.is_valid() ? b.overview_normal->get_texture_rd_rid() : RID();
		const bool have_overview = ov_albedo.is_valid() && ov_normal.is_valid() && rd->texture_is_valid(ov_albedo) && rd->texture_is_valid(ov_normal);
		const int levels = have_overview && b.overview_mip >= 0 ? FellowTerrainVT::OVERVIEW_MIPS : 0;
		// The layers' parameters, then the jobs (the pages', then a job per overview level).
		const int L = FellowTerrainVT::LAYERS;
		Vector<uint8_t> data;
		data.resize((3 * L + b.jobs.size() + levels) * 16);
		float *f = reinterpret_cast<float *>(data.ptrw());
		for (int i = 0; i < L; i++) {
			const Vector4 *v[3] = { &p.layer_color[i], &p.layer_params[i], &p.layer_flags[i] };
			for (int a = 0; a < 3; a++) {
				f[(a * L + i) * 4 + 0] = v[a]->x;
				f[(a * L + i) * 4 + 1] = v[a]->y;
				f[(a * L + i) * 4 + 2] = v[a]->z;
				f[(a * L + i) * 4 + 3] = v[a]->w;
			}
		}
		if (!b.jobs.is_empty()) {
			memcpy(data.ptrw() + 3 * L * 16, b.jobs.ptr(), b.jobs.size() * 16);
		}
		for (int k = 0; k < levels; k++) {
			const FellowTerrainVT::Job j = { 0, 0, b.overview_mip + k, 0 };
			memcpy(data.ptrw() + (3 * L + b.jobs.size() + k) * 16, &j, 16);
		}
		if (_vt_rt_prepare(rd, b, have_overview ? ov_albedo : RID(), have_overview ? ov_normal : RID(), data, 3 * L + b.jobs.size() + levels)) {
			const TerrainRT &t = vt_terrains[b.terrain];
			if (!b.jobs.is_empty()) {
				_vt_rt_dispatch(rd, p, t.set_atlas, 0, b.jobs.size(), 0);
			}
			for (int k = 0; k < levels; k++) {
				if (t.set_levels[k].is_valid()) {
					_vt_rt_dispatch(rd, p, t.set_levels[k], b.jobs.size() + k, 1, FellowTerrainVT::PAGE >> k);
				}
			}
		}
		Fellow::stats().terrain_vt_dispatch_usec.add(OS::get_singleton()->get_ticks_usec() - t0);
		Fellow::stats().terrain_vt_pages_baked.add(b.jobs.size());
	}
	rd->capture_timestamp("<FellowTerrain VT bake");
}

void FellowTerrainVT::bake(const LocalVector<Job> &p_jobs, const BakeParams &p_params) {
	if (p_jobs.is_empty() && !overview_due) {
		return;
	}
	{
		MutexLock lock(vt_mutex);
		BakeBatch b;
		b.terrain = id;
		b.jobs = p_jobs;
		b.params = p_params;
		b.overview_albedo = overview_albedo;
		b.overview_normal = overview_normal;
		if (overview_due) {
			b.overview_mip = mips - 1;
			overview_due = false;
			overview_baked++;
		}
		vt_batches.push_back(b);
	}
	RenderingServer::get_singleton()->call_on_render_thread(callable_mp_static(&_vt_rt_flush));
	Fellow::request_redraw();
}

// ---------------------------------------------------------------------------------------------------------------------
// The atlas's slots (main thread).

void FellowTerrainVT::add_user() {
	users++;
	if (users > 1 || RenderingServer::get_singleton()->get_rendering_device() == nullptr) {
		return;
	}
	const int size = MAX(int(GLOBAL_GET("rendering/fellow/terrain/virtual_texture_atlas_size")), PADDED * 2);
	slots_per_side = MIN(size / PADDED, 255);
	atlas_texels = slots_per_side * PADDED;
	slots.clear();
	slots.resize(slots_per_side * slots_per_side);
	free_slots.clear();
	for (int i = int(slots.size()) - 1; i >= 0; i--) {
		free_slots.push_back(i);
	}
	atlas_albedo.instantiate();
	atlas_normal.instantiate();
	RenderingServer::get_singleton()->call_on_render_thread(callable_mp_static(&_vt_rt_create_atlas).bind(atlas_texels, atlas_albedo, atlas_normal));
}

void FellowTerrainVT::remove_user() {
	ERR_FAIL_COND(users <= 0);
	users--;
	if (users > 0) {
		return;
	}
	slots.clear();
	free_slots.clear();
	slots_per_side = 0;
	if (atlas_albedo.is_valid()) {
		atlas_albedo->set_texture_rd_rid(RID());
		atlas_normal->set_texture_rd_rid(RID());
		atlas_albedo.unref();
		atlas_normal.unref();
	}
	if (RenderingServer::get_singleton() && RenderingServer::get_singleton()->get_rendering_device()) {
		RenderingServer::get_singleton()->call_on_render_thread(callable_mp_static(&_vt_rt_free_atlas));
	}
}

Ref<Texture2D> FellowTerrainVT::get_atlas_albedo() {
	return atlas_albedo;
}

Ref<Texture2D> FellowTerrainVT::get_atlas_normal() {
	return atlas_normal;
}

void FellowTerrainVT::_begin_frame() {
	const uint64_t f = Engine::get_singleton()->get_process_frames();
	if (f == frame) {
		return;
	}
	frame = f;
	budget_left = MAX(1, int(GLOBAL_GET_CACHED(int, "rendering/fellow/terrain/virtual_texture_pages_per_frame")));
	wanted_last_frame = wanted_this_frame;
	wanted_this_frame = 0;
	// The atlas nearly full: every terrain's pages a little coarser, until it is not (hysteresis between 60 and 90 %).
	const int cap = int(slots.size());
	if (cap > 0) {
		if (wanted_last_frame > cap * 9 / 10) {
			pressure_bias = MIN(pressure_bias + 0.25f, 4.0f);
		} else if (wanted_last_frame < cap * 6 / 10) {
			pressure_bias = MAX(pressure_bias - 0.25f, 0.0f);
		}
	}
}

// A free slot, or the one whose page was wanted longest ago (not this frame); -1 if every slot is wanted now.
int FellowTerrainVT::_take_slot(FellowTerrainVT *p_for) {
	int s = -1;
	if (!free_slots.is_empty()) {
		s = free_slots[free_slots.size() - 1];
		free_slots.resize(free_slots.size() - 1);
	} else {
		uint64_t oldest = frame;
		for (uint32_t i = 0; i < slots.size(); i++) {
			if (slots[i].used < oldest) {
				oldest = slots[i].used;
				s = int(i);
			}
		}
		if (s < 0) {
			return -1;
		}
		if (FellowTerrainVT *owner = slots[s].owner) {
			owner->_evict(slots[s].key);
			stat_evicted++;
			if (owner != p_for) {
				// Its table before the slot is baked again (this frame): never a frame drawing another page there.
				owner->upload_page_table();
			}
		}
	}
	slots[s].owner = p_for;
	slots[s].used = frame;
	return s;
}

Dictionary FellowTerrainVT::get_cache_stats() {
	Dictionary d;
	d["slots"] = int(slots.size());
	d["free_slots"] = int(free_slots.size());
	d["atlas_texels"] = atlas_texels;
	d["terrains"] = users;
	d["atlas_wanted"] = wanted_last_frame; // not "wanted": a terrain's own count (FellowTerrain3D) has that key
	d["pressure_bias"] = pressure_bias;
	d["baked"] = int64_t(stat_baked);
	d["evicted"] = int64_t(stat_evicted);
	d["wanted_missing"] = int64_t(stat_wanted_missing);
	return d;
}

// ---------------------------------------------------------------------------------------------------------------------
// A terrain's virtual texture.

FellowTerrainVT::FellowTerrainVT() {
	id = vt_next_id++;
	add_user();
}

FellowTerrainVT::~FellowTerrainVT() {
	configure(Vector2(), texels_per_metre);
	const bool rd = RenderingServer::get_singleton() && RenderingServer::get_singleton()->get_rendering_device();
	if (rd) {
		// Its sets, views and buffer first (they name the overview's textures).
		RenderingServer::get_singleton()->call_on_render_thread(callable_mp_static(&_vt_rt_forget).bind(id));
	}
	if (overview_albedo.is_valid()) {
		overview_albedo->set_texture_rd_rid(RID());
		overview_normal->set_texture_rd_rid(RID());
		if (rd) {
			RenderingServer::get_singleton()->call_on_render_thread(callable_mp_static(&_vt_rt_free_overview).bind(overview_albedo, overview_normal));
		}
	}
	remove_user();
}

void FellowTerrainVT::configure(const Vector2 &p_extent, float p_texels_per_metre) {
	// Every slot back.
	for (const KeyValue<uint64_t, int> &E : resident) {
		if (E.value >= 0 && E.value < int(slots.size()) && slots[E.value].owner == this) {
			slots[E.value] = Slot();
			free_slots.push_back(E.value);
		}
	}
	resident.clear();
	stale.clear();
	visited = false;
	wanted_slots.clear();
	table.clear();
	table_image.unref();
	table_texture.unref();
	height_ranges.clear();
	mips = 0;
	extent = p_extent;
	texels_per_metre = MAX(p_texels_per_metre, 0.01f);
	if (p_extent.x <= 0.0f || p_extent.y <= 0.0f || slots.is_empty()) {
		return;
	}
	const float page_m = float(PAGE) / texels_per_metre;
	pages0 = Vector2i(Math::next_power_of_2(uint32_t(MAX(1.0f, Math::ceil(p_extent.x / page_m)))), Math::next_power_of_2(uint32_t(MAX(1.0f, Math::ceil(p_extent.y / page_m)))));
	// The table's mips down to one page (a 2D texture's mips go down to 1 x 1).
	mips = int(Math::log2(double(MAX(pages0.x, pages0.y)))) + 1;
	if (mips > MAX_MIPS) {
		// Too fine for the map: fewer texels per metre.
		const int over = mips - MAX_MIPS;
		texels_per_metre /= float(1 << over);
		configure(p_extent, texels_per_metre);
		return;
	}
	table.resize(mips);
	int64_t bytes = 0;
	for (int m = 0; m < mips; m++) {
		const Vector2i n = _pages(m);
		table[m].resize(n.x * n.y);
		memset(table[m].ptr(), 0, n.x * n.y * 4);
		bytes += int64_t(n.x) * n.y * 4;
		table_dirty[m] = Rect2i();
	}
	Vector<uint8_t> data;
	data.resize(bytes);
	memset(data.ptrw(), 0, bytes);
	table_image = Image::create_from_data(pages0.x, pages0.y, mips > 1, Image::FORMAT_RGBA8, data);
	table_texture = ImageTexture::create_from_image(table_image);
	height_mip = MIN(HEIGHT_MIP, mips - 1);
	const Vector2i hn = _pages(height_mip);
	height_ranges.resize(hn.x * hn.y);
	for (Vector2 &r : height_ranges) {
		r = Vector2(0.0f, 0.0f);
	}
	if (overview_albedo.is_null()) {
		overview_albedo.instantiate();
		overview_normal.instantiate();
		RenderingServer::get_singleton()->call_on_render_thread(callable_mp_static(&_vt_rt_create_overview).bind(overview_albedo, overview_normal));
	}
	overview_stale = true;
}

Rect2 FellowTerrainVT::_page_rect(int p_mip, int p_x, int p_z) const {
	const float w = float(PAGE << p_mip) / texels_per_metre;
	return Rect2(p_x * w, p_z * w, w, w);
}

void FellowTerrainVT::update_heights(const float *p_heights, int p_w, int p_h, float p_spacing, const Rect2i &p_texels) {
	if (!is_configured() || !p_heights || p_w < 1 || p_h < 1) {
		return;
	}
	const Vector2i hn = _pages(height_mip);
	const float page_m = float(PAGE << height_mip) / texels_per_metre;
	// The pages over the texels (all of them for an empty rectangle).
	int px0 = 0, pz0 = 0, px1 = hn.x - 1, pz1 = hn.y - 1;
	if (p_texels.has_area()) {
		px0 = CLAMP(int(Math::floor((p_texels.position.x - 1) * p_spacing / page_m)), 0, hn.x - 1);
		pz0 = CLAMP(int(Math::floor((p_texels.position.y - 1) * p_spacing / page_m)), 0, hn.y - 1);
		px1 = CLAMP(int(Math::floor((p_texels.get_end().x + 1) * p_spacing / page_m)), 0, hn.x - 1);
		pz1 = CLAMP(int(Math::floor((p_texels.get_end().y + 1) * p_spacing / page_m)), 0, hn.y - 1);
	}
	for (int pz = pz0; pz <= pz1; pz++) {
		for (int px = px0; px <= px1; px++) {
			const int x0 = CLAMP(int(Math::floor(px * page_m / p_spacing)), 0, p_w - 1);
			const int z0 = CLAMP(int(Math::floor(pz * page_m / p_spacing)), 0, p_h - 1);
			const int x1 = CLAMP(int(Math::ceil((px + 1) * page_m / p_spacing)), 0, p_w - 1);
			const int z1 = CLAMP(int(Math::ceil((pz + 1) * page_m / p_spacing)), 0, p_h - 1);
			float lo = 1e30f, hi = -1e30f;
			for (int z = z0; z <= z1; z++) {
				const float *row = p_heights + int64_t(z) * p_w;
				for (int x = x0; x <= x1; x++) {
					lo = MIN(lo, row[x]);
					hi = MAX(hi, row[x]);
				}
			}
			height_ranges[pz * hn.x + px] = lo <= hi ? Vector2(lo, hi) : Vector2();
		}
	}
	height_all = Vector2(1e30f, -1e30f);
	for (const Vector2 &r : height_ranges) {
		height_all.x = MIN(height_all.x, r.x);
		height_all.y = MAX(height_all.y, r.y);
	}
}

Vector2 FellowTerrainVT::_page_heights(int p_mip, int p_x, int p_z) const {
	if (height_ranges.is_empty()) {
		return Vector2();
	}
	const Vector2i hn = _pages(height_mip);
	if (p_mip <= height_mip) {
		const int s = height_mip - p_mip;
		const int x = MIN(p_x >> s, hn.x - 1);
		const int z = MIN(p_z >> s, hn.y - 1);
		return height_ranges[z * hn.x + x];
	}
	// A coarser page: its HEIGHT_MIP pages' range.
	const int s = p_mip - height_mip;
	Vector2 r(1e30f, -1e30f);
	const int x0 = p_x << s, z0 = p_z << s;
	const int n = 1 << s;
	for (int z = z0; z < MIN(z0 + n, hn.y); z++) {
		for (int x = x0; x < MIN(x0 + n, hn.x); x++) {
			const Vector2 &h = height_ranges[z * hn.x + x];
			r.x = MIN(r.x, h.x);
			r.y = MAX(r.y, h.y);
		}
	}
	return r.x <= r.y ? r : height_all;
}

void FellowTerrainVT::_want(uint64_t p_key, float p_priority) {
	wanted.push_back(p_key);
	if (const int *s = resident.getptr(p_key)) {
		slots[*s].used = frame;
		if (stale.has(p_key)) {
			missing.push_back({ p_key, p_priority });
		}
	} else {
		missing.push_back({ p_key, p_priority });
	}
}

// The quadtree: a page is wanted, and its children too where the view needs finer texels than its own.
void FellowTerrainVT::_visit(const View &p_view, float p_bias, int p_mip, int p_x, int p_z) {
	const Rect2 r = _page_rect(p_mip, p_x, p_z);
	if (r.position.x >= extent.x || r.position.y >= extent.y) {
		return; // beyond the map (the table's padding to a power of two)
	}
	const Vector2 h = _page_heights(p_mip, p_x, p_z);
	const Vector3 lo(r.position.x, h.x, r.position.y);
	const Vector3 hi(MIN(r.get_end().x, extent.x), h.y, MIN(r.get_end().y, extent.y));
	const Vector3 c = p_view.camera;
	const Vector3 d(MAX(MAX(lo.x - c.x, c.x - hi.x), 0.0f), MAX(MAX(lo.y - c.y, c.y - hi.y), 0.0f), MAX(MAX(lo.z - c.z, c.z - hi.z), 0.0f));
	const float dist = d.length();
	bool inside = true;
	for (const Plane &pl : p_view.frustum) {
		// The box's corner farthest along the plane's normal outside: the box outside.
		const Vector3 v(pl.normal.x < 0.0f ? hi.x : lo.x, pl.normal.y < 0.0f ? hi.y : lo.y, pl.normal.z < 0.0f ? hi.z : lo.z);
		if (pl.distance_to(v) > 0.0f) {
			inside = false;
			break;
		}
	}
	// Coarsest and nearest first: a missing page's ancestors are baked before it. The coarsest mip is the overview.
	if (p_mip < mips - 1) {
		_want(_key(p_mip, p_x, p_z), float(mips - p_mip) * 1e6f + dist);
	}
	if (p_mip == 0) {
		return;
	}
	// The mip whose texel is as wide as a pixel there (a page out of view two mips coarser).
	const float texel_needed = MAX(dist, 0.05f) * p_view.pixel_angle;
	float want = Math::log2(texel_needed * texels_per_metre) + p_bias + (inside ? 0.0f : 2.0f);
	if (want < float(p_mip)) {
		const int m = p_mip - 1;
		const Vector2i n = _pages(m);
		for (int dz = 0; dz < 2; dz++) {
			for (int dx = 0; dx < 2; dx++) {
				const int x = p_x * 2 + dx, z = p_z * 2 + dz;
				if (x < n.x && z < n.y) {
					_visit(p_view, p_bias, m, x, z);
				}
			}
		}
	}
}

// A frame's start for this terrain (once a frame): the overview made due if stale; with p_may_keep, true when the last
// visit's choice is kept (milestone 71: nothing missing or stale, the same bias, the camera within 1 m, or 3 % of its
// distance to the map, of where it was, and not this terrain's frame of the REVISIT_FRAMES), its slots then kept recent.
bool FellowTerrainVT::_start(bool p_may_keep, const Vector3 &p_camera, float p_lod_bias) {
	_begin_frame();
	if (start_frame == frame) {
		return false;
	}
	start_frame = frame;
	updates++;
	// The overview first: what every fragment falls back on.
	if (overview_stale && budget_left > 0) {
		overview_stale = false;
		overview_due = true;
		budget_left--;
	}
	const float bias = p_lod_bias + pressure_bias;
	if (p_may_keep && visited && last_missing == 0 && stale.is_empty() && bias == visit_bias && (frame + id) % REVISIT_FRAMES != 0 &&
			p_camera.distance_to(visit_camera) < MAX(1.0f, 0.03f * visit_near)) {
		for (int s : wanted_slots) {
			slots[s].used = frame;
		}
		wanted_this_frame += last_wanted;
		return true;
	}
	return false;
}

bool FellowTerrainVT::keep(const Vector3 &p_camera, float p_lod_bias) {
	if (!is_configured() || slots.is_empty()) {
		return true;
	}
	return _start(true, p_camera, p_lod_bias);
}

void FellowTerrainVT::update(const View &p_view, float p_lod_bias, LocalVector<Job> &r_jobs) {
	r_jobs.clear();
	if (!is_configured() || slots.is_empty()) {
		return;
	}
	_start(false, p_view.camera, p_lod_bias); // (nothing if keep() started this frame)
	const float bias = p_lod_bias + pressure_bias;
	visits++;
	wanted.clear();
	missing.clear();
	_visit(p_view, bias, mips - 1, 0, 0);
	wanted_this_frame += wanted.size();
	last_wanted = wanted.size();
	missing.sort();
	int left = MIN(budget_left, int(missing.size()));
	int done = 0;
	for (const Missing &m : missing) {
		if (done >= left) {
			break;
		}
		const int mip = _key_mip(m.key), x = _key_x(m.key), z = _key_z(m.key);
		int s;
		if (const int *rs = resident.getptr(m.key)) {
			s = *rs; // stale: baked again in its slot
			stale.erase(m.key);
		} else {
			s = _take_slot(this);
			if (s < 0) {
				break; // every slot wanted this frame
			}
			slots[s].key = m.key;
			resident.insert(m.key, s);
			_set_entry_tree(mip, x, z);
		}
		Job j;
		j.slot_x = s % slots_per_side;
		j.slot_y = s / slots_per_side;
		j.mip = mip;
		j.page = x | (z << 16);
		r_jobs.push_back(j);
		done++;
	}
	budget_left -= done;
	baked += done;
	stat_baked += done;
	last_missing = int(missing.size()) - done;
	stat_wanted_missing += last_missing;
	// The slots this choice keeps recent until the next visit.
	wanted_slots.clear();
	for (const uint64_t k : wanted) {
		if (const int *rs = resident.getptr(k)) {
			wanted_slots.push_back(*rs);
		}
	}
	visited = true;
	visit_camera = p_view.camera;
	visit_bias = bias;
	const Vector3 c = p_view.camera;
	const Vector3 d(MAX(MAX(-c.x, c.x - extent.x), 0.0f), MAX(MAX(height_all.x - c.y, c.y - height_all.y), 0.0f), MAX(MAX(-c.z, c.z - extent.y), 0.0f));
	visit_near = d.length();
}

void FellowTerrainVT::_evict(uint64_t p_key) {
	if (!resident.erase(p_key)) {
		return;
	}
	visited = false; // the pages chosen again next frame (this one may still be wanted)
	stale.erase(p_key);
	_set_entry_tree(_key_mip(p_key), _key_x(p_key), _key_z(p_key));
}

// The table's entries over a page (its own and its descendants' that are not baked) after it was baked or evicted:
// its slot, or its parent's entry.
void FellowTerrainVT::_set_entry_tree(int p_mip, int p_x, int p_z) {
	for (int m = p_mip; m >= 0; m--) {
		const int s = p_mip - m;
		const Vector2i n = _pages(m);
		const int x0 = p_x << s, z0 = p_z << s;
		const int x1 = MIN(x0 + (1 << s), n.x), z1 = MIN(z0 + (1 << s), n.y);
		const Vector2i np = m + 1 < mips ? _pages(m + 1) : Vector2i();
		for (int z = z0; z < z1; z++) {
			for (int x = x0; x < x1; x++) {
				uint32_t e = 0;
				if (const int *slot = resident.getptr(_key(m, x, z))) {
					const uint32_t sx = uint32_t(*slot % slots_per_side), sy = uint32_t(*slot / slots_per_side);
					e = sx | (sy << 8) | (uint32_t(m) << 16) | (255u << 24);
				} else if (m + 1 < mips) {
					e = table[m + 1][MIN(z >> 1, np.y - 1) * np.x + MIN(x >> 1, np.x - 1)];
				}
				table[m][z * n.x + x] = e;
			}
		}
		const Rect2i r(x0, z0, x1 - x0, z1 - z0);
		table_dirty[m] = table_dirty[m].has_area() ? table_dirty[m].merge(r) : r;
	}
}

void FellowTerrainVT::invalidate(const Rect2 &p_local) {
	overview_stale = true;
	for (const KeyValue<uint64_t, int> &E : resident) {
		const Rect2 r = _page_rect(_key_mip(E.key), _key_x(E.key), _key_z(E.key));
		// The page's border texels too.
		const float b = float(BORDER << _key_mip(E.key)) / texels_per_metre;
		if (r.grow(b).intersects(p_local, true)) {
			stale.insert(E.key, true);
		}
	}
}

void FellowTerrainVT::invalidate_all() {
	overview_stale = true;
	for (const KeyValue<uint64_t, int> &E : resident) {
		stale.insert(E.key, true);
	}
}

void FellowTerrainVT::upload_page_table() {
	if (table_texture.is_null()) {
		return;
	}
	RenderingServer *rs = RenderingServer::get_singleton();
	const RID rid = table_texture->get_rid();
	for (int m = 0; m < mips; m++) {
		const Rect2i r = table_dirty[m];
		if (!r.has_area()) {
			continue;
		}
		table_dirty[m] = Rect2i();
		const Vector2i n = _pages(m);
		Vector<uint8_t> data;
		data.resize(r.size.x * r.size.y * 4);
		for (int z = 0; z < r.size.y; z++) {
			memcpy(data.ptrw() + z * r.size.x * 4, table[m].ptr() + (r.position.y + z) * n.x + r.position.x, r.size.x * 4);
		}
		rs->fellow_texture_2d_update_region(rid, m, r, data);
	}
}

Vector4 FellowTerrainVT::get_shader_params() const {
	return Vector4(texels_per_metre, float(mips), atlas_texels > 0 ? 1.0f / float(atlas_texels) : 0.0f, 0.0f);
}

Ref<Texture2D> FellowTerrainVT::get_overview_albedo() const {
	return overview_albedo;
}

Ref<Texture2D> FellowTerrainVT::get_overview_normal() const {
	return overview_normal;
}
