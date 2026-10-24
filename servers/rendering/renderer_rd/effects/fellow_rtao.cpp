/**************************************************************************/
/*  fellow_rtao.cpp                                                       */
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


#include "fellow_rtao.h"

#include "servers/rendering/renderer_rd/storage_rd/material_storage.h"
#include "servers/rendering/renderer_rd/uniform_set_cache_rd.h"

using namespace RendererRD;

FellowRTAO::FellowRTAO() {
	Vector<String> modes;
	modes.push_back("\n#define MODE_TRACE\n");
	modes.push_back("\n#define MODE_TEMPORAL\n");
	modes.push_back("\n#define MODE_DENOISE\n");
	shader.initialize(modes);
	shader_version = shader.version_create();
	for (int i = 0; i < MODE_MAX; i++) {
		pipelines[i] = RD::get_singleton()->compute_pipeline_create(shader.version_get_shader(shader_version, i));
	}
	ubo = RD::get_singleton()->uniform_buffer_create(sizeof(UBO));
}

FellowRTAO::~FellowRTAO() {
	RD::get_singleton()->free_rid(ubo);
	shader.version_free(shader_version);
}

static void _fellow_store_projection(const Projection &p_m, float *r_dst) {
	for (int c = 0; c < 4; c++) {
		for (int r = 0; r < 4; r++) {
			r_dst[c * 4 + r] = p_m.columns[c][r];
		}
	}
}

void FellowRTAO::process(const Params &p_params) {
	UniformSetCacheRD *uniform_set_cache = UniformSetCacheRD::get_singleton();
	ERR_FAIL_NULL(uniform_set_cache);
	MaterialStorage *material_storage = MaterialStorage::get_singleton();
	ERR_FAIL_NULL(material_storage);
	RID nearest = material_storage->sampler_rd_get_default(RSE::CANVAS_ITEM_TEXTURE_FILTER_NEAREST, RSE::CANVAS_ITEM_TEXTURE_REPEAT_DISABLED);

	// The depth buffer's NDC as the renderer's motion vectors take it (MotionVectorsStore): flipped Y, reversed Z.
	Projection correction;
	correction.set_depth_correction(true, true, false);
	UBO u;
	_fellow_store_projection((correction * p_params.projection).inverse(), u.inv_projection);
	_fellow_store_projection(Projection(p_params.cam_transform), u.view_to_world);
	_fellow_store_projection((correction * p_params.prev_projection) * Projection(p_params.prev_cam_transform.affine_inverse()), u.world_to_prev_clip);
	u.size[0] = p_params.size.x;
	u.size[1] = p_params.size.y;
	u.frame = p_params.frame;
	u.ray_count = CLAMP(p_params.ray_count, 1u, 16u);
	u.radius = MAX(p_params.radius, 0.01f);
	u.intensity = p_params.intensity;
	u.power = p_params.power;
	u.history_valid = p_params.history_valid ? 1 : 0;
	u.debug = p_params.debug;
	u.interleave = (p_params.interleaved && p_params.debug == 0) ? 4 : 1;
	u.pad[0] = u.pad[1] = 0;
	RD::get_singleton()->buffer_update(ubo, 0, sizeof(UBO), &u);

	RD::Uniform u_params(RD::UNIFORM_TYPE_UNIFORM_BUFFER, 0, ubo);
	RD::Uniform u_depth(RD::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE, 1, Vector<RID>({ nearest, p_params.depth }));
	RD::Uniform u_normal(RD::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE, 2, Vector<RID>({ nearest, p_params.normal_roughness }));

	RD::get_singleton()->draw_command_begin_label("FellowEngine RTAO");
	{
		RD::ComputeListID list = RD::get_singleton()->compute_list_begin();
		RID shader_rd = shader.version_get_shader(shader_version, MODE_TRACE);
		RD::get_singleton()->compute_list_bind_compute_pipeline(list, pipelines[MODE_TRACE]);
		RD::Uniform u_tlas(RD::UNIFORM_TYPE_ACCELERATION_STRUCTURE, 3, p_params.tlas);
		RD::Uniform u_raw(RD::UNIFORM_TYPE_IMAGE, 4, p_params.raw);
		RD::get_singleton()->compute_list_bind_uniform_set(list, uniform_set_cache->get_cache(shader_rd, 0, u_params, u_depth, u_normal, u_tlas, u_raw), 0);
		if (u.interleave == 4) {
			RD::get_singleton()->compute_list_dispatch_threads(list, (p_params.size.x + 1) / 2, (p_params.size.y + 1) / 2, 1);
		} else {
			RD::get_singleton()->compute_list_dispatch_threads(list, p_params.size.x, p_params.size.y, 1);
		}
		RD::get_singleton()->compute_list_end();
	}
	{
		RD::ComputeListID list = RD::get_singleton()->compute_list_begin();
		RID shader_rd = shader.version_get_shader(shader_version, MODE_TEMPORAL);
		RD::get_singleton()->compute_list_bind_compute_pipeline(list, pipelines[MODE_TEMPORAL]);
		RD::Uniform u_raw(RD::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE, 3, Vector<RID>({ nearest, p_params.raw }));
		RD::Uniform u_history(RD::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE, 4, Vector<RID>({ nearest, p_params.history_prev }));
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
