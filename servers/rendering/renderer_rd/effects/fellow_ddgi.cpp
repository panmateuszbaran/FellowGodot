/**************************************************************************/
/*  fellow_ddgi.cpp                                                       */
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


#include "fellow_ddgi.h"

#include "servers/rendering/renderer_rd/storage_rd/material_storage.h"
#include "servers/rendering/renderer_rd/storage_rd/texture_storage.h"
#include "servers/rendering/renderer_rd/uniform_set_cache_rd.h"

using namespace RendererRD;

FellowDDGI::FellowDDGI() {
	Vector<String> modes;
	modes.push_back("\n#define MODE_TRACE\n");
	modes.push_back("\n#define MODE_TRACE\n#define FELLOW_POSITION_FETCH\n");
	modes.push_back("\n#define MODE_TRACE\n#define FELLOW_RADIANCE_ARRAY\n");
	modes.push_back("\n#define MODE_TRACE\n#define FELLOW_POSITION_FETCH\n#define FELLOW_RADIANCE_ARRAY\n");
	modes.push_back("\n#define MODE_DISTANCE\n");
	modes.push_back("\n#define MODE_IRRADIANCE\n");
	shader.initialize(modes);
	position_fetch = RD::get_singleton()->fellow_ray_position_fetch_supported();
	if (!position_fetch) {
		shader.set_variant_enabled(MODE_TRACE_FETCH, false);
		shader.set_variant_enabled(MODE_TRACE_FETCH_ARRAY, false);
	}
	shader_version = shader.version_create();
	for (int i = 0; i < MODE_MAX; i++) {
		if (shader.is_variant_enabled(i)) {
			pipelines[i] = RD::get_singleton()->compute_pipeline_create(shader.version_get_shader(shader_version, i));
		}
	}
	ubo = RD::get_singleton()->uniform_buffer_create(sizeof(UBO));
}

FellowDDGI::~FellowDDGI() {
	RD::get_singleton()->free_rid(ubo);
	shader.version_free(shader_version);
}

static void _fellow_store_basis(const Basis &p_basis, float *r_dst) {
	// A mat4 (column-major) with the basis in its upper 3 x 3.
	for (int c = 0; c < 4; c++) {
		for (int r = 0; r < 4; r++) {
			r_dst[c * 4 + r] = (c < 3 && r < 3) ? p_basis.rows[r][c] : (c == r ? 1.0f : 0.0f);
		}
	}
}

void FellowDDGI::store_volume(const Volume &p_volume, uint8_t *r_dst) {
	memcpy(r_dst, &p_volume, sizeof(Volume));
}

void FellowDDGI::process(const Params &p_params) {
	UniformSetCacheRD *uniform_set_cache = UniformSetCacheRD::get_singleton();
	ERR_FAIL_NULL(uniform_set_cache);
	MaterialStorage *material_storage = MaterialStorage::get_singleton();
	ERR_FAIL_NULL(material_storage);
	RID linear = material_storage->sampler_rd_get_default(RSE::CANVAS_ITEM_TEXTURE_FILTER_LINEAR_WITH_MIPMAPS, RSE::CANVAS_ITEM_TEXTURE_REPEAT_DISABLED);

	const int32_t probes = p_params.volume.counts[0] * p_params.volume.counts[1] * p_params.volume.counts[2];
	const int32_t rays = p_params.volume.counts[3];
	ERR_FAIL_COND(probes <= 0 || rays <= 0);

	UBO u;
	u.volume = p_params.volume;
	_fellow_store_basis(p_params.ray_rotation, u.ray_rotation);
	u.sun_direction[0] = p_params.sun_direction.x;
	u.sun_direction[1] = p_params.sun_direction.y;
	u.sun_direction[2] = p_params.sun_direction.z;
	u.sun_direction[3] = p_params.has_sun ? 1.0f : 0.0f;
	u.sun_color[0] = p_params.sun_color.r;
	u.sun_color[1] = p_params.sun_color.g;
	u.sun_color[2] = p_params.sun_color.b;
	u.sun_color[3] = p_params.sun_shadow ? 1.0f : 0.0f;
	u.ambient_color[0] = p_params.ambient_color.r;
	u.ambient_color[1] = p_params.ambient_color.g;
	u.ambient_color[2] = p_params.ambient_color.b;
	u.ambient_color[3] = p_params.ambient_color.a;
	const bool sky = p_params.use_sky && p_params.radiance.is_valid();
	u.sky_params[0] = p_params.use_ambient ? 1.0f : 0.0f;
	u.sky_params[1] = sky ? 1.0f : 0.0f;
	u.sky_params[2] = p_params.sky_factor;
	u.sky_params[3] = p_params.radiance_level;
	_fellow_store_basis(p_params.sky_xform, u.sky_xform);
	u.radiance_border[0] = p_params.radiance_border[0];
	u.radiance_border[1] = p_params.radiance_border[1];
	u.radiance_border[2] = 0.0f;
	u.radiance_border[3] = 0.0f;
	u.trace[0] = p_params.ray_length;
	u.trace[1] = p_params.energy_keep;
	u.trace[2] = p_params.hysteresis;
	u.trace[3] = p_params.distance_hysteresis;
	u.frame[0] = int32_t(p_params.frame);
	u.frame[1] = u.frame[2] = u.frame[3] = 0;
	RD::get_singleton()->buffer_update(ubo, 0, sizeof(UBO), &u);

	RD::Uniform u_params(RD::UNIFORM_TYPE_UNIFORM_BUFFER, 0, ubo);
	RD::Uniform u_rays(RD::UNIFORM_TYPE_IMAGE, 1, p_params.ray_data);

	RD::get_singleton()->draw_command_begin_label("FellowEngine DDGI");
	{
		Mode mode = MODE_TRACE;
		if (p_params.radiance_array && sky) {
			mode = position_fetch ? MODE_TRACE_FETCH_ARRAY : MODE_TRACE_ARRAY;
		} else {
			mode = position_fetch ? MODE_TRACE_FETCH : MODE_TRACE;
		}
		RID radiance = sky ? p_params.radiance : RID();
		if (radiance.is_null()) {
			// Not read (no sky): any texture of the declared kind.
			TextureStorage *ts = TextureStorage::get_singleton();
			radiance = ts->texture_rd_get_default((mode == MODE_TRACE_ARRAY || mode == MODE_TRACE_FETCH_ARRAY) ? TextureStorage::DEFAULT_RD_TEXTURE_2D_ARRAY_BLACK : TextureStorage::DEFAULT_RD_TEXTURE_BLACK);
		}
		RD::ComputeListID list = RD::get_singleton()->compute_list_begin();
		RID shader_rd = shader.version_get_shader(shader_version, mode);
		RD::get_singleton()->compute_list_bind_compute_pipeline(list, pipelines[mode]);
		RD::Uniform u_tlas(RD::UNIFORM_TYPE_ACCELERATION_STRUCTURE, 2, p_params.tlas);
		RD::Uniform u_irr(RD::UNIFORM_TYPE_TEXTURE, 3, p_params.irradiance);
		RD::Uniform u_dist(RD::UNIFORM_TYPE_TEXTURE, 4, p_params.distance);
		RD::Uniform u_sampler(RD::UNIFORM_TYPE_SAMPLER, 5, linear);
		RD::Uniform u_inst(RD::UNIFORM_TYPE_STORAGE_BUFFER, 6, p_params.instances);
		RD::Uniform u_rad(RD::UNIFORM_TYPE_TEXTURE, 7, radiance);
		RD::get_singleton()->compute_list_bind_uniform_set(list, uniform_set_cache->get_cache(shader_rd, 0, u_params, u_rays, u_tlas, u_irr, u_dist, u_sampler, u_inst, u_rad), 0);
		RD::get_singleton()->compute_list_dispatch_threads(list, rays, probes, 1);
		RD::get_singleton()->compute_list_end();
	}
	{
		RD::ComputeListID list = RD::get_singleton()->compute_list_begin();
		RID shader_rd = shader.version_get_shader(shader_version, MODE_DISTANCE);
		RD::get_singleton()->compute_list_bind_compute_pipeline(list, pipelines[MODE_DISTANCE]);
		RD::Uniform u_dist(RD::UNIFORM_TYPE_IMAGE, 2, p_params.distance);
		RD::Uniform u_cells(RD::UNIFORM_TYPE_STORAGE_BUFFER, 3, p_params.probe_cells);
		RD::get_singleton()->compute_list_bind_uniform_set(list, uniform_set_cache->get_cache(shader_rd, 0, u_params, u_rays, u_dist, u_cells), 0);
		RD::get_singleton()->compute_list_dispatch(list, probes, 1, 1);
		RD::get_singleton()->compute_list_end();
	}
	{
		RD::ComputeListID list = RD::get_singleton()->compute_list_begin();
		RID shader_rd = shader.version_get_shader(shader_version, MODE_IRRADIANCE);
		RD::get_singleton()->compute_list_bind_compute_pipeline(list, pipelines[MODE_IRRADIANCE]);
		RD::Uniform u_irr(RD::UNIFORM_TYPE_IMAGE, 2, p_params.irradiance);
		RD::Uniform u_cells(RD::UNIFORM_TYPE_STORAGE_BUFFER, 3, p_params.probe_cells);
		RD::get_singleton()->compute_list_bind_uniform_set(list, uniform_set_cache->get_cache(shader_rd, 0, u_params, u_rays, u_irr, u_cells), 0);
		RD::get_singleton()->compute_list_dispatch(list, probes, 1, 1);
		RD::get_singleton()->compute_list_end();
	}
	RD::get_singleton()->draw_command_end_label();
}
