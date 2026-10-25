/**************************************************************************/
/*  fellow_atmosphere.cpp                                                 */
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


#include "fellow_atmosphere.h"

#include "servers/rendering/renderer_rd/storage_rd/material_storage.h"
#include "servers/rendering/renderer_rd/uniform_set_cache_rd.h"

using namespace RendererRD;

static RID _fellow_atmo_texture(RD::TextureType p_type, int p_w, int p_h, int p_d) {
	RD::TextureFormat tf;
	tf.texture_type = p_type;
	tf.format = RD::DATA_FORMAT_R16G16B16A16_SFLOAT;
	tf.width = p_w;
	tf.height = p_h;
	tf.depth = p_d;
	tf.usage_bits = RD::TEXTURE_USAGE_SAMPLING_BIT | RD::TEXTURE_USAGE_STORAGE_BIT | RD::TEXTURE_USAGE_CAN_COPY_TO_BIT;
	return RD::get_singleton()->texture_create(tf, RD::TextureView());
}

FellowAtmosphere::FellowAtmosphere() {
	Vector<String> modes;
	modes.push_back("\n#define MODE_TRANSMITTANCE\n");
	modes.push_back("\n#define MODE_MULTISCATTER\n");
	modes.push_back("\n#define MODE_SKY_VIEW\n");
	modes.push_back("\n#define MODE_AERIAL\n");
	modes.push_back("\n#define MODE_APPLY\n");
	shader.initialize(modes);
	shader_version = shader.version_create();
	for (int i = 0; i < MODE_MAX; i++) {
		pipelines[i] = RD::get_singleton()->compute_pipeline_create(shader.version_get_shader(shader_version, i));
	}
	memset(&ubo_data, 0, sizeof(UBO));
	Vector<uint8_t> zeros;
	zeros.resize(sizeof(UBO));
	zeros.fill(0);
	ubo = RD::get_singleton()->uniform_buffer_create(sizeof(UBO), zeros);
	transmittance = _fellow_atmo_texture(RD::TEXTURE_TYPE_2D, TRANSMITTANCE_W, TRANSMITTANCE_H, 1);
	multiscatter = _fellow_atmo_texture(RD::TEXTURE_TYPE_2D, MULTISCATTER_SIZE, MULTISCATTER_SIZE, 1);
	sky_view = _fellow_atmo_texture(RD::TEXTURE_TYPE_2D, SKY_VIEW_W, SKY_VIEW_H, 1);
	aerial = _fellow_atmo_texture(RD::TEXTURE_TYPE_3D, AERIAL_SIZE, AERIAL_SIZE, AERIAL_DEPTH);
	dummy = _fellow_atmo_texture(RD::TEXTURE_TYPE_2D, 1, 1, 1);
	RD::get_singleton()->texture_clear(dummy, Color(0, 0, 0, 0), 0, 1, 0, 1);
	RD::get_singleton()->texture_clear(sky_view, Color(0, 0, 0, 0), 0, 1, 0, 1);
	RD::get_singleton()->texture_clear(transmittance, Color(1, 1, 1, 1), 0, 1, 0, 1);
}

FellowAtmosphere::~FellowAtmosphere() {
	RID rids[] = { ubo, transmittance, multiscatter, sky_view, aerial, dummy };
	for (RID r : rids) {
		if (r.is_valid()) {
			RD::get_singleton()->free_rid(r);
		}
	}
	shader.version_free(shader_version);
}

static void _fellow_atmo_store_matrix(const Projection &p_m, float *r_dst) {
	for (int i = 0; i < 4; i++) {
		for (int j = 0; j < 4; j++) {
			r_dst[i * 4 + j] = p_m.columns[i][j];
		}
	}
}

void FellowAtmosphere::_fill_ubo(const Params &p_params, const Size2i &p_screen) {
	const Settings &s = p_params.settings;
	UBO &u = ubo_data;
	_fellow_atmo_store_matrix(p_params.view_projection.inverse(), u.inv_view_projection);
	const Vector3 sun = p_params.sun_direction.normalized();
	u.sun_direction[0] = sun.x;
	u.sun_direction[1] = sun.y;
	u.sun_direction[2] = sun.z;
	u.sun_direction[3] = 0.0f;
	u.camera_position[0] = p_params.camera_position.x;
	u.camera_position[1] = p_params.camera_position.y;
	u.camera_position[2] = p_params.camera_position.z;
	u.camera_position[3] = p_params.ground_height;
	u.rayleigh_scattering[0] = s.rayleigh_scattering.x;
	u.rayleigh_scattering[1] = s.rayleigh_scattering.y;
	u.rayleigh_scattering[2] = s.rayleigh_scattering.z;
	u.rayleigh_scattering[3] = MAX(s.rayleigh_height, 0.01f);
	u.mie_scattering[0] = u.mie_scattering[1] = u.mie_scattering[2] = s.mie_scattering;
	u.mie_scattering[3] = MAX(s.mie_height, 0.01f);
	u.mie_absorption[0] = u.mie_absorption[1] = u.mie_absorption[2] = s.mie_absorption;
	u.mie_absorption[3] = CLAMP(s.mie_g, -0.99f, 0.99f);
	u.ozone_absorption[0] = s.ozone_absorption.x;
	u.ozone_absorption[1] = s.ozone_absorption.y;
	u.ozone_absorption[2] = s.ozone_absorption.z;
	u.ozone_absorption[3] = s.ozone_center;
	u.ground_albedo[0] = s.ground_albedo.x;
	u.ground_albedo[1] = s.ground_albedo.y;
	u.ground_albedo[2] = s.ground_albedo.z;
	u.ground_albedo[3] = MAX(s.ozone_width, 0.01f);
	u.radii[0] = s.bottom_radius;
	u.radii[1] = MAX(s.top_radius, s.bottom_radius + 1.0f);
	u.radii[2] = MAX(s.aerial_max_distance, 0.1f);
	u.radii[3] = s.multiscatter;
	u.sizes[0] = SKY_VIEW_W;
	u.sizes[1] = SKY_VIEW_H;
	u.sizes[2] = AERIAL_SIZE;
	u.sizes[3] = AERIAL_DEPTH;
	u.sun_illuminance[0] = p_params.sun_illuminance.r;
	u.sun_illuminance[1] = p_params.sun_illuminance.g;
	u.sun_illuminance[2] = p_params.sun_illuminance.b;
	u.sun_illuminance[3] = MAX(p_params.aerial_distance_scale, 0.0f);
	u.screen[0] = p_screen.x;
	u.screen[1] = p_screen.y;
	u.screen[2] = 0;
	u.screen[3] = 0;
	RD::get_singleton()->buffer_update(ubo, 0, sizeof(UBO), &u);
}

bool FellowAtmosphere::update(const Params &p_params) {
	UniformSetCacheRD *uniform_set_cache = UniformSetCacheRD::get_singleton();
	ERR_FAIL_NULL_V(uniform_set_cache, false);
	MaterialStorage *material_storage = MaterialStorage::get_singleton();
	ERR_FAIL_NULL_V(material_storage, false);
	RID linear = material_storage->sampler_rd_get_default(RSE::CANVAS_ITEM_TEXTURE_FILTER_LINEAR, RSE::CANVAS_ITEM_TEXTURE_REPEAT_DISABLED);

	_fill_ubo(p_params, Size2i());
	RD::Uniform u_params(RD::UNIFORM_TYPE_UNIFORM_BUFFER, 0, ubo);

	auto run = [&](Mode p_mode, RID p_t, RID p_m, RID p_dest, int p_x, int p_y, int p_z) {
		RID shader_rd = shader.version_get_shader(shader_version, p_mode);
		RD::ComputeListID list = RD::get_singleton()->compute_list_begin();
		RD::get_singleton()->compute_list_bind_compute_pipeline(list, pipelines[p_mode]);
		RD::Uniform u_t(RD::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE, 1, Vector<RID>({ linear, p_t }));
		RD::Uniform u_m(RD::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE, 2, Vector<RID>({ linear, p_m }));
		RD::get_singleton()->compute_list_bind_uniform_set(list, uniform_set_cache->get_cache(shader_rd, 0, u_params, u_t, u_m), 0);
		RD::Uniform u_dest(RD::UNIFORM_TYPE_IMAGE, 0, p_dest);
		RD::get_singleton()->compute_list_bind_uniform_set(list, uniform_set_cache->get_cache(shader_rd, 1, u_dest), 1);
		RD::get_singleton()->compute_list_dispatch_threads(list, p_x, p_y, p_z);
		RD::get_singleton()->compute_list_end();
	};

	RD::get_singleton()->draw_command_begin_label("FellowEngine Atmosphere");
	bool recomputed = false;
	if (!static_valid || !(static_settings == p_params.settings)) {
		recomputed = true;
		run(MODE_TRANSMITTANCE, dummy, dummy, transmittance, TRANSMITTANCE_W, TRANSMITTANCE_H, 1);
		run(MODE_MULTISCATTER, transmittance, dummy, multiscatter, MULTISCATTER_SIZE, MULTISCATTER_SIZE, 1);
		static_settings = p_params.settings;
		static_valid = true;
	}
	run(MODE_SKY_VIEW, transmittance, multiscatter, sky_view, SKY_VIEW_W, SKY_VIEW_H, 1);
	if (p_params.aerial) {
		run(MODE_AERIAL, transmittance, multiscatter, aerial, AERIAL_SIZE, AERIAL_SIZE, AERIAL_DEPTH);
	}
	RD::get_singleton()->draw_command_end_label();
	return recomputed;
}

void FellowAtmosphere::apply(const Params &p_params, RID p_color, RID p_depth, const Size2i &p_size) {
	UniformSetCacheRD *uniform_set_cache = UniformSetCacheRD::get_singleton();
	ERR_FAIL_NULL(uniform_set_cache);
	MaterialStorage *material_storage = MaterialStorage::get_singleton();
	ERR_FAIL_NULL(material_storage);
	RID linear = material_storage->sampler_rd_get_default(RSE::CANVAS_ITEM_TEXTURE_FILTER_LINEAR, RSE::CANVAS_ITEM_TEXTURE_REPEAT_DISABLED);
	RID nearest = material_storage->sampler_rd_get_default(RSE::CANVAS_ITEM_TEXTURE_FILTER_NEAREST, RSE::CANVAS_ITEM_TEXTURE_REPEAT_DISABLED);

	_fill_ubo(p_params, p_size);
	RD::get_singleton()->draw_command_begin_label("FellowEngine Aerial Perspective");
	RID shader_rd = shader.version_get_shader(shader_version, MODE_APPLY);
	RD::ComputeListID list = RD::get_singleton()->compute_list_begin();
	RD::get_singleton()->compute_list_bind_compute_pipeline(list, pipelines[MODE_APPLY]);
	RD::Uniform u_params(RD::UNIFORM_TYPE_UNIFORM_BUFFER, 0, ubo);
	RD::Uniform u_t(RD::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE, 1, Vector<RID>({ linear, transmittance }));
	RD::Uniform u_m(RD::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE, 2, Vector<RID>({ linear, multiscatter }));
	RD::Uniform u_a(RD::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE, 3, Vector<RID>({ linear, aerial }));
	RD::Uniform u_d(RD::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE, 4, Vector<RID>({ nearest, p_depth }));
	RD::get_singleton()->compute_list_bind_uniform_set(list, uniform_set_cache->get_cache(shader_rd, 0, u_params, u_t, u_m, u_a, u_d), 0);
	RD::Uniform u_c(RD::UNIFORM_TYPE_IMAGE, 0, p_color);
	RD::get_singleton()->compute_list_bind_uniform_set(list, uniform_set_cache->get_cache(shader_rd, 1, u_c), 1);
	RD::get_singleton()->compute_list_dispatch_threads(list, p_size.x, p_size.y, 1);
	RD::get_singleton()->compute_list_end();
	RD::get_singleton()->draw_command_end_label();
}
