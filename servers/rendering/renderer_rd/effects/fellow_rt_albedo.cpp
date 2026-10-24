/**************************************************************************/
/*  fellow_rt_albedo.cpp                                                  */
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

#include "fellow_rt_albedo.h"

#include "servers/rendering/renderer_rd/storage_rd/material_storage.h"
#include "servers/rendering/renderer_rd/uniform_set_cache_rd.h"

using namespace RendererRD;

FellowRTAlbedo::FellowRTAlbedo() {
	Vector<String> modes;
	modes.push_back("");
	shader.initialize(modes);
	shader_version = shader.version_create();
	pipeline = RD::get_singleton()->compute_pipeline_create(shader.version_get_shader(shader_version, 0));
	buffer = RD::get_singleton()->storage_buffer_create(CAPACITY * sizeof(float) * 4);
}

FellowRTAlbedo::~FellowRTAlbedo() {
	RD::get_singleton()->free_rid(buffer);
	shader.version_free(shader_version);
}

void FellowRTAlbedo::average(const LocalVector<RID> &p_rd_textures, const LocalVector<uint32_t> &p_slots) {
	if (p_rd_textures.is_empty()) {
		return;
	}
	UniformSetCacheRD *uniform_set_cache = UniformSetCacheRD::get_singleton();
	ERR_FAIL_NULL(uniform_set_cache);
	MaterialStorage *material_storage = MaterialStorage::get_singleton();
	ERR_FAIL_NULL(material_storage);
	RID sampler = material_storage->sampler_rd_get_default(RSE::CANVAS_ITEM_TEXTURE_FILTER_LINEAR_WITH_MIPMAPS, RSE::CANVAS_ITEM_TEXTURE_REPEAT_DISABLED);
	RID shader_rd = shader.version_get_shader(shader_version, 0);
	RD::get_singleton()->draw_command_begin_label("FellowEngine RT Albedo Averages");
	RD::ComputeListID list = RD::get_singleton()->compute_list_begin();
	RD::get_singleton()->compute_list_bind_compute_pipeline(list, pipeline);
	RD::Uniform u_buffer(RD::UNIFORM_TYPE_STORAGE_BUFFER, 1, buffer);
	for (uint32_t i = 0; i < p_rd_textures.size(); i++) {
		const RD::TextureFormat tf = RD::get_singleton()->texture_get_format(p_rd_textures[i]);
		// The level of about 8 x 8 texels (its texel centres sampled), or the smallest there is.
		uint32_t level = 0;
		uint32_t w = MAX(tf.width, tf.height);
		while (w > 8 && level + 1 < tf.mipmaps) {
			w = MAX(w / 2, 1u);
			level++;
		}
		RD::Uniform u_tex(RD::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE, 0, Vector<RID>({ sampler, p_rd_textures[i] }));
		RD::get_singleton()->compute_list_bind_uniform_set(list, uniform_set_cache->get_cache(shader_rd, 0, u_tex, u_buffer), 0);
		PushConstant pc;
		pc.index = p_slots[i];
		pc.lod = float(level);
		pc.pad[0] = pc.pad[1] = 0;
		RD::get_singleton()->compute_list_set_push_constant(list, &pc, sizeof(pc));
		RD::get_singleton()->compute_list_dispatch(list, 1, 1, 1);
		RD::get_singleton()->compute_list_add_barrier(list);
	}
	RD::get_singleton()->compute_list_end();
	RD::get_singleton()->draw_command_end_label();
}
