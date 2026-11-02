/**************************************************************************/
/*  fellow_clouds.h                                                       */
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
#include "servers/rendering/renderer_rd/shaders/effects/fellow_clouds.glsl.gen.h"
#include "servers/rendering/rendering_device.h"

namespace RendererRD {

// FellowEngine volumetric clouds (docs/decisions/0109-volumetric-clouds.md): a raymarched layer of clouds (Nubis-style
// noise and lighting) at half resolution, accumulated over frames, composited over the sky's pixels before the
// transparent pass. Lit by the first directional light; with the sky atmosphere (decision 0108) through its tables.
class FellowClouds {
public:
	struct Params {
		RID color; // RGBA16F storage, the internal colour buffer
		RID depth;
		Size2i size;
		Projection view_projection; // corrected (jittered) projection * view
		Projection prev_view_projection;
		Vector3 camera_position;
		float ground_height = 0.0;
		Vector3 sun_direction; // towards the sun
		Color sun_illuminance;
		Color ambient; // the sky's light on the tops without the atmosphere
		float time = 0.0;
		float bottom_altitude = 1.5; // km
		float thickness = 2.5;
		float coverage = 0.45;
		float density = 1.0;
		float shape_scale = 0.16; // 1 / km: the shape noise's period 6.25 km, its features 0.4 to 1.6 km
		float detail_scale = 1.0; // the detail noise's period 1 km, its features 60 to 250 m
		float ground_albedo = 0.3; // the light from below
		Vector2 wind = Vector2(0.01, 0.003); // km / s
		float max_distance = 60.0; // km
		float fade_distance = 40.0;
		RID atmosphere_transmittance; // RID(): no atmosphere
		RID atmosphere_sky_view;
		uint32_t frame = 0;
		bool history_valid = false;
		bool interleaved = true; // decision 0115: a quarter of the pixels traced per frame
		// A lightning flash (decision 0131): the clouds around it glow.
		bool flash = false;
		Vector3 flash_position; // world (m)
		Color flash_color; // colour * energy, exposure included
		float flash_radius = 1000.0f; // m
	};

private:
	enum Mode {
		MODE_NOISE_SHAPE,
		MODE_NOISE_DETAIL,
		MODE_TRACE,
		MODE_TEMPORAL,
		MODE_COMPOSITE,
		MODE_MAX,
	};

	struct UBO { // std140
		float inv_view_projection[16];
		float prev_view_projection[16];
		float camera_position[4];
		float sun_direction[4];
		float sun_color[4];
		float ambient_color[4];
		float layer[4];
		float shape[4];
		float misc[4];
		int32_t sizes[4];
		int32_t flags[4];
		float flash[4]; // position (m), radius (m)
		float flash_color[4]; // colour * energy, on
	}; // 272 bytes

	FellowCloudsShaderRD shader;
	RID shader_version;
	RID pipelines[MODE_MAX];
	RID ubo;
	RID shape_noise; // RGBA8 128^3
	RID detail_noise; // RGBA8 32^3
	RID white; // 1 x 1 for the atmosphere's tables when there is none
	Size2i trace_size;
	RID trace;
	RID history[2];
	uint32_t parity = 0;
	bool interleave_primed = false; // every pixel of the trace written since the targets were made

	void _free_targets();

public:
	void process(const Params &p_params);

	FellowClouds();
	~FellowClouds();
};

} // namespace RendererRD
