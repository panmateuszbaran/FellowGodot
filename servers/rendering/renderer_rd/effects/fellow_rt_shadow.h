/**************************************************************************/
/*  fellow_rt_shadow.h                                                    */
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
#include "servers/rendering/renderer_rd/shaders/effects/fellow_rt_shadow.glsl.gen.h"
#include "servers/rendering/rendering_device.h"

namespace RendererRD {

// FellowEngine ray-traced sun shadows (docs/decisions/0082-rt-sun-shadows.md): the first directional light's shadow per
// pixel, from rays against the frame's ray tracing scene (decision 0080), read by the opaque colour pass in place of
// the shadow map where valid. Three compute passes: the trace, the temporal accumulation and a small blur (both for
// soft shadows only).
class FellowRTShadow {
public:
	struct Params {
		RID tlas;
		RID depth;
		RID raw; // RG16F
		RID history_prev; // RGBA16F
		RID history_out;
		RID dest; // RG8: shadow, valid
		Size2i size;
		Projection projection;
		Transform3D cam_transform;
		Projection prev_projection;
		Transform3D prev_cam_transform;
		Vector3 light_direction; // world space, towards the light
		float light_tan_radius = 0.0; // tan of the light's angular radius (0: hard shadows)
		bool history_valid = false;
		uint32_t frame = 0;
		uint32_t ray_count = 1;
		float max_distance = 100.0;
		float ray_length = 200.0;
		bool missing_casters = false; // shadow casters not in the scene: lit pixels left to the shadow map
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
		float light_direction[4];
		int32_t size[2];
		uint32_t frame;
		uint32_t ray_count;
		float max_distance;
		float ray_length;
		uint32_t history_valid;
		uint32_t missing_casters;
	}; // 240 bytes

	FellowRtShadowShaderRD shader;
	RID shader_version;
	RID pipelines[MODE_MAX];
	RID ubo;

public:
	void process(const Params &p_params);

	FellowRTShadow();
	~FellowRTShadow();
};

} // namespace RendererRD
