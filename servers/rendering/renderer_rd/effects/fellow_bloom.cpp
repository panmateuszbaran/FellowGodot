/**************************************************************************/
/*  fellow_bloom.cpp                                                      */
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

#include "fellow_bloom.h"

#include "servers/rendering/renderer_rd/storage_rd/material_storage.h"
#include "servers/rendering/renderer_rd/uniform_set_cache_rd.h"

using namespace RendererRD;

FellowBloom::FellowBloom() {
	Vector<String> modes;
	modes.push_back("\n#define MODE_DOWNSAMPLE_FIRST\n");
	modes.push_back("\n#define MODE_DOWNSAMPLE\n");
	modes.push_back("\n#define MODE_UPSAMPLE\n");
	modes.push_back("\n#define MODE_FLARE\n");
	modes.push_back("\n#define MODE_SHAFTS_MASK\n");
	modes.push_back("\n#define MODE_SHAFTS_BLUR\n");
	shader.initialize(modes);
	shader_version = shader.version_create();
	for (int i = 0; i < MODE_MAX; i++) {
		pipelines[i] = RD::get_singleton()->compute_pipeline_create(shader.version_get_shader(shader_version, i));
	}
}

void FellowBloom::_free_targets() {
	for (int i = 0; i < LEVELS; i++) {
		RID *views[] = { &down_mips[i], &up_mips[i] };
		for (RID *v : views) {
			if (v->is_valid()) {
				RD::get_singleton()->free_rid(*v);
				*v = RID();
			}
		}
	}
	RID *rids[] = { &down, &up, &flare, &shafts_mask, &shafts };
	for (RID *r : rids) {
		if (r->is_valid()) {
			RD::get_singleton()->free_rid(*r);
			*r = RID();
		}
	}
	size = Size2i();
	levels = 0;
	flare_valid = false;
	shafts_valid = false;
}

FellowBloom::~FellowBloom() {
	_free_targets();
	shader.version_free(shader_version);
}

void FellowBloom::_dispatch(Mode p_mode, RID p_source, RID p_source_low, RID p_dest, const Size2i &p_dest_size, const Size2i &p_texel_of, float p_scatter, const Params &p_params) {
	UniformSetCacheRD *uniform_set_cache = UniformSetCacheRD::get_singleton();
	MaterialStorage *material_storage = MaterialStorage::get_singleton();
	RID linear = material_storage->sampler_rd_get_default(RSE::CANVAS_ITEM_TEXTURE_FILTER_LINEAR, RSE::CANVAS_ITEM_TEXTURE_REPEAT_DISABLED);
	RID shader_rd = shader.version_get_shader(shader_version, p_mode);
	PushConstant pc = {};
	pc.dest_size[0] = p_dest_size.x;
	pc.dest_size[1] = p_dest_size.y;
	pc.source_texel[0] = 1.0f / float(MAX(p_texel_of.x, 1));
	pc.source_texel[1] = 1.0f / float(MAX(p_texel_of.y, 1));
	pc.scatter = p_scatter;
	pc.flare_threshold = p_params.flare_threshold;
	pc.flare_ghost_spacing = 0.3f;
	pc.flare_halo_width = 0.45f;
	pc.flare_distortion = 0.006f;
	pc.shafts[0] = p_params.sun_uv.x;
	pc.shafts[1] = p_params.sun_uv.y;
	pc.shafts[2] = p_params.shafts_threshold;
	pc.shafts[3] = p_params.shafts_length;
	RD::ComputeListID list = RD::get_singleton()->compute_list_begin();
	RD::get_singleton()->compute_list_bind_compute_pipeline(list, pipelines[p_mode]);
	RD::Uniform u_source(RD::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE, 0, Vector<RID>({ linear, p_source }));
	RD::Uniform u_low(RD::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE, 1, Vector<RID>({ linear, p_source_low.is_valid() ? p_source_low : p_source }));
	RD::get_singleton()->compute_list_bind_uniform_set(list, uniform_set_cache->get_cache(shader_rd, 0, u_source, u_low), 0);
	RD::Uniform u_dest(RD::UNIFORM_TYPE_IMAGE, 0, p_dest);
	RD::get_singleton()->compute_list_bind_uniform_set(list, uniform_set_cache->get_cache(shader_rd, 1, u_dest), 1);
	RD::get_singleton()->compute_list_set_push_constant(list, &pc, sizeof(PushConstant));
	RD::get_singleton()->compute_list_dispatch_threads(list, p_dest_size.x, p_dest_size.y, 1);
	RD::get_singleton()->compute_list_end();
}

void FellowBloom::process(const Params &p_params) {
	ERR_FAIL_COND(p_params.source.is_null());
	if (p_params.size != size) {
		_free_targets();
		size = p_params.size;
		Size2i s = Size2i(MAX((size.x + 1) / 2, 1), MAX((size.y + 1) / 2, 1));
		levels = 0;
		for (int i = 0; i < LEVELS; i++) {
			mip_sizes[i] = s;
			levels++;
			if (s.x <= 2 || s.y <= 2) {
				break;
			}
			s = Size2i(MAX((s.x + 1) / 2, 1), MAX((s.y + 1) / 2, 1));
		}
		RD::TextureFormat tf;
		tf.texture_type = RD::TEXTURE_TYPE_2D;
		tf.format = RD::DATA_FORMAT_R16G16B16A16_SFLOAT;
		tf.width = mip_sizes[0].x;
		tf.height = mip_sizes[0].y;
		tf.mipmaps = levels;
		tf.usage_bits = RD::TEXTURE_USAGE_SAMPLING_BIT | RD::TEXTURE_USAGE_STORAGE_BIT;
		down = RD::get_singleton()->texture_create(tf, RD::TextureView());
		for (int i = 0; i < levels; i++) {
			down_mips[i] = RD::get_singleton()->texture_create_shared_from_slice(RD::TextureView(), down, 0, i, 1, RD::TEXTURE_SLICE_2D);
		}
		if (levels > 1) {
			tf.mipmaps = levels - 1;
			up = RD::get_singleton()->texture_create(tf, RD::TextureView());
			for (int i = 0; i < levels - 1; i++) {
				up_mips[i] = RD::get_singleton()->texture_create_shared_from_slice(RD::TextureView(), up, 0, i, 1, RD::TEXTURE_SLICE_2D);
			}
		}
		if (levels > 2) {
			tf.width = mip_sizes[1].x;
			tf.height = mip_sizes[1].y;
			tf.mipmaps = 1;
			flare = RD::get_singleton()->texture_create(tf, RD::TextureView());
			shafts_mask = RD::get_singleton()->texture_create(tf, RD::TextureView());
			shafts = RD::get_singleton()->texture_create(tf, RD::TextureView());
		}
	}

	RD::get_singleton()->draw_command_begin_label("FellowEngine Bloom");
	// Downsamples: the source to level 0, each level to the next.
	_dispatch(MODE_DOWNSAMPLE_FIRST, p_params.source, RID(), down_mips[0], mip_sizes[0], size, 0.0f, p_params);
	for (int i = 1; i < levels; i++) {
		_dispatch(MODE_DOWNSAMPLE, down_mips[i - 1], RID(), down_mips[i], mip_sizes[i], mip_sizes[i - 1], 0.0f, p_params);
	}
	// Upsamples: from the lowest level up; up level i mixes down level i with the tent of up level i + 1 (or of the
	// lowest down level).
	for (int i = levels - 2; i >= 0; i--) {
		RID low = (i == levels - 2) ? down_mips[levels - 1] : up_mips[i + 1];
		_dispatch(MODE_UPSAMPLE, down_mips[i], low, up_mips[i], mip_sizes[i], mip_sizes[i + 1], CLAMP(p_params.scatter, 0.0f, 1.0f), p_params);
	}
	flare_valid = false;
	if (p_params.flare_intensity > 0.0f && levels > 2) {
		_dispatch(MODE_FLARE, down_mips[2], RID(), flare, mip_sizes[1], mip_sizes[2], 0.0f, p_params);
		flare_valid = true;
	}
	shafts_valid = false;
	if (p_params.shafts_intensity > 0.0f && levels > 2 && p_params.depth.is_valid()) {
		_dispatch(MODE_SHAFTS_MASK, down_mips[1], p_params.depth, shafts_mask, mip_sizes[1], mip_sizes[1], 0.0f, p_params);
		_dispatch(MODE_SHAFTS_BLUR, shafts_mask, RID(), shafts, mip_sizes[1], mip_sizes[1], 0.0f, p_params);
		shafts_valid = true;
	}
	RD::get_singleton()->draw_command_end_label();
}
