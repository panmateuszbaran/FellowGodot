/**************************************************************************/
/*  fellow_gas.cpp                                                        */
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

#include "fellow_gas.h"

#include "core/config/project_settings.h"
#include "core/object/callable_mp.h"
#include "servers/rendering/fellow_settings.h"
#include "servers/rendering/renderer_rd/renderer_scene_render_rd.h"
#include "servers/rendering/renderer_rd/storage_rd/texture_storage.h"
#include "servers/rendering/renderer_rd/uniform_set_cache_rd.h"
#include "servers/rendering/rendering_server_globals.h"

using namespace RendererRD;

FellowGas *FellowGas::singleton = nullptr;
SafeNumeric<uint32_t> FellowGas::next_id;

static RID _fellow_gas_tex3d(RD::DataFormat p_format, const Vector3i &p_size, uint32_t p_usage) {
	RD::TextureFormat tf;
	tf.texture_type = RD::TEXTURE_TYPE_3D;
	tf.format = p_format;
	tf.width = MAX(p_size.x, 1);
	tf.height = MAX(p_size.y, 1);
	tf.depth = MAX(p_size.z, 1);
	tf.usage_bits = p_usage;
	return RD::get_singleton()->texture_create(tf, RD::TextureView());
}

static void _fellow_gas_store_matrix(const Projection &p_m, float *r_dst) {
	for (int i = 0; i < 4; i++) {
		for (int j = 0; j < 4; j++) {
			r_dst[i * 4 + j] = p_m.columns[i][j];
		}
	}
}

static Vector3i _fellow_gas_coarse_size(const Vector3i &p_res) {
	return Vector3i((p_res.x + 3) / 4, (p_res.y + 3) / 4, (p_res.z + 3) / 4);
}

static Vector3i _fellow_gas_light_size(const Vector3i &p_res) {
	return Vector3i((p_res.x + 1) / 2, (p_res.y + 1) / 2, (p_res.z + 1) / 2);
}

FellowGas *FellowGas::get_singleton() {
	if (!singleton && RD::get_singleton()) {
		singleton = memnew(FellowGas);
	}
	return singleton;
}

void FellowGas::finalize() {
	if (singleton) {
		memdelete(singleton);
		singleton = nullptr;
	}
}

bool FellowGas::rendering_enabled() {
	return Fellow::enabled() && GLOBAL_GET_CACHED(bool, "rendering/fellow/fluid/gas_rendering");
}

uint32_t FellowGas::allocate_id() {
	return next_id.increment();
}

int64_t FellowGas::estimate_vram_bytes(const Vector3i &p_res, bool p_high_precision) {
	const int64_t cells = int64_t(p_res.x) * p_res.y * p_res.z;
	const int64_t faces = int64_t(p_res.x + 1) * (p_res.y + 1) * (p_res.z + 1);
	const int64_t texel = p_high_precision ? 16 : 8;
	const Vector3i cs = _fellow_gas_coarse_size(p_res);
	const Vector3i ls = _fellow_gas_light_size(p_res);
	return cells * texel * 3 + faces * texel * 3 + cells * (4 + 4 + 1) + int64_t(cs.x) * cs.y * cs.z * 4 + int64_t(ls.x) * ls.y * ls.z * 2;
}

FellowGas::FellowGas() {
	Vector<String> render_modes;
	render_modes.push_back("\n#define MODE_LIGHT\n");
	render_modes.push_back("\n#define MODE_SHADOW\n");
	render_modes.push_back("\n#define MODE_TRACE\n");
	render_modes.push_back("\n#define MODE_TRACE\n#define FELLOW_RADIANCE_ARRAY\n");
	render_modes.push_back("\n#define MODE_COMPOSITE\n");
	render_shader.initialize(render_modes);
	render_version = render_shader.version_create();
	for (int i = 0; i < RENDER_MAX; i++) {
		render_pipelines[i] = RD::get_singleton()->compute_pipeline_create(render_shader.version_get_shader(render_version, i));
	}
	Vector<uint8_t> zeros;
	zeros.resize(sizeof(RenderUBO));
	zeros.fill(0);
	render_ubo = RD::get_singleton()->uniform_buffer_create(sizeof(RenderUBO), zeros);
	dummy_3d = _fellow_gas_tex3d(RD::DATA_FORMAT_R16G16B16A16_SFLOAT, Vector3i(1, 1, 1), RD::TEXTURE_USAGE_SAMPLING_BIT | RD::TEXTURE_USAGE_CAN_COPY_TO_BIT);
	RD::get_singleton()->texture_clear(dummy_3d, Color(0, 0, 0, 0), 0, 1, 0, 1);

	RD::SamplerState ss;
	ss.mag_filter = RD::SAMPLER_FILTER_LINEAR;
	ss.min_filter = RD::SAMPLER_FILTER_LINEAR;
	ss.repeat_u = RD::SAMPLER_REPEAT_MODE_CLAMP_TO_EDGE;
	ss.repeat_v = RD::SAMPLER_REPEAT_MODE_CLAMP_TO_EDGE;
	ss.repeat_w = RD::SAMPLER_REPEAT_MODE_CLAMP_TO_EDGE;
	linear_sampler = RD::get_singleton()->sampler_create(ss);
	ss.mip_filter = RD::SAMPLER_FILTER_LINEAR;
	linear_mip_sampler = RD::get_singleton()->sampler_create(ss);
	ss.mag_filter = RD::SAMPLER_FILTER_NEAREST;
	ss.min_filter = RD::SAMPLER_FILTER_NEAREST;
	ss.mip_filter = RD::SAMPLER_FILTER_NEAREST;
	nearest_sampler = RD::get_singleton()->sampler_create(ss);
}

FellowGas::~FellowGas() {
	for (KeyValue<uint32_t, Volume> &kv : volumes) {
		_free_volume_textures(kv.value);
	}
	volumes.clear();
	RID rids[] = { render_ubo, dummy_3d, linear_sampler, linear_mip_sampler, nearest_sampler };
	for (RID r : rids) {
		if (r.is_valid()) {
			RD::get_singleton()->free_rid(r);
		}
	}
	render_shader.version_free(render_version);
	for (int p = 0; p < 2; p++) {
		if (sim_ready[p]) {
			sim_shader[p].version_free(sim_version[p]);
		}
	}
}

void FellowGas::_ensure_sim(int p_precision) {
	if (sim_ready[p_precision]) {
		return;
	}
	Vector<String> modes;
	const char *names[SIM_MAX] = { "OBSTACLES", "EMIT", "ADVECT_S1", "ADVECT_S2", "ADVECT_V1", "ADVECT_V2", "CURL", "CELL_FORCE", "FACE_FORCE", "DIVERGENCE", "PRESSURE", "PROJECT", "COARSE", "MG_RESTRICT", "MG_PROLONG", "MG_COARSE", "EXPORT", "SHIFT" };
	for (int i = 0; i < SIM_MAX; i++) {
		modes.push_back(String("\n#define MODE_") + names[i] + "\n");
	}
	sim_shader[p_precision].initialize(modes, p_precision ? "\n#define HIGH_PRECISION\n" : "");
	sim_version[p_precision] = sim_shader[p_precision].version_create();
	for (int i = 0; i < SIM_MAX; i++) {
		sim_pipelines[p_precision][i] = RD::get_singleton()->compute_pipeline_create(sim_shader[p_precision].version_get_shader(sim_version[p_precision], i));
	}
	sim_ready[p_precision] = true;
}

void FellowGas::_free_volume_textures(Volume &r_v) {
	RID *rids[] = { &r_v.scalars[0], &r_v.scalars[1], &r_v.scalars_hat, &r_v.velocity[0], &r_v.velocity[1], &r_v.velocity_hat, &r_v.pressure, &r_v.rhs, &r_v.solid, &r_v.coarse, &r_v.light, &r_v.emission_buffer, &r_v.velocity_export, &r_v.ubo };
	for (RID *r : rids) {
		if (r->is_valid()) {
			RD::get_singleton()->free_rid(*r);
			*r = RID();
		}
	}
	for (Level &l : r_v.levels) {
		RID lr[] = { l.pressure, l.rhs, l.solid };
		for (RID r : lr) {
			if (r.is_valid()) {
				RD::get_singleton()->free_rid(r);
			}
		}
	}
	r_v.levels.clear();
}

void FellowGas::_allocate(Volume &r_v) {
	_free_volume_textures(r_v);
	const Vector3i n = r_v.res;
	const Vector3i nv = n + Vector3i(1, 1, 1);
	const RD::DataFormat fmt = r_v.high_precision ? RD::DATA_FORMAT_R32G32B32A32_SFLOAT : RD::DATA_FORMAT_R16G16B16A16_SFLOAT;
	const uint32_t usage = RD::TEXTURE_USAGE_SAMPLING_BIT | RD::TEXTURE_USAGE_STORAGE_BIT | RD::TEXTURE_USAGE_CAN_COPY_TO_BIT | RD::TEXTURE_USAGE_CAN_COPY_FROM_BIT;
	for (int i = 0; i < 2; i++) {
		r_v.scalars[i] = _fellow_gas_tex3d(fmt, n, usage);
		RD::get_singleton()->set_resource_name(r_v.scalars[i], "FellowGas scalars " + itos(i));
		r_v.velocity[i] = _fellow_gas_tex3d(fmt, nv, usage);
		RD::get_singleton()->set_resource_name(r_v.velocity[i], "FellowGas velocity " + itos(i));
	}
	r_v.scalars_hat = _fellow_gas_tex3d(fmt, n, usage);
	r_v.velocity_hat = _fellow_gas_tex3d(fmt, nv, usage);
	r_v.pressure = _fellow_gas_tex3d(RD::DATA_FORMAT_R32_SFLOAT, n, RD::TEXTURE_USAGE_STORAGE_BIT | RD::TEXTURE_USAGE_CAN_COPY_TO_BIT | RD::TEXTURE_USAGE_CAN_COPY_FROM_BIT);
	r_v.rhs = _fellow_gas_tex3d(RD::DATA_FORMAT_R32_SFLOAT, n, RD::TEXTURE_USAGE_STORAGE_BIT | RD::TEXTURE_USAGE_CAN_COPY_TO_BIT | RD::TEXTURE_USAGE_CAN_COPY_FROM_BIT);
	r_v.solid = _fellow_gas_tex3d(RD::DATA_FORMAT_R8_UINT, n, RD::TEXTURE_USAGE_STORAGE_BIT | RD::TEXTURE_USAGE_CAN_COPY_TO_BIT | RD::TEXTURE_USAGE_CAN_COPY_FROM_BIT);
	r_v.coarse = _fellow_gas_tex3d(RD::DATA_FORMAT_R16G16_SFLOAT, _fellow_gas_coarse_size(n), RD::TEXTURE_USAGE_SAMPLING_BIT | RD::TEXTURE_USAGE_STORAGE_BIT | RD::TEXTURE_USAGE_CAN_COPY_TO_BIT);
	r_v.light = _fellow_gas_tex3d(RD::DATA_FORMAT_R16_SFLOAT, _fellow_gas_light_size(n), RD::TEXTURE_USAGE_SAMPLING_BIT | RD::TEXTURE_USAGE_STORAGE_BIT | RD::TEXTURE_USAGE_CAN_COPY_TO_BIT);
	const Vector3i bricks = _fellow_gas_coarse_size(n);
	const uint32_t groups = uint32_t((bricks.x + 3) / 4) * uint32_t((bricks.y + 3) / 4) * uint32_t((bricks.z + 3) / 4);
	r_v.emission_buffer = RD::get_singleton()->storage_buffer_create(groups * 32);
	Vector<uint8_t> zeros;
	zeros.resize(sizeof(SimUBO));
	zeros.fill(0);
	r_v.ubo = RD::get_singleton()->uniform_buffer_create(sizeof(SimUBO), zeros);
	// The multigrid's levels: halved (rounded up) until the smallest side is at most 4 cells.
	Vector3i ls = n;
	while (MIN(ls.x, MIN(ls.y, ls.z)) > 4) {
		ls = Vector3i((ls.x + 1) / 2, (ls.y + 1) / 2, (ls.z + 1) / 2);
		Level l;
		l.size = ls;
		const uint32_t u = RD::TEXTURE_USAGE_STORAGE_BIT | RD::TEXTURE_USAGE_CAN_COPY_TO_BIT;
		l.pressure = _fellow_gas_tex3d(RD::DATA_FORMAT_R32_SFLOAT, ls, u);
		l.rhs = _fellow_gas_tex3d(RD::DATA_FORMAT_R32_SFLOAT, ls, u);
		l.solid = _fellow_gas_tex3d(RD::DATA_FORMAT_R8_UINT, ls, u);
		r_v.levels.push_back(l);
	}
	r_v.needs_clear = true;
}

void FellowGas::_clear(Volume &r_v) {
	RID rids[] = { r_v.scalars[0], r_v.scalars[1], r_v.scalars_hat, r_v.velocity[0], r_v.velocity[1], r_v.velocity_hat, r_v.pressure, r_v.rhs, r_v.solid, r_v.coarse, r_v.light };
	for (RID r : rids) {
		RD::get_singleton()->texture_clear(r, Color(0, 0, 0, 0), 0, 1, 0, 1);
	}
	for (Level &l : r_v.levels) {
		RD::get_singleton()->texture_clear(l.pressure, Color(0, 0, 0, 0), 0, 1, 0, 1);
		RD::get_singleton()->texture_clear(l.rhs, Color(0, 0, 0, 0), 0, 1, 0, 1);
		RD::get_singleton()->texture_clear(l.solid, Color(0, 0, 0, 0), 0, 1, 0, 1);
	}
	r_v.current = 0;
	r_v.steps = 0;
	r_v.light_step = UINT64_MAX;
	r_v.needs_clear = false;
}

void FellowGas::volume_configure(uint32_t p_id, const Vector3i &p_res, float p_dx, bool p_high_precision) {
	ERR_FAIL_COND(p_res.x < 4 || p_res.y < 4 || p_res.z < 4);
	Volume *v = volumes.getptr(p_id);
	if (!v) {
		v = &volumes.insert(p_id, Volume())->value;
	}
	const bool realloc = v->res != p_res || v->high_precision != p_high_precision || v->ubo.is_null();
	v->res = p_res;
	v->dx = p_dx;
	v->high_precision = p_high_precision;
	_ensure_sim(p_high_precision ? 1 : 0);
	if (realloc) {
		_allocate(*v);
	}
	v->pending.clear();
	v->needs_clear = true;
}

void FellowGas::volume_free(uint32_t p_id) {
	Volume *v = volumes.getptr(p_id);
	if (!v) {
		return;
	}
	_free_volume_textures(*v);
	volumes.erase(p_id);
}

void FellowGas::volume_reset(uint32_t p_id) {
	Volume *v = volumes.getptr(p_id);
	ERR_FAIL_NULL(v);
	v->pending.clear();
	v->needs_clear = true;
}

void FellowGas::volume_queue_step(uint32_t p_id, const StepParams &p_step) {
	Volume *v = volumes.getptr(p_id);
	ERR_FAIL_NULL(v);
	if (v->pending.size() >= 64) {
		WARN_PRINT_ONCE("FellowGasVolume3D: more than 64 steps queued without a frame drawn; the oldest dropped.");
		v->pending.remove_at(0);
	}
	v->pending.push_back(p_step);
}

void FellowGas::volume_set_render(uint32_t p_id, const RenderParams &p_render) {
	Volume *v = volumes.getptr(p_id);
	ERR_FAIL_NULL(v);
	v->render = p_render;
	v->render_set = true;
}

void FellowGas::volume_set_emission_callback(uint32_t p_id, const Callable &p_callback) {
	Volume *v = volumes.getptr(p_id);
	ERR_FAIL_NULL(v);
	v->emission_callback = p_callback;
}

RID FellowGas::volume_enable_velocity_export(uint32_t p_id, bool p_enable, float p_scale) {
	Volume *v = volumes.getptr(p_id);
	ERR_FAIL_NULL_V(v, RID());
	v->velocity_export_scale = MAX(p_scale, 1e-3f);
	if (!p_enable) {
		if (v->velocity_export.is_valid()) {
			RD::get_singleton()->free_rid(v->velocity_export);
			v->velocity_export = RID();
		}
		return RID();
	}
	if (v->velocity_export.is_null()) {
		v->velocity_export = _fellow_gas_tex3d(RD::DATA_FORMAT_R16G16B16A16_SFLOAT, v->res, RD::TEXTURE_USAGE_SAMPLING_BIT | RD::TEXTURE_USAGE_STORAGE_BIT | RD::TEXTURE_USAGE_CAN_COPY_TO_BIT);
		RD::get_singleton()->texture_clear(v->velocity_export, Color(0.5, 0.5, 0.5, 1.0), 0, 1, 0, 1);
	}
	return v->velocity_export;
}

uint64_t FellowGas::volume_get_steps(uint32_t p_id) const {
	const Volume *v = volumes.getptr(p_id);
	return v ? v->steps : 0;
}

void FellowGas::_emission_read(const Vector<uint8_t> &p_data, Callable p_callback, uint32_t p_id) {
	if (singleton) {
		Volume *v = singleton->volumes.getptr(p_id);
		if (v) {
			v->emission_in_flight = false;
		}
	}
	if (p_callback.is_valid()) {
		p_callback.call(p_data);
	}
}

void FellowGas::_dispatch_sim(Volume &r_v, SimMode p_mode, const Vector<RD::Uniform> &p_uniforms, const Vector3i &p_threads, const MGPush *p_push, bool p_groups) {
	const int p = r_v.high_precision ? 1 : 0;
	RID shader = sim_shader[p].version_get_shader(sim_version[p], p_mode);
	LocalVector<RD::Uniform> u;
	u.push_back(RD::Uniform(RD::UNIFORM_TYPE_UNIFORM_BUFFER, 0, r_v.ubo));
	for (const RD::Uniform &x : p_uniforms) {
		u.push_back(x);
	}
	RID set = UniformSetCacheRD::get_singleton()->get_cache_vec(shader, 0, u);
	RD::ComputeListID list = RD::get_singleton()->compute_list_begin();
	RD::get_singleton()->compute_list_bind_compute_pipeline(list, sim_pipelines[p][p_mode]);
	RD::get_singleton()->compute_list_bind_uniform_set(list, set, 0);
	if (p_push) {
		RD::get_singleton()->compute_list_set_push_constant(list, p_push, sizeof(MGPush));
	}
	if (p_groups) {
		RD::get_singleton()->compute_list_dispatch(list, p_threads.x, p_threads.y, p_threads.z);
	} else {
		RD::get_singleton()->compute_list_dispatch_threads(list, p_threads.x, p_threads.y, p_threads.z);
	}
	RD::get_singleton()->compute_list_end();
}

void FellowGas::_smooth(Volume &r_v, const Vector3i &p_size, RID p_pressure, RID p_rhs, RID p_solid, float p_dx2, float p_omega, int p_iterations) {
	const Vector3i half((p_size.x + 1) / 2, p_size.y, p_size.z);
	MGPush push = { { p_size.x, p_size.y, p_size.z, 0 }, { p_dx2, p_omega, 0.0f, 0.0f }, { 0, 0, 0, 0 } };
	const Vector<RD::Uniform> uniforms = { RD::Uniform(RD::UNIFORM_TYPE_IMAGE, 1, p_rhs), RD::Uniform(RD::UNIFORM_TYPE_IMAGE, 4, p_pressure), RD::Uniform(RD::UNIFORM_TYPE_IMAGE, 5, p_solid) };
	for (int it = 0; it < p_iterations; it++) {
		for (int colour = 0; colour < 2; colour++) {
			push.size[3] = colour;
			_dispatch_sim(r_v, SIM_PRESSURE, uniforms, half, &push);
		}
	}
}

void FellowGas::_run_step(uint32_t p_id, Volume &r_v, const StepParams &p_step) {
	const Vector3i n = r_v.res;
	const Vector3i nv = n + Vector3i(1, 1, 1);
	const float dt = MAX(p_step.dt, 1e-5f);
	const float dx = r_v.dx;

	SimUBO u;
	memset(&u, 0, sizeof(SimUBO));
	u.size[0] = n.x;
	u.size[1] = n.y;
	u.size[2] = n.z;
	u.size[3] = CLAMP(p_step.emitter_count, 0, MAX_EMITTERS);
	u.grid[0] = dx;
	u.grid[1] = dt;
	u.grid[2] = 1.0f / dx;
	u.grid[3] = p_step.max_speed_cells * dx / dt;
	u.up[0] = p_step.up[0];
	u.up[1] = p_step.up[1];
	u.up[2] = p_step.up[2];
	u.up[3] = p_step.buoyancy;
	u.wind[0] = p_step.wind[0];
	u.wind[1] = p_step.wind[1];
	u.wind[2] = p_step.wind[2];
	u.wind[3] = float(1.0 - Math::exp(-double(p_step.wind_rate) * dt));
	u.forces[0] = p_step.smoke_weight;
	u.forces[1] = p_step.vorticity;
	u.forces[2] = p_step.turbulence;
	u.forces[3] = p_step.turbulence_frequency;
	u.decay[0] = float(Math::exp(-double(p_step.smoke_dissipation) * dt));
	u.decay[1] = float(Math::exp(-double(p_step.cooling) * dt));
	u.decay[2] = p_step.ignition;
	u.decay[3] = float(1.0 - Math::exp(-double(p_step.burn_rate) * dt));
	u.combustion[0] = p_step.heat;
	u.combustion[1] = p_step.soot;
	u.combustion[2] = p_step.expansion;
	u.combustion[3] = p_step.omega;
	uint32_t inflow = 0;
	for (int a = 0; a < 3; a++) {
		if (!(p_step.closed_faces & (1u << (2 * a))) && p_step.wind[a] > 1e-6f) {
			inflow |= 1u << (2 * a);
		}
		if (!(p_step.closed_faces & (1u << (2 * a + 1))) && p_step.wind[a] < -1e-6f) {
			inflow |= 1u << (2 * a + 1);
		}
	}
	u.flags[0] = int32_t(p_step.closed_faces & 63u);
	u.flags[1] = int32_t(inflow);
	u.flags[2] = CLAMP(p_step.obstacle_count, 0, MAX_OBSTACLES);
	u.flags[3] = p_step.maccormack ? 1 : 0;
	u.misc[0] = p_step.time;
	u.misc[1] = dx * dx;
	u.misc[2] = p_step.ambient_temperature;
	u.emission[0] = p_step.emission_intensity;
	u.emission[1] = dx * dx * dx;
	u.emission[2] = p_step.emission_temperature_scale;
	memcpy(u.emitters, p_step.emitters, sizeof(u.emitters));
	memcpy(u.obstacles, p_step.obstacles, sizeof(u.obstacles));
	RD::get_singleton()->buffer_update(r_v.ubo, 0, sizeof(SimUBO), &u);

	if (p_step.shift[0] != 0 || p_step.shift[1] != 0 || p_step.shift[2] != 0) {
		// The box followed its node by whole cells: the state shifted into the other buffers, the pressure's warm start
		// dropped (the multigrid solve recovers within the step).
		const uint32_t c = r_v.current;
		MGPush push = { { p_step.shift[0], p_step.shift[1], p_step.shift[2], 0 }, { 0.0f, 0.0f, 0.0f, 0.0f }, { 0, 0, 0, 0 } };
		_dispatch_sim(r_v, SIM_SHIFT, { RD::Uniform(RD::UNIFORM_TYPE_IMAGE, 1, r_v.scalars[c]), RD::Uniform(RD::UNIFORM_TYPE_IMAGE, 2, r_v.velocity[c]), RD::Uniform(RD::UNIFORM_TYPE_IMAGE, 3, r_v.scalars[c ^ 1u]), RD::Uniform(RD::UNIFORM_TYPE_IMAGE, 4, r_v.velocity[c ^ 1u]) }, n + Vector3i(1, 1, 1), &push);
		RD::get_singleton()->texture_clear(r_v.pressure, Color(0, 0, 0, 0), 0, 1, 0, 1);
		r_v.current = c ^ 1u;
	}
	const uint32_t cur = r_v.current;
	const uint32_t nxt = cur ^ 1u;
	const bool mc = p_step.maccormack != 0;
	auto image = [](int p_binding, RID p_tex) { return RD::Uniform(RD::UNIFORM_TYPE_IMAGE, p_binding, p_tex); };
	auto sampled = [this](int p_binding, RID p_tex) { return RD::Uniform(RD::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE, p_binding, Vector<RID>({ linear_sampler, p_tex })); };
	auto buffer = [](int p_binding, RID p_buf) { return RD::Uniform(RD::UNIFORM_TYPE_STORAGE_BUFFER, p_binding, p_buf); };

	RD::get_singleton()->draw_command_begin_label("FellowEngine Gas Step");
	// The obstacles (also when there are none left: the cells they covered become air).
	_dispatch_sim(r_v, SIM_OBSTACLES, { image(1, r_v.solid) }, n);
	if (u.size[3] > 0) {
		_dispatch_sim(r_v, SIM_EMIT, { image(1, r_v.scalars[cur]), image(2, r_v.solid) }, n);
	}
	// Scalars.
	_dispatch_sim(r_v, SIM_ADVECT_S1, { sampled(1, r_v.scalars[cur]), sampled(2, r_v.velocity[cur]), sampled(3, dummy_3d), image(4, mc ? r_v.scalars_hat : r_v.scalars[nxt]), image(5, r_v.solid) }, n);
	if (mc) {
		_dispatch_sim(r_v, SIM_ADVECT_S2, { sampled(1, r_v.scalars[cur]), sampled(2, r_v.velocity[cur]), sampled(3, r_v.scalars_hat), image(4, r_v.scalars[nxt]), image(5, r_v.solid) }, n);
	}
	// Velocity.
	_dispatch_sim(r_v, SIM_ADVECT_V1, { sampled(1, r_v.velocity[cur]), sampled(2, r_v.velocity[cur]), sampled(3, dummy_3d), image(4, mc ? r_v.velocity_hat : r_v.velocity[nxt]), image(5, r_v.solid) }, nv);
	if (mc) {
		_dispatch_sim(r_v, SIM_ADVECT_V2, { sampled(1, r_v.velocity[cur]), sampled(2, r_v.velocity[cur]), sampled(3, r_v.velocity_hat), image(4, r_v.velocity[nxt]), image(5, r_v.solid) }, nv);
	}
	// Forces.
	_dispatch_sim(r_v, SIM_CURL, { image(1, r_v.velocity[nxt]), image(4, r_v.scalars_hat) }, n);
	_dispatch_sim(r_v, SIM_CELL_FORCE, { image(1, r_v.scalars_hat), image(2, r_v.scalars[nxt]), image(4, r_v.velocity_hat), image(5, r_v.solid) }, n);
	_dispatch_sim(r_v, SIM_FACE_FORCE, { image(1, r_v.velocity_hat), image(4, r_v.velocity[nxt]) }, nv);
	// Projection.
	_dispatch_sim(r_v, SIM_DIVERGENCE, { image(1, r_v.velocity[nxt]), image(2, r_v.scalars[nxt]), image(4, r_v.rhs), image(5, r_v.solid) }, n);
	// rendering/fellow/fluid/gas_solver (measurements): 0 the node's choice, 1 SOR, 2 multigrid.
	const int forced = GLOBAL_GET_CACHED(int, "rendering/fellow/fluid/gas_solver");
	const bool multigrid = forced == 0 ? p_step.multigrid != 0 : forced == 2;
	if (multigrid && !r_v.levels.is_empty()) {
		// V-cycles (decision 0117): two red-black Gauss-Seidel sweeps (omega 1.15) before and after each level's
		// correction, the residual restricted to the coarser level (averaged over the fluid children), the coarsest
		// level solved by one workgroup, the correction interpolated back trilinearly.
		const float omega = 1.15f;
		const int cycles = CLAMP(p_step.multigrid_cycles, 1, 8);
		const int L = int(r_v.levels.size());
		for (int cycle = 0; cycle < cycles; cycle++) {
			for (int l = 0; l < L; l++) {
				const Vector3i fs = l == 0 ? n : r_v.levels[l - 1].size;
				const float fdx = dx * float(1 << l);
				RID fq = l == 0 ? r_v.pressure : r_v.levels[l - 1].pressure;
				RID fr = l == 0 ? r_v.rhs : r_v.levels[l - 1].rhs;
				RID fsolid = l == 0 ? r_v.solid : r_v.levels[l - 1].solid;
				_smooth(r_v, fs, fq, fr, fsolid, fdx * fdx, omega, 2);
				const Level &c = r_v.levels[l];
				MGPush push = { { fs.x, fs.y, fs.z, 0 }, { fdx * fdx, omega, 0.0f, 0.0f }, { c.size.x, c.size.y, c.size.z, 0 } };
				_dispatch_sim(r_v, SIM_MG_RESTRICT, { image(1, fr), image(3, c.rhs), image(4, fq), image(5, fsolid), image(6, c.pressure), image(7, c.solid) }, c.size, &push);
			}
			{
				const Level &c = r_v.levels[L - 1];
				const float cdx = dx * float(1 << L);
				MGPush push = { { c.size.x, c.size.y, c.size.z, 0 }, { cdx * cdx, omega, 20.0f, 0.0f }, { 0, 0, 0, 0 } };
				_dispatch_sim(r_v, SIM_MG_COARSE, { image(1, c.rhs), image(4, c.pressure), image(5, c.solid) }, Vector3i(1, 1, 1), &push, true);
			}
			for (int l = L - 1; l >= 0; l--) {
				const Vector3i fs = l == 0 ? n : r_v.levels[l - 1].size;
				const float fdx = dx * float(1 << l);
				RID fq = l == 0 ? r_v.pressure : r_v.levels[l - 1].pressure;
				RID fr = l == 0 ? r_v.rhs : r_v.levels[l - 1].rhs;
				RID fsolid = l == 0 ? r_v.solid : r_v.levels[l - 1].solid;
				const Level &c = r_v.levels[l];
				MGPush push = { { fs.x, fs.y, fs.z, 0 }, { fdx * fdx, omega, 0.0f, 0.0f }, { c.size.x, c.size.y, c.size.z, 0 } };
				_dispatch_sim(r_v, SIM_MG_PROLONG, { image(2, c.pressure), image(4, fq), image(5, fsolid) }, fs, &push);
				_smooth(r_v, fs, fq, fr, fsolid, fdx * fdx, omega, 2);
			}
		}
	} else {
		_smooth(r_v, n, r_v.pressure, r_v.rhs, r_v.solid, dx * dx, p_step.omega, CLAMP(p_step.iterations, 0, 200));
	}
	_dispatch_sim(r_v, SIM_PROJECT, { image(1, r_v.pressure), image(4, r_v.velocity[nxt]), image(5, r_v.solid) }, nv);
	// The bricks' largest values (the renderer's empty space) and the fire's emission (its light).
	_dispatch_sim(r_v, SIM_COARSE, { image(1, r_v.scalars[nxt]), image(4, r_v.coarse), buffer(6, r_v.emission_buffer) }, _fellow_gas_coarse_size(n));
	if (r_v.velocity_export.is_valid()) {
		MGPush push = { { 0, 0, 0, 0 }, { 1.0f / r_v.velocity_export_scale, 0.0f, 0.0f, 0.0f }, { 0, 0, 0, 0 } };
		_dispatch_sim(r_v, SIM_EXPORT, { image(1, r_v.velocity[nxt]), image(4, r_v.velocity_export) }, n, &push);
	}
	RD::get_singleton()->draw_command_end_label();

	r_v.current = nxt;
	r_v.steps++;
	if (p_step.read_emission && r_v.emission_callback.is_valid() && !r_v.emission_in_flight) {
		r_v.emission_in_flight = true;
		RD::get_singleton()->buffer_get_data_async(r_v.emission_buffer, callable_mp_static(&FellowGas::_emission_read).bind(r_v.emission_callback, p_id));
	}
}

void FellowGas::volume_debug_read(uint32_t p_id, int p_what, const Callable &p_callback) {
	Volume *v = volumes.getptr(p_id);
	ERR_FAIL_NULL(v);
	if (v->needs_clear) {
		_clear(*v);
	}
	// The steps queued before the request first (they would otherwise run at the next frame, after this read).
	for (const StepParams &s : v->pending) {
		_run_step(p_id, *v, s);
	}
	v->pending.clear();
	RID tex;
	switch (p_what) {
		case 0:
			tex = v->scalars[v->current];
			break;
		case 1:
			tex = v->velocity[v->current];
			break;
		case 2:
			tex = v->pressure;
			break;
		case 3: {
			// The divergence's right-hand side of the current velocity (the residual after the last projection).
			const uint32_t c = v->current;
			_dispatch_sim(*v, SIM_DIVERGENCE, { RD::Uniform(RD::UNIFORM_TYPE_IMAGE, 1, v->velocity[c]), RD::Uniform(RD::UNIFORM_TYPE_IMAGE, 2, v->scalars[c]), RD::Uniform(RD::UNIFORM_TYPE_IMAGE, 4, v->rhs), RD::Uniform(RD::UNIFORM_TYPE_IMAGE, 5, v->solid) }, v->res);
			tex = v->rhs;
		} break;
		case 4:
			tex = v->solid;
			break;
		default:
			ERR_FAIL();
	}
	RD::get_singleton()->texture_get_data_async(tex, 0, p_callback);
}

void FellowGas::process_pending() {
	bool any = false;
	for (const KeyValue<uint32_t, Volume> &kv : volumes) {
		if (!kv.value.pending.is_empty() || kv.value.needs_clear) {
			any = true;
			break;
		}
	}
	if (!any) {
		return;
	}
	RENDER_TIMESTAMP("FellowEngine Gas Simulation");
	// In the order of the volumes' ids: the same order on every run.
	LocalVector<uint32_t> ids;
	for (const KeyValue<uint32_t, Volume> &kv : volumes) {
		ids.push_back(kv.key);
	}
	ids.sort();
	for (uint32_t id : ids) {
		Volume &v = volumes[id];
		if (v.needs_clear) {
			_clear(v);
		}
		for (const StepParams &s : v.pending) {
			_run_step(id, v, s);
		}
		v.pending.clear();
	}
	RENDER_TIMESTAMP("FellowEngine Gas Simulation Done");
}

bool FellowGas::begin_frame(const FrameParams &p_frame) {
	frame_active = false;
	frame_volumes.clear();
	frame = p_frame;
	if (volumes.is_empty() || p_frame.size.x <= 0 || p_frame.size.y <= 0) {
		return false;
	}
	struct Candidate {
		uint32_t id;
		float distance;
		bool in_view;
		bool operator<(const Candidate &p_other) const { return distance < p_other.distance || (distance == p_other.distance && id < p_other.id); }
	};
	LocalVector<Candidate> candidates;
	Rect2i rect;
	bool rect_full = false;
	bool rect_any = false;
	for (const KeyValue<uint32_t, Volume> &kv : volumes) {
		const Volume &v = kv.value;
		if (!v.render_set || !v.render.visible || v.steps == 0) {
			continue;
		}
		const Transform3D &g2w = v.render.grid_to_world;
		const Vector3 n = Vector3(v.res);
		// The box's corners on the screen.
		bool in_view = false;
		bool behind = false;
		Vector2 smin(1e30, 1e30);
		Vector2 smax(-1e30, -1e30);
		int outside[6] = { 0, 0, 0, 0, 0, 0 };
		for (int c = 0; c < 8; c++) {
			const Vector3 g((c & 1) ? n.x : 0.0f, (c & 2) ? n.y : 0.0f, (c & 4) ? n.z : 0.0f);
			const Vector3 w = g2w.xform(g);
			const Plane clip = p_frame.view_projection.xform4(Plane(w.x, w.y, w.z, 1.0f));
			const float cx = clip.normal.x;
			const float cy = clip.normal.y;
			const float cz = clip.normal.z;
			const float cw = clip.d;
			outside[0] += cx < -cw;
			outside[1] += cx > cw;
			outside[2] += cy < -cw;
			outside[3] += cy > cw;
			outside[4] += cz < 0.0f; // reversed depth: in front of the near plane is cz <= cw, behind the camera cw < 0
			outside[5] += cw <= 0.0f;
			if (cw <= 1e-4f) {
				behind = true;
				continue;
			}
			const Vector2 s((cx / cw * 0.5f + 0.5f) * p_frame.size.x, (cy / cw * 0.5f + 0.5f) * p_frame.size.y);
			smin = smin.min(s);
			smax = smax.max(s);
		}
		in_view = outside[0] < 8 && outside[1] < 8 && outside[2] < 8 && outside[3] < 8 && outside[5] < 8;
		const float distance = (g2w.xform(n * 0.5f) - p_frame.camera_position).length();
		if (!in_view && !v.render.casts_shadows) {
			continue;
		}
		candidates.push_back({ kv.key, distance, in_view });
		if (in_view) {
			if (behind) {
				rect_full = true;
			} else {
				const Rect2i r(Point2i(int(Math::floor(smin.x)) - 2, int(Math::floor(smin.y)) - 2), Size2i(int(Math::ceil(smax.x - smin.x)) + 4, int(Math::ceil(smax.y - smin.y)) + 4));
				rect = rect_any ? rect.merge(r) : r;
				rect_any = true;
			}
		}
	}
	if (candidates.is_empty()) {
		return false;
	}
	candidates.sort();
	if (candidates.size() > MAX_RENDERED) {
		candidates.resize(MAX_RENDERED);
	}
	const Rect2i screen(Point2i(), p_frame.size);
	if (rect_full) {
		frame_rect = screen;
	} else if (rect_any) {
		frame_rect = rect.intersection(screen);
		// Even, so the half-resolution trace covers it.
		frame_rect.position.x &= ~1;
		frame_rect.position.y &= ~1;
	} else {
		frame_rect = Rect2i();
	}

	RenderUBO u;
	memset(&u, 0, sizeof(RenderUBO));
	_fellow_gas_store_matrix(p_frame.view_projection.inverse(), u.inv_view_projection);
	u.camera_position[0] = p_frame.camera_position.x;
	u.camera_position[1] = p_frame.camera_position.y;
	u.camera_position[2] = p_frame.camera_position.z;
	u.camera_position[3] = p_frame.exposure;
	const Vector3 sun = p_frame.sun_direction.normalized();
	u.sun_direction[0] = sun.x;
	u.sun_direction[1] = sun.y;
	u.sun_direction[2] = sun.z;
	u.sun_direction[3] = p_frame.has_sun ? 1.0f : 0.0f;
	u.sun_illuminance[0] = p_frame.sun_illuminance.r;
	u.sun_illuminance[1] = p_frame.sun_illuminance.g;
	u.sun_illuminance[2] = p_frame.sun_illuminance.b;
	u.ambient_color[0] = p_frame.ambient_color.r;
	u.ambient_color[1] = p_frame.ambient_color.g;
	u.ambient_color[2] = p_frame.ambient_color.b;
	u.ambient_color[3] = p_frame.ambient_color.a;
	u.sky_params[0] = p_frame.use_ambient ? 1.0f : 0.0f;
	u.sky_params[1] = (p_frame.use_sky && p_frame.radiance.is_valid()) ? 1.0f : 0.0f;
	u.sky_params[2] = p_frame.sky_factor;
	u.sky_params[3] = p_frame.radiance_level;
	u.radiance_border[0] = p_frame.radiance_border[0];
	u.radiance_border[1] = p_frame.radiance_border[1];
	for (int i = 0; i < 3; i++) {
		for (int j = 0; j < 3; j++) {
			u.sky_xform[i * 4 + j] = p_frame.sky_xform.rows[j][i];
		}
	}
	u.sky_xform[15] = 1.0f;
	trace_size = Size2i((p_frame.size.x + 1) / 2, (p_frame.size.y + 1) / 2);
	u.sizes[0] = trace_size.x;
	u.sizes[1] = trace_size.y;
	u.sizes[2] = p_frame.size.x;
	u.sizes[3] = p_frame.size.y;
	u.rect[0] = frame_rect.position.x;
	u.rect[1] = frame_rect.position.y;
	u.rect[2] = frame_rect.size.x;
	u.rect[3] = frame_rect.size.y;
	u.counts[0] = int32_t(candidates.size());
	u.counts[1] = 384;
	u.counts[2] = int32_t(p_frame.frame & 0x7fffffff);
	for (uint32_t i = 0; i < candidates.size(); i++) {
		const Volume &v = volumes[candidates[i].id];
		const RenderParams &r = v.render;
		const Transform3D w2g = r.grid_to_world.affine_inverse();
		for (int j = 0; j < 3; j++) {
			float *row = j == 0 ? u.volumes[i].w2g0 : (j == 1 ? u.volumes[i].w2g1 : u.volumes[i].w2g2);
			row[0] = w2g.basis.rows[j][0];
			row[1] = w2g.basis.rows[j][1];
			row[2] = w2g.basis.rows[j][2];
			row[3] = w2g.origin[j];
		}
		u.volumes[i].size[0] = v.res.x;
		u.volumes[i].size[1] = v.res.y;
		u.volumes[i].size[2] = v.res.z;
		u.volumes[i].size[3] = (r.casts_shadows ? 1 : 0) | (candidates[i].in_view ? 2 : 0);
		const Vector3i ls = _fellow_gas_light_size(v.res);
		u.volumes[i].light_size[0] = ls.x;
		u.volumes[i].light_size[1] = ls.y;
		u.volumes[i].light_size[2] = ls.z;
		u.volumes[i].albedo_density[0] = r.albedo[0];
		u.volumes[i].albedo_density[1] = r.albedo[1];
		u.volumes[i].albedo_density[2] = r.albedo[2];
		u.volumes[i].albedo_density[3] = r.density;
		// Metres per cell: the grid's scale (the smallest axis).
		const float mpc = MIN(r.grid_to_world.basis.get_column(0).length(), MIN(r.grid_to_world.basis.get_column(1).length(), r.grid_to_world.basis.get_column(2).length()));
		u.volumes[i].emission[0] = r.emission_intensity;
		u.volumes[i].emission[1] = r.ambient_temperature;
		u.volumes[i].emission[2] = MAX(r.step_cells, 0.25f) * mpc;
		u.volumes[i].emission[3] = CLAMP(r.anisotropy, -0.95f, 0.95f);
		u.volumes[i].fire_light_position[0] = r.fire_light_position[0];
		u.volumes[i].fire_light_position[1] = r.fire_light_position[1];
		u.volumes[i].fire_light_position[2] = r.fire_light_position[2];
		u.volumes[i].fire_light_position[3] = r.fire_light ? 1.0f : 0.0f;
		u.volumes[i].fire_light_intensity[0] = r.fire_light_intensity[0] * p_frame.exposure;
		u.volumes[i].fire_light_intensity[1] = r.fire_light_intensity[1] * p_frame.exposure;
		u.volumes[i].fire_light_intensity[2] = r.fire_light_intensity[2] * p_frame.exposure;
		u.volumes[i].misc[0] = r.ambient;
		u.volumes[i].misc[1] = r.self_shadow;
		u.volumes[i].misc[2] = r.scene_shadow;
		u.volumes[i].misc[3] = mpc;
		u.volumes[i].fire[0] = r.emission_temperature_scale;
		frame_volumes.push_back(candidates[i].id);
	}
	RD::get_singleton()->buffer_update(render_ubo, 0, sizeof(RenderUBO), &u);

	// The light volumes, where the smoke or the sun moved since they were computed.
	if (p_frame.has_sun) {
		RID shader = render_shader.version_get_shader(render_version, RENDER_LIGHT);
		for (uint32_t i = 0; i < frame_volumes.size(); i++) {
			Volume &v = volumes[frame_volumes[i]];
			if (v.light_step == v.steps && v.light_sun.dot(sun) > 0.99999f) {
				continue;
			}
			RENDER_TIMESTAMP("FellowEngine Gas Light");
			LocalVector<RD::Uniform> uniforms;
			uniforms.push_back(RD::Uniform(RD::UNIFORM_TYPE_UNIFORM_BUFFER, 0, render_ubo));
			uniforms.push_back(RD::Uniform(RD::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE, 1, Vector<RID>({ linear_sampler, v.scalars[v.current] })));
			uniforms.push_back(RD::Uniform(RD::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE, 2, Vector<RID>({ nearest_sampler, v.coarse })));
			uniforms.push_back(RD::Uniform(RD::UNIFORM_TYPE_IMAGE, 3, v.light));
			RID set = UniformSetCacheRD::get_singleton()->get_cache_vec(shader, 0, uniforms);
			int32_t push[4] = { int32_t(i), 0, 0, 0 };
			RD::ComputeListID list = RD::get_singleton()->compute_list_begin();
			RD::get_singleton()->compute_list_bind_compute_pipeline(list, render_pipelines[RENDER_LIGHT]);
			RD::get_singleton()->compute_list_bind_uniform_set(list, set, 0);
			RD::get_singleton()->compute_list_set_push_constant(list, push, 16);
			const Vector3i ls = _fellow_gas_light_size(v.res);
			RD::get_singleton()->compute_list_dispatch_threads(list, ls.x, ls.y, ls.z);
			RD::get_singleton()->compute_list_end();
			v.light_step = v.steps;
			v.light_sun = sun;
		}
	}
	frame_active = true;
	return true;
}

bool FellowGas::frame_has_shadows() const {
	if (!frame_active || !frame.has_sun) {
		return false;
	}
	for (uint32_t id : frame_volumes) {
		const Volume *v = volumes.getptr(id);
		if (v && v->render.casts_shadows) {
			return true;
		}
	}
	return false;
}

void FellowGas::apply_shadow(RID p_mask, bool p_multiply, RID p_depth) {
	ERR_FAIL_COND(!frame_active);
	RENDER_TIMESTAMP("FellowEngine Gas Shadow");
	RID shader = render_shader.version_get_shader(render_version, RENDER_SHADOW);
	Vector<RID> lights;
	for (int i = 0; i < MAX_RENDERED; i++) {
		lights.push_back(linear_sampler);
		lights.push_back(i < int(frame_volumes.size()) ? volumes[frame_volumes[i]].light : dummy_3d);
	}
	LocalVector<RD::Uniform> uniforms;
	uniforms.push_back(RD::Uniform(RD::UNIFORM_TYPE_UNIFORM_BUFFER, 0, render_ubo));
	uniforms.push_back(RD::Uniform(RD::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE, 1, lights));
	uniforms.push_back(RD::Uniform(RD::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE, 2, Vector<RID>({ nearest_sampler, p_depth })));
	uniforms.push_back(RD::Uniform(RD::UNIFORM_TYPE_IMAGE, 3, p_mask));
	RID set = UniformSetCacheRD::get_singleton()->get_cache_vec(shader, 0, uniforms);
	int32_t push[4] = { p_multiply ? 1 : 0, 0, 0, 0 };
	RD::ComputeListID list = RD::get_singleton()->compute_list_begin();
	RD::get_singleton()->compute_list_bind_compute_pipeline(list, render_pipelines[RENDER_SHADOW]);
	RD::get_singleton()->compute_list_bind_uniform_set(list, set, 0);
	RD::get_singleton()->compute_list_set_push_constant(list, push, 16);
	RD::get_singleton()->compute_list_dispatch_threads(list, frame.size.x, frame.size.y, 1);
	RD::get_singleton()->compute_list_end();
}

void FellowGas::render(RID p_color, RID p_depth, RID p_trace, RID p_trace_depth) {
	if (!frame_active || frame_rect.size.x <= 0 || frame_rect.size.y <= 0) {
		return;
	}
	ERR_FAIL_COND(p_trace.is_null() || p_trace_depth.is_null());
	RENDER_TIMESTAMP("FellowEngine Gas Trace");
	const RID trace_tex = p_trace;
	const RID trace_depth_tex = p_trace_depth;
	const bool array = RendererSceneRenderRD::get_singleton()->is_using_radiance_octmap_array();
	const RenderMode trace_mode = array ? RENDER_TRACE_ARRAY : RENDER_TRACE;
	RID shader = render_shader.version_get_shader(render_version, trace_mode);
	Vector<RID> scal;
	Vector<RID> light;
	Vector<RID> coarse;
	for (int i = 0; i < MAX_RENDERED; i++) {
		const Volume *v = i < int(frame_volumes.size()) ? volumes.getptr(frame_volumes[i]) : nullptr;
		scal.push_back(linear_sampler);
		scal.push_back(v ? v->scalars[v->current] : dummy_3d);
		light.push_back(linear_sampler);
		light.push_back(v ? v->light : dummy_3d);
		coarse.push_back(nearest_sampler);
		coarse.push_back(v ? v->coarse : dummy_3d);
	}
	RID radiance = frame.radiance;
	if (radiance.is_null()) {
		RendererRD::TextureStorage *ts = RendererRD::TextureStorage::get_singleton();
		radiance = ts->texture_rd_get_default(array ? RendererRD::TextureStorage::DEFAULT_RD_TEXTURE_2D_ARRAY_BLACK : RendererRD::TextureStorage::DEFAULT_RD_TEXTURE_BLACK);
	}
	{
		LocalVector<RD::Uniform> uniforms;
		uniforms.push_back(RD::Uniform(RD::UNIFORM_TYPE_UNIFORM_BUFFER, 0, render_ubo));
		uniforms.push_back(RD::Uniform(RD::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE, 1, scal));
		uniforms.push_back(RD::Uniform(RD::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE, 2, light));
		uniforms.push_back(RD::Uniform(RD::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE, 3, coarse));
		uniforms.push_back(RD::Uniform(RD::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE, 4, Vector<RID>({ nearest_sampler, p_depth })));
		uniforms.push_back(RD::Uniform(RD::UNIFORM_TYPE_IMAGE, 5, trace_tex));
		uniforms.push_back(RD::Uniform(RD::UNIFORM_TYPE_IMAGE, 6, trace_depth_tex));
		uniforms.push_back(RD::Uniform(RD::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE, 7, Vector<RID>({ linear_mip_sampler, radiance })));
		RID set = UniformSetCacheRD::get_singleton()->get_cache_vec(shader, 0, uniforms);
		RD::ComputeListID list = RD::get_singleton()->compute_list_begin();
		RD::get_singleton()->compute_list_bind_compute_pipeline(list, render_pipelines[trace_mode]);
		RD::get_singleton()->compute_list_bind_uniform_set(list, set, 0);
		const int hx0 = frame_rect.position.x / 2;
		const int hy0 = frame_rect.position.y / 2;
		const int hx1 = MIN((frame_rect.position.x + frame_rect.size.x + 1) / 2, trace_size.x);
		const int hy1 = MIN((frame_rect.position.y + frame_rect.size.y + 1) / 2, trace_size.y);
		RD::get_singleton()->compute_list_dispatch_threads(list, MAX(hx1 - hx0, 1), MAX(hy1 - hy0, 1), 1);
		RD::get_singleton()->compute_list_end();
	}
	RENDER_TIMESTAMP("FellowEngine Gas Composite");
	{
		RID cshader = render_shader.version_get_shader(render_version, RENDER_COMPOSITE);
		LocalVector<RD::Uniform> uniforms;
		uniforms.push_back(RD::Uniform(RD::UNIFORM_TYPE_UNIFORM_BUFFER, 0, render_ubo));
		uniforms.push_back(RD::Uniform(RD::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE, 1, Vector<RID>({ nearest_sampler, trace_tex })));
		uniforms.push_back(RD::Uniform(RD::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE, 2, Vector<RID>({ nearest_sampler, trace_depth_tex })));
		uniforms.push_back(RD::Uniform(RD::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE, 4, Vector<RID>({ nearest_sampler, p_depth })));
		uniforms.push_back(RD::Uniform(RD::UNIFORM_TYPE_IMAGE, 5, p_color));
		RID set = UniformSetCacheRD::get_singleton()->get_cache_vec(cshader, 0, uniforms);
		RD::ComputeListID list = RD::get_singleton()->compute_list_begin();
		RD::get_singleton()->compute_list_bind_compute_pipeline(list, render_pipelines[RENDER_COMPOSITE]);
		RD::get_singleton()->compute_list_bind_uniform_set(list, set, 0);
		RD::get_singleton()->compute_list_dispatch_threads(list, frame_rect.size.x, frame_rect.size.y, 1);
		RD::get_singleton()->compute_list_end();
	}
}
