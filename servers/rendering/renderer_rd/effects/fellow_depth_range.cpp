/**************************************************************************/
/*  fellow_depth_range.cpp                                                */
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

#include "fellow_depth_range.h"

#include "servers/rendering/renderer_rd/storage_rd/material_storage.h"
#include "servers/rendering/renderer_rd/uniform_set_cache_rd.h"

using namespace RendererRD;

FellowDepthRange::FellowDepthRange() {
	Vector<String> modes;
	modes.push_back("\n");
	shader.initialize(modes);
	shader_version = shader.version_create();
	pipeline = RD::get_singleton()->compute_pipeline_create(shader.version_get_shader(shader_version, 0));
	Vector<uint8_t> init;
	init.resize(16);
	init.fill(0);
	buffer = RD::get_singleton()->storage_buffer_create(16, init);
}

FellowDepthRange::~FellowDepthRange() {
	RD::get_singleton()->free_rid(buffer);
	shader.version_free(shader_version);
}

void FellowDepthRange::process(const Params &p_params, const Callable &p_callback) {
	UniformSetCacheRD *uniform_set_cache = UniformSetCacheRD::get_singleton();
	ERR_FAIL_NULL(uniform_set_cache);
	MaterialStorage *material_storage = MaterialStorage::get_singleton();
	ERR_FAIL_NULL(material_storage);
	RID nearest = material_storage->sampler_rd_get_default(RSE::CANVAS_ITEM_TEXTURE_FILTER_NEAREST, RSE::CANVAS_ITEM_TEXTURE_REPEAT_DISABLED);

	Projection correction;
	correction.set_depth_correction(true, true, false);
	const Projection inv = (correction * p_params.projection).inverse();
	Push push;
	memset(&push, 0, sizeof(Push));
	for (int i = 0; i < 4; i++) {
		push.z_row[i] = inv.columns[i][2];
		push.w_row[i] = inv.columns[i][3];
	}
	push.size[0] = p_params.size.x;
	push.size[1] = p_params.size.y;

	const uint32_t reset[4] = { 0x7f7fffffu, 0u, 0u, 0u };
	RD::get_singleton()->buffer_update(buffer, 0, 16, reset);

	RD::get_singleton()->draw_command_begin_label("FellowEngine Depth Range");
	RD::ComputeListID list = RD::get_singleton()->compute_list_begin();
	RID shader_rd = shader.version_get_shader(shader_version, 0);
	RD::get_singleton()->compute_list_bind_compute_pipeline(list, pipeline);
	RD::Uniform u_depth(RD::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE, 0, Vector<RID>({ nearest, p_params.depth }));
	RD::Uniform u_buffer(RD::UNIFORM_TYPE_STORAGE_BUFFER, 1, buffer);
	RD::get_singleton()->compute_list_bind_uniform_set(list, uniform_set_cache->get_cache(shader_rd, 0, u_depth, u_buffer), 0);
	RD::get_singleton()->compute_list_set_push_constant(list, &push, sizeof(Push));
	RD::get_singleton()->compute_list_dispatch_threads(list, (p_params.size.x + 3) / 4, (p_params.size.y + 3) / 4, 1);
	RD::get_singleton()->compute_list_end();
	RD::get_singleton()->draw_command_end_label();

	RD::get_singleton()->buffer_get_data_async(buffer, p_callback, 0, 16);
}
