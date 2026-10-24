/**************************************************************************/
/*  fellow_rt_reflections.h                                               */
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
#include "servers/rendering/renderer_rd/effects/fellow_ddgi.h"
#include "servers/rendering/renderer_rd/shaders/effects/fellow_rt_reflections.glsl.gen.h"
#include "servers/rendering/rendering_device.h"

namespace RendererRD {

// FellowEngine ray-traced reflections (docs/decisions/0084-rt-reflections.md): a GGX-sampled reflection ray per pixel up
// to a maximum roughness against the frame's ray tracing scene (decision 0080), its hits lit as DDGI's (0083); read by the
// opaque colour passes in place of the probes' and the sky's indirect specular light. Three compute passes: the trace,
// the temporal accumulation (rough surfaces), a roughness-sized blur.
class FellowRTReflections {
public:
	struct Params {
		RID tlas;
		RID instances;
		RID depth;
		RID normal_roughness;
		RID raw; // RGBA16F
		RID history_prev; // RGBA16F
		RID history_out;
		RID dest; // RGBA16F: radiance, weight
		RID irradiance; // the DDGI volume (or any texture with volume.base_cell[3] = 0)
		RID distance;
		RID radiance; // the sky
		bool radiance_array = false;
		float radiance_border[2] = { 0.0, 1.0 };
		float radiance_level = 0.0;
		FellowDDGI::Volume volume;
		Size2i size;
		Projection projection;
		Transform3D cam_transform;
		Projection prev_projection;
		Transform3D prev_cam_transform;
		Vector3 sun_direction;
		Color sun_color;
		bool has_sun = false;
		bool sun_shadow = false;
		Color ambient_color;
		bool use_ambient = false;
		bool use_sky = false;
		float sky_factor = 1.0;
		Basis sky_xform;
		bool history_valid = false;
		uint32_t frame = 0;
		float max_roughness = 0.5;
		float ray_length = 200.0;
		// Surfaces rougher than this are traced one pixel of each 2 x 2 per frame (the others from history); 1: off.
		float interleave_roughness = 1.0;
	};

private:
	enum Mode {
		MODE_TRACE,
		MODE_TRACE_FETCH,
		MODE_TRACE_ARRAY,
		MODE_TRACE_FETCH_ARRAY,
		MODE_TEMPORAL,
		MODE_DENOISE,
		MODE_MAX,
	};

	struct UBO { // std140, as Params in fellow_rt_reflections.glsl (448 bytes)
		float inv_projection[16];
		float view_to_world[16];
		float world_to_prev_clip[16];
		FellowDDGI::Volume volume;
		float sun_direction[4];
		float sun_color[4];
		float ambient_color[4];
		float sky_params[4];
		float sky_xform[16];
		float radiance_border[4];
		int32_t size[2];
		uint32_t frame;
		float max_roughness;
		float ray_length;
		uint32_t history_valid;
		float interleave_roughness;
		uint32_t pad;
	};

	FellowRtReflectionsShaderRD shader;
	RID shader_version;
	RID pipelines[MODE_MAX];
	RID ubo;
	bool position_fetch = false;

public:
	void process(const Params &p_params);

	FellowRTReflections();
	~FellowRTReflections();
};

} // namespace RendererRD
