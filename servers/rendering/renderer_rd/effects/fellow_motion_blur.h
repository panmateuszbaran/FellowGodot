/**************************************************************************/
/*  fellow_motion_blur.h                                                  */
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

#include "servers/rendering/renderer_rd/shaders/effects/fellow_motion_blur.glsl.gen.h"
#include "core/math/projection.h"
#include "servers/rendering/rendering_device.h"

namespace RendererRD {

// FellowEngine motion blur (docs/decisions/0078-motion-blur.md): a reconstruction filter on the HDR image after TAA,
// from the velocity buffer the renderer writes for TAA (camera and object motion; where nothing was drawn, the camera's
// motion reprojected). Three compute passes: the largest
// blur of each 32 x 32 tile, the largest of each tile's 3 x 3 neighbourhood, the per-pixel gather.
class FellowMotionBlur {
public:
	static constexpr int TILE_SIZE = 32; // matches the shader

	struct Params {
		RID source; // HDR colour (sampled)
		RID depth;
		RID velocity;
		RID tile_max; // RG16F, ceil(size / TILE_SIZE)
		RID neighbor_max; // the same size
		RID dest; // RGBA16F storage, the size of source
		Size2i size;
		float amount = 0.5; // fraction of the frame the shutter is open
		float max_radius = 32.0; // pixels, at most TILE_SIZE
		int sample_count = 12;
		float z_near = 0.05;
		float z_far = 4000.0;
		bool orthogonal = false;
		Projection sky_reprojection; // this frame's NDC to the previous frame's (motion_vector_inc.glsl), for the background
	};

private:
	enum Mode {
		MODE_TILE_MAX,
		MODE_NEIGHBOR_MAX,
		MODE_GATHER,
		MODE_MAX,
	};

	struct PushConstant {
		float sky_reprojection[16];
		int32_t size[2];
		int32_t tile_count[2];
		float velocity_scale[2];
		float max_radius;
		int32_t sample_count;
		float z_near;
		float z_far;
		int32_t orthogonal;
		float soft_z;
	}; // 112 bytes

	FellowMotionBlurShaderRD shader;
	RID shader_version;
	RID pipelines[MODE_MAX];

public:
	void process(const Params &p_params);

	FellowMotionBlur();
	~FellowMotionBlur();
};

} // namespace RendererRD
