/**************************************************************************/
/*  fellow_bloom.h                                                        */
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

#include "servers/rendering/renderer_rd/shaders/effects/fellow_bloom.glsl.gen.h"
#include "servers/rendering/rendering_device.h"

namespace RendererRD {

// FellowEngine bloom (docs/decisions/0112-bloom.md), lens flare (0113) and light shafts (0116): an energy-conserving bloom of the whole image
// (a 13-tap downsample chain and a tent upsample chain, Jimenez 2014) at half resolution and down to 1/64; the tone
// mapping pass mixes it in (and the lens dirt, decision 0111, on it). The lens flare's ghosts and halo at quarter
// resolution from the 1/8 level.
class FellowBloom {
public:
	static constexpr int LEVELS = 6; // 1/2 to 1/64 of the internal size

	struct Params {
		RID source; // the scene colour (after TAA and motion blur), scene-linear
		Size2i size; // the source's
		float scatter = 0.7;
		float flare_intensity = 0.0; // 0: no flare pass
		float flare_threshold = 8.0;
		// Light shafts (decision 0116): 0 no shafts pass.
		float shafts_intensity = 0.0;
		float shafts_threshold = 0.5;
		float shafts_length = 0.9;
		Vector2 sun_uv; // the sun on the screen (it may be outside)
		RID depth; // full resolution, the sky at 0
	};

private:
	enum Mode {
		MODE_DOWNSAMPLE_FIRST,
		MODE_DOWNSAMPLE,
		MODE_UPSAMPLE,
		MODE_FLARE,
		MODE_SHAFTS_MASK,
		MODE_SHAFTS_BLUR,
		MODE_MAX,
	};

	struct PushConstant {
		int32_t dest_size[2];
		float source_texel[2];
		float scatter;
		float flare_threshold;
		float flare_ghost_spacing;
		float flare_halo_width;
		float flare_distortion;
		float pad[3];
		float shafts[4];
	}; // 64 bytes

	FellowBloomShaderRD shader;
	RID shader_version;
	RID pipelines[MODE_MAX];
	Size2i size;
	int levels = 0;
	RID down; // RGBA16F, levels mipmaps
	RID up; // RGBA16F, levels - 1 mipmaps (up level 0 is the bloom)
	RID flare; // RGBA16F, quarter resolution
	RID shafts_mask; // RGBA16F, quarter resolution
	RID shafts; // RGBA16F, quarter resolution
	RID down_mips[LEVELS];
	RID up_mips[LEVELS];
	Size2i mip_sizes[LEVELS];
	bool flare_valid = false;
	bool shafts_valid = false;

	void _free_targets();
	void _dispatch(Mode p_mode, RID p_source, RID p_source_low, RID p_dest, const Size2i &p_dest_size, const Size2i &p_texel_of, float p_scatter, const Params &p_params);

public:
	FellowBloom();
	~FellowBloom();

	void process(const Params &p_params);
	RID get_bloom() const { return levels > 1 ? up_mips[0] : down_mips[0]; }
	RID get_flare() const { return flare_valid ? flare : RID(); }
	RID get_shafts() const { return shafts_valid ? shafts : RID(); }
};

} // namespace RendererRD
