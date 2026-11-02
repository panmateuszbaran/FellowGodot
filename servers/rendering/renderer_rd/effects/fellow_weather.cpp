/**************************************************************************/
/*  fellow_weather.cpp                                                    */
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

#include "fellow_weather.h"

#include "servers/rendering/renderer_rd/storage_rd/particles_storage.h"

namespace RendererRD {

FellowWeather *FellowWeather::singleton = nullptr;

FellowWeather *FellowWeather::get_singleton() {
	if (!singleton && RD::get_singleton()) {
		singleton = memnew(FellowWeather);
	}
	return singleton;
}

void FellowWeather::finalize() {
	if (singleton) {
		memdelete(singleton);
		singleton = nullptr;
	}
}

void FellowWeather::set_state(const State &p_state) {
	state = p_state;
	ubo_dirty = true;
}

void FellowWeather::set_wind_state(const WindState &p_wind) {
	if (!p_wind.enabled) {
		wind = WindState();
		wind_prev = WindState();
		wind_has_prev = false;
		ubo_dirty = true;
		return;
	}
	wind_prev = wind_has_prev ? wind : p_wind;
	wind = p_wind;
	wind_has_prev = true;
	ubo_dirty = true;
}

RID FellowWeather::get_gust_texture() const {
	if (!wind.enabled || wind.gust_texture.is_null() || !RD::get_singleton()->texture_is_valid(wind.gust_texture)) {
		return RID();
	}
	return wind.gust_texture;
}

RID FellowWeather::get_occlusion_texture() const {
	if (!state.enabled || state.occlusion_collider.is_null()) {
		return RID();
	}
	ParticlesStorage *ps = ParticlesStorage::get_singleton();
	if (!ps || !ps->particles_collision_is_heightfield(state.occlusion_collider)) {
		return RID();
	}
	RID tex = ps->fellow_particles_collision_get_heightfield_texture(state.occlusion_collider);
	return RD::get_singleton()->texture_is_valid(tex) ? tex : RID();
}

RID FellowWeather::get_ripple_texture() const {
	if (!state.enabled || state.ripple_texture.is_null() || !RD::get_singleton()->texture_is_valid(state.ripple_texture)) {
		return RID();
	}
	return state.ripple_texture;
}

RID FellowWeather::get_uniform_buffer() {
	if (ubo.is_null()) {
		ubo = RD::get_singleton()->uniform_buffer_create(sizeof(UBO));
		ubo_dirty = true;
	}
	// Rewritten every frame while enabled: the heightfield's texture appears once the scene cull has drawn it.
	if (ubo_dirty || state.enabled || wind.enabled) {
		UBO u;
		memset(&u, 0, sizeof(UBO));
		u.params[0] = state.enabled ? CLAMP(state.wetness, 0.0f, 1.0f) : 0.0f;
		u.params[1] = CLAMP(state.puddles, 0.0f, 1.0f);
		u.params[2] = get_ripple_texture().is_valid() ? CLAMP(state.rain, 0.0f, 1.0f) : 0.0f;
		u.params[3] = state.time;
		bool occ = get_occlusion_texture().is_valid();
		Vector3 e = state.occlusion_extents;
		if (occ && e.x > 0.0f && e.z > 0.0f && e.y > 0.0f) {
			u.occlusion[0] = state.occlusion_transform.origin.x;
			u.occlusion[1] = state.occlusion_transform.origin.z;
			u.occlusion[2] = 1.0f / (2.0f * e.x);
			u.occlusion[3] = 1.0f / (2.0f * e.z);
			u.occlusion2[0] = state.occlusion_transform.origin.y;
			u.occlusion2[1] = e.y;
			u.occlusion2[2] = 1.0f;
			u.occlusion2[3] = state.occlusion_bias;
		}
		u.ripples[0] = 1.0f / MAX(state.ripple_tile, 0.01f);
		u.ripples[1] = state.ripple_strength;
		u.snow[0] = state.enabled ? CLAMP(state.snow, 0.0f, 1.0f) : 0.0f;
		u.snow[1] = CLAMP(state.snow_sparkle, 0.0f, 1.0f);
		const bool gusts = get_gust_texture().is_valid();
		const WindState *ws[2] = { &wind, &wind_prev };
		float *w0[2] = { u.wind, u.wind_prev };
		float *w1[2] = { u.wind2, u.wind2_prev };
		for (int i = 0; i < 2; i++) {
			const WindState &w = *ws[i];
			if (!w.enabled) {
				continue;
			}
			Vector2 d = w.direction.length_squared() > 0.0f ? w.direction.normalized() : Vector2(1.0f, 0.0f);
			w0[i][0] = d.x;
			w0[i][1] = d.y;
			w0[i][2] = MAX(0.0f, w.speed);
			w0[i][3] = gusts ? CLAMP(w.gustiness, 0.0f, 1.0f) : 0.0f;
			w1[i][0] = w.offset;
			w1[i][1] = 1.0f / MAX(w.gust_size, 0.1f);
			w1[i][2] = MAX(0.0f, w.flutter);
			w1[i][3] = 1.0f;
			u.wind_time[i] = w.time;
		}
		RD::get_singleton()->buffer_update(ubo, 0, sizeof(UBO), &u);
		ubo_dirty = false;
	}
	return ubo;
}

FellowWeather::FellowWeather() {
}

FellowWeather::~FellowWeather() {
	if (ubo.is_valid() && RD::get_singleton()) {
		RD::get_singleton()->free_rid(ubo);
	}
}

} // namespace RendererRD
