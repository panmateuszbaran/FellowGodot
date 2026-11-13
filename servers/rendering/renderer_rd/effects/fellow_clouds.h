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
		// Rain shafts (decision 0135): the rain's rate (0..1), and whether it snows.
		float precipitation = 0.0f;
		bool snow = false;
	};

	// The clouds' shadow map (decision 0133): the sun's light through the layer over the ground around the camera.
	struct ShadowParams {
		Vector3 camera_position;
		float ground_height = 0.0;
		Vector3 sun_direction; // towards the sun
		float time = 0.0;
		float bottom_altitude = 1.5; // km
		float thickness = 2.5;
		float coverage = 0.45;
		float density = 1.0;
		float shape_scale = 0.16;
		float detail_scale = 1.0;
		Vector2 wind = Vector2(0.01, 0.003); // km / s
	};
	static constexpr int SHADOW_SIZE = 512;
	static constexpr float SHADOW_EXTENT = 16000.0f; // m: the map's side (31 m texels)

private:
	enum Mode {
		MODE_NOISE_SHAPE,
		MODE_NOISE_DETAIL,
		MODE_TRACE,
		MODE_TEMPORAL,
		MODE_COMPOSITE,
		MODE_SHADOW,
		MODE_PROBE,
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
		float shadow[4]; // the shadow map's centre x, z (m), side (m), the ground's height (m)
		float precip[4]; // rain shafts: the rain's rate, 1 when it snows
		float field[4]; // decision 0211: the world's x, z (km) at the planet-centred frame's top
	}; // 320 bytes

	FellowCloudsShaderRD shader;
	RID shader_version;
	RID pipelines[MODE_MAX];
	RID ubo;
	RID shadow_ubo; // decision 0133: the shadow pass's own, written before the frame's colour passes
	RID shadow_maps[2]; // R16F SHADOW_SIZE^2: written by turns
	uint32_t shadow_parity = 0;
	Vector2 shadow_origins[2]; // m: each map's centre
	Vector2 shadow_read_origin;
	RID probe_ubo; // decision 0137
	RID probe_buffer; // 16 bytes: the rain at the camera, read back
	static void _probe_readback(const Vector<uint8_t> &p_data);
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
	// Writes the shadow map for this frame into one of two maps and returns the other, written by the previous call —
	// the one the colour passes read (a map written and read in the same frame lost the device with DDGI on, decision
	// 0133) — centred at get_shadow_origin() (m), SHADOW_EXTENT on a side.
	RID process_shadow(const ShadowParams &p_params);
	Vector2 get_shadow_origin() const { return shadow_read_origin; }
	// Showers (decision 0137): the rain the layer lets fall at the camera (p_params' camera, layer, wind and time), read
	// back a frame or two later into FellowWeather::set_rain_here().
	void process_probe(const ShadowParams &p_params);

	FellowClouds();
	~FellowClouds();
};

} // namespace RendererRD
