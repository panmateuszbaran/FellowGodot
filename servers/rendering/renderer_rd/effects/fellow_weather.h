/**************************************************************************/
/*  fellow_weather.h                                                      */
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

#include "core/math/transform_3d.h"
#include "core/templates/safe_refcount.h"
#include "servers/rendering/rendering_device.h"

namespace RendererRD {

// FellowEngine weather: wet surfaces (docs/decisions/0124-rain.md). The scene side is modules/fellow_fluid's
// FellowRain3D, which hands its state over on the render thread once per frame. While it is enabled the Forward+
// renderer specializes its depth pre-pass and opaque colour passes with `fellow_wetness`: every opaque material is
// wetted after its own code (and its decals) — porous surfaces darker, all of them glossier, puddles on flat ground with
// the rain's ripples — where the rain reaches it (a heightfield of the scene seen from above: a particle collision
// heightfield, which the rain's drops collide with too).
//
// Snow (docs/decisions/0128-snow.md): the same node in its snow mode; the same pass covers what faces up, where the
// snow falls, with snow.
//
// Wind (docs/decisions/0125-wind.md): the scene side is FellowWind3D. Materials with the render mode `fellow_wind` (a
// BaseMaterial3D's Wind group) are displaced in the vertex shader by the wind and its gusts, a noise field travelling
// with the wind; the previous frame's wind is kept for the motion vectors.
class FellowWeather {
	static FellowWeather *singleton;

public:
	struct State {
		bool enabled = false;
		float wetness = 0.0f; // 0..1
		float puddles = 0.0f; // 0..1: share of the flat ground under standing water
		float rain = 0.0f; // 0..1: drops' rate (the ripples)
		float time = 0.0f;
		RID occlusion_collider; // a particles collision heightfield (RS base RID), or none: everything is exposed
		Transform3D occlusion_transform;
		Vector3 occlusion_extents;
		float occlusion_bias = 0.25f; // m: a surface this far below the heightfield's top still counts as the top
		RID ripple_texture; // RD texture: rain ripples' slopes (rg), mipmapped, tiling
		float ripple_tile = 1.5f; // m
		float ripple_strength = 1.0f;
		float snow = 0.0f; // 0..1: the snow cover (decision 0128)
		float snow_sparkle = 0.0f; // 0..1: the fresh snow's glints
		float freeze = 0.0f; // 0..1: how much of the wet surfaces' water is ice (decision 0136)
		float frost = 0.0f; // 0..1: hoarfrost on what is exposed to the sky
		float drying_patches = 0.0f; // 0..1: the ground wets and dries in patches, the puddles shrinking (decision 0138)
		float snow_depth = 0.08f; // m (decision 0130: how deep tracks can go before the ground shows)
		RID tracks_texture; // RD texture (r16f): the tracks' depth in m (negative: a rim), world-anchored, wrapping
		Vector2 tracks_origin; // m: the window's centre
		float tracks_window = 0.0f; // m: its side (0: no tracks)
		float rainbow = 0.0f; // the rain's rainbow (decision 0134): its strength times the rain seen lately
		float shafts = 0.0f; // rain shafts (decision 0135): the rain's (or snow's) rate, times its shafts' strength
		bool shafts_snow = false;
		float showers = 0.0f; // decision 0137: > 0 while a rain node raining asks where the clouds let it fall
	};

	struct WindState {
		bool enabled = false;
		Vector2 direction = Vector2(1.0f, 0.0f); // xz, unit: where the wind blows to
		float speed = 0.0f; // m/s
		float gustiness = 0.0f; // 0..1: the gusts' share of the speed
		float offset = 0.0f; // m: how far the gust field has travelled along the wind
		float gust_size = 30.0f; // m
		float flutter = 1.0f; // scale of the leaves' flutter
		float time = 0.0f; // s: the sway's clock (FellowWind3D's time, repeating)
		RID gust_texture; // RD texture: the gust field's tiling noise (r)
	};

	// A lightning flash (decision 0131): the volumetric clouds glow around it.
	struct LightningState {
		bool active = false;
		Vector3 position; // the flash in the cloud, world (m)
		Color color; // its light (colour * energy, the stroke's envelope included)
		float radius = 1000.0f; // m: how far through the cloud it lights
	};

	// The sky under a FellowWeather3D (decision 0132): the volumetric clouds' coverage and density in place of the project
	// settings', and the sun dimmed on the scene's surfaces by the clouds over it (not in the sky or on the clouds).
	struct SkyState {
		bool active = false; // the clouds' coverage and density
		float coverage = 0.45f;
		float density = 1.0f;
		RID sun_light; // RS light (base) RID: the sun the clouds dim
		float sun_factor = 1.0f;
	};

	// The volumetric clouds' shadow on the scene (decision 0133): the sun's light through the cloud layer over the ground
	// around the camera, set by the renderer each frame before the colour passes.
	struct CloudShadowState {
		bool active = false;
		RID texture; // RD texture (r16f): the sun's transmittance through the clouds
		Vector2 origin; // m: the map's centre
		float extent = 16000.0f; // m: its side
		float strength = 1.0f;
		uint32_t light_index = 0; // the sun's index among the frame's directional lights
		Vector3 sun_direction = Vector3(0, 1, 0); // world, towards the sun
		float ground_height = 0.0f; // m
		float beyond = 1.0f; // the sun's share beyond the map (the clear sky's share)
		float sky_cover = 0.0f; // 0..1: how grey the sky's light is under the clouds (its ambient light, its reflections)
	};

	// std140: the shader's FellowWeatherData.
	struct UBO {
		float params[4]; // wetness, puddles, rain, time
		float occlusion[4]; // origin x, origin z, 1 / (2 extents x), 1 / (2 extents z)
		float occlusion2[4]; // origin y, extents y, enabled, bias
		float ripples[4]; // 1 / tile, strength, -, -
		float wind[4]; // direction x, direction z, speed, gustiness
		float wind2[4]; // offset along the wind, 1 / gust size, flutter, enabled
		float wind_prev[4]; // the previous frame's wind (motion vectors)
		float wind2_prev[4];
		float wind_time[4]; // this frame's and the previous frame's time of the wind
		float snow[4]; // cover, sparkle, depth (m), tracks on (decision 0128, 0130)
		float tracks[4]; // the tracks' window: centre x, z (m), 1 / side (m), half its side less a margin (m)
		float cloud_shadow[4]; // the clouds' shadow map (decision 0133): centre x, z (m), 1 / side (1/m), strength
		float cloud_shadow2[4]; // the sun's directional light index, its direction's x / y and z / y, the ground (m)
		float cloud_shadow3[4]; // beyond the map: the sun's share there; the sky's light's cover (greyed)
		float frost[4]; // decision 0136: the water's ice (0..1), the hoarfrost (0..1); 0138: the drying's patches (0..1)
	};

private:
	State state;
	WindState wind;
	WindState wind_prev;
	bool wind_has_prev = false;
	LightningState lightning;
	SkyState sky;
	CloudShadowState cloud_shadow;
	RID ubo;
	bool ubo_dirty = true;
	// Showers (decision 0137): the clouds' rain at the camera, read back from the GPU (render thread), read by the rain
	// node (main thread).
	static SafeNumeric<float> rain_here;
	static SafeNumeric<uint64_t> rain_here_msec;

public:
	static FellowWeather *get_singleton();
	static FellowWeather *get_singleton_if_exists() { return singleton; }
	static void finalize();

	void set_state(const State &p_state);
	// Once per frame while a FellowWind3D is active; the previous call's state becomes the previous frame's.
	void set_wind_state(const WindState &p_wind);
	bool is_active() const { return state.enabled || wind.enabled || cloud_shadow.active; }
	const State &get_state() const { return state; }
	const WindState &get_wind_state() const { return wind; }
	void set_lightning_state(const LightningState &p_lightning) { lightning = p_lightning; }
	const LightningState &get_lightning_state() const { return lightning; }
	void set_sky_state(const SkyState &p_sky) { sky = p_sky; }
	const SkyState &get_sky_state() const { return sky; }
	void set_cloud_shadow(const CloudShadowState &p_shadow);
	const CloudShadowState &get_cloud_shadow() const { return cloud_shadow; }
	RID get_cloud_shadow_texture() const { return cloud_shadow.active ? cloud_shadow.texture : RID(); }
	// The clouds shade the sun this frame (their map, at some strength).
	bool is_cloud_shadowing() const { return cloud_shadow.active && cloud_shadow.texture.is_valid() && cloud_shadow.strength > 0.0f; }
	static void set_rain_here(float p_rain);
	// The share of the rain falling at the camera (0..1) as the clouds let it, or -1 when it is not known (no clouds
	// probed in the last half second).
	static float get_rain_here();
	RID get_uniform_buffer();
	RID get_occlusion_texture() const;
	RID get_ripple_texture() const;
	RID get_gust_texture() const;
	RID get_tracks_texture() const;

	FellowWeather();
	~FellowWeather();
};

} // namespace RendererRD
