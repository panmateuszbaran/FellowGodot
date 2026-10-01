/**************************************************************************/
/*  fellow_shadow_max_mip.h                                               */
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

#include "servers/rendering/renderer_rd/shaders/effects/fellow_shadow_max_mip.glsl.gen.h"
#include "core/templates/hash_map.h"
#include "servers/rendering/rendering_device.h"

namespace RendererRD {

// FellowEngine shadow min/max window pyramids (docs/decisions/0005-directional-shadow-sampling.md): for a shadow atlas,
// mip m holds the smallest (.r) and largest (.g) depth of every atlas window of 2^(m+2) texels starting every
// 2^(m+1) texels (windows overlap by half), so the Forward+ shadow filters can prove with one fetch that a filter
// region is fully lit or fully shadowed. One pyramid per atlas (the directional atlas, each positional atlas).
// Stored as RG16 UNORM when the GPU supports it (exact for a 16-bit atlas; for a 32-bit atlas the minimum is rounded
// down and the maximum up, which keeps every early-out decision safe), else RG32F. Only the windows over atlas texels
// written since the last update are rebuilt: a frame in which no shadow texel changed costs nothing.
class FellowShadowMaxMip {
	struct PushConstant {
		int32_t source_size[2];
		int32_t dest_size[2];
		int32_t source_is_pyramid;
		int32_t round_mode; // 0: store as is, 1: round the minimum down and the maximum up to 16-bit UNORM steps
		int32_t dest_offset[2];
		int32_t valid_count; // finest level: only atlas blocks touching these rects are read (0: all)
		int32_t pad[3];
		int32_t valid_rects[4][4]; // atlas texels (lo.x, lo.y, hi.x, hi.y), end exclusive
	};
	static constexpr int MAX_VALID_RECTS = 4; // matches the shader

	struct Target {
		RID texture; // (atlas size / 2) with the full mip chain
		LocalVector<RID> level_views;
		Size2i atlas_size;
		RID built_from; // atlas texture of the last build
		bool valid = false; // matches the atlas content (built from it and no atlas texel written since)
	};

	FellowShadowMaxMipShaderRD shader;
	RID shader_version;
	RID pipeline_rg16;
	RID pipeline_rg32;
	RID pipeline_tail_rg16; // the mips of 32 x 32 texels and smaller in one workgroup
	RID pipeline_tail_rg32;
	static constexpr int TAIL_MAX_LEVELS = 8; // matches the shader
	static constexpr int TAIL_MAX_SIZE = 32;
	bool rg16_supported = false;

	HashMap<RID, Target> targets; // key: the shadow atlas (RID() for the directional atlas)

	void _free_target(Target &r_target);

public:
	// Brings the pyramid of p_key up to date with its atlas, rebuilding the windows over p_dirty (atlas texels written
	// since the last call; everything when the atlas texture or its size changed or the pyramid is not valid).
	// Call after the shadow passes of the frame.
	// p_valid (optional, non-empty): within the rebuilt region, only the windows over atlas texels touching one of these
	// rects are computed from the atlas; the others are set to (0, 1), which bounds any content (no early-out there).
	// Exact for any p_valid; it only decides where the early-outs can fire. More than 4 rects are merged.
	void build(RID p_key, RID p_atlas_texture, const Size2i &p_atlas_size, const Rect2i &p_dirty, bool p_atlas_16_bits, const LocalVector<Rect2i> *p_valid = nullptr);
	// The same for several written rects: one update over their union when it is not much larger than the rects
	// together (each update has a fixed cost: a dispatch and a barrier per mip), else one update per rect.
	void build_rects(RID p_key, RID p_atlas_texture, const Size2i &p_atlas_size, const LocalVector<Rect2i> &p_dirty, bool p_atlas_16_bits, const LocalVector<Rect2i> *p_valid = nullptr);
	void invalidate(RID p_key);
	bool is_valid(RID p_key) const;
	RID get_texture(RID p_key) const;
	// Frees the pyramids whose key no longer satisfies p_keep (atlases that were freed).
	template <typename F>
	void free_unused(F p_keep) {
		LocalVector<RID> gone;
		for (KeyValue<RID, Target> &kv : targets) {
			if (kv.key.is_valid() && !p_keep(kv.key)) {
				gone.push_back(kv.key);
			}
		}
		for (const RID &k : gone) {
			_free_target(targets[k]);
			targets.erase(k);
		}
	}
	uint64_t get_vram_bytes() const;

	FellowShadowMaxMip();
	~FellowShadowMaxMip();
};

} // namespace RendererRD
