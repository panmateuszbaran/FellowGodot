/**************************************************************************/
/*  fellow_wind_3d.cpp                                                    */
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

#include "fellow_wind_3d.h"

#include "fellow_editor_still.h"

#include "core/config/engine.h"
#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "scene/3d/node_3d.h"
#include "servers/audio/audio_server.h"
#include "servers/rendering/fellow_settings.h"
#include "servers/rendering/renderer_rd/effects/fellow_weather.h"
#include "servers/rendering/renderer_rd/storage_rd/texture_storage.h"
#include "servers/rendering/rendering_server.h"

FellowWind3D *FellowWind3D::active = nullptr;

static const int GUST_SIZE = 256; // texels a side
static const int GUST_CELLS = 8; // gust cells a side (the shader's 0.125)

static float wind_hash(int p_x, int p_y, int p_seed) {
	uint32_t h = uint32_t(p_x) * 0x8da6b343u ^ uint32_t(p_y) * 0xd8163841u ^ uint32_t(p_seed) * 0xcb1ab31fu;
	h ^= h >> 13;
	h *= 0x5bd1e995u;
	h ^= h >> 15;
	return float(h & 0xffffffu) / float(0xffffff);
}

// A tiling value noise of p_cells cells a side, at a point in [0, 1)².
static float wind_value_noise(float p_u, float p_v, int p_cells, int p_seed) {
	float x = p_u * p_cells;
	float y = p_v * p_cells;
	int ix = int(Math::floor(x));
	int iy = int(Math::floor(y));
	float fx = x - ix;
	float fy = y - iy;
	fx = fx * fx * (3.0f - 2.0f * fx);
	fy = fy * fy * (3.0f - 2.0f * fy);
	auto at = [&](int i, int j) {
		return wind_hash(((i % p_cells) + p_cells) % p_cells, ((j % p_cells) + p_cells) % p_cells, p_seed);
	};
	float a = Math::lerp(at(ix, iy), at(ix + 1, iy), fx);
	float b = Math::lerp(at(ix, iy + 1), at(ix + 1, iy + 1), fx);
	return Math::lerp(a, b, fy);
}

void FellowWind3D::_build_gusts() {
	if (gust_image.is_valid()) {
		return;
	}
	// Two octaves (the gust cells, and a quarter of them), stretched around the middle: gusts and lulls.
	gust_image = Image::create_empty(GUST_SIZE, GUST_SIZE, false, Image::FORMAT_R8);
	for (int y = 0; y < GUST_SIZE; y++) {
		for (int x = 0; x < GUST_SIZE; x++) {
			float u = (x + 0.5f) / GUST_SIZE;
			float v = (y + 0.5f) / GUST_SIZE;
			float n = wind_value_noise(u, v, GUST_CELLS, 1) * 0.7f + wind_value_noise(u, v, GUST_CELLS * 4, 2) * 0.3f;
			n = CLAMP((n - 0.5f) * 2.0f + 0.5f, 0.0f, 1.0f);
			gust_image->set_pixel(x, y, Color(n, n, n));
		}
	}
	gust_texture = ImageTexture::create_from_image(gust_image);
}

Vector3 FellowWind3D::get_direction() const {
	Vector3 f = is_inside_tree() ? -get_global_basis().get_column(Vector3::AXIS_Z) : -get_basis().get_column(Vector3::AXIS_Z);
	f.y = 0.0f;
	return f.length_squared() > 1e-8f ? f.normalized() : Vector3(1.0f, 0.0f, 0.0f);
}

Vector3 FellowWind3D::get_velocity_at(const Vector3 &p_position) const {
	Vector3 d = get_direction();
	if (gust_image.is_null() || gustiness <= 0.0f) {
		return d * speed;
	}
	// As the shader: cells of the gust size, twice as long along the wind, the field travelled `offset` along it.
	Vector3 side(-d.z, 0.0f, d.x);
	float along = (p_position.dot(d) - float(offset)) * 0.5f;
	float across = p_position.dot(side);
	float scale = 1.0f / (gust_size * GUST_CELLS);
	float u = along * scale;
	float v = across * scale;
	u -= Math::floor(u);
	v -= Math::floor(v);
	float fx = u * GUST_SIZE - 0.5f;
	float fy = v * GUST_SIZE - 0.5f;
	int ix = int(Math::floor(fx));
	int iy = int(Math::floor(fy));
	float tx = fx - ix;
	float ty = fy - iy;
	auto px = [&](int i, int j) {
		return gust_image->get_pixel((i % GUST_SIZE + GUST_SIZE) % GUST_SIZE, (j % GUST_SIZE + GUST_SIZE) % GUST_SIZE).r;
	};
	float g = Math::lerp(Math::lerp(px(ix, iy), px(ix + 1, iy), tx), Math::lerp(px(ix, iy + 1), px(ix + 1, iy + 1), tx), ty);
	float s = speed * MAX(0.0f, 1.0f + gustiness * CLAMP((g - 0.5f) * 3.0f, -1.0f, 1.0f));
	return d * s;
}

Vector4 FellowWind3D::get_gust_mapping() const {
	const Vector3 d = get_direction();
	return Vector4(d.x, d.z, float(Math::fmod(offset, double(gust_size) * GUST_CELLS * 2.0)), 1.0f / (gust_size * GUST_CELLS));
}

void FellowWind3D::_push_state(bool p_enabled) {
	RendererRD::FellowWeather::WindState st;
	st.enabled = p_enabled;
	Vector3 d = get_direction();
	st.direction = Vector2(d.x, d.z);
	st.speed = speed;
	st.gustiness = gustiness;
	st.offset = float(Math::fmod(offset, double(gust_size) * GUST_CELLS * 2.0)); // the field repeats there
	st.gust_size = gust_size;
	st.flutter = flutter;
	st.time = float(Math::fmod(time, 3600.0));
	PackedByteArray state;
	state.resize(sizeof(st));
	memcpy(state.ptrw(), &st, sizeof(st));
	RID tex = gust_texture.is_valid() ? gust_texture->get_rid() : RID();
	RS::get_singleton()->call_on_render_thread(callable_mp_static(&FellowWind3D::_rt_set).bind(state, tex));
}

void FellowWind3D::_rt_set(PackedByteArray p_state, RID p_texture) {
	if (p_state.size() != int64_t(sizeof(RendererRD::FellowWeather::WindState))) {
		return;
	}
	RendererRD::FellowWeather *w = RendererRD::FellowWeather::get_singleton();
	if (!w) {
		return;
	}
	RendererRD::FellowWeather::WindState st;
	memcpy(&st, p_state.ptr(), sizeof(st));
	st.gust_texture = RID();
	if (st.enabled && p_texture.is_valid()) {
		if (RendererRD::TextureStorage *ts = RendererRD::TextureStorage::get_singleton()) {
			st.gust_texture = ts->texture_get_rd_texture(p_texture);
		}
	}
	w->set_wind_state(st);
}

PackedStringArray FellowWind3D::get_configuration_warnings() const {
	PackedStringArray warnings = Node3D::get_configuration_warnings();
	if (is_inside_tree() && active != this) {
		warnings.push_back(RTR("Another FellowWind3D is the wind: one wind node works at a time (the last to enter the tree)."));
	}
	return warnings;
}

// Milestone 178 (decision 0320): the wind as heard where the listener is (its gust there).
void FellowWind3D::_sound_update() {
	const bool want = sound_enabled && active == this && is_visible_in_tree() && !Engine::get_singleton()->is_editor_hint() && AudioServer::get_singleton();
	Node3D *listener = want ? fellow_sound_listener(this) : nullptr;
	const float v = listener ? float(get_velocity_at(listener->get_global_position()).length()) : 0.0f;
	if (v < 0.3f) {
		if (wind_voice.is_playing()) {
			if (FellowWindSynth *synth = (FellowWindSynth *)wind_voice.get_source()) {
				FellowWindSynth::Params pr;
				synth->post(pr);
			}
			if (sound_silent_since < 0.0) {
				sound_silent_since = time;
			} else if (time - sound_silent_since > 1.0 || time < sound_silent_since) {
				wind_voice.stop();
				sound_silent_since = -1.0;
			}
		}
		return;
	}
	sound_silent_since = -1.0;
	FellowWindSynth *synth = (FellowWindSynth *)wind_voice.start(this, &FellowWindSynth::make, sound_bus, sound_volume_db);
	if (synth) {
		FellowWindSynth::Params pr;
		pr.speed = v;
		pr.whistle = sound_whistle;
		pr.leaves = sound_leaves;
		synth->post(pr);
	}
}

void FellowWind3D::set_sound_volume_db(float p_db) {
	sound_volume_db = p_db;
	wind_voice.set_volume_db(p_db);
}

void FellowWind3D::set_sound_bus(const StringName &p_bus) {
	sound_bus = p_bus;
	wind_voice.set_bus(p_bus);
}

Dictionary FellowWind3D::get_sound_stats() const {
	Dictionary d;
	uint64_t frames = 0, usec = 0;
	wind_voice.get_mix_stats(frames, usec);
	const float rate = AudioServer::get_singleton() ? AudioServer::get_singleton()->get_mix_rate() : 48000.0f;
	d["playing"] = wind_voice.is_playing();
	d["mixed_seconds"] = double(frames) / rate;
	d["mix_usec_per_second"] = frames > 0 ? double(usec) / (double(frames) / rate) : 0.0;
	return d;
}

// The wind's sound offline (for tests): events by time, {time, speed} (m/s at the listener) — interleaved stereo,
// with this node's sound_whistle and sound_leaves.
PackedFloat32Array FellowWind3D::debug_render_sound(const Array &p_events, float p_seconds, float p_rate) const {
	PackedFloat32Array out;
	const int total = int(MAX(0.0f, p_seconds) * p_rate);
	out.resize(total * 2);
	FellowWindSynth synth;
	synth.set_rate(p_rate);
	LocalVector<float> l, r;
	float *w = out.ptrw();
	int pos = 0;
	FellowWindSynth::Params pr;
	pr.whistle = sound_whistle;
	pr.leaves = sound_leaves;
	for (int e = 0; e <= p_events.size() && pos < total; e++) {
		const Dictionary ev = e < p_events.size() ? Dictionary(p_events[e]) : Dictionary();
		const int at = e < p_events.size() ? CLAMP(int(float(ev.get("time", 0.0)) * p_rate), pos, total) : total;
		while (pos < at) {
			const int n = MIN(1024, at - pos);
			l.resize(n);
			r.resize(n);
			synth.render(l.ptr(), r.ptr(), n);
			for (int i = 0; i < n; i++) {
				w[(pos + i) * 2] = l[i];
				w[(pos + i) * 2 + 1] = r[i];
			}
			pos += n;
		}
		pr.speed = ev.get("speed", pr.speed);
		synth.post(pr);
	}
	return out;
}

void FellowWind3D::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_ENTER_TREE: {
			if (active && active != this) {
				FellowWind3D *previous = active;
				active = this;
				previous->update_configuration_warnings(); // no longer the wind
			}
			active = this;
			_build_gusts();
			set_process_internal(true);
		} break;
		case NOTIFICATION_INTERNAL_PROCESS: {
			// Still in the editor with its Animate Nodes toggle off (milestone 52): the sway and the gusts hold.
			if (time_auto && !fellow_editor_still()) {
				time += get_process_delta_time();
			}
			// The gusts travel with the mean wind, by the wind's clock (a jump of the clock, as a bench's, moves them
			// as far: set_time() is deterministic).
			if (last_time >= 0.0) {
				offset += double(speed) * (time - last_time);
			}
			last_time = time;
			if (active == this) {
				_push_state(is_visible_in_tree() && Fellow::fluid_enabled());
			}
			_sound_update();
		} break;
		case NOTIFICATION_EXIT_TREE: {
			wind_voice.stop();
			if (active == this) {
				active = nullptr;
				_push_state(false);
			}
			last_time = -1.0;
		} break;
	}
}

void FellowWind3D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_speed", "speed"), &FellowWind3D::set_speed);
	ClassDB::bind_method(D_METHOD("get_speed"), &FellowWind3D::get_speed);
	ClassDB::bind_method(D_METHOD("set_gustiness", "gustiness"), &FellowWind3D::set_gustiness);
	ClassDB::bind_method(D_METHOD("get_gustiness"), &FellowWind3D::get_gustiness);
	ClassDB::bind_method(D_METHOD("set_gust_size", "metres"), &FellowWind3D::set_gust_size);
	ClassDB::bind_method(D_METHOD("get_gust_size"), &FellowWind3D::get_gust_size);
	ClassDB::bind_method(D_METHOD("set_flutter", "flutter"), &FellowWind3D::set_flutter);
	ClassDB::bind_method(D_METHOD("get_flutter"), &FellowWind3D::get_flutter);
	ClassDB::bind_method(D_METHOD("set_time", "seconds"), &FellowWind3D::set_time);
	ClassDB::bind_method(D_METHOD("get_time"), &FellowWind3D::get_time);
	ClassDB::bind_method(D_METHOD("set_time_auto", "enabled"), &FellowWind3D::set_time_auto);
	ClassDB::bind_method(D_METHOD("is_time_auto"), &FellowWind3D::is_time_auto);
	ClassDB::bind_method(D_METHOD("get_direction"), &FellowWind3D::get_direction);
	ClassDB::bind_method(D_METHOD("get_velocity"), &FellowWind3D::get_velocity);
	ClassDB::bind_method(D_METHOD("get_velocity_at", "position"), &FellowWind3D::get_velocity_at);
	ClassDB::bind_method(D_METHOD("set_sound_enabled", "enabled"), &FellowWind3D::set_sound_enabled);
	ClassDB::bind_method(D_METHOD("is_sound_enabled"), &FellowWind3D::is_sound_enabled);
	ClassDB::bind_method(D_METHOD("set_sound_volume_db", "db"), &FellowWind3D::set_sound_volume_db);
	ClassDB::bind_method(D_METHOD("get_sound_volume_db"), &FellowWind3D::get_sound_volume_db);
	ClassDB::bind_method(D_METHOD("set_sound_bus", "bus"), &FellowWind3D::set_sound_bus);
	ClassDB::bind_method(D_METHOD("get_sound_bus"), &FellowWind3D::get_sound_bus);
	ClassDB::bind_method(D_METHOD("set_sound_whistle", "amount"), &FellowWind3D::set_sound_whistle);
	ClassDB::bind_method(D_METHOD("get_sound_whistle"), &FellowWind3D::get_sound_whistle);
	ClassDB::bind_method(D_METHOD("set_sound_leaves", "amount"), &FellowWind3D::set_sound_leaves);
	ClassDB::bind_method(D_METHOD("get_sound_leaves"), &FellowWind3D::get_sound_leaves);
	ClassDB::bind_method(D_METHOD("get_sound_stats"), &FellowWind3D::get_sound_stats);
	ClassDB::bind_method(D_METHOD("debug_render_sound", "events", "seconds", "rate"), &FellowWind3D::debug_render_sound, DEFVAL(48000.0f));
	ClassDB::bind_method(D_METHOD("get_gust_texture"), &FellowWind3D::get_gust_texture);

	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "speed", PROPERTY_HINT_RANGE, "0,60,0.01,or_greater,suffix:m/s"), "set_speed", "get_speed");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "gustiness", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_gustiness", "get_gustiness");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "gust_size", PROPERTY_HINT_RANGE, "0.1,500,0.1,or_greater,suffix:m"), "set_gust_size", "get_gust_size");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "flutter", PROPERTY_HINT_RANGE, "0,4,0.01"), "set_flutter", "get_flutter");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "time", PROPERTY_HINT_NONE, "suffix:s", PROPERTY_USAGE_NONE), "set_time", "get_time");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "time_auto"), "set_time_auto", "is_time_auto");
	ADD_GROUP("Sound", "sound_");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "sound_enabled"), "set_sound_enabled", "is_sound_enabled");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "sound_volume_db", PROPERTY_HINT_RANGE, "-60,24,0.1,suffix:dB"), "set_sound_volume_db", "get_sound_volume_db");
	ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "sound_bus"), "set_sound_bus", "get_sound_bus");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "sound_whistle", PROPERTY_HINT_RANGE, "0,4,0.01"), "set_sound_whistle", "get_sound_whistle");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "sound_leaves", PROPERTY_HINT_RANGE, "0,4,0.01"), "set_sound_leaves", "get_sound_leaves");
	ADD_GROUP("", "");
}

FellowWind3D::FellowWind3D() {
}

FellowWind3D::~FellowWind3D() {
	if (active == this) {
		active = nullptr;
	}
}
