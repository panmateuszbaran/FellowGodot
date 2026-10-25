/**************************************************************************/
/*  fellow_settings_look.h                                                */
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

#include "servers/rendering/fellow_settings.h"

// FellowEngine milestone 18 ("the look"): getters of the visual features' settings, in their own header so that the
// many files including fellow_settings.h do not depend on them. Registered in fellow_settings.cpp.
namespace Fellow {

// post/contact_shadows/* (decision 0107): screen-space contact shadows for the first directional light (a visual
// feature, off by default).
inline bool contact_shadows_enabled() {
	return enabled() && GLOBAL_GET_CACHED(bool, "rendering/fellow/post/contact_shadows/enabled");
}
inline float contact_shadows_length() {
	return GLOBAL_GET_CACHED(float, "rendering/fellow/post/contact_shadows/length");
}
inline int contact_shadows_steps() {
	return GLOBAL_GET_CACHED(int, "rendering/fellow/post/contact_shadows/steps");
}
inline float contact_shadows_thickness() {
	return GLOBAL_GET_CACHED(float, "rendering/fellow/post/contact_shadows/thickness");
}

// post/atmosphere/* (decision 0108): Hillaire's sky atmosphere (a sky shader reads its tables through the global shader
// parameters fellow_atmosphere_sky_view and fellow_atmosphere_transmittance) and aerial perspective on the opaque
// surfaces (a visual feature, off by default).
inline bool atmosphere_enabled() {
	return enabled() && GLOBAL_GET_CACHED(bool, "rendering/fellow/post/atmosphere/enabled");
}
inline bool atmosphere_sun_transmittance() {
	return GLOBAL_GET_CACHED(bool, "rendering/fellow/post/atmosphere/sun_transmittance");
}
inline bool atmosphere_aerial_perspective() {
	return GLOBAL_GET_CACHED(bool, "rendering/fellow/post/atmosphere/aerial_perspective");
}
inline float atmosphere_aerial_distance_scale() {
	return GLOBAL_GET_CACHED(float, "rendering/fellow/post/atmosphere/aerial_distance_scale");
}
inline float atmosphere_mie_scale() {
	return GLOBAL_GET_CACHED(float, "rendering/fellow/post/atmosphere/mie_scale");
}
inline float atmosphere_rayleigh_scale() {
	return GLOBAL_GET_CACHED(float, "rendering/fellow/post/atmosphere/rayleigh_scale");
}
// The Rayleigh coefficients integrated over the sRGB channels (reproduces a spectral sky) instead of Hillaire's and
// Unreal's three wavelengths (680, 550, 440 nm: the sky about 20 % too low in red).
inline bool atmosphere_srgb_rayleigh() {
	return GLOBAL_GET_CACHED(bool, "rendering/fellow/post/atmosphere/srgb_rayleigh");
}
inline float atmosphere_ground_albedo() {
	return GLOBAL_GET_CACHED(float, "rendering/fellow/post/atmosphere/ground_albedo");
}
// post/clouds/* (decision 0109): volumetric clouds over the sky (a visual feature, off by default).
inline bool clouds_enabled() {
	return enabled() && GLOBAL_GET_CACHED(bool, "rendering/fellow/post/clouds/enabled");
}
inline float clouds_coverage() {
	return GLOBAL_GET_CACHED(float, "rendering/fellow/post/clouds/coverage");
}
inline float clouds_density() {
	return GLOBAL_GET_CACHED(float, "rendering/fellow/post/clouds/density");
}
inline float clouds_altitude() {
	return GLOBAL_GET_CACHED(float, "rendering/fellow/post/clouds/altitude");
}
inline float clouds_thickness() {
	return GLOBAL_GET_CACHED(float, "rendering/fellow/post/clouds/thickness");
}
inline float clouds_wind() {
	return GLOBAL_GET_CACHED(float, "rendering/fellow/post/clouds/wind");
}
// post/sharpen/strength (decision 0110): contrast-adaptive sharpening of the TAA output in the tone mapping pass (0: off).
inline float sharpen_strength() {
	return GLOBAL_GET_CACHED(float, "rendering/fellow/post/sharpen/strength");
}
// post/bloom/lens_dirt (decision 0111): the environment's glow map as Unreal's lens dirt, added where the glow is with
// this intensity (0: Godot's mix by the glow map strength).
inline float lens_dirt_intensity() {
	return GLOBAL_GET_CACHED(float, "rendering/fellow/post/bloom/lens_dirt");
}

} // namespace Fellow
