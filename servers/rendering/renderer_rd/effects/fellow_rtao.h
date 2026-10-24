/**************************************************************************/
/*  fellow_rtao.h                                                         */
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

#include "core/math/projection.h"
#include "servers/rendering/renderer_rd/shaders/effects/fellow_rtao.glsl.gen.h"
#include "servers/rendering/rendering_device.h"

namespace RendererRD {

// FellowEngine ray-traced ambient occlusion (docs/decisions/0081-rtao.md): rays against the frame's ray tracing scene
// (decision 0080) write the buffer SSAO would. Three compute passes: the trace, the temporal accumulation (reprojected,
// depth-checked), the depth- and normal-aware blur into the SSAO buffer.
class FellowRTAO {
public:
	struct Params {
		RID tlas;
		RID depth; // the depth buffer (sampled)
		RID normal_roughness;
		RID raw; // R16F, the trace
		RID history_prev; // RGBA16F: AO, frames accumulated, view depth
		RID history_out;
		RID dest; // the SSAO buffer (R8)
		Size2i size;
		Projection projection; // the camera's, not jittered
		Transform3D cam_transform;
		Projection prev_projection;
		Transform3D prev_cam_transform;
		bool history_valid = false;
		uint32_t frame = 0;
		uint32_t ray_count = 2;
		float radius = 1.0;
		float intensity = 2.0;
		float power = 1.5;
		uint32_t debug = 0; // rendering/fellow/debug/raytracing_view
	};

private:
	enum Mode {
		MODE_TRACE,
		MODE_TEMPORAL,
		MODE_DENOISE,
		MODE_MAX,
	};

	struct UBO { // std140
		float inv_projection[16];
		float view_to_world[16];
		float world_to_prev_clip[16];
		int32_t size[2];
		uint32_t frame;
		uint32_t ray_count;
		float radius;
		float intensity;
		float power;
		uint32_t history_valid;
		uint32_t debug;
		uint32_t pad[3];
	}; // 240 bytes

	FellowRtaoShaderRD shader;
	RID shader_version;
	RID pipelines[MODE_MAX];
	RID ubo;

public:
	void process(const Params &p_params);

	FellowRTAO();
	~FellowRTAO();
};

} // namespace RendererRD
