/**************************************************************************/
/*  fellow_atmosphere.h                                                   */
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
#include "servers/rendering/renderer_rd/shaders/effects/fellow_atmosphere.glsl.gen.h"
#include "servers/rendering/rendering_device.h"

namespace RendererRD {

// FellowEngine sky atmosphere (docs/decisions/0108-sky-atmosphere.md): Hillaire's lookup tables (EGSR 2020, Unreal's
// SkyAtmosphere) for an Earth-like atmosphere lit by the first directional light: the transmittance and the multiple
// scattering (when the atmosphere's parameters change), the sky seen from the camera and the aerial perspective
// froxels (every frame). A sky shader reads the sky-view and transmittance tables through global shader parameters;
// the aerial perspective is applied to the opaque surfaces before the transparent pass.
class FellowAtmosphere {
public:
	static constexpr int TRANSMITTANCE_W = 256;
	static constexpr int TRANSMITTANCE_H = 64;
	static constexpr int MULTISCATTER_SIZE = 32;
	static constexpr int SKY_VIEW_W = 192;
	static constexpr int SKY_VIEW_H = 108;
	static constexpr int AERIAL_SIZE = 32;
	static constexpr int AERIAL_DEPTH = 32;

	struct Settings {
		float bottom_radius = 6360.0; // km
		float top_radius = 6460.0;
		Vector3 rayleigh_scattering = Vector3(5.802e-3, 13.558e-3, 33.1e-3); // per km
		float rayleigh_height = 8.0; // km
		float mie_scattering = 3.996e-3;
		float mie_absorption = 0.444e-3; // Hillaire 2020: Mie extinction 4.440e-3 per km, of which 3.996e-3 scattering
		float mie_height = 1.2;
		float mie_g = 0.8;
		Vector3 ozone_absorption = Vector3(0.650e-3, 1.881e-3, 0.085e-3);
		float ozone_center = 25.0;
		float ozone_width = 30.0;
		Vector3 ground_albedo = Vector3(0.3, 0.3, 0.3);
		float multiscatter = 1.0;
		float aerial_max_distance = 32.0; // km
		bool operator==(const Settings &p_o) const { return memcmp(this, &p_o, sizeof(Settings)) == 0; }
	};

	struct Params {
		Settings settings;
		Vector3 sun_direction; // world, towards the sun
		Color sun_illuminance; // colour * energy * intensity (the aerial perspective's in-scattering)
		Vector3 camera_position; // world, metres
		float ground_height = 0.0; // metres
		Projection view_projection; // corrected projection * view (world to clip), for the froxels and the apply pass
		float aerial_distance_scale = 1.0;
		bool aerial = true;
	};

private:
	enum Mode {
		MODE_TRANSMITTANCE,
		MODE_MULTISCATTER,
		MODE_SKY_VIEW,
		MODE_AERIAL,
		MODE_APPLY,
		MODE_MAX,
	};

	struct UBO { // std140
		float inv_view_projection[16];
		float sun_direction[4];
		float camera_position[4];
		float rayleigh_scattering[4];
		float mie_scattering[4];
		float mie_absorption[4];
		float ozone_absorption[4];
		float ground_albedo[4];
		float radii[4];
		int32_t sizes[4];
		float sun_illuminance[4];
		int32_t screen[4];
	}; // 224 bytes

	FellowAtmosphereShaderRD shader;
	RID shader_version;
	RID pipelines[MODE_MAX];
	RID ubo;
	RID transmittance;
	RID multiscatter;
	RID sky_view;
	RID aerial;
	RID dummy; // a 1 x 1 texture for the tables a pass writes (never sampled there)
	Settings static_settings;
	bool static_valid = false;
	UBO ubo_data;

	void _fill_ubo(const Params &p_params, const Size2i &p_screen);

public:
	// The tables for this frame (the sky shader reads them during the sky's update and draw).
	void update(const Params &p_params);
	// Aerial perspective on p_color (RGBA16F storage) where p_depth holds a surface.
	void apply(const Params &p_params, RID p_color, RID p_depth, const Size2i &p_size);

	RID get_sky_view() const { return sky_view; }
	RID get_transmittance() const { return transmittance; }

	FellowAtmosphere();
	~FellowAtmosphere();
};

} // namespace RendererRD
