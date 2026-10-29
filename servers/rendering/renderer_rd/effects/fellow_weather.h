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
#include "servers/rendering/rendering_device.h"

namespace RendererRD {

// FellowEngine weather: wet surfaces (docs/decisions/0124-rain.md). The scene side is modules/fellow_fluid's
// FellowRain3D, which hands its state over on the render thread once per frame. While it is enabled the Forward+
// renderer specializes its depth pre-pass and opaque colour passes with `fellow_wetness`: every opaque material is
// wetted after its own code (and its decals) — porous surfaces darker, all of them glossier, puddles on flat ground with
// the rain's ripples — where the rain reaches it (a heightfield of the scene seen from above: a particle collision
// heightfield, which the rain's drops collide with too).
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
	};

private:
	State state;
	WindState wind;
	WindState wind_prev;
	bool wind_has_prev = false;
	RID ubo;
	bool ubo_dirty = true;

public:
	static FellowWeather *get_singleton();
	static FellowWeather *get_singleton_if_exists() { return singleton; }
	static void finalize();

	void set_state(const State &p_state);
	// Once per frame while a FellowWind3D is active; the previous call's state becomes the previous frame's.
	void set_wind_state(const WindState &p_wind);
	bool is_active() const { return state.enabled || wind.enabled; }
	const State &get_state() const { return state; }
	const WindState &get_wind_state() const { return wind; }
	RID get_uniform_buffer();
	RID get_occlusion_texture() const;
	RID get_ripple_texture() const;
	RID get_gust_texture() const;

	FellowWeather();
	~FellowWeather();
};

} // namespace RendererRD
