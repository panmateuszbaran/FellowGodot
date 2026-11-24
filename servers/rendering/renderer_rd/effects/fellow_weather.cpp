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

#include "core/os/os.h"

#include "servers/rendering/renderer_rd/storage_rd/particles_storage.h"

namespace RendererRD {

FellowWeather *FellowWeather::singleton = nullptr;
SafeNumeric<float> FellowWeather::rain_here(-1.0f);
SafeNumeric<uint64_t> FellowWeather::rain_here_msec(0);

void FellowWeather::set_rain_here(float p_rain) {
	rain_here.set(p_rain);
	rain_here_msec.set(OS::get_singleton()->get_ticks_msec());
}

float FellowWeather::get_rain_here() {
	const uint64_t at = rain_here_msec.get();
	if (at == 0 || OS::get_singleton()->get_ticks_msec() - at > 500) {
		return -1.0f;
	}
	return rain_here.get();
}

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

RID FellowWeather::get_tracks_texture() const {
	if (!state.enabled || state.tracks_texture.is_null() || !RD::get_singleton()->texture_is_valid(state.tracks_texture)) {
		return RID();
	}
	return state.tracks_texture;
}

static RID fellow_heightfield_texture(RID p_collider) {
	if (p_collider.is_null()) {
		return RID();
	}
	ParticlesStorage *ps = ParticlesStorage::get_singleton();
	if (!ps || !ps->particles_collision_is_heightfield(p_collider)) {
		return RID();
	}
	RID tex = ps->fellow_particles_collision_get_heightfield_texture(p_collider);
	return RD::get_singleton()->texture_is_valid(tex) ? tex : RID();
}

RID FellowWeather::get_occlusion_texture() const {
	return state.enabled ? fellow_heightfield_texture(state.occlusion_collider) : RID();
}

RID FellowWeather::get_weathering_layers_texture() const {
	if (!is_weathering() || weathering_array.is_null() || !RD::get_singleton()->texture_is_valid(weathering_array)) {
		return RID();
	}
	return weathering_array;
}

void FellowWeather::_weathering_untarget() {
	ParticlesStorage *ps = ParticlesStorage::get_singleton();
	for (int i = 0; i < WEATHERING_MAX_LAYERS; i++) {
		if (weathering_targeted[i].is_valid() && ps) {
			ps->fellow_particles_collision_set_heightfield_target(weathering_targeted[i], RID(), 0); // a freed one is ignored
		}
		weathering_targeted[i] = RID();
	}
}

void FellowWeather::set_weathering_state(const WeatheringState &p_weathering) {
	weathering = p_weathering;
	ubo_dirty = true;
	const uint32_t count = MIN(weathering.layer_count, uint32_t(WEATHERING_MAX_LAYERS));
	if (!weathering.enabled || count == 0) {
		// Off: the layers' memory given back (64 MB at the defaults).
		_weathering_untarget();
		if (weathering_array.is_valid()) {
			RD::get_singleton()->free_rid(weathering_array);
			weathering_array = RID();
		}
		weathering_array_size = 0;
		weathering_array_layers = 0;
		return;
	}
	const uint32_t size = CLAMP(weathering.resolution, 16u, 16384u);
	if (weathering_array.is_null() || weathering_array_size != size || weathering_array_layers != count) {
		_weathering_untarget();
		if (weathering_array.is_valid()) {
			RD::get_singleton()->free_rid(weathering_array);
		}
		RD::TextureFormat tf;
		tf.format = RD::DATA_FORMAT_D32_SFLOAT;
		tf.width = size;
		tf.height = size;
		tf.array_layers = count;
		tf.texture_type = RD::TEXTURE_TYPE_2D_ARRAY;
		tf.usage_bits = RD::TEXTURE_USAGE_SAMPLING_BIT | RD::TEXTURE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
		weathering_array = RD::get_singleton()->texture_create(tf, RD::TextureView());
		RD::get_singleton()->set_resource_name(weathering_array, "FellowEngine weathering layers");
		weathering_array_size = size;
		weathering_array_layers = count;
	}
	// Each heightfield drawn into its layer (drawn again when it changes: this frame, before the scene).
	ParticlesStorage *ps = ParticlesStorage::get_singleton();
	for (uint32_t i = 0; i < uint32_t(WEATHERING_MAX_LAYERS); i++) {
		const RID want = i < count ? weathering.layers[i] : RID();
		if (weathering_targeted[i] != want) {
			if (weathering_targeted[i].is_valid() && ps) {
				ps->fellow_particles_collision_set_heightfield_target(weathering_targeted[i], RID(), 0);
			}
			if (want.is_valid() && ps) {
				ps->fellow_particles_collision_set_heightfield_target(want, weathering_array, int(i));
			}
			weathering_targeted[i] = want;
		}
	}
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
	if (ubo_dirty || state.enabled || wind.enabled || weathering.enabled) {
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
		u.frost[0] = state.enabled ? CLAMP(state.freeze, 0.0f, 1.0f) : 0.0f;
		u.frost[1] = state.enabled ? CLAMP(state.frost, 0.0f, 1.0f) : 0.0f;
		u.frost[2] = state.enabled ? CLAMP(state.drying_patches, 0.0f, 1.0f) : 0.0f;
		u.frost[3] = state.enabled ? CLAMP(state.slush, 0.0f, 1.0f) : 0.0f;
		u.slant[0] = state.enabled ? state.rain_drift.x : 0.0f;
		u.slant[1] = state.enabled ? state.rain_drift.y : 0.0f;
		u.slant[2] = state.enabled ? CLAMP(state.rain_slant, 0.0f, 1.0f) : 0.0f;
		u.snow[2] = MAX(0.001f, state.snow_depth);
		if (get_tracks_texture().is_valid() && state.tracks_window > 0.0f) {
			u.snow[3] = 1.0f;
			u.tracks[0] = state.tracks_origin.x;
			u.tracks[1] = state.tracks_origin.y;
			u.tracks[2] = 1.0f / state.tracks_window;
			u.tracks[3] = state.tracks_window * 0.5f * 0.97f;
		}
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
		if (cloud_shadow.active && cloud_shadow.texture.is_valid()) {
			u.cloud_shadow[0] = cloud_shadow.origin.x;
			u.cloud_shadow[1] = cloud_shadow.origin.y;
			u.cloud_shadow[2] = 1.0f / MAX(cloud_shadow.extent, 1.0f);
			u.cloud_shadow[3] = CLAMP(cloud_shadow.strength, 0.0f, 1.0f);
			const Vector3 sd = cloud_shadow.sun_direction;
			const float sy = MAX(sd.y, 0.01f);
			u.cloud_shadow2[0] = float(cloud_shadow.light_index);
			u.cloud_shadow2[1] = sd.x / sy;
			u.cloud_shadow2[2] = sd.z / sy;
			u.cloud_shadow2[3] = cloud_shadow.ground_height;
			u.cloud_shadow3[0] = CLAMP(cloud_shadow.beyond, 0.0f, 1.0f);
		}
		if (cloud_shadow.active) {
			u.cloud_shadow3[1] = CLAMP(cloud_shadow.sky_cover, 0.0f, 1.0f);
		}
		// Weathering (decision 0332); its layers are bound at 55.
		if (is_weathering()) {
			const WeatheringState &we = weathering;
			u.weathering[0] = CLAMP(we.strength, 0.0f, 1.0f);
			u.weathering[1] = CLAMP(we.streaks, 0.0f, 1.0f);
			u.weathering[2] = 1.0f / MAX(we.streak_length, 0.01f);
			u.weathering[3] = CLAMP(we.damp, 0.0f, 1.0f);
			u.weathering2[0] = MAX(we.damp_height, 0.0f);
			u.weathering2[1] = CLAMP(we.patina, 0.0f, 1.0f);
			u.weathering2[2] = float(we.cull_mask & 0xFFFFFu); // exact in a float
			u.weathering2[3] = we.seed;
			u.weathering3[0] = we.stain_color.r;
			u.weathering3[1] = we.stain_color.g;
			u.weathering3[2] = we.stain_color.b;
			u.weathering4[0] = we.damp_color.r;
			u.weathering4[1] = we.damp_color.g;
			u.weathering4[2] = we.damp_color.b;
			u.weathering4[3] = MAX(we.layer_overlap, 0.0f);
			if (get_weathering_layers_texture().is_valid() && we.size_x > 0.0f && we.size_z > 0.0f && we.layer_height > 0.0f) {
				u.weathering_occ[0] = 1.0f / we.size_x;
				u.weathering_occ[1] = 1.0f / we.size_z;
				for (int i = 0; i < WEATHERING_MAX_LAYERS; i++) {
					u.weathering_centers[i / 2][(i % 2) * 2 + 0] = we.layer_center[i][0];
					u.weathering_centers[i / 2][(i % 2) * 2 + 1] = we.layer_center[i][1];
				}
				u.weathering_occ2[0] = we.base;
				u.weathering_occ2[1] = we.layer_height;
				u.weathering_occ2[2] = float(weathering_array_layers);
				u.weathering_occ2[3] = float(weathering_array_size) / we.size_x;
			}
		}
		RD::get_singleton()->buffer_update(ubo, 0, sizeof(UBO), &u);
		ubo_dirty = false;
	}
	return ubo;
}

void FellowWeather::set_cloud_shadow(const CloudShadowState &p_shadow) {
	const CloudShadowState &c = cloud_shadow;
	if (c.active != p_shadow.active || c.texture != p_shadow.texture || c.origin != p_shadow.origin || c.extent != p_shadow.extent ||
			c.strength != p_shadow.strength || c.light_index != p_shadow.light_index || c.sun_direction != p_shadow.sun_direction ||
			c.ground_height != p_shadow.ground_height || c.beyond != p_shadow.beyond || c.sky_cover != p_shadow.sky_cover) {
		ubo_dirty = true;
	}
	cloud_shadow = p_shadow;
}

FellowWeather::FellowWeather() {
}

FellowWeather::~FellowWeather() {
	if (ubo.is_valid() && RD::get_singleton()) {
		RD::get_singleton()->free_rid(ubo);
	}
	if (weathering_array.is_valid() && RD::get_singleton()) {
		_weathering_untarget();
		RD::get_singleton()->free_rid(weathering_array);
	}
}

} // namespace RendererRD
