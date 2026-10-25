/**************************************************************************/
/*  fellow_contact_shadow.h                                               */
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
#include "servers/rendering/renderer_rd/shaders/effects/fellow_contact_shadow.glsl.gen.h"
#include "servers/rendering/rendering_device.h"

namespace RendererRD {

// FellowEngine contact shadows (docs/decisions/0107-contact-shadows.md): for the first directional light, per pixel of
// the depth pre-pass, a short march towards the light in screen space against the depth buffer (Unreal's contact
// shadows); the opaque colour pass multiplies the light's shadow by it. Catches the small contacts the shadow map's
// resolution and bias lose (feet on the ground, objects on tables, wrinkles of geometry).
class FellowContactShadow {
public:
	struct Params {
		RID depth;
		RID dest; // RG8: shadow (1 lit), 1
		Size2i size;
		Projection projection; // the jittered projection the depth buffer was rendered with
		Vector3 light_direction_view; // towards the light, view space
		float length = 0.5; // metres
		int steps = 8;
		float thickness = 0.3; // metres: a surface at most this far in front of the march occludes
		uint32_t frame = 0;
	};

private:
	struct UBO { // std140
		float inv_projection[16];
		float projection[16];
		float light_direction[4]; // xyz, w: length
		int32_t size[2];
		uint32_t frame;
		uint32_t steps;
		float thickness;
		float pad[3];
	}; // 176 bytes

	FellowContactShadowShaderRD shader;
	RID shader_version;
	RID pipeline;
	RID ubo;

public:
	void process(const Params &p_params);

	FellowContactShadow();
	~FellowContactShadow();
};

} // namespace RendererRD
