/**************************************************************************/
/*  fellow_motion_blur.cpp                                                */
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


#include "fellow_motion_blur.h"

#include "servers/rendering/renderer_rd/storage_rd/material_storage.h"
#include "servers/rendering/renderer_rd/uniform_set_cache_rd.h"

using namespace RendererRD;

FellowMotionBlur::FellowMotionBlur() {
	Vector<String> modes;
	modes.push_back("\n#define MODE_TILE_MAX\n");
	modes.push_back("\n#define MODE_NEIGHBOR_MAX\n");
	modes.push_back("\n#define MODE_GATHER\n");
	shader.initialize(modes);
	shader_version = shader.version_create();
	for (int i = 0; i < MODE_MAX; i++) {
		pipelines[i] = RD::get_singleton()->compute_pipeline_create(shader.version_get_shader(shader_version, i));
	}
}

FellowMotionBlur::~FellowMotionBlur() {
	shader.version_free(shader_version); // also frees the pipelines (dependencies of the shader)
}

void FellowMotionBlur::process(const Params &p_params) {
	UniformSetCacheRD *uniform_set_cache = UniformSetCacheRD::get_singleton();
	ERR_FAIL_NULL(uniform_set_cache);
	MaterialStorage *material_storage = MaterialStorage::get_singleton();
	ERR_FAIL_NULL(material_storage);
	RID nearest = material_storage->sampler_rd_get_default(RSE::CANVAS_ITEM_TEXTURE_FILTER_NEAREST, RSE::CANVAS_ITEM_TEXTURE_REPEAT_DISABLED);
	RID linear = material_storage->sampler_rd_get_default(RSE::CANVAS_ITEM_TEXTURE_FILTER_LINEAR, RSE::CANVAS_ITEM_TEXTURE_REPEAT_DISABLED);

	const Size2i tiles((p_params.size.x + TILE_SIZE - 1) / TILE_SIZE, (p_params.size.y + TILE_SIZE - 1) / TILE_SIZE);
	PushConstant push;
	push.size[0] = p_params.size.x;
	push.size[1] = p_params.size.y;
	push.tile_count[0] = tiles.x;
	push.tile_count[1] = tiles.y;
	// The velocity buffer holds the UV offset from this frame to the previous one; the blur's half extent in pixels is
	// minus half of it, scaled by the shutter.
	push.velocity_scale[0] = -0.5f * p_params.amount * float(p_params.size.x);
	push.velocity_scale[1] = -0.5f * p_params.amount * float(p_params.size.y);
	push.max_radius = CLAMP(p_params.max_radius, 1.0f, float(TILE_SIZE));
	push.sample_count = CLAMP(p_params.sample_count, 2, 64);
	push.z_near = p_params.z_near;
	push.z_far = p_params.z_far;
	push.orthogonal = p_params.orthogonal ? 1 : 0;
	push.soft_z = 0.02f;
	for (int col = 0; col < 4; col++) {
		for (int k = 0; k < 4; k++) {
			push.sky_reprojection[col * 4 + k] = p_params.sky_reprojection.columns[col][k];
		}
	}

	RD::get_singleton()->draw_command_begin_label("FellowEngine Motion Blur");

	{
		RD::ComputeListID compute_list = RD::get_singleton()->compute_list_begin();
		RID shader_rd = shader.version_get_shader(shader_version, MODE_TILE_MAX);
		RD::get_singleton()->compute_list_bind_compute_pipeline(compute_list, pipelines[MODE_TILE_MAX]);
		RD::Uniform u_velocity(RD::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE, 0, Vector<RID>({ nearest, p_params.velocity }));
		RD::Uniform u_depth(RD::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE, 1, Vector<RID>({ nearest, p_params.depth }));
		RD::get_singleton()->compute_list_bind_uniform_set(compute_list, uniform_set_cache->get_cache(shader_rd, 0, u_velocity, u_depth), 0);
		RD::Uniform u_tile(RD::UNIFORM_TYPE_IMAGE, 0, Vector<RID>({ p_params.tile_max }));
		RD::get_singleton()->compute_list_bind_uniform_set(compute_list, uniform_set_cache->get_cache(shader_rd, 1, u_tile), 1);
		RD::get_singleton()->compute_list_set_push_constant(compute_list, &push, sizeof(PushConstant));
		RD::get_singleton()->compute_list_dispatch(compute_list, tiles.x, tiles.y, 1);
		RD::get_singleton()->compute_list_end();
	}
	{
		RD::ComputeListID compute_list = RD::get_singleton()->compute_list_begin();
		RID shader_rd = shader.version_get_shader(shader_version, MODE_NEIGHBOR_MAX);
		RD::get_singleton()->compute_list_bind_compute_pipeline(compute_list, pipelines[MODE_NEIGHBOR_MAX]);
		RD::Uniform u_tile(RD::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE, 0, Vector<RID>({ nearest, p_params.tile_max }));
		RD::get_singleton()->compute_list_bind_uniform_set(compute_list, uniform_set_cache->get_cache(shader_rd, 0, u_tile), 0);
		RD::Uniform u_neighbor(RD::UNIFORM_TYPE_IMAGE, 0, Vector<RID>({ p_params.neighbor_max }));
		RD::get_singleton()->compute_list_bind_uniform_set(compute_list, uniform_set_cache->get_cache(shader_rd, 1, u_neighbor), 1);
		RD::get_singleton()->compute_list_set_push_constant(compute_list, &push, sizeof(PushConstant));
		RD::get_singleton()->compute_list_dispatch_threads(compute_list, tiles.x, tiles.y, 1);
		RD::get_singleton()->compute_list_end();
	}
	{
		RD::ComputeListID compute_list = RD::get_singleton()->compute_list_begin();
		RID shader_rd = shader.version_get_shader(shader_version, MODE_GATHER);
		RD::get_singleton()->compute_list_bind_compute_pipeline(compute_list, pipelines[MODE_GATHER]);
		RD::Uniform u_color(RD::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE, 0, Vector<RID>({ linear, p_params.source }));
		RD::Uniform u_depth(RD::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE, 1, Vector<RID>({ nearest, p_params.depth }));
		RD::Uniform u_velocity(RD::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE, 2, Vector<RID>({ nearest, p_params.velocity }));
		RD::Uniform u_neighbor(RD::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE, 3, Vector<RID>({ nearest, p_params.neighbor_max }));
		RD::get_singleton()->compute_list_bind_uniform_set(compute_list, uniform_set_cache->get_cache(shader_rd, 0, u_color, u_depth, u_velocity, u_neighbor), 0);
		RD::Uniform u_dest(RD::UNIFORM_TYPE_IMAGE, 0, Vector<RID>({ p_params.dest }));
		RD::get_singleton()->compute_list_bind_uniform_set(compute_list, uniform_set_cache->get_cache(shader_rd, 1, u_dest), 1);
		RD::get_singleton()->compute_list_set_push_constant(compute_list, &push, sizeof(PushConstant));
		RD::get_singleton()->compute_list_dispatch_threads(compute_list, p_params.size.x, p_params.size.y, 1);
		RD::get_singleton()->compute_list_end();
	}

	RD::get_singleton()->draw_command_end_label();
}
