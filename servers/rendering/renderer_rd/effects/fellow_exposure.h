/**************************************************************************/
/*  fellow_exposure.h                                                     */
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

#include "servers/rendering/renderer_rd/shaders/effects/fellow_exposure.glsl.gen.h"
#include "servers/rendering/rendering_device.h"

namespace RendererRD {

// FellowEngine exposure (docs/decisions/0105-exposure-histogram.md, 0106-local-exposure.md): Unreal-style histogram eye
// adaptation (the average luminance the tone mapper reads, from the pixels between two percentiles of a log-luminance
// histogram, adapted exponentially with separate speeds) and local exposure (a bilateral grid of log luminance and a
// wide blur of it, which the tone mapper uses to compress the scene's large-scale contrast while keeping its detail).
class FellowExposure {
public:
	static constexpr int TILE = 32; // pixels per grid cell (matches the shader)
	static constexpr int SLICES = 32; // log-luminance slices of the grid, 1 EV each over [-16, 16]
	static constexpr int BINS = 64; // histogram bins over [-14, 14] EV

	struct Params {
		RID source; // HDR colour (sampled)
		Size2i size;
		float luminance_multiplier = 1.0;

		bool histogram = false;
		RID current; // the luminance buffers' current (R32F 1 x 1): the previous value read, the new one written
		float low_percent = 0.1;
		float high_percent = 0.9;
		float speed_up = 3.0;
		float speed_down = 1.0;
		float dt = 1.0 / 60.0;
		bool immediate = false;
		float min_luminance = 0.0;
		float max_luminance = 1e9;

		bool local = false;
		float blur_size = 0.5; // the blur's diameter, a fraction of the screen's width
	};

private:
	enum Mode {
		MODE_BUILD,
		MODE_GRID_BLUR,
		MODE_TILE_BLUR,
		MODE_ADAPT,
		MODE_MAX,
	};

	struct PushConstant {
		int32_t size[2];
		int32_t cells[2];

		float luminance_multiplier;
		float low_percent;
		float high_percent;
		float speed_up;

		float speed_down;
		float dt;
		float min_luminance;
		float max_luminance;

		int32_t flags;
		int32_t blur_radius;
		float blur_sigma;
		int32_t blur_axis;
	}; // 64 bytes

	FellowExposureShaderRD shader;
	RID shader_version;
	RID pipelines[MODE_MAX];

	RID histogram_buffer;
	// Local exposure's textures, for one size of the screen.
	Size2i cells;
	RID grid; // RG16F, cells x SLICES
	RID grid_blurred;
	RID tile_mean; // R16F, cells
	RID tile_tmp;
	RID tile_blurred;

	void _free_local();

public:
	void process(const Params &p_params);

	// For the tone mapper (valid after a process() with local on).
	RID get_grid() const { return grid_blurred; }
	RID get_blurred() const { return tile_blurred; }
	// From screen UV to the grid's texture coordinates (the cells cover a little more than the screen).
	Vector2 get_grid_scale(const Size2i &p_size) const;

	FellowExposure();
	~FellowExposure();
};

} // namespace RendererRD
