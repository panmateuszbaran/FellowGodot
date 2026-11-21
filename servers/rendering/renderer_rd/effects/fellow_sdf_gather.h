/**************************************************************************/
/*  fellow_sdf_gather.h                                                   */
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

#include "core/math/basis.h"
#include "core/math/projection.h"
#include "core/math/transform_3d.h"
#include "servers/rendering/renderer_rd/shaders/effects/fellow_sdf_gather.glsl.gen.h"
#include "servers/rendering/rendering_device.h"

namespace RendererRD {

// FellowEngine SDF screen gather (docs/decisions/0308-sdf-screen-gather.md): SDFGI's diffuse light gathered per screen
// probe, traced through its distance field cascades, in place of its world probes' interpolated light.
class FellowSDFGather {
public:
	static constexpr int MAX_CASCADES = 8; // as SDFGI's

	struct Params {
		// SDFGI's cascades.
		int cascade_count = 0;
		RID sdf_distance[MAX_CASCADES];
		RID sdf_light[MAX_CASCADES];
		RID sdf_aniso0[MAX_CASCADES];
		RID sdf_aniso1[MAX_CASCADES];
		RID sdf_cascades_ubo;
		float cascade_size = 128.0;
		float y_mult = 1.0;
		int max_steps = 64;
		float light_scale = 1.0; // the voxels' light to the scene shader's units
		float energy = 1.0;
		float normal_bias = 1.0; // cascade 0 cells
		// The environment's ambient light (as DDGI's FellowRTLighting).
		RID radiance; // the sky's radiance octmap; RID(): none
		bool radiance_array = false;
		float radiance_border[2] = { 0.0, 1.0 };
		float radiance_level = 0.0;
		Color ambient_color; // a: sky contribution
		bool use_ambient = false;
		bool use_sky = false;
		float sky_factor = 1.0;
		Basis sky_xform;
		// The view.
		RID depth;
		RID normal_roughness;
		Size2i internal_size;
		Projection inv_projection; // NDC (x, y, depth * 2 - 1) to view
		Transform3D view_to_world;
		Projection prev_world_to_clip;
		float pixel_footprint = 0.001; // a pixel's size at 1 m (metres)
		RID ambient; // SDFGI's ambient buffer (RGBA16F, storage)
		Size2i ambient_size;
		// The probes (the grid's size): this frame's and the previous frame's.
		RID irradiance[2];
		RID position[2];
		RID normal[2];
		RID filtered; // the irradiance filtered over the probes on the same surface, for the resolve
		bool filter = true;
		int current = 0;
		Size2i grid;
		int tile = 16;
		int rays = 32;
		uint32_t frame = 0;
		bool history = false;
		float max_samples = 16.0;
	};

private:
	enum Mode {
		MODE_TRACE,
		MODE_TRACE_ARRAY,
		MODE_RESOLVE,
		MODE_FILTER,
		MODE_MAX,
	};

	struct UBO { // std140, as Params in fellow_sdf_gather.glsl (400 bytes)
		float inv_projection[16];
		float view_to_world[16];
		float prev_world_to_clip[16];
		float sky_xform[16];
		float sky_params[4];
		float ambient_color[4];
		float radiance_border[4];
		float sdf[4];
		float light[4];
		float footprint[4];
		int32_t screen[4];
		int32_t probes[4];
		int32_t frame[4];
	};

	FellowSdfGatherShaderRD shader;
	RID shader_version;
	RID pipelines[MODE_MAX];
	RID ubo;

public:
	void process(const Params &p_params);

	FellowSDFGather();
	~FellowSDFGather();
};

} // namespace RendererRD
