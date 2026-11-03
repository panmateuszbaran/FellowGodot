/**************************************************************************/
/*  fellow_rainbow.h                                                      */
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
#include "servers/rendering/renderer_rd/shaders/effects/fellow_rainbow.glsl.gen.h"
#include "servers/rendering/rendering_device.h"

namespace RendererRD {

// FellowEngine rainbows (docs/decisions/0134-rainbows.md): sunlight refracted by falling rain, seen around the point
// opposite the sun — the primary bow at about 42°, red outside, and the fainter secondary at about 51°, its colours
// reversed, the sky brighter inside the primary. Added over the opaque surfaces and the sky (before the transparent
// pass) where it rains in front of them (more of it the further they are).
class FellowRainbow {
public:
	struct Params {
		RID color; // RGBA16F storage, the internal colour buffer
		RID depth;
		Size2i size;
		Projection view_projection; // corrected (jittered) projection * view
		Vector3 camera_position;
		Vector3 sun_direction; // towards the sun
		Color light; // the bow's brightness: the sun's illuminance * the rain's * exposure
		float distance = 150.0f; // m: the rain between a surface and the camera this far shows half the bow
		// The clouds' shadow at the camera (decision 0133): the sun the rain sees.
		RID cloud_shadow; // RID(): none
		Vector2 cloud_shadow_origin;
		float cloud_shadow_inv_extent = 0.0f;
		Vector2 cloud_shadow_slope; // the sun's direction x / y, z / y
		float cloud_shadow_strength = 0.0f;
	};

private:
	struct UBO { // std140
		float inv_view_projection[16];
		float camera_position[4]; // w: 1 / distance (1/m)
		float anti_sun[4]; // the point opposite the sun (unit)
		float light[4]; // rgb
		float cloud_shadow[4]; // origin x, z (m), 1 / extent, strength
		float cloud_shadow2[4]; // the sun's slope x, z
		int32_t size[4];
	}; // 160 bytes

	static constexpr int TABLE_SIZE = 512;
	static constexpr float TABLE_MIN = 30.0f; // degrees from the point opposite the sun
	static constexpr float TABLE_MAX = 60.0f;

	FellowRainbowShaderRD shader;
	RID shader_version;
	RID pipeline;
	RID ubo;
	RID table; // RGBA16F TABLE_SIZE x 1: the bows' colour by angle
	RID white;

	void _build_table();

public:
	void process(const Params &p_params);

	FellowRainbow();
	~FellowRainbow();
};

} // namespace RendererRD
