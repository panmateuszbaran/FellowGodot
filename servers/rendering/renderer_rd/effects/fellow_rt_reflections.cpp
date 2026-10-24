/**************************************************************************/
/*  fellow_rt_reflections.cpp                                             */
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


#include "fellow_rt_reflections.h"

#include "servers/rendering/renderer_rd/storage_rd/material_storage.h"
#include "servers/rendering/renderer_rd/storage_rd/texture_storage.h"
#include "servers/rendering/renderer_rd/uniform_set_cache_rd.h"

using namespace RendererRD;

FellowRTReflections::FellowRTReflections() {
	Vector<String> modes;
	modes.push_back("\n#define MODE_TRACE\n");
	modes.push_back("\n#define MODE_TRACE\n#define FELLOW_POSITION_FETCH\n");
	modes.push_back("\n#define MODE_TRACE\n#define FELLOW_RADIANCE_ARRAY\n");
	modes.push_back("\n#define MODE_TRACE\n#define FELLOW_POSITION_FETCH\n#define FELLOW_RADIANCE_ARRAY\n");
	modes.push_back("\n#define MODE_TEMPORAL\n");
	modes.push_back("\n#define MODE_DENOISE\n");
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

FellowRTReflections::~FellowRTReflections() {
	RD::get_singleton()->free_rid(ubo);
	shader.version_free(shader_version);
}

static void _fellow_store_mat4(const Projection &p_m, float *r_dst) {
	for (int c = 0; c < 4; c++) {
		for (int r = 0; r < 4; r++) {
			r_dst[c * 4 + r] = p_m.columns[c][r];
		}
	}
}

void FellowRTReflections::process(const Params &p_params) {
	UniformSetCacheRD *uniform_set_cache = UniformSetCacheRD::get_singleton();
	ERR_FAIL_NULL(uniform_set_cache);
	MaterialStorage *material_storage = MaterialStorage::get_singleton();
	ERR_FAIL_NULL(material_storage);
	RID nearest = material_storage->sampler_rd_get_default(RSE::CANVAS_ITEM_TEXTURE_FILTER_NEAREST, RSE::CANVAS_ITEM_TEXTURE_REPEAT_DISABLED);
	RID linear = material_storage->sampler_rd_get_default(RSE::CANVAS_ITEM_TEXTURE_FILTER_LINEAR_WITH_MIPMAPS, RSE::CANVAS_ITEM_TEXTURE_REPEAT_DISABLED);

	Projection correction;
	correction.set_depth_correction(true, true, false);
	UBO u;
	_fellow_store_mat4((correction * p_params.projection).inverse(), u.inv_projection);
	_fellow_store_mat4(Projection(p_params.cam_transform), u.view_to_world);
	_fellow_store_mat4((correction * p_params.prev_projection) * Projection(p_params.prev_cam_transform.affine_inverse()), u.world_to_prev_clip);
	u.volume = p_params.volume;
	u.outer = p_params.outer;
	const bool outer = p_params.outer.base_cell[3] != 0 && p_params.outer_irradiance.is_valid() && p_params.outer_distance.is_valid();
	if (!outer) {
		u.outer.base_cell[3] = 0;
	}
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
	Projection sky_xform; // a mat4 holding the basis
	sky_xform.set_identity();
	for (int c = 0; c < 3; c++) {
		for (int r = 0; r < 3; r++) {
			sky_xform.columns[c][r] = p_params.sky_xform.rows[r][c];
		}
	}
	_fellow_store_mat4(sky_xform, u.sky_xform);
	u.radiance_border[0] = p_params.radiance_border[0];
	u.radiance_border[1] = p_params.radiance_border[1];
	u.radiance_border[2] = u.radiance_border[3] = 0.0f;
	u.size[0] = p_params.size.x;
	u.size[1] = p_params.size.y;
	u.frame = p_params.frame;
	u.max_roughness = p_params.max_roughness;
	u.ray_length = p_params.ray_length;
	u.history_valid = p_params.history_valid ? 1 : 0;
	u.interleave_roughness = p_params.interleave_roughness;
	u.pad = 0;
	RD::get_singleton()->buffer_update(ubo, 0, sizeof(UBO), &u);

	RD::Uniform u_params(RD::UNIFORM_TYPE_UNIFORM_BUFFER, 0, ubo);
	RD::Uniform u_depth(RD::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE, 1, Vector<RID>({ nearest, p_params.depth }));
	RD::Uniform u_normal(RD::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE, 2, Vector<RID>({ nearest, p_params.normal_roughness }));

	RD::get_singleton()->draw_command_begin_label("FellowEngine RT Reflections");
	{
		const bool array = p_params.radiance_array && sky;
		const Mode mode = array ? (position_fetch ? MODE_TRACE_FETCH_ARRAY : MODE_TRACE_ARRAY) : (position_fetch ? MODE_TRACE_FETCH : MODE_TRACE);
		RID radiance = sky ? p_params.radiance : TextureStorage::get_singleton()->texture_rd_get_default(array ? TextureStorage::DEFAULT_RD_TEXTURE_2D_ARRAY_BLACK : TextureStorage::DEFAULT_RD_TEXTURE_BLACK);
		RD::ComputeListID list = RD::get_singleton()->compute_list_begin();
		RID shader_rd = shader.version_get_shader(shader_version, mode);
		RD::get_singleton()->compute_list_bind_compute_pipeline(list, pipelines[mode]);
		RD::Uniform u_tlas(RD::UNIFORM_TYPE_ACCELERATION_STRUCTURE, 3, p_params.tlas);
		RD::Uniform u_raw(RD::UNIFORM_TYPE_IMAGE, 4, p_params.raw);
		RD::Uniform u_irr(RD::UNIFORM_TYPE_TEXTURE, 5, p_params.irradiance);
		RD::Uniform u_dist(RD::UNIFORM_TYPE_TEXTURE, 6, p_params.distance);
		RD::Uniform u_sampler(RD::UNIFORM_TYPE_SAMPLER, 7, linear);
		RD::Uniform u_inst(RD::UNIFORM_TYPE_STORAGE_BUFFER, 8, p_params.instances);
		RD::Uniform u_rad(RD::UNIFORM_TYPE_TEXTURE, 9, radiance);
		RID outer_irradiance = outer ? p_params.outer_irradiance : TextureStorage::get_singleton()->texture_rd_get_default(TextureStorage::DEFAULT_RD_TEXTURE_BLACK);
		RD::Uniform u_outer_irr(RD::UNIFORM_TYPE_TEXTURE, 10, outer_irradiance);
		RD::Uniform u_outer_dist(RD::UNIFORM_TYPE_TEXTURE, 11, outer ? p_params.outer_distance : outer_irradiance);
		RD::get_singleton()->compute_list_bind_uniform_set(list, uniform_set_cache->get_cache(shader_rd, 0, u_params, u_depth, u_normal, u_tlas, u_raw, u_irr, u_dist, u_sampler, u_inst, u_rad, u_outer_irr, u_outer_dist), 0);
		RD::get_singleton()->compute_list_dispatch_threads(list, p_params.size.x, p_params.size.y, 1);
		RD::get_singleton()->compute_list_end();
	}
	{
		RD::ComputeListID list = RD::get_singleton()->compute_list_begin();
		RID shader_rd = shader.version_get_shader(shader_version, MODE_TEMPORAL);
		RD::get_singleton()->compute_list_bind_compute_pipeline(list, pipelines[MODE_TEMPORAL]);
		RD::Uniform u_raw(RD::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE, 3, Vector<RID>({ nearest, p_params.raw }));
		RD::Uniform u_history(RD::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE, 4, Vector<RID>({ linear, p_params.history_prev }));
		RD::Uniform u_out(RD::UNIFORM_TYPE_IMAGE, 5, p_params.history_out);
		RD::get_singleton()->compute_list_bind_uniform_set(list, uniform_set_cache->get_cache(shader_rd, 0, u_params, u_depth, u_normal, u_raw, u_history, u_out), 0);
		RD::get_singleton()->compute_list_dispatch_threads(list, p_params.size.x, p_params.size.y, 1);
		RD::get_singleton()->compute_list_end();
	}
	{
		RD::ComputeListID list = RD::get_singleton()->compute_list_begin();
		RID shader_rd = shader.version_get_shader(shader_version, MODE_DENOISE);
		RD::get_singleton()->compute_list_bind_compute_pipeline(list, pipelines[MODE_DENOISE]);
		RD::Uniform u_acc(RD::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE, 3, Vector<RID>({ nearest, p_params.history_out }));
		RD::Uniform u_dest(RD::UNIFORM_TYPE_IMAGE, 4, p_params.dest);
		RD::get_singleton()->compute_list_bind_uniform_set(list, uniform_set_cache->get_cache(shader_rd, 0, u_params, u_depth, u_normal, u_acc, u_dest), 0);
		RD::get_singleton()->compute_list_dispatch_threads(list, p_params.size.x, p_params.size.y, 1);
		RD::get_singleton()->compute_list_end();
	}
	RD::get_singleton()->draw_command_end_label();
}
