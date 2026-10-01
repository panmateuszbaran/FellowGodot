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
	modes.push_back("\n#define DEST_RG16\n");
	shader.initialize(modes);
	// RG16 UNORM storage images are optional in Vulkan: compile that variant only where they are supported.
	rg16_supported = RD::get_singleton()->texture_is_format_supported_for_usage(RD::DATA_FORMAT_R16G16_UNORM, RD::TEXTURE_USAGE_SAMPLING_BIT | RD::TEXTURE_USAGE_STORAGE_BIT);
	shader.set_variant_enabled(1, rg16_supported);
	shader_version = shader.version_create();
	// Pipelines are created on first use (the RG16 one only where RG16 storage images are supported).
}

FellowShadowMaxMip::~FellowShadowMaxMip() {
	for (KeyValue<RID, Target> &kv : targets) {
		_free_target(kv.value);
	}
	targets.clear();
	shader.version_free(shader_version); // also frees the pipelines (dependencies of the shader)
}

void FellowShadowMaxMip::_free_target(Target &r_target) {
	if (r_target.texture.is_valid()) {
		RD::get_singleton()->free_rid(r_target.texture); // also frees the dependent level views
	}
	r_target.texture = RID();
	r_target.level_views.clear();
	r_target.atlas_size = Size2i();
	r_target.built_from = RID();
	r_target.valid = false;
}

void FellowShadowMaxMip::invalidate(RID p_key) {
	Target *t = targets.getptr(p_key);
	if (t) {
		t->valid = false;
	}
}

bool FellowShadowMaxMip::is_valid(RID p_key) const {
	const Target *t = targets.getptr(p_key);
	return t && t->valid;
}

RID FellowShadowMaxMip::get_texture(RID p_key) const {
	const Target *t = targets.getptr(p_key);
	return t ? t->texture : RID();
}

uint64_t FellowShadowMaxMip::get_vram_bytes() const {
	uint64_t total = 0;
	for (const KeyValue<RID, Target> &kv : targets) {
		Size2i s = kv.value.atlas_size;
		for (uint32_t i = 0; i < kv.value.level_views.size(); i++) {
			s = Size2i((s.x + 1) / 2, (s.y + 1) / 2);
			total += (uint64_t)s.x * s.y * (rg16_supported ? 4 : 8);
		}
	}
	return total;
}

void FellowShadowMaxMip::build(RID p_key, RID p_atlas_texture, const Size2i &p_atlas_size, const Rect2i &p_dirty, bool p_atlas_16_bits) {
	Target &target = targets[p_key];
	if (p_atlas_texture.is_null() || p_atlas_size.x < 2 || p_atlas_size.y < 2) {
		target.valid = false;
		return;
	}
	if (target.texture.is_valid() && target.atlas_size != p_atlas_size) {
		_free_target(target);
	}
	if (target.texture.is_null()) {
		Size2i base((p_atlas_size.x + 1) / 2, (p_atlas_size.y + 1) / 2);
		uint32_t levels = 1;
		for (Size2i s = base; s.x > 1 || s.y > 1; s = Size2i((s.x + 1) / 2, (s.y + 1) / 2)) {
			levels++;
		}
		RD::TextureFormat tf;
		tf.width = base.x;
		tf.height = base.y;
		tf.mipmaps = levels;
		tf.usage_bits = RD::TEXTURE_USAGE_SAMPLING_BIT | RD::TEXTURE_USAGE_STORAGE_BIT;
		tf.format = rg16_supported ? RD::DATA_FORMAT_R16G16_UNORM : RD::DATA_FORMAT_R32G32_SFLOAT;
		target.texture = RD::get_singleton()->texture_create(tf, RD::TextureView());
		RD::get_singleton()->set_resource_name(target.texture, "Fellow shadow min/max pyramid");
		for (uint32_t i = 0; i < levels; i++) {
			target.level_views.push_back(RD::get_singleton()->texture_create_shared_from_slice(RD::TextureView(), target.texture, 0, i, 1, RD::TEXTURE_SLICE_2D));
		}
		target.atlas_size = p_atlas_size;
		target.valid = false;
	}
	const bool texture_rg16 = rg16_supported;
	LocalVector<RID> &level_views = target.level_views;

	// Region of the atlas to rebuild (texels, end exclusive).
	Rect2i region = p_dirty.intersection(Rect2i(Point2i(), p_atlas_size));
	if (!target.valid || target.built_from != p_atlas_texture) {
		region = Rect2i(Point2i(), p_atlas_size);
	}
	target.built_from = p_atlas_texture;
	target.valid = true;
	if (region.size.x <= 0 || region.size.y <= 0) {
		return; // No atlas texel changed: the pyramid still matches.
	}

	UniformSetCacheRD *uniform_set_cache = UniformSetCacheRD::get_singleton();
	ERR_FAIL_NULL(uniform_set_cache);
	MaterialStorage *material_storage = MaterialStorage::get_singleton();
	ERR_FAIL_NULL(material_storage);
	RID nearest = material_storage->sampler_rd_get_default(RSE::CANVAS_ITEM_TEXTURE_FILTER_NEAREST, RSE::CANVAS_ITEM_TEXTURE_REPEAT_DISABLED);
	RID shader_rd = shader.version_get_shader(shader_version, texture_rg16 ? 1 : 0);
	ERR_FAIL_COND(shader_rd.is_null());
	RID &pipeline = texture_rg16 ? pipeline_rg16 : pipeline_rg32;
	if (pipeline.is_null()) {
		pipeline = RD::get_singleton()->compute_pipeline_create(shader_rd);
	}

	// Floor division (the region's lower bound minus the window overlap can be negative).
	auto floor_div2 = [](int p_v) -> int { return p_v >= 0 ? p_v / 2 : -((-p_v + 1) / 2); };

	// One compute list per mip (like the SSR Hi-Z): a mip written as a storage image is read as a sampled texture by
	// the next one, and the layout transition between the two only happens between compute lists.
	RD::get_singleton()->draw_command_begin_label("FellowEngine Shadow Min/Max Pyramid");
	Size2i src_size = p_atlas_size;
	Point2i src_lo = region.position;
	Point2i src_hi = region.position + region.size;
	for (uint32_t i = 0; i < level_views.size(); i++) {
		Size2i dst_size((src_size.x + 1) / 2, (src_size.y + 1) / 2);
		// Destination texels whose window reads the changed source texels: mip 0 window [2j, 2j + 4) of the atlas,
		// mip m texels 2j and 2j + 2 of mip m - 1.
		const int back = i == 0 ? 2 : 1;
		Point2i dst_lo(MAX(0, floor_div2(src_lo.x - back)), MAX(0, floor_div2(src_lo.y - back)));
		Point2i dst_hi(MIN(dst_size.x, floor_div2(src_hi.x - 1) + 1), MIN(dst_size.y, floor_div2(src_hi.y - 1) + 1));
		if (src_hi.x >= src_size.x) {
			dst_hi.x = dst_size.x; // windows clamped at the end read the last source texel
		}
		if (src_hi.y >= src_size.y) {
			dst_hi.y = dst_size.y;
		}
		if (dst_hi.x <= dst_lo.x || dst_hi.y <= dst_lo.y) {
			break;
		}
		RID src = i == 0 ? p_atlas_texture : level_views[i - 1];
		RD::Uniform u_src(RD::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE, 0, Vector<RID>({ nearest, src }));
		RD::Uniform u_dst(RD::UNIFORM_TYPE_IMAGE, 0, Vector<RID>({ level_views[i] }));
		PushConstant pc;
		pc.source_size[0] = src_size.x;
		pc.source_size[1] = src_size.y;
		pc.dest_size[0] = dst_size.x;
		pc.dest_size[1] = dst_size.y;
		pc.source_is_pyramid = i == 0 ? 0 : 1;
		pc.round_mode = (i == 0 && texture_rg16 && !p_atlas_16_bits) ? 1 : 0;
		pc.dest_offset[0] = dst_lo.x;
		pc.dest_offset[1] = dst_lo.y;
		RD::ComputeListID compute_list = RD::get_singleton()->compute_list_begin();
		RD::get_singleton()->compute_list_bind_compute_pipeline(compute_list, pipeline);
		RD::get_singleton()->compute_list_bind_uniform_set(compute_list, uniform_set_cache->get_cache(shader_rd, 0, u_src), 0);
		RD::get_singleton()->compute_list_bind_uniform_set(compute_list, uniform_set_cache->get_cache(shader_rd, 1, u_dst), 1);
		RD::get_singleton()->compute_list_set_push_constant(compute_list, &pc, sizeof(PushConstant));
		RD::get_singleton()->compute_list_dispatch_threads(compute_list, dst_hi.x - dst_lo.x, dst_hi.y - dst_lo.y, 1);
		RD::get_singleton()->compute_list_end();
		src_size = dst_size;
		src_lo = dst_lo;
		src_hi = dst_hi;
	}
	RD::get_singleton()->draw_command_end_label();
}
