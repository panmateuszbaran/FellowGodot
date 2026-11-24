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
#include "servers/rendering/renderer_rd/effects/fellow_ddgi.h"
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
		float surface_bias = 1.0; // cells: the distance field's values are this much more than the hits' (SDFGI's own rays take 1)
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
		Projection world_to_clip;
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
		int rays = 16;
		uint32_t frame = 0;
		bool history = false;
		float max_samples = 16.0;
		bool persistent = true; // a probe keeps its point and history while that point stays in view in its tile
		// The pixels' own history (sdfgi/screen_gather_pixel_history; milestone 191): the resolve's result averaged over
		// up to `pixel_history` frames where the pixel's point stayed in view (1: none). This frame's and the previous
		// frame's result (the ambient buffer's size: rgb the light, a its frames) and its pixels' distance from the eye.
		int pixel_history = 1;
		bool pixel_history_valid = false; // the previous frame's result holds data
		RID resolved[2];
		RID resolved_distance[2];
		Vector3 prev_eye;
		// The rays against the ray tracing scene instead (raytracing/gi_screen_gather): the hits lit as DDGI's rays' are
		// (the sun, its probes for the further bounces, emission); `ambient` is then DDGI's screen pass texture.
		bool rt = false;
		RID tlas;
		RID instances;
		RID ddgi_irradiance;
		RID ddgi_distance;
		RID ddgi_outer_irradiance;
		RID ddgi_outer_distance;
		FellowDDGI::Volume ddgi_volume;
		FellowDDGI::Volume ddgi_outer; // base_cell[3] 0: none
		Vector3 sun_direction; // towards the light
		Color sun_color; // scene units
		bool has_sun = false;
		bool sun_shadow = false;
		float ray_length = 100.0;
		float energy_keep = 0.95;
		bool feedback = true; // the hits' further bounces from the previous frame's screen probes where it saw them
	};

private:
	enum Mode {
		MODE_TRACE,
		MODE_TRACE_ARRAY,
		MODE_RESOLVE,
		MODE_FILTER,
		MODE_TRACE_RT,
		MODE_TRACE_RT_ARRAY,
		MODE_TRACE_RT_FETCH,
		MODE_TRACE_RT_FETCH_ARRAY,
		MODE_RESOLVE_PIXEL_HISTORY,
		MODE_MAX,
	};

	struct RTUBO { // std140, as RTParams in fellow_sdf_gather.glsl (208 bytes)
		FellowDDGI::Volume volume;
		FellowDDGI::Volume outer;
		float sun_direction[4];
		float sun_color[4];
		float trace[4];
	};

	struct UBO { // std140, as Params in fellow_sdf_gather.glsl (480 bytes)
		float inv_projection[16];
		float view_to_world[16];
		float prev_world_to_clip[16];
		float world_to_clip[16];
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
		float temporal[4];
	};

	FellowSdfGatherShaderRD shader;
	RID shader_version;
	RID pipelines[MODE_MAX];
	RID ubo;
	RID rt_ubo;
	bool rt_supported = false;
	bool position_fetch = false;

public:
	bool is_rt_supported() const { return rt_supported; }
	void process(const Params &p_params);

	FellowSDFGather();
	~FellowSDFGather();
};

} // namespace RendererRD
