/**************************************************************************/
/*  fellow_audio_director.cpp                                             */
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

#include "fellow_audio_director.h"

#include "core/config/engine.h"
#include "core/object/class_db.h"
#include "core/os/os.h"
#include "scene/3d/audio_listener_3d.h"
#include "scene/3d/audio_stream_player_3d.h"
#include "scene/3d/camera_3d.h"
#include "scene/audio/audio_stream_player.h"
#include "scene/main/viewport.h"
#include "scene/resources/audio_stream_wav.h"
#include "servers/audio/audio_server.h"
#include "servers/audio/audio_stream.h"

// --- FellowSoundEvent --------------------------------------------------------------------------------------------

float FellowSoundEvent::get_audibility_db(float p_distance) const {
	if (!spatial) {
		return volume_db;
	}
	const float d = MAX(p_distance, 0.0f) / unit_size;
	float att = 0.0f;
	switch (attenuation_model) {
		case 0: { // the same as AudioStreamPlayer3D's
			att = Math::linear_to_db(1.0f / (d + CMP_EPSILON));
		} break;
		case 1: {
			att = Math::linear_to_db(1.0f / (d * d + CMP_EPSILON));
		} break;
		case 2: {
			att = -20.0f * Math::log(d + CMP_EPSILON);
		} break;
		default:
			break;
	}
	return volume_db + MIN(att, 3.0f); // the player's max_db
}

void FellowSoundEvent::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_streams", "streams"), &FellowSoundEvent::set_streams);
	ClassDB::bind_method(D_METHOD("get_streams"), &FellowSoundEvent::get_streams);
	ClassDB::bind_method(D_METHOD("set_selection", "selection"), &FellowSoundEvent::set_selection);
	ClassDB::bind_method(D_METHOD("get_selection"), &FellowSoundEvent::get_selection);
	ClassDB::bind_method(D_METHOD("set_volume_db", "db"), &FellowSoundEvent::set_volume_db);
	ClassDB::bind_method(D_METHOD("get_volume_db"), &FellowSoundEvent::get_volume_db);
	ClassDB::bind_method(D_METHOD("set_volume_random_db", "db"), &FellowSoundEvent::set_volume_random_db);
	ClassDB::bind_method(D_METHOD("get_volume_random_db"), &FellowSoundEvent::get_volume_random_db);
	ClassDB::bind_method(D_METHOD("set_pitch_scale", "scale"), &FellowSoundEvent::set_pitch_scale);
	ClassDB::bind_method(D_METHOD("get_pitch_scale"), &FellowSoundEvent::get_pitch_scale);
	ClassDB::bind_method(D_METHOD("set_pitch_random_semitones", "semitones"), &FellowSoundEvent::set_pitch_random_semitones);
	ClassDB::bind_method(D_METHOD("get_pitch_random_semitones"), &FellowSoundEvent::get_pitch_random_semitones);
	ClassDB::bind_method(D_METHOD("set_max_instances", "count"), &FellowSoundEvent::set_max_instances);
	ClassDB::bind_method(D_METHOD("get_max_instances"), &FellowSoundEvent::get_max_instances);
	ClassDB::bind_method(D_METHOD("set_cooldown", "seconds"), &FellowSoundEvent::set_cooldown);
	ClassDB::bind_method(D_METHOD("get_cooldown"), &FellowSoundEvent::get_cooldown);
	ClassDB::bind_method(D_METHOD("set_priority", "priority"), &FellowSoundEvent::set_priority);
	ClassDB::bind_method(D_METHOD("get_priority"), &FellowSoundEvent::get_priority);
	ClassDB::bind_method(D_METHOD("set_bus", "bus"), &FellowSoundEvent::set_bus);
	ClassDB::bind_method(D_METHOD("get_bus"), &FellowSoundEvent::get_bus);
	ClassDB::bind_method(D_METHOD("set_spatial", "enabled"), &FellowSoundEvent::set_spatial);
	ClassDB::bind_method(D_METHOD("is_spatial"), &FellowSoundEvent::is_spatial);
	ClassDB::bind_method(D_METHOD("set_unit_size", "size"), &FellowSoundEvent::set_unit_size);
	ClassDB::bind_method(D_METHOD("get_unit_size"), &FellowSoundEvent::get_unit_size);
	ClassDB::bind_method(D_METHOD("set_max_distance", "distance"), &FellowSoundEvent::set_max_distance);
	ClassDB::bind_method(D_METHOD("get_max_distance"), &FellowSoundEvent::get_max_distance);
	ClassDB::bind_method(D_METHOD("set_attenuation_model", "model"), &FellowSoundEvent::set_attenuation_model);
	ClassDB::bind_method(D_METHOD("get_attenuation_model"), &FellowSoundEvent::get_attenuation_model);
	ClassDB::bind_method(D_METHOD("set_virtual_voice", "enabled"), &FellowSoundEvent::set_virtual_voice);
	ClassDB::bind_method(D_METHOD("is_virtual_voice"), &FellowSoundEvent::is_virtual_voice);
	ClassDB::bind_method(D_METHOD("set_steal_oldest", "enabled"), &FellowSoundEvent::set_steal_oldest);
	ClassDB::bind_method(D_METHOD("is_steal_oldest"), &FellowSoundEvent::is_steal_oldest);
	ClassDB::bind_method(D_METHOD("get_audibility_db", "distance"), &FellowSoundEvent::get_audibility_db);

	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "streams", PROPERTY_HINT_ARRAY_TYPE, MAKE_RESOURCE_TYPE_HINT("AudioStream")), "set_streams", "get_streams");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "selection", PROPERTY_HINT_ENUM, "Random No Repeat,Random,Sequence"), "set_selection", "get_selection");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "volume_db", PROPERTY_HINT_RANGE, "-80,24,0.1,suffix:dB"), "set_volume_db", "get_volume_db");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "volume_random_db", PROPERTY_HINT_RANGE, "0,24,0.1,suffix:dB"), "set_volume_random_db", "get_volume_random_db");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "pitch_scale", PROPERTY_HINT_RANGE, "0.01,4,0.01"), "set_pitch_scale", "get_pitch_scale");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "pitch_random_semitones", PROPERTY_HINT_RANGE, "0,12,0.1"), "set_pitch_random_semitones", "get_pitch_random_semitones");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "max_instances", PROPERTY_HINT_RANGE, "0,64,1"), "set_max_instances", "get_max_instances");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "cooldown", PROPERTY_HINT_RANGE, "0,5,0.01,suffix:s"), "set_cooldown", "get_cooldown");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "priority", PROPERTY_HINT_RANGE, "0,255,1"), "set_priority", "get_priority");
	ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "bus"), "set_bus", "get_bus");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "spatial"), "set_spatial", "is_spatial");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "unit_size", PROPERTY_HINT_RANGE, "0.1,100,0.01,or_greater"), "set_unit_size", "get_unit_size");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "max_distance", PROPERTY_HINT_RANGE, "0,4096,0.01,or_greater,suffix:m"), "set_max_distance", "get_max_distance");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "attenuation_model", PROPERTY_HINT_ENUM, "Inverse,Inverse Square,Logarithmic,Disabled"), "set_attenuation_model", "get_attenuation_model");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "virtual_voice"), "set_virtual_voice", "is_virtual_voice");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "steal_oldest"), "set_steal_oldest", "is_steal_oldest");

	BIND_ENUM_CONSTANT(SELECTION_RANDOM_NO_REPEAT);
	BIND_ENUM_CONSTANT(SELECTION_RANDOM);
	BIND_ENUM_CONSTANT(SELECTION_SEQUENCE);
}

// --- FellowMixSnapshot -------------------------------------------------------------------------------------------

void FellowMixSnapshot::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_bus_volumes", "volumes"), &FellowMixSnapshot::set_bus_volumes);
	ClassDB::bind_method(D_METHOD("get_bus_volumes"), &FellowMixSnapshot::get_bus_volumes);
	ClassDB::bind_method(D_METHOD("set_fade_in", "seconds"), &FellowMixSnapshot::set_fade_in);
	ClassDB::bind_method(D_METHOD("get_fade_in"), &FellowMixSnapshot::get_fade_in);
	ClassDB::bind_method(D_METHOD("set_fade_out", "seconds"), &FellowMixSnapshot::set_fade_out);
	ClassDB::bind_method(D_METHOD("get_fade_out"), &FellowMixSnapshot::get_fade_out);
	ClassDB::bind_method(D_METHOD("set_trigger_bus", "bus"), &FellowMixSnapshot::set_trigger_bus);
	ClassDB::bind_method(D_METHOD("get_trigger_bus"), &FellowMixSnapshot::get_trigger_bus);

	ADD_PROPERTY(PropertyInfo(Variant::DICTIONARY, "bus_volumes", PROPERTY_HINT_DICTIONARY_TYPE, "StringName;float"), "set_bus_volumes", "get_bus_volumes");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "fade_in", PROPERTY_HINT_RANGE, "0,10,0.01,suffix:s"), "set_fade_in", "get_fade_in");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "fade_out", PROPERTY_HINT_RANGE, "0,10,0.01,suffix:s"), "set_fade_out", "get_fade_out");
	ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "trigger_bus"), "set_trigger_bus", "get_trigger_bus");
}

// --- FellowAudioDirector -----------------------------------------------------------------------------------------

Node3D *FellowAudioDirector::_listener() const {
	if (!listener_path.is_empty()) {
		return Object::cast_to<Node3D>(get_node_or_null(listener_path));
	}
	Viewport *vp = get_viewport();
	if (!vp) {
		return nullptr;
	}
	if (AudioListener3D *l = vp->get_audio_listener_3d()) {
		return l;
	}
	return vp->get_camera_3d();
}

Vector3 FellowAudioDirector::_listener_position() const {
	const Node3D *l = _listener();
	return l ? l->get_global_position() : Vector3();
}

float FellowAudioDirector::_audibility(const Voice &p_voice, const Vector3 &p_listener) const {
	const float dist = p_voice.spatial ? p_voice.position.distance_to(p_listener) : 0.0f;
	return p_voice.event->get_audibility_db(dist) + (p_voice.volume_db - p_voice.event->get_volume_db());
}

int FellowAudioDirector::_active_voices() const {
	return active_count;
}

Node *FellowAudioDirector::_take_player(bool p_spatial) {
	LocalVector<Node *> &pool = p_spatial ? free_players_3d : free_players;
	if (!pool.is_empty()) {
		Node *p = pool[pool.size() - 1];
		pool.remove_at(pool.size() - 1);
		return p;
	}
	Node *p = nullptr;
	if (p_spatial) {
		p = memnew(AudioStreamPlayer3D);
	} else {
		p = memnew(AudioStreamPlayer);
	}
	add_child(p, false, INTERNAL_MODE_BACK);
	return p;
}

void FellowAudioDirector::_release(uint32_t p_index) {
	Voice &v = voices[p_index];
	if (!v.fading && !v.is_virtual) {
		active_count--;
	}
	const int id = v.id;
	const bool stolen = v.stolen;
	if (v.player) {
		if (AudioStreamPlayer3D *p3 = Object::cast_to<AudioStreamPlayer3D>(v.player)) {
			p3->stop();
			free_players_3d.push_back(v.player);
		} else if (AudioStreamPlayer *p2 = Object::cast_to<AudioStreamPlayer>(v.player)) {
			p2->stop();
			free_players.push_back(v.player);
		}
	}
	voices.remove_at_unordered(p_index);
	emit_signal(SNAME("voice_finished"), id, stolen);
}

// A voice that loses its player (given up, or out of reach) goes on as a virtual voice.
void FellowAudioDirector::_release_player_to_virtual(uint32_t p_index) {
	Voice &v = voices[p_index];
	if (!v.fading && !v.is_virtual) {
		active_count--;
	}
	if (v.player) {
		if (AudioStreamPlayer3D *p3 = Object::cast_to<AudioStreamPlayer3D>(v.player)) {
			p3->stop();
			free_players_3d.push_back(v.player);
		} else if (AudioStreamPlayer *p2 = Object::cast_to<AudioStreamPlayer>(v.player)) {
			p2->stop();
			free_players.push_back(v.player);
		}
	}
	v.player = nullptr;
	v.is_virtual = true;
	v.fading = false;
	v.to_virtual = false;
	v.stolen = false;
	stats.virtualized++;
}

FellowAudioDirector::SnapshotState *FellowAudioDirector::_snapshot_state(const Ref<FellowMixSnapshot> &p_snapshot, bool p_create) {
	for (SnapshotState &s : snapshot_states) {
		if (s.snapshot == p_snapshot) {
			return &s;
		}
	}
	if (!p_create) {
		return nullptr;
	}
	SnapshotState s;
	s.snapshot = p_snapshot;
	snapshot_states.push_back(s);
	return &snapshot_states[snapshot_states.size() - 1];
}

void FellowAudioDirector::set_snapshots(const TypedArray<FellowMixSnapshot> &p_snapshots) {
	snapshots = p_snapshots;
	for (int i = 0; i < snapshots.size(); i++) {
		Ref<FellowMixSnapshot> s = snapshots[i];
		if (s.is_valid()) {
			_snapshot_state(s, true);
		}
	}
	update_configuration_warnings();
}

void FellowAudioDirector::set_seed(int64_t p_seed) {
	seed = p_seed;
	rng.seed(uint64_t(seed));
}

// Where a voice that went virtual would be now (s into its stream; a loop wraps); r_ended when a one-shot is over.
float FellowAudioDirector::_virtual_position(const Voice &p_voice, bool &r_ended) const {
	r_ended = false;
	const float elapsed = float(clock - p_voice.started) * p_voice.pitch;
	const float length = p_voice.stream.is_valid() ? float(p_voice.stream->get_length()) : 0.0f;
	if (length <= 0.0f) {
		return elapsed;
	}
	if (const AudioStreamWAV *wav = Object::cast_to<AudioStreamWAV>(p_voice.stream.ptr())) {
		if (wav->get_loop_mode() != AudioStreamWAV::LOOP_DISABLED) {
			const float rate = float(MAX(1, wav->get_mix_rate()));
			const float end = wav->get_loop_end() > 0 ? float(wav->get_loop_end()) / rate : length;
			const float begin = float(wav->get_loop_begin()) / rate;
			return elapsed < end ? elapsed : begin + Math::fmod(elapsed - end, MAX(end - begin, 1e-3f));
		}
	} else if (p_voice.stream->has_loop()) {
		return Math::fmod(elapsed, length);
	}
	r_ended = elapsed >= length;
	return elapsed;
}

bool FellowAudioDirector::_in_reach(const Voice &p_voice, const Vector3 &p_listener, float p_margin) const {
	const float reach = p_voice.event->get_max_distance();
	return !p_voice.spatial || reach <= 0.0f || p_voice.position.distance_to(p_listener) <= reach * p_margin;
}

// A voice given a player (at its start, or resumed from virtual at p_from seconds into its stream).
void FellowAudioDirector::_realize(Voice &p_voice, float p_from) {
	const Ref<FellowSoundEvent> &e = p_voice.event;
	p_voice.player = _take_player(p_voice.spatial);
	p_voice.is_virtual = false;
	active_count++;
	if (AudioStreamPlayer3D *p3 = Object::cast_to<AudioStreamPlayer3D>(p_voice.player)) {
		p3->set_stream(p_voice.stream);
		p3->set_volume_db(p_voice.volume_db);
		p3->set_pitch_scale(p_voice.pitch);
		p3->set_bus(e->get_bus());
		p3->set_unit_size(e->get_unit_size());
		p3->set_max_distance(e->get_max_distance());
		p3->set_attenuation_model(AudioStreamPlayer3D::AttenuationModel(e->get_attenuation_model()));
		p3->set_global_position(p_voice.position);
		p3->play(p_from);
	} else if (AudioStreamPlayer *p2 = Object::cast_to<AudioStreamPlayer>(p_voice.player)) {
		p2->set_stream(p_voice.stream);
		p2->set_volume_db(p_voice.volume_db);
		p2->set_pitch_scale(p_voice.pitch);
		p2->set_bus(e->get_bus());
		p2->play(p_from);
	}
}

int FellowAudioDirector::_play(const Ref<FellowSoundEvent> &p_event, const Vector3 &p_position, Node3D *p_follow) {
	ERR_FAIL_COND_V_MSG(p_event.is_null(), 0, "FellowAudioDirector: no event to play.");
	ERR_FAIL_COND_V_MSG(!is_inside_tree(), 0, "FellowAudioDirector: not in the scene tree.");
	const TypedArray<AudioStream> streams = p_event->get_streams();
	ERR_FAIL_COND_V_MSG(streams.is_empty(), 0, "FellowAudioDirector: the event has no streams.");
	const uint64_t t0 = OS::get_singleton()->get_ticks_usec();
	EventState &state = event_states[p_event->get_instance_id()];
	auto done = [&](int p_id) {
		stats.play_usec += OS::get_singleton()->get_ticks_usec() - t0;
		return p_id;
	};
	const bool virtual_ok = p_event->is_virtual_voice();
	// Its cooldown and its instances (over them: refused, or with steal_oldest the oldest gives way).
	if (p_event->get_cooldown() > 0.0f && clock - state.last_start < double(p_event->get_cooldown()) - 1e-6) {
		stats.rejected_cooldown++;
		return done(0);
	}
	if (p_event->get_max_instances() > 0 && get_event_voice_count(p_event) >= p_event->get_max_instances()) {
		if (!p_event->is_steal_oldest()) {
			stats.rejected_instances++;
			return done(0);
		}
		int oldest = -1;
		for (uint32_t i = 0; i < voices.size(); i++) {
			if (!voices[i].fading && voices[i].event == p_event && (oldest < 0 || voices[i].order < voices[oldest].order)) {
				oldest = int(i);
			}
		}
		if (oldest >= 0) {
			Voice &o = voices[oldest];
			o.stolen = true;
			stats.stolen++;
			if (o.is_virtual || steal_fade <= 0.0f) {
				_release(uint32_t(oldest));
			} else {
				active_count--;
				o.fading = true;
				o.fade_time = steal_fade;
				o.fade_left = steal_fade;
			}
		}
	}
	Voice nv;
	nv.event = p_event;
	nv.spatial = p_event->is_spatial();
	nv.position = p_follow ? p_follow->get_global_position() : p_position;
	nv.follow = p_follow ? p_follow->get_instance_id() : ObjectID();
	const Vector3 listener = _listener_position();
	bool start_virtual = false;
	if (!_in_reach(nv, listener, 1.0f)) {
		if (!virtual_ok) {
			stats.culled++;
			return done(0);
		}
		start_virtual = true; // out of reach: it goes on unheard until it comes within it
	}
	const float vr = p_event->get_volume_random_db();
	nv.volume_db = p_event->get_volume_db() + (vr > 0.0f ? rng.random(-vr, vr) : 0.0f);
	nv.audibility = _audibility(nv, listener);
	// The budget: a voice given up by the least important, quietest one if the new one matters more.
	if (!start_virtual && _active_voices() >= max_voices) {
		int victim = -1;
		for (uint32_t i = 0; i < voices.size(); i++) {
			const Voice &v = voices[i];
			if (v.fading || v.is_virtual) {
				continue;
			}
			if (victim < 0) {
				victim = int(i);
				continue;
			}
			const Voice &w = voices[victim];
			const int vp = v.event->get_priority();
			const int wp = w.event->get_priority();
			if (vp < wp || (vp == wp && (v.audibility < w.audibility || (v.audibility == w.audibility && v.order < w.order)))) {
				victim = int(i);
			}
		}
		bool take = steal_voices && victim >= 0;
		if (take) {
			const Voice &w = voices[victim];
			const int np = p_event->get_priority();
			const int wp = w.event->get_priority();
			take = !(wp > np || (wp == np && w.audibility >= nv.audibility));
		}
		if (!take) {
			if (!virtual_ok) {
				stats.rejected_voices++;
				return done(0);
			}
			start_virtual = true; // no voice for it now: unheard until one frees
		} else {
			Voice &w = voices[victim];
			w.stolen = true;
			stats.stolen++;
			w.to_virtual = w.event->is_virtual_voice();
			if (steal_fade > 0.0f) {
				active_count--;
				w.fading = true;
				w.fade_time = steal_fade;
				w.fade_left = steal_fade;
			} else if (w.to_virtual) {
				_release_player_to_virtual(uint32_t(victim));
			} else {
				_release(uint32_t(victim));
			}
		}
	}
	// The variation.
	const int n = streams.size();
	int index = 0;
	switch (p_event->get_selection()) {
		case FellowSoundEvent::SELECTION_RANDOM_NO_REPEAT: {
			if (n > 1) {
				index = int(rng.rand() % uint32_t(state.last_index >= 0 ? n - 1 : n));
				if (state.last_index >= 0 && index >= state.last_index) {
					index++;
				}
			}
		} break;
		case FellowSoundEvent::SELECTION_RANDOM: {
			index = int(rng.rand() % uint32_t(n));
		} break;
		case FellowSoundEvent::SELECTION_SEQUENCE: {
			index = state.next_index % n;
			state.next_index = (index + 1) % n;
		} break;
	}
	nv.stream = streams[index];
	ERR_FAIL_COND_V_MSG(nv.stream.is_null(), done(0), "FellowAudioDirector: an empty stream in the event.");
	const float pr = p_event->get_pitch_random_semitones();
	nv.pitch = p_event->get_pitch_scale() * (pr > 0.0f ? Math::pow(2.0f, rng.random(-pr, pr) / 12.0f) : 1.0f);
	nv.started = clock;
	nv.id = next_id++;
	if (next_id <= 0) {
		next_id = 1;
	}
	nv.order = next_order++;
	if (start_virtual) {
		nv.is_virtual = true;
		stats.virtualized++;
	} else {
		_realize(nv, 0.0f);
	}
	voices.push_back(nv);
	state.last_start = clock;
	state.last_index = index;
	stats.started++;
	stats.peak = MAX(stats.peak, uint64_t(_active_voices()));
	return done(nv.id);
}

int FellowAudioDirector::play(const Ref<FellowSoundEvent> &p_event, const Vector3 &p_position) {
	return _play(p_event, p_position, nullptr);
}

int FellowAudioDirector::play_on(const Ref<FellowSoundEvent> &p_event, Node3D *p_node) {
	ERR_FAIL_NULL_V_MSG(p_node, 0, "FellowAudioDirector: no node to play on.");
	return _play(p_event, Vector3(), p_node);
}

void FellowAudioDirector::stop(int p_voice, float p_fade) {
	for (uint32_t i = 0; i < voices.size(); i++) {
		if (voices[i].id != p_voice) {
			continue;
		}
		if (p_fade > 0.0f && !voices[i].is_virtual) {
			Voice &v = voices[i];
			v.to_virtual = false;
			if (!v.fading) {
				active_count--;
				v.fading = true;
				v.fade_time = p_fade;
				v.fade_left = p_fade;
			}
		} else {
			_release(i);
		}
		return;
	}
}

void FellowAudioDirector::stop_event(const Ref<FellowSoundEvent> &p_event, float p_fade) {
	LocalVector<int> ids;
	for (const Voice &v : voices) {
		if (v.event == p_event) {
			ids.push_back(v.id);
		}
	}
	for (int id : ids) {
		stop(id, p_fade);
	}
}

void FellowAudioDirector::stop_all(float p_fade) {
	LocalVector<int> ids;
	for (const Voice &v : voices) {
		ids.push_back(v.id);
	}
	for (int id : ids) {
		stop(id, p_fade);
	}
}

bool FellowAudioDirector::is_playing(int p_voice) const {
	for (const Voice &v : voices) {
		if (v.id == p_voice) {
			return !v.fading;
		}
	}
	return false;
}

bool FellowAudioDirector::is_virtual(int p_voice) const {
	for (const Voice &v : voices) {
		if (v.id == p_voice) {
			return v.is_virtual;
		}
	}
	return false;
}

int FellowAudioDirector::get_event_voice_count(const Ref<FellowSoundEvent> &p_event) const {
	int n = 0;
	for (const Voice &v : voices) {
		n += (!v.fading && v.event == p_event) ? 1 : 0;
	}
	return n;
}

Node *FellowAudioDirector::get_voice_player(int p_voice) const {
	for (const Voice &v : voices) {
		if (v.id == p_voice) {
			return v.player;
		}
	}
	return nullptr;
}

Array FellowAudioDirector::get_voices() const {
	Array a;
	for (const Voice &v : voices) {
		Dictionary d;
		d["id"] = v.id;
		d["event"] = v.event;
		d["priority"] = v.event->get_priority();
		d["audibility_db"] = v.audibility;
		d["volume_db"] = v.volume_db;
		d["bus"] = v.event->get_bus();
		d["position"] = v.position;
		d["fading"] = v.fading;
		d["virtual"] = v.is_virtual;
		a.push_back(d);
	}
	return a;
}

void FellowAudioDirector::push_snapshot(const Ref<FellowMixSnapshot> &p_snapshot) {
	ERR_FAIL_COND(p_snapshot.is_null());
	_snapshot_state(p_snapshot, true)->pushed = true;
}

void FellowAudioDirector::pop_snapshot(const Ref<FellowMixSnapshot> &p_snapshot) {
	if (SnapshotState *s = _snapshot_state(p_snapshot, false)) {
		s->pushed = false;
	}
}

float FellowAudioDirector::get_snapshot_weight(const Ref<FellowMixSnapshot> &p_snapshot) const {
	for (const SnapshotState &s : snapshot_states) {
		if (s.snapshot == p_snapshot) {
			return s.weight;
		}
	}
	return 0.0f;
}

void FellowAudioDirector::_update(double p_delta) {
	const uint64_t t0 = OS::get_singleton()->get_ticks_usec();
	clock += p_delta;
	const Vector3 listener = _listener_position();
	// The voices: where they are, how loud the listener hears them, which have ended or faded out; the virtual ones
	// going on in time, resumed where they would be when within reach and a voice is free.
	for (int i = int(voices.size()) - 1; i >= 0; i--) {
		Voice &v = voices[i];
		AudioStreamPlayer3D *p3 = Object::cast_to<AudioStreamPlayer3D>(v.player);
		AudioStreamPlayer *p2 = p3 ? nullptr : Object::cast_to<AudioStreamPlayer>(v.player);
		if (v.follow.is_valid()) {
			if (const Node3D *n = ObjectDB::get_instance<Node3D>(v.follow)) {
				v.position = n->get_global_position();
				if (p3) {
					p3->set_global_position(v.position);
				}
			} else {
				v.follow = ObjectID(); // gone: the sound goes on where it was
			}
		}
		if (v.is_virtual) {
			bool ended = false;
			const float at = _virtual_position(v, ended);
			if (ended) {
				stats.finished++;
				_release(uint32_t(i));
				continue;
			}
			if (active_count < max_voices && _in_reach(v, listener, 1.0f)) {
				v.audibility = _audibility(v, listener);
				_realize(v, at);
				stats.realized++;
			}
			continue;
		}
		if (v.fading) {
			v.fade_left -= float(p_delta);
			if (v.fade_left <= 0.0f) {
				if (v.to_virtual) {
					_release_player_to_virtual(uint32_t(i));
				} else {
					_release(uint32_t(i));
				}
				continue;
			}
			const float db = v.volume_db + Math::linear_to_db(MAX(v.fade_left / v.fade_time, 1e-4f));
			if (p3) {
				p3->set_volume_db(db);
			} else if (p2) {
				p2->set_volume_db(db);
			}
			continue;
		}
		const bool playing = p3 ? p3->is_playing() : (p2 ? p2->is_playing() : false);
		if (!playing) {
			stats.finished++;
			_release(uint32_t(i));
			continue;
		}
		v.audibility = _audibility(v, listener);
		if (v.event->is_virtual_voice() && !_in_reach(v, listener, 1.05f)) {
			_release_player_to_virtual(uint32_t(i)); // out of reach (inaudible there): unheard until it comes back
		}
	}
	// The snapshots: in while pushed or while a voice plays on their trigger bus.
	HashMap<StringName, float> offsets;
	for (int i = int(snapshot_states.size()) - 1; i >= 0; i--) {
		SnapshotState &s = snapshot_states[i];
		bool active = s.pushed;
		const StringName trigger = s.snapshot->get_trigger_bus();
		if (!active && trigger != StringName()) {
			for (const Voice &v : voices) {
				if (!v.fading && !v.is_virtual && v.event->get_bus() == trigger) {
					active = true;
					break;
				}
			}
		}
		const float fade = active ? s.snapshot->get_fade_in() : s.snapshot->get_fade_out();
		const float step = fade > 0.0f ? float(p_delta) / fade : 1.0f;
		s.weight = active ? MIN(1.0f, s.weight + step) : MAX(0.0f, s.weight - step);
		if (s.weight <= 0.0f && !s.pushed && !snapshots.has(s.snapshot)) {
			snapshot_states.remove_at(i);
			continue;
		}
		if (s.weight > 0.0f) {
			const Dictionary bv = s.snapshot->get_bus_volumes();
			for (const KeyValue<Variant, Variant> &kv : bv) {
				const StringName bus = kv.key;
				offsets[bus] = (offsets.has(bus) ? offsets[bus] : 0.0f) + s.weight * float(kv.value);
			}
		}
	}
	// The buses: each a snapshot changes at its level before them plus the snapshots'; given back when none does.
	AudioServer *as = AudioServer::get_singleton();
	LocalVector<StringName> released;
	for (const KeyValue<StringName, float> &kv : bus_base) {
		if (!offsets.has(kv.key)) {
			const int idx = as->get_bus_index(kv.key);
			if (idx >= 0) {
				as->set_bus_volume_db(idx, kv.value);
			}
			released.push_back(kv.key);
		}
	}
	for (const StringName &b : released) {
		bus_base.erase(b);
	}
	for (const KeyValue<StringName, float> &kv : offsets) {
		const int idx = as->get_bus_index(kv.key);
		if (idx < 0) {
			continue;
		}
		if (!bus_base.has(kv.key)) {
			bus_base[kv.key] = as->get_bus_volume_db(idx);
		}
		as->set_bus_volume_db(idx, bus_base[kv.key] + kv.value);
	}
	stats.frames++;
	stats.usec += OS::get_singleton()->get_ticks_usec() - t0;
}

void FellowAudioDirector::_restore_buses() {
	AudioServer *as = AudioServer::get_singleton();
	if (!as) {
		return;
	}
	for (const KeyValue<StringName, float> &kv : bus_base) {
		const int idx = as->get_bus_index(kv.key);
		if (idx >= 0) {
			as->set_bus_volume_db(idx, kv.value);
		}
	}
	bus_base.clear();
	for (SnapshotState &s : snapshot_states) {
		s.weight = 0.0f;
	}
}

void FellowAudioDirector::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_READY: {
			if (!Engine::get_singleton()->is_editor_hint()) {
				set_process_internal(true);
			}
		} break;
		case NOTIFICATION_INTERNAL_PROCESS: {
			_update(get_process_delta_time());
		} break;
		case NOTIFICATION_EXIT_TREE: {
			while (!voices.is_empty()) {
				_release(voices.size() - 1);
			}
			_restore_buses();
		} break;
	}
}

Dictionary FellowAudioDirector::get_stats() const {
	Dictionary d;
	int virtual_count = 0;
	for (const Voice &v : voices) {
		virtual_count += v.is_virtual ? 1 : 0;
	}
	d["voices"] = _active_voices();
	d["virtual"] = virtual_count;
	d["fading"] = int(voices.size()) - _active_voices() - virtual_count;
	d["virtualized"] = int64_t(stats.virtualized);
	d["realized"] = int64_t(stats.realized);
	d["peak"] = int64_t(stats.peak);
	d["started"] = int64_t(stats.started);
	d["stolen"] = int64_t(stats.stolen);
	d["rejected_voices"] = int64_t(stats.rejected_voices);
	d["rejected_instances"] = int64_t(stats.rejected_instances);
	d["rejected_cooldown"] = int64_t(stats.rejected_cooldown);
	d["culled"] = int64_t(stats.culled);
	d["finished"] = int64_t(stats.finished);
	d["players"] = int(voices.size() + free_players.size() + free_players_3d.size());
	d["frames"] = int64_t(stats.frames);
	d["usec"] = int64_t(stats.usec);
	d["play_usec"] = int64_t(stats.play_usec);
	return d;
}

PackedStringArray FellowAudioDirector::get_configuration_warnings() const {
	PackedStringArray warnings = Node::get_configuration_warnings();
	AudioServer *as = AudioServer::get_singleton();
	for (int i = 0; i < snapshots.size(); i++) {
		Ref<FellowMixSnapshot> s = snapshots[i];
		if (s.is_null() || !as) {
			continue;
		}
		const Dictionary bv = s->get_bus_volumes();
		for (const KeyValue<Variant, Variant> &kv : bv) {
			if (as->get_bus_index(kv.key) < 0) {
				warnings.push_back(vformat(RTR("A snapshot changes the bus \"%s\", which is not in the audio bus layout."), String(kv.key)));
			}
		}
	}
	return warnings;
}

void FellowAudioDirector::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_max_voices", "count"), &FellowAudioDirector::set_max_voices);
	ClassDB::bind_method(D_METHOD("get_max_voices"), &FellowAudioDirector::get_max_voices);
	ClassDB::bind_method(D_METHOD("set_steal_voices", "enabled"), &FellowAudioDirector::set_steal_voices);
	ClassDB::bind_method(D_METHOD("is_steal_voices"), &FellowAudioDirector::is_steal_voices);
	ClassDB::bind_method(D_METHOD("set_steal_fade", "seconds"), &FellowAudioDirector::set_steal_fade);
	ClassDB::bind_method(D_METHOD("get_steal_fade"), &FellowAudioDirector::get_steal_fade);
	ClassDB::bind_method(D_METHOD("set_snapshots", "snapshots"), &FellowAudioDirector::set_snapshots);
	ClassDB::bind_method(D_METHOD("get_snapshots"), &FellowAudioDirector::get_snapshots);
	ClassDB::bind_method(D_METHOD("set_seed", "seed"), &FellowAudioDirector::set_seed);
	ClassDB::bind_method(D_METHOD("get_seed"), &FellowAudioDirector::get_seed);
	ClassDB::bind_method(D_METHOD("set_listener_path", "path"), &FellowAudioDirector::set_listener_path);
	ClassDB::bind_method(D_METHOD("get_listener_path"), &FellowAudioDirector::get_listener_path);
	ClassDB::bind_method(D_METHOD("play", "event", "position"), &FellowAudioDirector::play, DEFVAL(Vector3()));
	ClassDB::bind_method(D_METHOD("play_on", "event", "node"), &FellowAudioDirector::play_on);
	ClassDB::bind_method(D_METHOD("stop", "voice", "fade"), &FellowAudioDirector::stop, DEFVAL(0.0f));
	ClassDB::bind_method(D_METHOD("stop_event", "event", "fade"), &FellowAudioDirector::stop_event, DEFVAL(0.0f));
	ClassDB::bind_method(D_METHOD("stop_all", "fade"), &FellowAudioDirector::stop_all, DEFVAL(0.0f));
	ClassDB::bind_method(D_METHOD("is_playing", "voice"), &FellowAudioDirector::is_playing);
	ClassDB::bind_method(D_METHOD("is_virtual", "voice"), &FellowAudioDirector::is_virtual);
	ClassDB::bind_method(D_METHOD("get_voice_count"), &FellowAudioDirector::get_voice_count);
	ClassDB::bind_method(D_METHOD("get_event_voice_count", "event"), &FellowAudioDirector::get_event_voice_count);
	ClassDB::bind_method(D_METHOD("get_voice_player", "voice"), &FellowAudioDirector::get_voice_player);
	ClassDB::bind_method(D_METHOD("get_voices"), &FellowAudioDirector::get_voices);
	ClassDB::bind_method(D_METHOD("push_snapshot", "snapshot"), &FellowAudioDirector::push_snapshot);
	ClassDB::bind_method(D_METHOD("pop_snapshot", "snapshot"), &FellowAudioDirector::pop_snapshot);
	ClassDB::bind_method(D_METHOD("get_snapshot_weight", "snapshot"), &FellowAudioDirector::get_snapshot_weight);
	ClassDB::bind_method(D_METHOD("get_stats"), &FellowAudioDirector::get_stats);
	ClassDB::bind_method(D_METHOD("reset_stats"), &FellowAudioDirector::reset_stats);

	ADD_PROPERTY(PropertyInfo(Variant::INT, "max_voices", PROPERTY_HINT_RANGE, "1,256,1,or_greater"), "set_max_voices", "get_max_voices");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "steal_voices"), "set_steal_voices", "is_steal_voices");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "steal_fade", PROPERTY_HINT_RANGE, "0,1,0.001,suffix:s"), "set_steal_fade", "get_steal_fade");
	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "snapshots", PROPERTY_HINT_ARRAY_TYPE, MAKE_RESOURCE_TYPE_HINT("FellowMixSnapshot")), "set_snapshots", "get_snapshots");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "seed"), "set_seed", "get_seed");
	ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "listener_path", PROPERTY_HINT_NODE_PATH_VALID_TYPES, "Node3D"), "set_listener_path", "get_listener_path");

	ADD_SIGNAL(MethodInfo("voice_finished", PropertyInfo(Variant::INT, "voice"), PropertyInfo(Variant::BOOL, "stolen")));
}

FellowAudioDirector::FellowAudioDirector() {
	rng.seed(0);
}

FellowAudioDirector::~FellowAudioDirector() {
}
