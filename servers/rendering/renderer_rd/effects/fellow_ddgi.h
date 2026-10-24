/**************************************************************************/
/*  fellow_ddgi.h                                                         */
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
#include "servers/rendering/renderer_rd/shaders/effects/fellow_ddgi.glsl.gen.h"
#include "servers/rendering/rendering_device.h"

namespace RendererRD {

// FellowEngine dynamic diffuse global illumination (docs/decisions/0083-ddgi.md): a scrolling volume of irradiance
// probes around the camera, updated every frame by rays against the frame's ray tracing scene (decision 0080), read by
// the scene shader in place of the environment's ambient light (fellow_ddgi_inc.glsl).
class FellowDDGI {
public:
	// std140, as FellowDDGIVolume in fellow_ddgi_inc.glsl (64 bytes).
	struct Volume {
		int32_t counts[4] = { 0, 0, 0, 0 }; // probes, rays per probe
		int32_t base_cell[4] = { 0, 0, 0, 0 }; // w 1: holds data
		float spacing[4] = { 1.0, 0.0, 0.0, 1.0 }; // spacing, normal bias, view bias, energy
		float atlas_inv_size[4] = { 1.0, 1.0, 1.0, 1.0 };
	};

	struct Params {
		RID tlas;
		RID instances; // FellowRTInstance per instance of the scene (custom index)
		RID ray_data; // RGBA16F: rays x probes
		RID irradiance; // RGBA16F atlas
		RID distance; // RG16F atlas
		RID probe_cells; // ivec4 per probe
		RID radiance; // the sky's radiance octmap (array or not); RID(): no sky
		bool radiance_array = false;
		float radiance_border[2] = { 0.0, 1.0 };
		float radiance_level = 0.0;
		Volume volume;
		Basis ray_rotation;
		Vector3 sun_direction; // towards the light, world
		Color sun_color; // scene units
		bool has_sun = false;
		bool sun_shadow = false;
		Color ambient_color; // rgb; a: sky contribution
		bool use_ambient = false;
		bool use_sky = false;
		float sky_factor = 1.0;
		Basis sky_xform; // world directions to the sky's
		float ray_length = 100.0;
		float energy_keep = 0.95;
		float hysteresis = 0.97;
		float distance_hysteresis = 0.97;
		uint32_t frame = 0;
	};

	static constexpr int IRRADIANCE_TILE = 8; // as fellow_ddgi_inc.glsl
	static constexpr int DISTANCE_TILE = 16;

private:
	enum Mode {
		MODE_TRACE,
		MODE_TRACE_FETCH,
		MODE_TRACE_ARRAY,
		MODE_TRACE_FETCH_ARRAY,
		MODE_DISTANCE,
		MODE_IRRADIANCE,
		MODE_MAX,
	};

	struct UBO { // std140, as Params in fellow_ddgi.glsl (304 bytes)
		Volume volume;
		float ray_rotation[16];
		float sun_direction[4];
		float sun_color[4];
		float ambient_color[4];
		float sky_params[4];
		float sky_xform[16];
		float radiance_border[4];
		float trace[4];
		int32_t frame[4];
	};

	FellowDdgiShaderRD shader;
	RID shader_version;
	RID pipelines[MODE_MAX];
	RID ubo;
	bool position_fetch = false;

public:
	void process(const Params &p_params);
	// Writes the volume the scene shader reads (binding 46 of the render pass set).
	static void store_volume(const Volume &p_volume, uint8_t *r_dst);

	FellowDDGI();
	~FellowDDGI();
};

} // namespace RendererRD
