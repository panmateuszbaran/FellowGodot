/**************************************************************************/
/*  fellow_shadow_max_mip.cpp                                             */
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


#include "fellow_shadow_max_mip.h"

#include "servers/rendering/renderer_rd/storage_rd/material_storage.h"
#include "servers/rendering/renderer_rd/uniform_set_cache_rd.h"

using namespace RendererRD;

FellowShadowMaxMip::FellowShadowMaxMip() {
	Vector<String> modes;
	modes.push_back("\n");
	shader.initialize(modes);
	shader_version = shader.version_create();
	pipeline = RD::get_singleton()->compute_pipeline_create(shader.version_get_shader(shader_version, 0));
}

FellowShadowMaxMip::~FellowShadowMaxMip() {
	_free_texture();
	shader.version_free(shader_version); // also frees the pipeline (dependency of the shader)
}

void FellowShadowMaxMip::_free_texture() {
	if (texture.is_valid()) {
		RD::get_singleton()->free_rid(texture); // also frees the dependent level views
	}
	texture = RID();
	level_views.clear();
	atlas_size = Size2i();
	valid = false;
}

uint64_t FellowShadowMaxMip::get_vram_bytes() const {
	uint64_t total = 0;
	Size2i s = atlas_size;
	for (uint32_t i = 0; i < level_views.size(); i++) {
		s = Size2i((s.x + 1) / 2, (s.y + 1) / 2);
		total += (uint64_t)s.x * s.y * 4;
	}
	return total;
}

void FellowShadowMaxMip::build(RID p_atlas_texture, const Size2i &p_atlas_size) {
	valid = false;
	if (p_atlas_texture.is_null() || p_atlas_size.x < 2 || p_atlas_size.y < 2) {
		return;
	}
	if (texture.is_valid() && atlas_size != p_atlas_size) {
		_free_texture();
	}
	if (texture.is_null()) {
		Size2i base((p_atlas_size.x + 1) / 2, (p_atlas_size.y + 1) / 2);
		uint32_t levels = 1;
		for (Size2i s = base; s.x > 1 || s.y > 1; s = Size2i((s.x + 1) / 2, (s.y + 1) / 2)) {
			levels++;
		}
		RD::TextureFormat tf;
		tf.format = RD::DATA_FORMAT_R32_SFLOAT;
		tf.width = base.x;
		tf.height = base.y;
		tf.mipmaps = levels;
		tf.usage_bits = RD::TEXTURE_USAGE_SAMPLING_BIT | RD::TEXTURE_USAGE_STORAGE_BIT;
		texture = RD::get_singleton()->texture_create(tf, RD::TextureView());
		RD::get_singleton()->set_resource_name(texture, "Fellow directional shadow max pyramid");
		for (uint32_t i = 0; i < levels; i++) {
			level_views.push_back(RD::get_singleton()->texture_create_shared_from_slice(RD::TextureView(), texture, 0, i, 1, RD::TEXTURE_SLICE_2D));
		}
		atlas_size = p_atlas_size;
	}

	UniformSetCacheRD *uniform_set_cache = UniformSetCacheRD::get_singleton();
	ERR_FAIL_NULL(uniform_set_cache);
	MaterialStorage *material_storage = MaterialStorage::get_singleton();
	ERR_FAIL_NULL(material_storage);
	RID nearest = material_storage->sampler_rd_get_default(RSE::CANVAS_ITEM_TEXTURE_FILTER_NEAREST, RSE::CANVAS_ITEM_TEXTURE_REPEAT_DISABLED);
	RID shader_rd = shader.version_get_shader(shader_version, 0);
	ERR_FAIL_COND(shader_rd.is_null());

	RD::ComputeListID compute_list = RD::get_singleton()->compute_list_begin();
	RD::get_singleton()->draw_command_begin_label("FellowEngine Shadow Max Pyramid");
	RD::get_singleton()->compute_list_bind_compute_pipeline(compute_list, pipeline);
	Size2i src_size = p_atlas_size;
	for (uint32_t i = 0; i < level_views.size(); i++) {
		Size2i dst_size((src_size.x + 1) / 2, (src_size.y + 1) / 2);
		RID src = i == 0 ? p_atlas_texture : level_views[i - 1];
		RD::Uniform u_src(RD::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE, 0, Vector<RID>({ nearest, src }));
		RD::Uniform u_dst(RD::UNIFORM_TYPE_IMAGE, 0, Vector<RID>({ level_views[i] }));
		RD::get_singleton()->compute_list_bind_uniform_set(compute_list, uniform_set_cache->get_cache(shader_rd, 0, u_src), 0);
		RD::get_singleton()->compute_list_bind_uniform_set(compute_list, uniform_set_cache->get_cache(shader_rd, 1, u_dst), 1);
		PushConstant pc;
		pc.source_size[0] = src_size.x;
		pc.source_size[1] = src_size.y;
		pc.dest_size[0] = dst_size.x;
		pc.dest_size[1] = dst_size.y;
		RD::get_singleton()->compute_list_set_push_constant(compute_list, &pc, sizeof(PushConstant));
		RD::get_singleton()->compute_list_dispatch_threads(compute_list, dst_size.x, dst_size.y, 1);
		if (i + 1 < level_views.size()) {
			RD::get_singleton()->compute_list_add_barrier(compute_list);
		}
		src_size = dst_size;
	}
	RD::get_singleton()->draw_command_end_label();
	RD::get_singleton()->compute_list_end();
	valid = true;
}
