/**************************************************************************/
/*  fellow_sdf_gather.cpp                                                 */
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



#include "fellow_sdf_gather.h"

#include "servers/rendering/renderer_rd/storage_rd/material_storage.h"
#include "servers/rendering/renderer_rd/storage_rd/texture_storage.h"
#include "servers/rendering/renderer_rd/uniform_set_cache_rd.h"

using namespace RendererRD;

FellowSDFGather::FellowSDFGather() {
	Vector<String> modes;
	modes.push_back("\n#define MODE_TRACE\n");
	modes.push_back("\n#define MODE_TRACE\n#define FELLOW_RADIANCE_ARRAY\n");
	modes.push_back("\n#define MODE_RESOLVE\n");
	modes.push_back("\n#define MODE_FILTER\n");
	shader.initialize(modes);
	shader_version = shader.version_create();
	for (int i = 0; i < MODE_MAX; i++) {
		pipelines[i] = RD::get_singleton()->compute_pipeline_create(shader.version_get_shader(shader_version, i));
	}
	ubo = RD::get_singleton()->uniform_buffer_create(sizeof(UBO));
}

FellowSDFGather::~FellowSDFGather() {
	RD::get_singleton()->free_rid(ubo);
	shader.version_free(shader_version);
}

static void _fellow_store_basis4(const Basis &p_basis, float *r_dst) {
	// A mat4 (column-major) with the basis in its upper 3 x 3.
	for (int c = 0; c < 4; c++) {
		for (int r = 0; r < 4; r++) {
			r_dst[c * 4 + r] = (c < 3 && r < 3) ? p_basis.rows[r][c] : (c == r ? 1.0f : 0.0f);
		}
	}
}

void FellowSDFGather::process(const Params &p_params) {
	UniformSetCacheRD *uniform_set_cache = UniformSetCacheRD::get_singleton();
	ERR_FAIL_NULL(uniform_set_cache);
	MaterialStorage *material_storage = MaterialStorage::get_singleton();
	ERR_FAIL_NULL(material_storage);
	TextureStorage *texture_storage = TextureStorage::get_singleton();
	ERR_FAIL_COND(p_params.cascade_count <= 0 || p_params.grid.x <= 0 || p_params.grid.y <= 0);
	RID linear = material_storage->sampler_rd_get_default(RSE::CANVAS_ITEM_TEXTURE_FILTER_LINEAR_WITH_MIPMAPS, RSE::CANVAS_ITEM_TEXTURE_REPEAT_DISABLED);
	RID nearest = material_storage->sampler_rd_get_default(RSE::CANVAS_ITEM_TEXTURE_FILTER_NEAREST, RSE::CANVAS_ITEM_TEXTURE_REPEAT_DISABLED);

	UBO u;
	MaterialStorage::store_camera(p_params.inv_projection, u.inv_projection);
	MaterialStorage::store_transform(p_params.view_to_world, u.view_to_world);
	MaterialStorage::store_camera(p_params.prev_world_to_clip, u.prev_world_to_clip);
	_fellow_store_basis4(p_params.sky_xform, u.sky_xform);
	const bool sky = p_params.use_sky && p_params.radiance.is_valid();
	u.sky_params[0] = p_params.use_ambient ? 1.0f : 0.0f;
	u.sky_params[1] = sky ? 1.0f : 0.0f;
	u.sky_params[2] = p_params.sky_factor;
	u.sky_params[3] = p_params.radiance_level;
	u.ambient_color[0] = p_params.ambient_color.r;
	u.ambient_color[1] = p_params.ambient_color.g;
	u.ambient_color[2] = p_params.ambient_color.b;
	u.ambient_color[3] = p_params.ambient_color.a;
	u.radiance_border[0] = p_params.radiance_border[0];
	u.radiance_border[1] = p_params.radiance_border[1];
	u.radiance_border[2] = 0.0f;
	u.radiance_border[3] = 0.0f;
	u.sdf[0] = p_params.cascade_size;
	u.sdf[1] = float(MIN(p_params.cascade_count, MAX_CASCADES));
	u.sdf[2] = p_params.y_mult;
	u.sdf[3] = float(MAX(8, p_params.max_steps));
	u.light[0] = p_params.light_scale;
	u.light[1] = p_params.energy;
	u.light[2] = MAX(1.0f, p_params.max_samples);
	u.light[3] = p_params.normal_bias;
	u.footprint[0] = p_params.pixel_footprint;
	u.footprint[1] = 0.0f;
	u.footprint[2] = 0.0f;
	u.footprint[3] = 0.0f;
	u.screen[0] = p_params.internal_size.x;
	u.screen[1] = p_params.internal_size.y;
	u.screen[2] = p_params.ambient_size.x;
	u.screen[3] = p_params.ambient_size.y;
	u.probes[0] = p_params.grid.x;
	u.probes[1] = p_params.grid.y;
	u.probes[2] = MAX(1, p_params.tile);
	u.probes[3] = CLAMP(p_params.rays, 1, 64);
	u.frame[0] = int32_t(p_params.frame);
	u.frame[1] = p_params.history ? 1 : 0;
	u.frame[2] = 0;
	u.frame[3] = 0;
	RD::get_singleton()->buffer_update(ubo, 0, sizeof(UBO), &u);

	const int cur = p_params.current & 1;
	const int prev = cur ^ 1;
	RD::Uniform u_params(RD::UNIFORM_TYPE_UNIFORM_BUFFER, 0, ubo);
	RD::Uniform u_depth(RD::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE, 1, Vector<RID>({ nearest, p_params.depth }));
	RD::Uniform u_nr(RD::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE, 2, Vector<RID>({ nearest, p_params.normal_roughness }));
	RD::Uniform u_sampler(RD::UNIFORM_TYPE_SAMPLER, 7, linear);

	RD::get_singleton()->draw_command_begin_label("FellowEngine SDF Screen Gather");
	{
		const Mode mode = (p_params.radiance_array && sky) ? MODE_TRACE_ARRAY : MODE_TRACE;
		RID radiance = sky ? p_params.radiance : texture_storage->texture_rd_get_default(mode == MODE_TRACE_ARRAY ? TextureStorage::DEFAULT_RD_TEXTURE_2D_ARRAY_BLACK : TextureStorage::DEFAULT_RD_TEXTURE_BLACK);
		RID unused = texture_storage->texture_rd_get_default(TextureStorage::DEFAULT_RD_TEXTURE_3D_WHITE);
		RD::Uniform u_sdf(RD::UNIFORM_TYPE_TEXTURE, 3, Vector<RID>());
		RD::Uniform u_light(RD::UNIFORM_TYPE_TEXTURE, 4, Vector<RID>());
		RD::Uniform u_aniso0(RD::UNIFORM_TYPE_TEXTURE, 5, Vector<RID>());
		RD::Uniform u_aniso1(RD::UNIFORM_TYPE_TEXTURE, 6, Vector<RID>());
		for (int i = 0; i < MAX_CASCADES; i++) {
			const bool used = i < p_params.cascade_count;
			u_sdf.append_id(used ? p_params.sdf_distance[i] : unused);
			u_light.append_id(used ? p_params.sdf_light[i] : unused);
			u_aniso0.append_id(used ? p_params.sdf_aniso0[i] : unused);
			u_aniso1.append_id(used ? p_params.sdf_aniso1[i] : unused);
		}
		RD::Uniform u_rad(RD::UNIFORM_TYPE_TEXTURE, 8, radiance);
		RD::Uniform u_cascades(RD::UNIFORM_TYPE_UNIFORM_BUFFER, 9, p_params.sdf_cascades_ubo);
		RD::Uniform u_out_irr(RD::UNIFORM_TYPE_IMAGE, 10, p_params.irradiance[cur]);
		RD::Uniform u_out_pos(RD::UNIFORM_TYPE_IMAGE, 11, p_params.position[cur]);
		RD::Uniform u_out_nrm(RD::UNIFORM_TYPE_IMAGE, 12, p_params.normal[cur]);
		RD::Uniform u_prev_irr(RD::UNIFORM_TYPE_TEXTURE, 13, p_params.irradiance[prev]);
		RD::Uniform u_prev_pos(RD::UNIFORM_TYPE_TEXTURE, 14, p_params.position[prev]);
		RD::Uniform u_prev_nrm(RD::UNIFORM_TYPE_TEXTURE, 15, p_params.normal[prev]);
		RD::ComputeListID list = RD::get_singleton()->compute_list_begin();
		RID shader_rd = shader.version_get_shader(shader_version, mode);
		RD::get_singleton()->compute_list_bind_compute_pipeline(list, pipelines[mode]);
		RD::get_singleton()->compute_list_bind_uniform_set(list, uniform_set_cache->get_cache(shader_rd, 0, u_params, u_depth, u_nr, u_sdf, u_light, u_aniso0, u_aniso1, u_sampler, u_rad, u_cascades, u_out_irr, u_out_pos, u_out_nrm, u_prev_irr, u_prev_pos, u_prev_nrm), 0);
		RD::get_singleton()->compute_list_dispatch(list, p_params.grid.x, p_params.grid.y, 1);
		RD::get_singleton()->compute_list_end();
	}
	const bool filter = p_params.filter && p_params.filtered.is_valid();
	if (filter) {
		RD::Uniform u_irr(RD::UNIFORM_TYPE_TEXTURE, 13, p_params.irradiance[cur]);
		RD::Uniform u_pos(RD::UNIFORM_TYPE_TEXTURE, 14, p_params.position[cur]);
		RD::Uniform u_nrm(RD::UNIFORM_TYPE_TEXTURE, 15, p_params.normal[cur]);
		RD::Uniform u_out(RD::UNIFORM_TYPE_IMAGE, 17, p_params.filtered);
		RD::ComputeListID list = RD::get_singleton()->compute_list_begin();
		RID shader_rd = shader.version_get_shader(shader_version, MODE_FILTER);
		RD::get_singleton()->compute_list_bind_compute_pipeline(list, pipelines[MODE_FILTER]);
		RD::get_singleton()->compute_list_bind_uniform_set(list, uniform_set_cache->get_cache(shader_rd, 0, u_params, u_depth, u_nr, u_sampler, u_irr, u_pos, u_nrm, u_out), 0);
		RD::get_singleton()->compute_list_dispatch_threads(list, p_params.grid.x, p_params.grid.y, 1);
		RD::get_singleton()->compute_list_end();
	}
	{
		RD::Uniform u_irr(RD::UNIFORM_TYPE_TEXTURE, 13, filter ? p_params.filtered : p_params.irradiance[cur]);
		RD::Uniform u_pos(RD::UNIFORM_TYPE_TEXTURE, 14, p_params.position[cur]);
		RD::Uniform u_nrm(RD::UNIFORM_TYPE_TEXTURE, 15, p_params.normal[cur]);
		RD::Uniform u_dest(RD::UNIFORM_TYPE_IMAGE, 16, p_params.ambient);
		RD::ComputeListID list = RD::get_singleton()->compute_list_begin();
		RID shader_rd = shader.version_get_shader(shader_version, MODE_RESOLVE);
		RD::get_singleton()->compute_list_bind_compute_pipeline(list, pipelines[MODE_RESOLVE]);
		RD::get_singleton()->compute_list_bind_uniform_set(list, uniform_set_cache->get_cache(shader_rd, 0, u_params, u_depth, u_nr, u_sampler, u_irr, u_pos, u_nrm, u_dest), 0);
		RD::get_singleton()->compute_list_dispatch_threads(list, p_params.ambient_size.x, p_params.ambient_size.y, 1);
		RD::get_singleton()->compute_list_end();
	}
	RD::get_singleton()->draw_command_end_label();
}
