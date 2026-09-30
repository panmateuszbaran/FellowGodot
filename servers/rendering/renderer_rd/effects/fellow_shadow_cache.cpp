/**************************************************************************/
/*  fellow_shadow_cache.cpp                                               */
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

#include "fellow_shadow_cache.h"

#include "servers/rendering/renderer_rd/storage_rd/material_storage.h"
#include "servers/rendering/renderer_rd/uniform_set_cache_rd.h"

using namespace RendererRD;

FellowShadowCache::FellowShadowCache() {
	Vector<String> modes;
	modes.push_back("\n");
	copy_shader.initialize(modes);
	copy_shader_version = copy_shader.version_create();
	RID shader = copy_shader.version_get_shader(copy_shader_version, 0);

	RD::PipelineDepthStencilState dss;
	dss.enable_depth_test = true;
	dss.depth_compare_operator = RD::COMPARE_OP_ALWAYS;
	dss.enable_depth_write = true;
	copy_pipeline.setup(shader, RD::RENDER_PRIMITIVE_TRIANGLES, RD::PipelineRasterizationState(), RD::PipelineMultisampleState(), dss, RD::PipelineColorBlendState(), 0);
}

FellowShadowCache::~FellowShadowCache() {
	for (KeyValue<int64_t, Entry> &E : entries) {
		_free_entry(E.value);
	}
	entries.clear();
	copy_pipeline.clear();
	copy_shader.version_free(copy_shader_version);
}

void FellowShadowCache::_free_entry(Entry &p_entry) {
	for (int i = 0; i < 4; i++) {
		if (p_entry.texture[i].is_valid()) {
			RD::get_singleton()->free_rid(p_entry.texture[i]); // also frees the dependent framebuffer
			p_entry.texture[i] = RID();
			p_entry.framebuffer[i] = RID();
			p_entry.size[i] = 0;
		}
	}
}

RID FellowShadowCache::get_framebuffer(int64_t p_cache_id, int p_cascade, int p_size, RD::DataFormat p_format, uint32_t p_usage_bits) {
	ERR_FAIL_INDEX_V(p_cascade, 4, RID());
	Entry &e = entries[p_cache_id];
	if (e.texture[p_cascade].is_valid() && (e.size[p_cascade] != p_size || e.format[p_cascade] != p_format)) {
		RD::get_singleton()->free_rid(e.texture[p_cascade]);
		e.texture[p_cascade] = RID();
		e.framebuffer[p_cascade] = RID();
		e.content_valid[p_cascade] = false;
	}
	if (e.texture[p_cascade].is_null()) {
		RD::TextureFormat tf;
		tf.format = p_format;
		tf.width = p_size;
		tf.height = p_size;
		tf.usage_bits = p_usage_bits;
		e.texture[p_cascade] = RD::get_singleton()->texture_create(tf, RD::TextureView());
		RD::get_singleton()->set_resource_name(e.texture[p_cascade], "Fellow directional shadow cache " + itos(p_cascade));
		Vector<RID> fb_tex;
		fb_tex.push_back(e.texture[p_cascade]);
		e.framebuffer[p_cascade] = RD::get_singleton()->framebuffer_create(fb_tex);
		e.size[p_cascade] = p_size;
		e.format[p_cascade] = p_format;
		e.content_valid[p_cascade] = false;
	}
	return e.framebuffer[p_cascade];
}

void FellowShadowCache::set_content_valid(int64_t p_cache_id, int p_cascade) {
	ERR_FAIL_INDEX(p_cascade, 4);
	Entry *e = entries.getptr(p_cache_id);
	if (e) {
		e->content_valid[p_cascade] = true;
	}
}

RID FellowShadowCache::get_valid_texture(int64_t p_cache_id, int p_cascade) {
	ERR_FAIL_INDEX_V(p_cascade, 4, RID());
	Entry *e = entries.getptr(p_cache_id);
	if (e && e->texture[p_cascade].is_valid() && e->content_valid[p_cascade]) {
		return e->texture[p_cascade];
	}
	if (e) {
		e->lost = true;
	}
	return RID();
}

bool FellowShadowCache::take_lost(int64_t p_cache_id) {
	Entry *e = entries.getptr(p_cache_id);
	if (e && e->lost) {
		e->lost = false;
		return true;
	}
	return false;
}

RID FellowShadowCache::get_texture(int64_t p_cache_id, int p_cascade) const {
	ERR_FAIL_INDEX_V(p_cascade, 4, RID());
	const Entry *e = entries.getptr(p_cache_id);
	return e ? e->texture[p_cascade] : RID();
}

void FellowShadowCache::free_cache(int64_t p_cache_id) {
	Entry *e = entries.getptr(p_cache_id);
	if (e) {
		_free_entry(*e);
		entries.erase(p_cache_id);
	}
}

uint64_t FellowShadowCache::get_vram_bytes() const {
	uint64_t total = 0;
	for (const KeyValue<int64_t, Entry> &E : entries) {
		for (int i = 0; i < 4; i++) {
			if (E.value.texture[i].is_valid()) {
				total += (uint64_t)E.value.size[i] * E.value.size[i] * (E.value.format[i] == RD::DATA_FORMAT_D16_UNORM ? 2 : 4);
			}
		}
	}
	return total;
}

void FellowShadowCache::copy(RID p_source_texture, RID p_dst_framebuffer, const Rect2i &p_dst_rect, const Vector2i &p_src_offset, float p_scale, float p_bias) {
	UniformSetCacheRD *uniform_set_cache = UniformSetCacheRD::get_singleton();
	ERR_FAIL_NULL(uniform_set_cache);
	MaterialStorage *material_storage = MaterialStorage::get_singleton();
	ERR_FAIL_NULL(material_storage);

	PushConstant pc;
	pc.dst_origin[0] = p_dst_rect.position.x;
	pc.dst_origin[1] = p_dst_rect.position.y;
	pc.src_offset[0] = p_src_offset.x;
	pc.src_offset[1] = p_src_offset.y;
	pc.scale = p_scale;
	pc.bias = p_bias;
	pc.pad[0] = 0.0;
	pc.pad[1] = 0.0;

	RID sampler = material_storage->sampler_rd_get_default(RSE::CANVAS_ITEM_TEXTURE_FILTER_NEAREST, RSE::CANVAS_ITEM_TEXTURE_REPEAT_DISABLED);
	RD::Uniform u_source(RD::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE, 0, Vector<RID>({ sampler, p_source_texture }));

	RID shader = copy_shader.version_get_shader(copy_shader_version, 0);
	ERR_FAIL_COND(shader.is_null());

	RD::DrawListID draw_list = RD::get_singleton()->draw_list_begin(p_dst_framebuffer, RD::DRAW_DEFAULT_ALL, Vector<Color>(), 0.0f, 0, p_dst_rect);
	RD::get_singleton()->draw_list_bind_render_pipeline(draw_list, copy_pipeline.get_render_pipeline(RD::INVALID_ID, RD::get_singleton()->framebuffer_get_format(p_dst_framebuffer)));
	RD::get_singleton()->draw_list_bind_uniform_set(draw_list, uniform_set_cache->get_cache(shader, 0, u_source), 0);
	RD::get_singleton()->draw_list_set_push_constant(draw_list, &pc, sizeof(PushConstant));
	RD::get_singleton()->draw_list_draw(draw_list, false, 1, 3);
	RD::get_singleton()->draw_list_end();
}
