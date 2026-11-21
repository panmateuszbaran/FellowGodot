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
#include "core/math/projection.h"
#include "core/math/transform_3d.h"
#include "servers/rendering/renderer_rd/shaders/effects/fellow_ddgi.glsl.gen.h"
#include "servers/rendering/rendering_device.h"

namespace RendererRD {

// FellowEngine dynamic diffuse global illumination (docs/decisions/0083-ddgi.md): a scrolling volume of irradiance
// probes around the camera, updated every frame by rays against the frame's ray tracing scene (decision 0080), read by
// the scene shader in place of the environment's ambient light (fellow_ddgi_inc.glsl).
class FellowDDGI {
public:
	// std140, as FellowDDGIVolume in fellow_ddgi_inc.glsl (80 bytes).
	struct Volume {
		int32_t counts[4] = { 0, 0, 0, 0 }; // probes, rays per probe
		int32_t base_cell[4] = { 0, 0, 0, 0 }; // w 1: holds data
		float spacing[4] = { 1.0, 0.0, 0.0, 1.0 }; // spacing, normal bias, view bias, energy
		float atlas_inv_size[4] = { 1.0, 1.0, 1.0, 1.0 };
		float shading[4] = { 0.0, 0.0, 0.0, 0.0 }; // sky leak, 1: probes relocated
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
		// Probes updated per frame: those whose index is the frame modulo update_interval (the hysteresis applies per
		// update: the caller raises it to the interval's power).
		uint32_t update_interval = 1;
		// The screen pass (optional): per pixel of the depth pre-pass, the volume's irradiance for the opaque colour
		// passes, which then read it instead of looking the probes up themselves.
		RID depth;
		RID normal_roughness;
		RID screen; // RGBA16F: irradiance, coverage
		Size2i screen_size;
		Projection inv_projection; // NDC (x, y, depth * 2 - 1) to view
		Transform3D view_to_world;
		// The coarser cascade around this volume (outer.base_cell[3] 0: none), read by the bounces and the screen pass
		// where this volume ends.
		Volume outer;
		RID outer_irradiance;
		RID outer_distance;
		uint32_t cascade = 0; // which of the effect's uniform buffers (one per cascade processed in a frame)
		// The path-traced reference (debug/raytracing_gi_reference) in place of the screen pass: bounces > 0.
		int reference_bounces = 0;
		RID reference_accumulation; // RGBA32F, the screen's size
		bool reference_restart = false;
	};

	static constexpr int IRRADIANCE_TILE = 8; // as fellow_ddgi_inc.glsl
	static constexpr int DISTANCE_TILE = 16;
	// The distance atlas: the tiles, then a strip of counts.z rows holding the probes' offsets (relocation).
	static int distance_atlas_height(int p_counts_z) { return p_counts_z * DISTANCE_TILE + p_counts_z; }

private:
	enum Mode {
		MODE_TRACE,
		MODE_TRACE_FETCH,
		MODE_TRACE_ARRAY,
		MODE_TRACE_FETCH_ARRAY,
		MODE_DISTANCE,
		MODE_IRRADIANCE,
		MODE_SCREEN,
		MODE_REFERENCE,
		MODE_REFERENCE_FETCH,
		MODE_REFERENCE_ARRAY,
		MODE_REFERENCE_FETCH_ARRAY,
		MODE_MAX,
	};

	struct UBO { // std140, as Params in fellow_ddgi.glsl (544 bytes)
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
		float inv_projection[16];
		float view_to_world[16];
		int32_t screen[4]; // xy size, z the reference's bounces
		Volume outer;
	};

	FellowDdgiShaderRD shader;
	RID shader_version;
	RID pipelines[MODE_MAX];
	RID ubo[2];
	bool position_fetch = false;

public:
	void process(const Params &p_params);
	// Writes the volume the scene shader reads (binding 46 of the render pass set).
	static void store_volume(const Volume &p_volume, uint8_t *r_dst);

	FellowDDGI();
	~FellowDDGI();
};

} // namespace RendererRD
