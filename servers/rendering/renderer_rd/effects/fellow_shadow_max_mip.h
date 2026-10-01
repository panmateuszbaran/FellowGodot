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

// FellowEngine directional shadow max pyramid (docs/decisions/0005-directional-shadow-sampling.md): mip k (k >= 1 of
// the atlas, stored from k = 1 as level 0 of this texture) holds the maximum depth (reverse Z: the caster closest to
// the light) of every 2^k x 2^k atlas block. The Forward+ PCSS blocker search uses it to prove that a search region
// holds no blocker (then the result is exactly 1.0, as in the stock path) with four fetches.
class FellowShadowMaxMip {
	struct PushConstant {
		int32_t source_size[2];
		int32_t dest_size[2];
	};

	FellowShadowMaxMipShaderRD shader;
	RID shader_version;
	RID pipeline;

	RID texture; // R32F, (atlas size / 2) with the full mip chain
	LocalVector<RID> level_views;
	Size2i atlas_size;
	bool valid = false; // built from the atlas content of the current frame

	void _free_texture();

public:
	// (Re)builds the pyramid from the directional shadow atlas. Call after the shadow passes of the frame.
	void build(RID p_atlas_texture, const Size2i &p_atlas_size);
	void invalidate() { valid = false; }
	bool is_valid() const { return valid; }
	RID get_texture() const { return texture; }
	uint64_t get_vram_bytes() const;

	FellowShadowMaxMip();
	~FellowShadowMaxMip();
};

} // namespace RendererRD
