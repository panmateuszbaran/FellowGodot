/**************************************************************************/
/*  fellow_shadow_cache.h                                                 */
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

#pragma once

#include "servers/rendering/renderer_rd/pipeline_cache_rd.h"
#include "servers/rendering/renderer_rd/shaders/effects/fellow_shadow_cache_copy.glsl.gen.h"
#include "servers/rendering/rendering_device.h"

namespace RendererRD {

// FellowEngine directional shadow cache (docs/decisions/0004-directional-shadow-cache.md): owns the per-cascade cache
// depth textures and copies a cache region into the directional shadow atlas, re-basing the stored depth from the
// cache's depth range to the cascade's.
class FellowShadowCache {
	struct PushConstant {
		int32_t dst_origin[2];
		int32_t src_offset[2];
		float scale;
		float bias;
		float pad[2];
	};

	FellowShadowCacheCopyShaderRD copy_shader;
	RID copy_shader_version;
	PipelineCacheRD copy_pipeline;

	struct Entry {
		RID texture[4];
		RID framebuffer[4];
		int size[4] = { 0, 0, 0, 0 };
		RD::DataFormat format[4] = { RD::DATA_FORMAT_MAX, RD::DATA_FORMAT_MAX, RD::DATA_FORMAT_MAX, RD::DATA_FORMAT_MAX };
		bool content_valid[4] = { false, false, false, false };
		bool lost = false; // a cascade was needed but its content is gone: the scene cull must rebuild
	};
	HashMap<int64_t, Entry> entries;

	// What each directional-atlas rect holds since it was last written, so that an unchanged cascade (same cache
	// content, same offset and depth remap, no dynamic casters now or last time) is neither cleared nor copied again.
	struct AtlasRectState {
		RID atlas;
		Rect2i rect;
		RID source;
		Vector2i offset;
		float scale = 0.0;
		float bias = 0.0;
		bool dynamic = false;
	};
	LocalVector<AtlasRectState> atlas_states;

	void _free_entry(Entry &p_entry);

public:
	// Returns the framebuffer of the cache texture of cascade p_cascade, (re)creating it if size or format changed.
	RID get_framebuffer(int64_t p_cache_id, int p_cascade, int p_size, RD::DataFormat p_format, uint32_t p_usage_bits);
	RID get_texture(int64_t p_cache_id, int p_cascade) const;
	void set_content_valid(int64_t p_cache_id, int p_cascade);
	// Returns the cache texture if it holds a rebuilt content, otherwise marks the cache lost and returns RID().
	RID get_valid_texture(int64_t p_cache_id, int p_cascade);
	bool take_lost(int64_t p_cache_id);

	// Atlas rect bookkeeping (see AtlasRectState).
	void atlas_invalidate_all() { atlas_states.clear(); }
	bool atlas_rect_holds(RID p_atlas, const Rect2i &p_rect, RID p_source, const Vector2i &p_offset, float p_scale, float p_bias) const;
	void atlas_rect_set(RID p_atlas, const Rect2i &p_rect, RID p_source, const Vector2i &p_offset, float p_scale, float p_bias, bool p_dynamic);
	void free_cache(int64_t p_cache_id);
	uint64_t get_vram_bytes() const;

	// Writes depth = clamp(cache(p + src_offset) * scale + bias, 0, 0.9999) into p_dst_rect of p_dst_framebuffer.
	void copy(RID p_source_texture, RID p_dst_framebuffer, const Rect2i &p_dst_rect, const Vector2i &p_src_offset, float p_scale, float p_bias);

	FellowShadowCache();
	~FellowShadowCache();
};

} // namespace RendererRD
