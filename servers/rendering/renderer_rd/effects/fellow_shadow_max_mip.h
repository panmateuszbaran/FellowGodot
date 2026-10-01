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
#include "servers/rendering/rendering_device.h"

namespace RendererRD {

// FellowEngine directional shadow min/max pyramid (docs/decisions/0005-directional-shadow-sampling.md): level k holds the
// smallest (.r) and largest (.g) depth of every 2^(k+1) x 2^(k+1) block of the directional shadow atlas, so that the
// Forward+ shadow filters can prove with four fetches that a region is fully lit or fully shadowed.
// Stored as RG16 UNORM when the GPU supports it (exact for a 16-bit atlas; for a 32-bit atlas the minimum is rounded
// down and the maximum up, which keeps every early-out decision safe), else RG32F. Only the atlas regions written since
// the last build are rebuilt; a frame in which no directional shadow texel changed (shadow cache, static scene) costs
// nothing.
class FellowShadowMaxMip {
	struct PushConstant {
		int32_t source_size[2];
		int32_t dest_size[2];
		int32_t source_is_pyramid;
		int32_t round_mode; // 0: store as is, 1: round the minimum down and the maximum up to 16-bit UNORM steps
		int32_t dest_offset[2];
	};

	FellowShadowMaxMipShaderRD shader;
	RID shader_version;
	RID pipeline_rg16;
	RID pipeline_rg32;

	RID texture; // (atlas size / 2) with the full mip chain
	bool rg16_supported = false;
	bool texture_rg16 = false;
	LocalVector<RID> level_views;
	Size2i atlas_size;
	RID built_from; // atlas texture of the last build
	bool valid = false; // matches the atlas content (built from it and no atlas texel written since)

	void _free_texture();

public:
	// Brings the pyramid up to date with the directional shadow atlas, rebuilding the blocks over p_dirty (atlas texels
	// written since the last call; everything when the atlas texture or its size changed or the pyramid is not valid).
	// Call after the shadow passes of the frame.
	void build(RID p_atlas_texture, const Size2i &p_atlas_size, const Rect2i &p_dirty, bool p_atlas_16_bits);
	void invalidate() { valid = false; }
	bool is_valid() const { return valid; }
	RID get_texture() const { return texture; }
	uint64_t get_vram_bytes() const;

	FellowShadowMaxMip();
	~FellowShadowMaxMip();
};

} // namespace RendererRD
