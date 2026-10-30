/**************************************************************************/
/*  fellow_gas_burn.cpp                                                   */
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

// FellowEngine burning solids (decision 0120): the render-thread side of FellowBurnable3D, coupled with the gas
// volumes of fellow_gas.cpp (the gas's EMIT and OBSTACLES read each burnable's INJECT texture).

#include "fellow_gas.h"

#include "core/object/callable_mp.h"
#include "servers/rendering/renderer_rd/storage_rd/material_storage.h"
#include "servers/rendering/renderer_rd/uniform_set_cache_rd.h"

using namespace RendererRD;

static RID _fellow_burn_tex3d(RD::DataFormat p_format, const Vector3i &p_size, uint32_t p_usage, const Vector<uint8_t> &p_data = Vector<uint8_t>()) {
	RD::TextureFormat tf;
	tf.texture_type = RD::TEXTURE_TYPE_3D;
	tf.format = p_format;
	tf.width = MAX(p_size.x, 1);
	tf.height = MAX(p_size.y, 1);
	tf.depth = MAX(p_size.z, 1);
	tf.usage_bits = p_usage;
	Vector<Vector<uint8_t>> data;
	if (!p_data.is_empty()) {
		data.push_back(p_data);
	}
	return RD::get_singleton()->texture_create(tf, RD::TextureView(), data);
}

void FellowGas::_ensure_burn() {
	if (burn_ready) {
		return;
	}
	Vector<String> modes;
	const char *names[BURN_MAX] = { "IGNITE", "HEAT", "SETTLE", "INJECT", "REDUCE", "VERTICES" };
	for (int i = 0; i < BURN_MAX; i++) {
		modes.push_back(String("\n#define MODE_") + names[i] + "\n");
	}
	burn_shader.initialize(modes, "");
	burn_version = burn_shader.version_create();
	for (int i = 0; i < BURN_MAX; i++) {
		burn_pipelines[i] = RD::get_singleton()->compute_pipeline_create(burn_shader.version_get_shader(burn_version, i));
	}
	burn_ready = true;
}

void FellowGas::_free_burnable(Burnable &r_b) {
	RID *rids[] = { &r_b.state[0], &r_b.state[1], &r_b.display, &r_b.emission[0], &r_b.emission[1], &r_b.inject, &r_b.ubo, &r_b.totals, &r_b.vertices, &r_b.vertex_params, &r_b.vertex_result };
	for (RID *r : rids) {
		if (r->is_valid()) {
			RD::get_singleton()->free_rid(*r);
			*r = RID();
		}
	}
}

void FellowGas::burnable_configure(uint32_t p_id, const Vector3i &p_res, const Vector<uint8_t> &p_initial) {
	ERR_FAIL_COND(p_res.x < 1 || p_res.y < 1 || p_res.z < 1);
	ERR_FAIL_COND(p_initial.size() != int64_t(p_res.x) * p_res.y * p_res.z * 16);
	_ensure_burn();
	Burnable *b = burnables.getptr(p_id);
	if (!b) {
		b = &burnables.insert(p_id, Burnable())->value;
	}
	_free_burnable(*b);
	b->res = p_res;
	const uint32_t state_usage = RD::TEXTURE_USAGE_STORAGE_BIT | RD::TEXTURE_USAGE_CAN_COPY_FROM_BIT | RD::TEXTURE_USAGE_CAN_COPY_TO_BIT | RD::TEXTURE_USAGE_CAN_UPDATE_BIT;
	b->state[0] = _fellow_burn_tex3d(RD::DATA_FORMAT_R32G32B32A32_SFLOAT, p_res, state_usage, p_initial);
	b->state[1] = _fellow_burn_tex3d(RD::DATA_FORMAT_R32G32B32A32_SFLOAT, p_res, state_usage, p_initial);
	RD::get_singleton()->set_resource_name(b->state[0], "FellowBurn state 0");
	RD::get_singleton()->set_resource_name(b->state[1], "FellowBurn state 1");
	// The display copy: the initial state in half precision.
	Vector<uint8_t> half;
	half.resize(p_initial.size() / 2);
	{
		const float *src = reinterpret_cast<const float *>(p_initial.ptr());
		uint16_t *dst = reinterpret_cast<uint16_t *>(half.ptrw());
		for (int64_t i = 0; i < p_initial.size() / 4; i++) {
			dst[i] = Math::make_half_float(src[i]);
		}
	}
	const uint32_t view_usage = RD::TEXTURE_USAGE_SAMPLING_BIT | RD::TEXTURE_USAGE_STORAGE_BIT | RD::TEXTURE_USAGE_CAN_COPY_FROM_BIT | RD::TEXTURE_USAGE_CAN_COPY_TO_BIT;
	b->display = _fellow_burn_tex3d(RD::DATA_FORMAT_R16G16B16A16_SFLOAT, p_res, view_usage, half);
	RD::get_singleton()->set_resource_name(b->display, "FellowBurn display");
	for (int i = 0; i < 2; i++) {
		b->emission[i] = _fellow_burn_tex3d(RD::DATA_FORMAT_R16G16B16A16_SFLOAT, p_res, view_usage);
		RD::get_singleton()->texture_clear(b->emission[i], Color(0, 0, 0, 0), 0, 1, 0, 1);
	}
	b->emission_current = 0;
	Vector<uint8_t> zeros;
	zeros.resize(sizeof(BurnUBO));
	zeros.fill(0);
	b->ubo = RD::get_singleton()->uniform_buffer_create(sizeof(BurnUBO), zeros);
	const uint32_t groups = uint32_t((p_res.x + 3) / 4) * uint32_t((p_res.y + 3) / 4) * uint32_t((p_res.z + 3) / 4);
	b->totals = RD::get_singleton()->storage_buffer_create(groups * 32);
	b->current = 0;
	b->pending.clear();
	b->injected = false;
	b->inject_size = Vector3i();
}

void FellowGas::burnable_free(uint32_t p_id) {
	Burnable *b = burnables.getptr(p_id);
	if (!b) {
		return;
	}
	_free_burnable(*b);
	burnables.erase(p_id);
}

void FellowGas::burnable_queue_step(uint32_t p_id, const BurnStepParams &p_step) {
	Burnable *b = burnables.getptr(p_id);
	ERR_FAIL_NULL(b);
	if (b->pending.size() >= 64) {
		b->pending.remove_at(0);
	}
	b->pending.push_back(p_step);
}

RID FellowGas::burnable_set_vertices(uint32_t p_id, const Vector<uint8_t> &p_vertices, uint32_t p_count, uint32_t p_row, const Vector<uint8_t> &p_params) {
	Burnable *b = burnables.getptr(p_id);
	ERR_FAIL_NULL_V(b, RID());
	ERR_FAIL_COND_V(p_vertices.size() != int64_t(p_count) * 48 || p_params.size() != 64 || p_count == 0 || p_row == 0, RID());
	RID *rids[] = { &b->vertices, &b->vertex_params, &b->vertex_result };
	for (RID *r : rids) {
		if (r->is_valid()) {
			RD::get_singleton()->free_rid(*r);
			*r = RID();
		}
	}
	b->vertices = RD::get_singleton()->storage_buffer_create(p_vertices.size(), p_vertices);
	b->vertex_params = RD::get_singleton()->uniform_buffer_create(64, p_params);
	RD::TextureFormat tf;
	tf.format = RD::DATA_FORMAT_R32G32B32A32_SFLOAT;
	tf.width = p_row * 3;
	tf.height = (p_count + p_row - 1) / p_row;
	tf.usage_bits = RD::TEXTURE_USAGE_SAMPLING_BIT | RD::TEXTURE_USAGE_STORAGE_BIT;
	b->vertex_result = RD::get_singleton()->texture_create(tf, RD::TextureView());
	RD::get_singleton()->set_resource_name(b->vertex_result, "FellowBurn vertices");
	b->vertex_count = p_count;
	b->vertex_row = p_row;
	b->vertex_dirty = true; // marched at the start of the next frame (process_pending)
	return b->vertex_result;
}

void FellowGas::_burn_vertices(Burnable &r_b) {
	r_b.vertex_dirty = false;
	if (r_b.vertex_count == 0 || r_b.vertex_result.is_null()) {
		return;
	}
	MaterialStorage *material_storage = MaterialStorage::get_singleton();
	// The materials' sampler for the state (filter_linear, repeat_disable).
	const RID sampler = material_storage->sampler_rd_get_default(RSE::CANVAS_ITEM_TEXTURE_FILTER_LINEAR, RSE::CANVAS_ITEM_TEXTURE_REPEAT_DISABLED);
	RD::get_singleton()->draw_command_begin_label("FellowEngine Burn Vertices");
	_dispatch_burn(r_b, BURN_VERTICES, { RD::Uniform(RD::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE, 1, Vector<RID>({ sampler, r_b.display })), RD::Uniform(RD::UNIFORM_TYPE_STORAGE_BUFFER, 2, r_b.vertices), RD::Uniform(RD::UNIFORM_TYPE_IMAGE, 3, r_b.vertex_result), RD::Uniform(RD::UNIFORM_TYPE_UNIFORM_BUFFER, 4, r_b.vertex_params) }, Vector3i(4, 4, int((r_b.vertex_count + 15) / 16)));
	RD::get_singleton()->draw_command_end_label();
}

RID FellowGas::burnable_get_display(uint32_t p_id) const {
	const Burnable *b = burnables.getptr(p_id);
	return b ? b->display : RID();
}

void FellowGas::burnable_set_totals_callback(uint32_t p_id, const Callable &p_callback) {
	Burnable *b = burnables.getptr(p_id);
	ERR_FAIL_NULL(b);
	b->totals_callback = p_callback;
}

void FellowGas::burnable_debug_read(uint32_t p_id, int p_what, const Callable &p_callback) {
	Burnable *b = burnables.getptr(p_id);
	ERR_FAIL_NULL(b);
	for (const BurnStepParams &s : b->pending) {
		_run_burn_step(p_id, *b, s);
	}
	b->pending.clear();
	if (b->vertex_dirty) {
		_burn_vertices(*b);
	}
	RID tex = p_what == 1 ? b->emission[b->emission_current] : (p_what == 2 ? b->inject : b->state[b->current]);
	if (tex.is_null()) {
		p_callback.call(Vector<uint8_t>());
		return;
	}
	RD::get_singleton()->texture_get_data_async(tex, 0, p_callback);
}

void FellowGas::_burn_totals_read(const Vector<uint8_t> &p_data, Callable p_callback, uint32_t p_id) {
	if (singleton) {
		Burnable *b = singleton->burnables.getptr(p_id);
		if (b) {
			b->totals_in_flight = false;
		}
	}
	if (p_callback.is_valid()) {
		p_callback.call(p_data);
	}
}

void FellowGas::_dispatch_burn(Burnable &r_b, BurnMode p_mode, const Vector<RD::Uniform> &p_uniforms, const Vector3i &p_threads) {
	RID shader = burn_shader.version_get_shader(burn_version, p_mode);
	LocalVector<RD::Uniform> u;
	u.push_back(RD::Uniform(RD::UNIFORM_TYPE_UNIFORM_BUFFER, 0, r_b.ubo));
	for (const RD::Uniform &x : p_uniforms) {
		u.push_back(x);
	}
	RID set = UniformSetCacheRD::get_singleton()->get_cache_vec(shader, 0, u);
	RD::ComputeListID list = RD::get_singleton()->compute_list_begin();
	RD::get_singleton()->compute_list_bind_compute_pipeline(list, burn_pipelines[p_mode]);
	RD::get_singleton()->compute_list_bind_uniform_set(list, set, 0);
	RD::get_singleton()->compute_list_dispatch_threads(list, MAX(p_threads.x, 1), MAX(p_threads.y, 1), MAX(p_threads.z, 1));
	RD::get_singleton()->compute_list_end();
}

void FellowGas::_run_burn_step(uint32_t p_id, Burnable &r_b, const BurnStepParams &p_step) {
	const Vector3i n = r_b.res;
	const Volume *gas = p_step.gas_id != 0 ? volumes.getptr(p_step.gas_id) : nullptr;
	if (gas && gas->ubo.is_null()) {
		gas = nullptr;
	}
	BurnUBO u;
	memset(&u, 0, sizeof(BurnUBO));
	u.size[0] = n.x;
	u.size[1] = n.y;
	u.size[2] = n.z;
	u.size[3] = gas ? 1 : 0;
	u.grid[0] = p_step.voxel_size;
	u.grid[1] = MAX(p_step.dt, 1e-6f);
	u.grid[2] = 1.0f / MAX(p_step.dt, 1e-6f);
	u.grid[3] = p_step.settle;
	memcpy(u.densities, p_step.densities, sizeof(u.densities));
	memcpy(u.heats, p_step.heats, sizeof(u.heats));
	memcpy(u.conduct, p_step.conduct, sizeof(u.conduct));
	memcpy(u.pyro, p_step.pyro, sizeof(u.pyro));
	memcpy(u.glow, p_step.glow, sizeof(u.glow));
	memcpy(u.misc, p_step.misc, sizeof(u.misc));
	for (int r = 0; r < 3; r++) {
		for (int c = 0; c < 4; c++) {
			u.voxel_to_gas[r][c] = p_step.voxel_to_gas[r * 4 + c];
			u.gas_to_voxel[r][c] = p_step.gas_to_voxel[r * 4 + c];
		}
	}
	if (gas) {
		u.gas_size[0] = gas->res.x;
		u.gas_size[1] = gas->res.y;
		u.gas_size[2] = gas->res.z;
	}
	memcpy(u.ignite, p_step.ignite, sizeof(u.ignite));
	u.ignite_temperature[0] = p_step.ignite_temperature;
	for (int i = 0; i < 3; i++) {
		u.region_origin[i] = p_step.region_origin[i];
		u.region_size[i] = p_step.region_size[i];
	}
	u.inject[0] = p_step.footprint;
	u.settle[0] = CLAMP(p_step.settle_axis, 0, 2);
	u.settle[1] = p_step.settle_sign < 0 ? -1 : 1;
	u.settle[2] = p_step.settle_floor;
	u.flame[0] = p_step.flame_temperature;
	u.flame[1] = MAX(p_step.flame_flux, 1e-6f);
	u.flame[2] = CLAMP(Math::round(p_step.flame_reach), 0.0f, 8.0f);
	RD::get_singleton()->buffer_update(r_b.ubo, 0, sizeof(BurnUBO), &u);

	auto image = [](int p_binding, RID p_tex) { return RD::Uniform(RD::UNIFORM_TYPE_IMAGE, p_binding, p_tex); };
	auto sampled = [this](int p_binding, RID p_tex) { return RD::Uniform(RD::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE, p_binding, Vector<RID>({ linear_sampler, p_tex })); };
	const uint32_t cur = r_b.current;
	const uint32_t nxt = cur ^ 1u;
	RD::get_singleton()->draw_command_begin_label("FellowEngine Burn Step");
	if (p_step.ignite[3] > 0.0f) {
		_dispatch_burn(r_b, BURN_IGNITE, { image(1, r_b.state[cur]) }, n);
	}
	RID gas_scalars = gas ? gas->scalars[gas->current] : dummy_3d;
	// The emission ping-pongs: HEAT reads the step before's (the surface's own flame) and writes this step's.
	const uint32_t em_prev = r_b.emission_current;
	r_b.emission_current ^= 1u;
	const RID em = r_b.emission[r_b.emission_current];
	_dispatch_burn(r_b, BURN_HEAT, { image(1, r_b.state[cur]), image(2, r_b.state[nxt]), image(3, em), sampled(4, gas_scalars), image(5, r_b.emission[em_prev]) }, n);
	_dispatch_burn(r_b, BURN_SETTLE, { image(1, r_b.state[nxt]), image(2, r_b.state[cur]), image(3, r_b.display) }, n);
	r_b.vertex_dirty = r_b.vertex_count > 0; // marched again after this frame's steps (process_pending)
	// The gas's region (its EMIT and OBSTACLES at the next step).
	const Vector3i region(p_step.region_size[0], p_step.region_size[1], p_step.region_size[2]);
	if (gas && region.x > 0 && region.y > 0 && region.z > 0) {
		if (r_b.inject.is_null() || r_b.inject_size != region) {
			if (r_b.inject.is_valid()) {
				RD::get_singleton()->free_rid(r_b.inject);
			}
			r_b.inject = _fellow_burn_tex3d(RD::DATA_FORMAT_R16G16B16A16_SFLOAT, region, RD::TEXTURE_USAGE_SAMPLING_BIT | RD::TEXTURE_USAGE_STORAGE_BIT | RD::TEXTURE_USAGE_CAN_COPY_FROM_BIT);
			r_b.inject_size = region;
		}
		_dispatch_burn(r_b, BURN_INJECT, { sampled(1, em), image(2, r_b.inject) }, region);
		r_b.gas_id = p_step.gas_id;
		for (int i = 0; i < 3; i++) {
			r_b.region_origin[i] = p_step.region_origin[i];
			r_b.region_size[i] = p_step.region_size[i];
		}
		r_b.smoke_per_fuel = p_step.smoke_per_fuel;
		r_b.heat_rate = p_step.heat_rate;
		r_b.attached = p_step.attached;
		r_b.flicker = p_step.flicker;
		r_b.injected = true;
	} else {
		r_b.injected = false;
		r_b.gas_id = 0;
	}
	if (p_step.read_totals && r_b.totals_callback.is_valid() && !r_b.totals_in_flight) {
		_dispatch_burn(r_b, BURN_REDUCE, { image(1, r_b.state[cur]), image(2, em), RD::Uniform(RD::UNIFORM_TYPE_STORAGE_BUFFER, 3, r_b.totals) }, n);
		r_b.totals_in_flight = true;
		RD::get_singleton()->buffer_get_data_async(r_b.totals, callable_mp_static(&FellowGas::_burn_totals_read).bind(r_b.totals_callback, p_id));
	}
	RD::get_singleton()->draw_command_end_label();
}

int FellowGas::_gather_burnables(uint32_t p_volume, SimUBO &r_ubo) {
	step_burn_uniforms.clear();
	LocalVector<uint32_t> ids;
	for (const KeyValue<uint32_t, Burnable> &kv : burnables) {
		if (kv.value.injected && kv.value.gas_id == p_volume && kv.value.inject.is_valid()) {
			ids.push_back(kv.key);
		}
	}
	ids.sort();
	const int count = MIN(int(ids.size()), MAX_BURNABLES);
	Vector<RID> textures;
	for (int i = 0; i < MAX_BURNABLES; i++) {
		RID tex = dummy_3d;
		if (i < count) {
			const Burnable &b = burnables[ids[i]];
			tex = b.inject;
			for (int a = 0; a < 3; a++) {
				r_ubo.burn_origin[i][a] = b.region_origin[a];
				r_ubo.burn_size[i][a] = b.region_size[a];
			}
			r_ubo.burn_params[i][0] = b.smoke_per_fuel;
			r_ubo.burn_params[i][1] = b.heat_rate;
			r_ubo.burn_params[i][2] = b.attached;
			r_ubo.burn_params[i][3] = b.flicker;
		}
		textures.push_back(nearest_sampler);
		textures.push_back(tex);
	}
	r_ubo.burn_count[0] = count;
	step_burn_uniforms.push_back(RD::Uniform(RD::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE, 16, textures));
	return count;
}
