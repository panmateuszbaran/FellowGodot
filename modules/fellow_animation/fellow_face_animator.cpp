/**************************************************************************/
/*  fellow_face_animator.cpp                                              */
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

#include "fellow_face_animator.h"

#include "core/config/engine.h"
#include "core/object/class_db.h"
#include "core/os/os.h"
#include "scene/2d/audio_stream_player_2d.h"
#include "scene/3d/audio_stream_player_3d.h"
#include "scene/3d/mesh_instance_3d.h"
#include "scene/3d/node_3d.h"
#include "scene/audio/audio_stream_player.h"
#include "servers/audio/audio_server.h"

FellowFaceAnimator::GlobalStats FellowFaceAnimator::global_stats;

// Per hop: the five mouth channels, then the level (dB) and the scores (dB) they came from.
static constexpr int TRACK_STRIDE = 10;

struct FellowFaceBiquad {
	float b0 = 0, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
	float x1 = 0, x2 = 0, y1 = 0, y2 = 0;
	void bandpass(float p_center, float p_q, float p_rate) {
		const float w0 = Math::TAU * p_center / p_rate;
		const float alpha = Math::sin(w0) / (2.0f * p_q);
		const float a0 = 1.0f + alpha;
		b0 = alpha / a0;
		b1 = 0.0f;
		b2 = -alpha / a0;
		a1 = -2.0f * Math::cos(w0) / a0;
		a2 = (1.0f - alpha) / a0;
	}
	_FORCE_INLINE_ float run(float x) {
		const float y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
		x2 = x1;
		x1 = x;
		y2 = y1;
		y1 = y;
		return y;
	}
};

namespace {
float smooth_range(float p_a, float p_b, float p_x) {
	const float t = CLAMP((p_x - p_a) / (p_b - p_a), 0.0f, 1.0f);
	return t * t * (3.0f - 2.0f * t);
}

float to_db(float p_energy) {
	return 4.3429448f * Math::log(MAX(p_energy, 1e-12f)); // 10 log10
}
} //namespace

const char *FellowFaceAnimator::channel_name(int p_channel) {
	static const char *names[CHANNEL_MAX] = { "jaw_open", "mouth_wide", "mouth_round", "mouth_closed", "teeth", "blink_left", "blink_right",
		"look_out_left", "look_in_left", "look_up_left", "look_down_left", "look_out_right", "look_in_right", "look_up_right", "look_down_right",
		"brow_inner_up", "brow_down_left", "brow_down_right", "brow_outer_up_left", "brow_outer_up_right", "smile_left", "smile_right",
		"frown_left", "frown_right", "cheek_squint_left", "cheek_squint_right", "eye_squint_left", "eye_squint_right", "eye_wide_left",
		"eye_wide_right", "nose_sneer_left", "nose_sneer_right" };
	return (p_channel >= 0 && p_channel < CHANNEL_MAX) ? names[p_channel] : "";
}

// Milestone 100 (decision 0208): the built-in expressions, after the action units Ekman and Friesen give for the basic
// emotions (FACS), on ARKit's shapes: happiness (6+12: the cheeks raised, the lips' corners pulled), sadness (1+4+15:
// the inner brows raised and drawn, the corners down), anger (4+5+7+23: the brows down, the lids tightened, the lips
// pressed), surprise (1+2+5+26: the brows up, the eyes wide, the jaw dropped), fear (1+2+4+5+20+26), disgust (9+15+16:
// the nose wrinkled, the corners down).
namespace {
struct ExpressionPreset {
	const char *name;
	int count;
	struct {
		int channel;
		float weight;
	} values[10];
};
const ExpressionPreset EXPRESSION_PRESETS[] = {
	{ "happy", 6, { { FellowFaceAnimator::CHANNEL_SMILE_LEFT, 0.8f }, { FellowFaceAnimator::CHANNEL_SMILE_RIGHT, 0.8f }, { FellowFaceAnimator::CHANNEL_CHEEK_SQUINT_LEFT, 0.5f }, { FellowFaceAnimator::CHANNEL_CHEEK_SQUINT_RIGHT, 0.5f }, { FellowFaceAnimator::CHANNEL_EYE_SQUINT_LEFT, 0.25f }, { FellowFaceAnimator::CHANNEL_EYE_SQUINT_RIGHT, 0.25f } } },
	{ "sad", 5, { { FellowFaceAnimator::CHANNEL_BROW_INNER_UP, 0.7f }, { FellowFaceAnimator::CHANNEL_BROW_DOWN_LEFT, 0.3f }, { FellowFaceAnimator::CHANNEL_BROW_DOWN_RIGHT, 0.3f }, { FellowFaceAnimator::CHANNEL_FROWN_LEFT, 0.6f }, { FellowFaceAnimator::CHANNEL_FROWN_RIGHT, 0.6f } } },
	{ "angry", 7, { { FellowFaceAnimator::CHANNEL_BROW_DOWN_LEFT, 0.9f }, { FellowFaceAnimator::CHANNEL_BROW_DOWN_RIGHT, 0.9f }, { FellowFaceAnimator::CHANNEL_EYE_SQUINT_LEFT, 0.4f }, { FellowFaceAnimator::CHANNEL_EYE_SQUINT_RIGHT, 0.4f }, { FellowFaceAnimator::CHANNEL_NOSE_SNEER_LEFT, 0.3f }, { FellowFaceAnimator::CHANNEL_NOSE_SNEER_RIGHT, 0.3f }, { FellowFaceAnimator::CHANNEL_MOUTH_CLOSED, 0.3f } } },
	{ "surprised", 6, { { FellowFaceAnimator::CHANNEL_BROW_INNER_UP, 0.8f }, { FellowFaceAnimator::CHANNEL_BROW_OUTER_UP_LEFT, 0.8f }, { FellowFaceAnimator::CHANNEL_BROW_OUTER_UP_RIGHT, 0.8f }, { FellowFaceAnimator::CHANNEL_EYE_WIDE_LEFT, 0.7f }, { FellowFaceAnimator::CHANNEL_EYE_WIDE_RIGHT, 0.7f }, { FellowFaceAnimator::CHANNEL_JAW_OPEN, 0.25f } } },
	{ "afraid", 8, { { FellowFaceAnimator::CHANNEL_BROW_INNER_UP, 0.9f }, { FellowFaceAnimator::CHANNEL_BROW_OUTER_UP_LEFT, 0.5f }, { FellowFaceAnimator::CHANNEL_BROW_OUTER_UP_RIGHT, 0.5f }, { FellowFaceAnimator::CHANNEL_BROW_DOWN_LEFT, 0.3f }, { FellowFaceAnimator::CHANNEL_BROW_DOWN_RIGHT, 0.3f }, { FellowFaceAnimator::CHANNEL_EYE_WIDE_LEFT, 0.8f }, { FellowFaceAnimator::CHANNEL_EYE_WIDE_RIGHT, 0.8f }, { FellowFaceAnimator::CHANNEL_JAW_OPEN, 0.15f } } },
	{ "disgusted", 6, { { FellowFaceAnimator::CHANNEL_NOSE_SNEER_LEFT, 0.8f }, { FellowFaceAnimator::CHANNEL_NOSE_SNEER_RIGHT, 0.8f }, { FellowFaceAnimator::CHANNEL_FROWN_LEFT, 0.4f }, { FellowFaceAnimator::CHANNEL_FROWN_RIGHT, 0.4f }, { FellowFaceAnimator::CHANNEL_EYE_SQUINT_LEFT, 0.3f }, { FellowFaceAnimator::CHANNEL_EYE_SQUINT_RIGHT, 0.3f } } },
};

int channel_by_name(const String &p_name) {
	for (int c = 0; c < FellowFaceAnimator::CHANNEL_MAX; c++) {
		if (p_name == FellowFaceAnimator::channel_name_of(c)) {
			return c;
		}
	}
	return -1;
}
} // namespace

struct FellowFaceAnimator::Track {
	float hop = 0.01f;
	float length = 0.0f; // s analysed
	float stream_length = 0.0f; // the stream's (0: unknown)
	LocalVector<float> frames; // TRACK_STRIDE a hop
	uint64_t usec = 0;
	// The analysis going on.
	Ref<AudioStreamPlayback> playback;
	FellowFaceBiquad filters[5][2];
	float acc[6] = {};
	float prev[6] = {};
	int in_hop = 0;
	int hop_samples = 1;
	int total = 0; // samples to analyse at most
	int done = 0;
	float rate = 44100.0f;
	float floor_db = -45.0f;
	float full_db = -18.0f;
	bool finished = false;
};

HashMap<ObjectID, FellowFaceAnimator::Track *> FellowFaceAnimator::tracks;

// The voice analysed as it is needed: its playback mixed offline at the mix rate (a little ahead of where it is read,
// so its cost is spread over the frames it plays in, about 0.2 ms a frame), filtered into five bands — 200-500 Hz,
// 500-900, 900-1500, 1500-3000 (where the first two formants of the vowels lie) and 4-8 kHz (the hiss of s, f, sh) —
// each a cascade of two band-passes; every 10 ms the level and the bands' energies over the last 20 ms give the
// channels: the jaw opens with the energy at 500-1500 Hz against 200-500 Hz (an open vowel's high first formant), the
// mouth widens with 1500-3000 Hz against 900-1500 Hz (a front vowel's second formant high, the valley below it), rounds
// with the energy below 900 Hz against 900-3000 Hz (a back vowel's second formant low) when not wide, the lower lip goes
// under the teeth with the hiss; all scaled by the level, and the lips close as it falls silent. Shared by every face.
FellowFaceAnimator::Track *FellowFaceAnimator::_track_for(const Ref<AudioStream> &p_stream, float p_max_seconds, float p_floor_db, float p_full_db) {
	if (p_stream.is_null()) {
		return nullptr;
	}
	const ObjectID id = p_stream->get_instance_id();
	if (Track **t = tracks.getptr(id)) {
		return *t;
	}
	if (tracks.size() >= 256) {
		clear_analysis_cache(); // a bound on the cache: analysed again when played again
	}
	Track *track = memnew(Track);
	tracks[id] = track;
	track->playback = p_stream->instantiate_playback();
	track->rate = AudioServer::get_singleton() ? AudioServer::get_singleton()->get_mix_rate() : 44100.0f;
	track->stream_length = float(p_stream->get_length());
	const float seconds = track->stream_length > 0.0f ? MIN(track->stream_length, p_max_seconds) : p_max_seconds;
	track->total = int(seconds * track->rate);
	track->hop_samples = MAX(1, int(track->rate * track->hop));
	track->floor_db = p_floor_db;
	track->full_db = p_full_db;
	track->finished = track->playback.is_null();
	if (track->playback.is_valid()) {
		track->playback->start(0.0);
	}
	static const float bands[5][2] = { { 200, 500 }, { 500, 900 }, { 900, 1500 }, { 1500, 3000 }, { 4000, 8000 } };
	for (int b = 0; b < 5; b++) {
		const float hi = MIN(bands[b][1], track->rate * 0.45f);
		const float center = Math::sqrt(bands[b][0] * hi);
		const float q = center / MAX(1.0f, hi - bands[b][0]);
		track->filters[b][0].bandpass(center, q, track->rate);
		track->filters[b][1].bandpass(center, q, track->rate);
	}
	global_stats.analyses++;
	return track;
}

void FellowFaceAnimator::_extend(Track *p_track, float p_until) {
	if (!p_track || p_track->finished || p_track->length >= p_until) {
		return;
	}
	const uint64_t t0 = OS::get_singleton()->get_ticks_usec();
	Track &track = *p_track;
	const int done_before = track.done;
	const int until = MIN(track.total, int((p_until + track.hop) * track.rate));
	AudioFrame buffer[512];
	while (track.done < until) {
		const int want = MIN(512, until - track.done);
		const int got = track.playback->mix(buffer, 1.0f, want);
		if (got <= 0) {
			track.finished = true;
			break;
		}
		for (int i = 0; i < got; i++) {
			const float x = (buffer[i].left + buffer[i].right) * 0.5f;
			track.acc[5] += x * x;
			for (int b = 0; b < 5; b++) {
				const float y = track.filters[b][1].run(track.filters[b][0].run(x));
				track.acc[b] += y * y;
			}
			if (++track.in_hop < track.hop_samples) {
				continue;
			}
			// The last 20 ms (this hop and the one before).
			float e[6];
			for (int k = 0; k < 6; k++) {
				e[k] = (track.acc[k] + track.prev[k]) / float(2 * track.hop_samples);
				track.prev[k] = track.acc[k];
				track.acc[k] = 0.0f;
			}
			track.in_hop = 0;
			const float level_db = to_db(e[5]) + 3.0f; // a sine's peak level for its mean square
			const float lv = CLAMP((level_db - track.floor_db) / MAX(1.0f, track.full_db - track.floor_db), 0.0f, 1.0f);
			const float open_score = to_db(e[1] + e[2]) - to_db(e[0]);
			const float wide_score = to_db(e[3]) - to_db(e[2]);
			const float low_score = to_db(e[0] + e[1]) - to_db(e[2] + e[3]);
			const float hiss_score = to_db(e[4]) - to_db(e[0] + e[1] + e[2] + e[3]);
			const float hiss = smooth_range(-10.0f, 0.0f, hiss_score);
			const float wide = smooth_range(-6.0f, 2.0f, wide_score);
			float values[TRACK_STRIDE];
			values[CHANNEL_JAW_OPEN] = lv * (0.2f + 0.8f * smooth_range(-8.0f, 8.0f, open_score)) * (1.0f - 0.8f * hiss);
			values[CHANNEL_MOUTH_WIDE] = lv * wide * (1.0f - hiss);
			values[CHANNEL_MOUTH_ROUND] = lv * smooth_range(6.0f, 16.0f, low_score) * (1.0f - wide) * (1.0f - hiss);
			values[CHANNEL_MOUTH_CLOSED] = 1.0f - lv;
			values[CHANNEL_TEETH] = lv * hiss;
			values[5] = level_db;
			values[6] = open_score;
			values[7] = wide_score;
			values[8] = low_score;
			values[9] = hiss_score;
			for (int k = 0; k < TRACK_STRIDE; k++) {
				track.frames.push_back(values[k]);
			}
		}
		track.done += got;
		if (got < want) {
			track.finished = true; // the stream ended
			break;
		}
	}
	if (track.done >= track.total) {
		track.finished = true;
	}
	if (track.finished) {
		track.playback.unref();
	}
	track.length = float(track.frames.size() / TRACK_STRIDE) * track.hop;
	const uint64_t spent = OS::get_singleton()->get_ticks_usec() - t0;
	track.usec += spent;
	global_stats.analysis_usec += spent;
	global_stats.analysed_seconds += double(track.done - done_before) / track.rate;
}

// The channels at p_time (s into the stream), between the hops; silent outside what was analysed (a loop wraps).
void FellowFaceAnimator::_sample(const Track &p_track, float p_time, float r_values[MOUTH_CHANNELS]) {
	const int hops = int(p_track.frames.size() / TRACK_STRIDE);
	for (int c = 0; c < MOUTH_CHANNELS; c++) {
		r_values[c] = c == CHANNEL_MOUTH_CLOSED ? 1.0f : 0.0f;
	}
	if (hops == 0 || p_time < 0.0f) {
		return;
	}
	float t = p_time;
	if (p_track.stream_length > 0.0f && t >= p_track.stream_length && p_track.finished && p_track.length >= p_track.stream_length - 0.02f) {
		t = Math::fmod(t, p_track.stream_length);
	}
	// Hop k covers (k, k + 1] * hop; its value is at its end.
	const float f = t / p_track.hop - 1.0f;
	if (f >= float(hops - 1)) {
		return;
	}
	const int k0 = MAX(0, int(Math::floor(f)));
	const int k1 = MIN(hops - 1, k0 + 1);
	const float w = CLAMP(f - float(k0), 0.0f, 1.0f);
	for (int c = 0; c < MOUTH_CHANNELS; c++) {
		r_values[c] = Math::lerp(p_track.frames[k0 * TRACK_STRIDE + c], p_track.frames[k1 * TRACK_STRIDE + c], w);
	}
}

// Milestone 100: the voice's level (dB) at p_time, as _sample() reads its channels.
float FellowFaceAnimator::_sample_level(const Track &p_track, float p_time) {
	const int hops = int(p_track.frames.size() / TRACK_STRIDE);
	if (hops == 0 || p_time < 0.0f) {
		return -100.0f;
	}
	float t = p_time;
	if (p_track.stream_length > 0.0f && t >= p_track.stream_length && p_track.finished && p_track.length >= p_track.stream_length - 0.02f) {
		t = Math::fmod(t, p_track.stream_length);
	}
	const float f = t / p_track.hop - 1.0f;
	if (f >= float(hops - 1)) {
		return -100.0f;
	}
	const int k0 = MAX(0, int(Math::floor(f)));
	const int k1 = MIN(hops - 1, k0 + 1);
	const float w = CLAMP(f - float(k0), 0.0f, 1.0f);
	return Math::lerp(p_track.frames[k0 * TRACK_STRIDE + 5], p_track.frames[k1 * TRACK_STRIDE + 5], w);
}

bool FellowFaceAnimator::_resolve_expression(const StringName &p_name, LocalVector<Pair<int, float>> &r_values) const {
	r_values.clear();
	const Variant custom = expressions.get(p_name, Variant());
	if (custom.get_type() == Variant::DICTIONARY) {
		const Dictionary d = custom;
		for (const KeyValue<Variant, Variant> &kv : d) {
			const int c = channel_by_name(kv.key);
			if (c >= 0) {
				r_values.push_back(Pair<int, float>(c, CLAMP(float(kv.value), -1.0f, 1.0f)));
			}
		}
		return true;
	}
	for (const ExpressionPreset &preset : EXPRESSION_PRESETS) {
		if (p_name == StringName(preset.name)) {
			for (int i = 0; i < preset.count; i++) {
				r_values.push_back(Pair<int, float>(preset.values[i].channel, preset.values[i].weight));
			}
			return true;
		}
	}
	return false;
}

void FellowFaceAnimator::set_expressions(const Dictionary &p_expressions) {
	expressions = p_expressions;
	for (ExpressionState &e : expression_states) {
		_resolve_expression(e.name, e.values);
	}
}

void FellowFaceAnimator::set_expression(const StringName &p_name, float p_weight, float p_fade) {
	ExpressionState *state = nullptr;
	for (ExpressionState &e : expression_states) {
		if (e.name == p_name) {
			state = &e;
			break;
		}
	}
	if (!state) {
		ExpressionState e;
		e.name = p_name;
		ERR_FAIL_COND_MSG(!_resolve_expression(p_name, e.values), vformat("No expression \"%s\" (built in: happy, sad, angry, surprised, afraid, disgusted; or one in expressions).", p_name));
		expression_states.push_back(e);
		state = &expression_states[expression_states.size() - 1];
	}
	state->target = CLAMP(p_weight, 0.0f, 1.0f);
	state->rate = p_fade > 0.0f ? Math::abs(state->target - state->weight) / p_fade : 0.0f;
}

void FellowFaceAnimator::clear_expressions(float p_fade) {
	for (ExpressionState &e : expression_states) {
		e.target = 0.0f;
		e.rate = p_fade > 0.0f ? e.weight / p_fade : 0.0f;
	}
}

float FellowFaceAnimator::get_expression_weight(const StringName &p_name) const {
	for (const ExpressionState &e : expression_states) {
		if (e.name == p_name) {
			return e.weight;
		}
	}
	return 0.0f;
}

PackedStringArray FellowFaceAnimator::get_expression_names() const {
	PackedStringArray names;
	for (const ExpressionPreset &preset : EXPRESSION_PRESETS) {
		names.push_back(preset.name);
	}
	for (const KeyValue<Variant, Variant> &kv : expressions) {
		if (!names.has(String(kv.key))) {
			names.push_back(kv.key);
		}
	}
	return names;
}

MeshInstance3D *FellowFaceAnimator::_mesh() {
	MeshInstance3D *mi = mesh_path.is_empty() ? nullptr : Object::cast_to<MeshInstance3D>(get_node_or_null(mesh_path));
	if (!mi) {
		mesh_id = ObjectID();
		mesh_cache.unref();
		return nullptr;
	}
	if (mi->get_instance_id() != mesh_id || mi->get_mesh() != mesh_cache) {
		_resolve_shapes(mi);
	}
	return mi;
}

void FellowFaceAnimator::_resolve_shapes(MeshInstance3D *p_mesh) {
	mesh_id = p_mesh->get_instance_id();
	mesh_cache = p_mesh->get_mesh();
	for (int c = 0; c < CHANNEL_MAX; c++) {
		shape_index[c].clear();
		const Variant v = blend_shapes.get(channel_name(c), Variant());
		PackedStringArray names;
		if (v.get_type() == Variant::STRING || v.get_type() == Variant::STRING_NAME) {
			names.push_back(v);
		} else if (v.get_type() == Variant::PACKED_STRING_ARRAY || v.get_type() == Variant::ARRAY) {
			names = v;
		}
		for (const String &n : names) {
			const int idx = p_mesh->find_blend_shape_by_name(n);
			if (idx >= 0) {
				shape_index[c].push_back(idx);
			}
		}
	}
	written.clear();
	written.resize(mesh_cache.is_valid() ? mesh_cache->get_blend_shape_count() : 0);
	for (float &w : written) {
		w = -1.0f;
	}
}

double FellowFaceAnimator::_next_interval() {
	const float lo = MIN(blink_interval_min, blink_interval_max);
	const float hi = MAX(blink_interval_min, blink_interval_max);
	return double(rng.random(lo, hi));
}

void FellowFaceAnimator::_update(double p_delta) {
	const uint64_t t0 = OS::get_singleton()->get_ticks_usec();
	clock += p_delta;
	// The mouth: the voice's channels where its player is (a little ahead).
	float target[MOUTH_CHANNELS] = { 0, 0, 0, 1, 0 };
	float level_db = -100.0f; // milestone 100: the voice's level where the mouth reads it
	bool voice_on = false;
	if (lip_sync && voice_time >= 0.0f && voice_stream.is_valid()) {
		// Driven by a timeline (an animation's track sets voice_time): the stream read there.
		if (Track *track = _track_for(voice_stream, max_analysis_seconds, level_floor_db, level_full_db)) {
			_extend(track, voice_time + anticipation + 0.25f);
			_sample(*track, voice_time + anticipation, target);
			level_db = _sample_level(*track, voice_time + anticipation);
			voice_on = true;
		}
	} else if (lip_sync && !voice_path.is_empty()) {
		Node *voice = get_node_or_null(voice_path);
		Ref<AudioStream> stream;
		bool playing = false;
		float position = 0.0f;
		if (AudioStreamPlayer *p = Object::cast_to<AudioStreamPlayer>(voice)) {
			stream = p->get_stream();
			playing = p->is_playing();
			position = p->get_playback_position();
		} else if (AudioStreamPlayer3D *p3 = Object::cast_to<AudioStreamPlayer3D>(voice)) {
			stream = p3->get_stream();
			playing = p3->is_playing();
			position = p3->get_playback_position();
		} else if (AudioStreamPlayer2D *p2 = Object::cast_to<AudioStreamPlayer2D>(voice)) {
			stream = p2->get_stream();
			playing = p2->is_playing();
			position = p2->get_playback_position();
		}
		if (playing && stream.is_valid()) {
			if (Engine::get_singleton()->get_write_movie_path().is_empty() && AudioServer::get_singleton()) {
				// What is heard now: the mix's position, since the mix and minus the output's latency.
				position += float(AudioServer::get_singleton()->get_time_since_last_mix() - AudioServer::get_singleton()->get_output_latency());
			}
			if (Track *track = _track_for(stream, max_analysis_seconds, level_floor_db, level_full_db)) {
				_extend(track, position + anticipation + 0.25f);
				_sample(*track, position + anticipation, target);
				level_db = _sample_level(*track, position + anticipation);
				voice_on = true;
			}
		}
	}
	for (int c = 0; c < MOUTH_CHANNELS; c++) {
		const float goal = c == CHANNEL_MOUTH_CLOSED ? target[c] : target[c] * strength;
		const float tau = goal > channels[c] ? attack : release;
		const float k = tau > 0.0f ? 1.0f - Math::exp(-float(p_delta) / tau) : 1.0f;
		channels[c] += (goal - channels[c]) * k;
	}
	// Milestone 100 (decision 0208): the brows on the voice's stressed syllables — its level rising speech_emphasis_db
	// above its last half second's (an onset), at most every quarter second; the pulse fades in 0.3 s.
	const float dt = float(p_delta);
	const float rise = voice_on ? level_db - level_slow : -100.0f;
	if (emphasis_armed && voice_on && level_db > level_floor_db + 6.0f && rise > speech_emphasis_db && (last_emphasis < 0.0 || clock - last_emphasis > 0.25)) {
		emphasis = 1.0f;
		last_emphasis = clock;
		emphases++;
		emphasis_armed = false; // once a rise: again after the level has come back near its average (a held vowel is one)
		last_emphasis_strength = CLAMP(rise / (2.0f * speech_emphasis_db), 0.0f, 1.0f);
		emit_signal(SNAME("emphasized"), last_emphasis_strength);
	} else if (rise < speech_emphasis_db * 0.5f) {
		emphasis_armed = true;
	}
	level_slow += ((voice_on ? MAX(level_db, level_floor_db) : level_floor_db) - level_slow) * (1.0f - Math::exp(-dt / 0.5f));
	// Milestone 104 (decision 0212): how much the face is speaking (the voice's level between the floor and full, eased
	// over a quarter second), for the head's motion.
	const float activity = voice_on ? CLAMP((level_db - level_floor_db) / MAX(level_full_db - level_floor_db, 1.0f), 0.0f, 1.0f) : 0.0f;
	speech_activity += (activity - speech_activity) * (1.0f - Math::exp(-dt / 0.25f));
	emphasis *= Math::exp(-dt / 0.3f);
	// The expressions, each faded towards its weight; their channels and the brows' pulse added up.
	for (int i = int(expression_states.size()) - 1; i >= 0; i--) {
		ExpressionState &e = expression_states[i];
		if (e.rate <= 0.0f) {
			e.weight = e.target;
		} else {
			const float step = e.rate * dt;
			e.weight = e.weight < e.target ? MIN(e.target, e.weight + step) : MAX(e.target, e.weight - step);
		}
		if (e.weight <= 0.0f && e.target <= 0.0f) {
			expression_states.remove_at(i);
		}
	}
	for (float &v : extra) {
		v = 0.0f;
	}
	for (const ExpressionState &e : expression_states) {
		for (const Pair<int, float> &cv : e.values) {
			extra[cv.first] += e.weight * cv.second;
		}
	}
	if (speech_brows > 0.0f && emphasis > 1e-3f) {
		extra[CHANNEL_BROW_INNER_UP] += speech_brows * emphasis;
		extra[CHANNEL_BROW_OUTER_UP_LEFT] += speech_brows * 0.6f * emphasis;
		extra[CHANNEL_BROW_OUTER_UP_RIGHT] += speech_brows * 0.6f * emphasis;
	}
	// The eyes: a blink at random intervals (closing in a third of it, held a sixth, opening in half).
	if (blinks && blink_t < 0.0 && clock >= next_blink) {
		blink_t = 0.0;
	}
	float lid = 0.0f;
	if (blink_t >= 0.0) {
		blink_t += p_delta;
		const float u = float(blink_t / blink_duration);
		if (u < 1.0f / 3.0f) {
			lid = smooth_range(0.0f, 1.0f / 3.0f, u);
		} else if (u < 0.5f) {
			lid = 1.0f;
		} else {
			lid = 1.0f - smooth_range(0.5f, 1.0f, u);
		}
		if (u >= 1.0f) {
			blink_t = -1.0;
			lid = 0.0f;
			next_blink = clock + _next_interval();
		}
	}
	channels[CHANNEL_BLINK_LEFT] = lid;
	channels[CHANNEL_BLINK_RIGHT] = lid;
	// Milestone 89: the gaze — towards the target (its direction in the face mesh's space from eye_center, the face
	// looking along +Z, its left +X), within the eyes' range, plus small saccades at random intervals; eased.
	Vector2 goal;
	float vergence = 0.0f;
	if (gaze) {
		const Node3D *target = look_target_path.is_empty() ? nullptr : Object::cast_to<Node3D>(get_node_or_null(look_target_path));
		const MeshInstance3D *face = mesh_path.is_empty() ? nullptr : Object::cast_to<MeshInstance3D>(get_node_or_null(mesh_path));
		if (target && face) {
			const Transform3D xf = face->get_global_transform();
			const Vector3 d = xf.basis.inverse().xform(target->get_global_position() - xf.xform(eye_center));
			if (d.length() > 1e-4f) {
				goal.x = Math::rad_to_deg(Math::atan2(d.x, d.z));
				goal.y = Math::rad_to_deg(Math::atan2(d.y, Vector2(d.x, d.z).length()));
				// Milestone 94 (decision 0202): each eye turned in by half the angle the target subtends between them.
				if (eye_spacing > 0.0f) {
					vergence = Math::rad_to_deg(Math::atan2(eye_spacing * 0.5f, float(d.length())));
				}
			}
		}
		if (saccades && saccade_amplitude > 0.0f) {
			if (clock >= next_saccade) {
				saccade = Vector2(saccade_rng.random(-saccade_amplitude, saccade_amplitude), saccade_rng.random(-saccade_amplitude, saccade_amplitude) * 0.6f);
				next_saccade = clock + saccade_rng.random(MIN(saccade_interval_min, saccade_interval_max), MAX(saccade_interval_min, saccade_interval_max));
			}
			goal += saccade;
		}
		goal.x = CLAMP(goal.x, -gaze_range_horizontal, gaze_range_horizontal);
		goal.y = CLAMP(goal.y, -gaze_range_vertical, gaze_range_vertical);
	}
	const float gk = gaze_time > 0.0f ? 1.0f - Math::exp(-float(p_delta) / gaze_time) : 1.0f;
	gaze_now += (goal - gaze_now) * gk;
	vergence_now += (vergence - vergence_now) * gk;
	// Each eye's yaw (+ the character's left): the left eye turned in (to its right) by the vergence, the right eye in
	// (to its left).
	const float yaw_left = CLAMP(gaze_now.x - vergence_now, -gaze_range_horizontal, gaze_range_horizontal) / gaze_range_horizontal;
	const float yaw_right = CLAMP(gaze_now.x + vergence_now, -gaze_range_horizontal, gaze_range_horizontal) / gaze_range_horizontal;
	const float pitch = gaze_now.y / gaze_range_vertical;
	channels[CHANNEL_LOOK_OUT_LEFT] = MAX(0.0f, yaw_left);
	channels[CHANNEL_LOOK_IN_RIGHT] = MAX(0.0f, yaw_right);
	channels[CHANNEL_LOOK_IN_LEFT] = MAX(0.0f, -yaw_left);
	channels[CHANNEL_LOOK_OUT_RIGHT] = MAX(0.0f, -yaw_right);
	channels[CHANNEL_LOOK_UP_LEFT] = channels[CHANNEL_LOOK_UP_RIGHT] = MAX(0.0f, pitch);
	channels[CHANNEL_LOOK_DOWN_LEFT] = channels[CHANNEL_LOOK_DOWN_RIGHT] = MAX(0.0f, -pitch);
	// The face: each blend shape the sum of its channels' (at most 1), written when it changed.
	if (MeshInstance3D *mi = _mesh()) {
		LocalVector<float> values;
		values.resize(written.size());
		for (float &v : values) {
			v = 0.0f;
		}
		for (int c = 0; c < CHANNEL_MAX; c++) {
			const float cv = CLAMP(channels[c] + extra[c], 0.0f, 1.0f);
			for (int idx : shape_index[c]) {
				if (idx < int(values.size())) {
					values[idx] += cv;
				}
			}
		}
		for (int c = 0; c < CHANNEL_MAX; c++) {
			for (int idx : shape_index[c]) {
				if (idx >= int(values.size())) {
					continue;
				}
				const float v = CLAMP(values[idx], 0.0f, 1.0f);
				if (Math::abs(v - written[idx]) > 1e-4f) {
					mi->set_blend_shape_value(idx, v);
					written[idx] = v;
				}
			}
		}
	}
	frames++;
	const uint64_t spent = OS::get_singleton()->get_ticks_usec() - t0;
	usec += spent;
	global_stats.frames++;
	global_stats.usec += spent;
}

void FellowFaceAnimator::blink() {
	if (blink_t < 0.0) {
		blink_t = 0.0;
	}
}

float FellowFaceAnimator::get_channel(Channel p_channel) const {
	ERR_FAIL_INDEX_V(int(p_channel), int(CHANNEL_MAX), 0.0f);
	return CLAMP(channels[p_channel] + extra[p_channel], 0.0f, 1.0f);
}

Dictionary FellowFaceAnimator::get_channels() const {
	Dictionary d;
	for (int c = 0; c < CHANNEL_MAX; c++) {
		d[channel_name(c)] = CLAMP(channels[c] + extra[c], 0.0f, 1.0f);
	}
	return d;
}

Dictionary FellowFaceAnimator::sample_voice(const Ref<AudioStream> &p_stream, float p_time) const {
	Dictionary d;
	Track *track = _track_for(p_stream, max_analysis_seconds, level_floor_db, level_full_db);
	if (!track) {
		return d;
	}
	_extend(track, p_time + 0.05f);
	float values[MOUTH_CHANNELS];
	_sample(*track, p_time, values);
	for (int c = 0; c < MOUTH_CHANNELS; c++) {
		d[channel_name(c)] = values[c];
	}
	const int hops = int(track->frames.size() / TRACK_STRIDE);
	const int k = CLAMP(int(p_time / track->hop) - 1, 0, MAX(0, hops - 1));
	if (hops > 0) {
		d["level_db"] = track->frames[k * TRACK_STRIDE + 5];
		d["open_score"] = track->frames[k * TRACK_STRIDE + 6];
		d["wide_score"] = track->frames[k * TRACK_STRIDE + 7];
		d["low_score"] = track->frames[k * TRACK_STRIDE + 8];
		d["hiss_score"] = track->frames[k * TRACK_STRIDE + 9];
	}
	d["analysed_seconds"] = track->length;
	d["analysis_usec"] = int64_t(track->usec);
	return d;
}

void FellowFaceAnimator::set_mesh_path(const NodePath &p_path) {
	mesh_path = p_path;
	mesh_id = ObjectID();
	mesh_cache.unref();
	update_configuration_warnings();
}

void FellowFaceAnimator::set_blend_shapes(const Dictionary &p_shapes) {
	blend_shapes = p_shapes;
	mesh_id = ObjectID();
	mesh_cache.unref();
}

void FellowFaceAnimator::set_seed(int64_t p_seed) {
	seed = p_seed;
	rng.seed(uint64_t(seed));
	saccade_rng.seed(uint64_t(seed) + 1);
	next_blink = clock + _next_interval();
	next_saccade = clock;
}

Dictionary FellowFaceAnimator::get_stats() const {
	Dictionary d;
	d["frames"] = int64_t(frames);
	d["usec"] = int64_t(usec);
	d["emphases"] = int64_t(emphases); // milestone 100
	d["expressions"] = int(expression_states.size());
	return d;
}

Dictionary FellowFaceAnimator::get_global_stats() {
	Dictionary d;
	d["frames"] = int64_t(global_stats.frames);
	d["usec"] = int64_t(global_stats.usec);
	d["analyses"] = int64_t(global_stats.analyses);
	d["analysis_usec"] = int64_t(global_stats.analysis_usec);
	d["analysed_seconds"] = global_stats.analysed_seconds;
	d["cached_voices"] = tracks.size();
	return d;
}

void FellowFaceAnimator::reset_global_stats() {
	global_stats = GlobalStats();
}

void FellowFaceAnimator::clear_analysis_cache() {
	for (KeyValue<ObjectID, Track *> &kv : tracks) {
		memdelete(kv.value);
	}
	tracks.clear();
}

PackedStringArray FellowFaceAnimator::get_configuration_warnings() const {
	PackedStringArray warnings = Node::get_configuration_warnings();
	if (mesh_path.is_empty() || !Object::cast_to<MeshInstance3D>(get_node_or_null(mesh_path))) {
		warnings.push_back(RTR("A FellowFaceAnimator moves a face mesh's blend shapes: set mesh_path to a MeshInstance3D."));
	}
	return warnings;
}

void FellowFaceAnimator::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_READY: {
			if (!Engine::get_singleton()->is_editor_hint()) {
				set_process_internal(true);
			}
		} break;
		case NOTIFICATION_INTERNAL_PROCESS: {
			_update(get_process_delta_time());
		} break;
	}
}

void FellowFaceAnimator::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_mesh_path", "path"), &FellowFaceAnimator::set_mesh_path);
	ClassDB::bind_method(D_METHOD("get_mesh_path"), &FellowFaceAnimator::get_mesh_path);
	ClassDB::bind_method(D_METHOD("set_voice_path", "path"), &FellowFaceAnimator::set_voice_path);
	ClassDB::bind_method(D_METHOD("get_voice_path"), &FellowFaceAnimator::get_voice_path);
	ClassDB::bind_method(D_METHOD("set_voice_stream", "stream"), &FellowFaceAnimator::set_voice_stream);
	ClassDB::bind_method(D_METHOD("get_voice_stream"), &FellowFaceAnimator::get_voice_stream);
	ClassDB::bind_method(D_METHOD("set_voice_time", "time"), &FellowFaceAnimator::set_voice_time);
	ClassDB::bind_method(D_METHOD("get_voice_time"), &FellowFaceAnimator::get_voice_time);
	ClassDB::bind_method(D_METHOD("set_blend_shapes", "shapes"), &FellowFaceAnimator::set_blend_shapes);
	ClassDB::bind_method(D_METHOD("get_blend_shapes"), &FellowFaceAnimator::get_blend_shapes);
	ClassDB::bind_method(D_METHOD("set_lip_sync", "enabled"), &FellowFaceAnimator::set_lip_sync);
	ClassDB::bind_method(D_METHOD("is_lip_sync"), &FellowFaceAnimator::is_lip_sync);
	ClassDB::bind_method(D_METHOD("set_strength", "strength"), &FellowFaceAnimator::set_strength);
	ClassDB::bind_method(D_METHOD("get_strength"), &FellowFaceAnimator::get_strength);
	ClassDB::bind_method(D_METHOD("set_anticipation", "seconds"), &FellowFaceAnimator::set_anticipation);
	ClassDB::bind_method(D_METHOD("get_anticipation"), &FellowFaceAnimator::get_anticipation);
	ClassDB::bind_method(D_METHOD("set_attack", "seconds"), &FellowFaceAnimator::set_attack);
	ClassDB::bind_method(D_METHOD("get_attack"), &FellowFaceAnimator::get_attack);
	ClassDB::bind_method(D_METHOD("set_release", "seconds"), &FellowFaceAnimator::set_release);
	ClassDB::bind_method(D_METHOD("get_release"), &FellowFaceAnimator::get_release);
	ClassDB::bind_method(D_METHOD("set_level_floor_db", "db"), &FellowFaceAnimator::set_level_floor_db);
	ClassDB::bind_method(D_METHOD("get_level_floor_db"), &FellowFaceAnimator::get_level_floor_db);
	ClassDB::bind_method(D_METHOD("set_level_full_db", "db"), &FellowFaceAnimator::set_level_full_db);
	ClassDB::bind_method(D_METHOD("get_level_full_db"), &FellowFaceAnimator::get_level_full_db);
	ClassDB::bind_method(D_METHOD("set_max_analysis_seconds", "seconds"), &FellowFaceAnimator::set_max_analysis_seconds);
	ClassDB::bind_method(D_METHOD("get_max_analysis_seconds"), &FellowFaceAnimator::get_max_analysis_seconds);
	ClassDB::bind_method(D_METHOD("set_blinks", "enabled"), &FellowFaceAnimator::set_blinks);
	ClassDB::bind_method(D_METHOD("is_blinks"), &FellowFaceAnimator::is_blinks);
	ClassDB::bind_method(D_METHOD("set_blink_interval_min", "seconds"), &FellowFaceAnimator::set_blink_interval_min);
	ClassDB::bind_method(D_METHOD("get_blink_interval_min"), &FellowFaceAnimator::get_blink_interval_min);
	ClassDB::bind_method(D_METHOD("set_blink_interval_max", "seconds"), &FellowFaceAnimator::set_blink_interval_max);
	ClassDB::bind_method(D_METHOD("get_blink_interval_max"), &FellowFaceAnimator::get_blink_interval_max);
	ClassDB::bind_method(D_METHOD("set_blink_duration", "seconds"), &FellowFaceAnimator::set_blink_duration);
	ClassDB::bind_method(D_METHOD("get_blink_duration"), &FellowFaceAnimator::get_blink_duration);
	ClassDB::bind_method(D_METHOD("set_seed", "seed"), &FellowFaceAnimator::set_seed);
	ClassDB::bind_method(D_METHOD("get_seed"), &FellowFaceAnimator::get_seed);
	ClassDB::bind_method(D_METHOD("set_gaze", "enabled"), &FellowFaceAnimator::set_gaze);
	ClassDB::bind_method(D_METHOD("is_gaze"), &FellowFaceAnimator::is_gaze);
	ClassDB::bind_method(D_METHOD("set_look_target_path", "path"), &FellowFaceAnimator::set_look_target_path);
	ClassDB::bind_method(D_METHOD("get_look_target_path"), &FellowFaceAnimator::get_look_target_path);
	ClassDB::bind_method(D_METHOD("set_eye_center", "center"), &FellowFaceAnimator::set_eye_center);
	ClassDB::bind_method(D_METHOD("get_eye_center"), &FellowFaceAnimator::get_eye_center);
	ClassDB::bind_method(D_METHOD("set_gaze_range_horizontal", "degrees"), &FellowFaceAnimator::set_gaze_range_horizontal);
	ClassDB::bind_method(D_METHOD("get_gaze_range_horizontal"), &FellowFaceAnimator::get_gaze_range_horizontal);
	ClassDB::bind_method(D_METHOD("set_gaze_range_vertical", "degrees"), &FellowFaceAnimator::set_gaze_range_vertical);
	ClassDB::bind_method(D_METHOD("get_gaze_range_vertical"), &FellowFaceAnimator::get_gaze_range_vertical);
	ClassDB::bind_method(D_METHOD("set_gaze_time", "seconds"), &FellowFaceAnimator::set_gaze_time);
	ClassDB::bind_method(D_METHOD("get_gaze_time"), &FellowFaceAnimator::get_gaze_time);
	ClassDB::bind_method(D_METHOD("set_saccades", "enabled"), &FellowFaceAnimator::set_saccades);
	ClassDB::bind_method(D_METHOD("is_saccades"), &FellowFaceAnimator::is_saccades);
	ClassDB::bind_method(D_METHOD("set_saccade_amplitude", "degrees"), &FellowFaceAnimator::set_saccade_amplitude);
	ClassDB::bind_method(D_METHOD("get_saccade_amplitude"), &FellowFaceAnimator::get_saccade_amplitude);
	ClassDB::bind_method(D_METHOD("get_gaze_angles"), &FellowFaceAnimator::get_gaze_angles);
	ClassDB::bind_method(D_METHOD("set_eye_spacing", "spacing"), &FellowFaceAnimator::set_eye_spacing);
	ClassDB::bind_method(D_METHOD("get_eye_spacing"), &FellowFaceAnimator::get_eye_spacing);
	ClassDB::bind_method(D_METHOD("get_vergence"), &FellowFaceAnimator::get_vergence);
	ClassDB::bind_method(D_METHOD("blink"), &FellowFaceAnimator::blink);
	ClassDB::bind_method(D_METHOD("set_expressions", "expressions"), &FellowFaceAnimator::set_expressions);
	ClassDB::bind_method(D_METHOD("get_expressions"), &FellowFaceAnimator::get_expressions);
	ClassDB::bind_method(D_METHOD("set_speech_brows", "amount"), &FellowFaceAnimator::set_speech_brows);
	ClassDB::bind_method(D_METHOD("get_speech_brows"), &FellowFaceAnimator::get_speech_brows);
	ClassDB::bind_method(D_METHOD("set_speech_emphasis_db", "db"), &FellowFaceAnimator::set_speech_emphasis_db);
	ClassDB::bind_method(D_METHOD("get_speech_emphasis_db"), &FellowFaceAnimator::get_speech_emphasis_db);
	ClassDB::bind_method(D_METHOD("set_expression", "name", "weight", "fade"), &FellowFaceAnimator::set_expression, DEFVAL(1.0f), DEFVAL(0.3f));
	ClassDB::bind_method(D_METHOD("clear_expressions", "fade"), &FellowFaceAnimator::clear_expressions, DEFVAL(0.3f));
	ClassDB::bind_method(D_METHOD("get_expression_weight", "name"), &FellowFaceAnimator::get_expression_weight);
	ClassDB::bind_method(D_METHOD("get_expression_names"), &FellowFaceAnimator::get_expression_names);
	ClassDB::bind_method(D_METHOD("get_emphasis"), &FellowFaceAnimator::get_emphasis);
	ClassDB::bind_method(D_METHOD("get_emphasis_count"), &FellowFaceAnimator::get_emphasis_count);
	ClassDB::bind_method(D_METHOD("get_last_emphasis_strength"), &FellowFaceAnimator::get_last_emphasis_strength);
	ClassDB::bind_method(D_METHOD("get_speech_activity"), &FellowFaceAnimator::get_speech_activity);
	ClassDB::bind_method(D_METHOD("get_channel", "channel"), &FellowFaceAnimator::get_channel);
	ClassDB::bind_method(D_METHOD("get_channels"), &FellowFaceAnimator::get_channels);
	ClassDB::bind_method(D_METHOD("sample_voice", "stream", "time"), &FellowFaceAnimator::sample_voice);
	ClassDB::bind_method(D_METHOD("get_stats"), &FellowFaceAnimator::get_stats);
	ClassDB::bind_static_method("FellowFaceAnimator", D_METHOD("get_global_stats"), &FellowFaceAnimator::get_global_stats);
	ClassDB::bind_static_method("FellowFaceAnimator", D_METHOD("reset_global_stats"), &FellowFaceAnimator::reset_global_stats);
	ClassDB::bind_static_method("FellowFaceAnimator", D_METHOD("clear_analysis_cache"), &FellowFaceAnimator::clear_analysis_cache);

	ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "mesh_path", PROPERTY_HINT_NODE_PATH_VALID_TYPES, "MeshInstance3D"), "set_mesh_path", "get_mesh_path");
	ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "voice_path", PROPERTY_HINT_NODE_PATH_VALID_TYPES, "AudioStreamPlayer,AudioStreamPlayer2D,AudioStreamPlayer3D"), "set_voice_path", "get_voice_path");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "voice_stream", PROPERTY_HINT_RESOURCE_TYPE, "AudioStream"), "set_voice_stream", "get_voice_stream");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "voice_time", PROPERTY_HINT_RANGE, "-1,600,0.001,or_greater,suffix:s"), "set_voice_time", "get_voice_time");
	ADD_PROPERTY(PropertyInfo(Variant::DICTIONARY, "blend_shapes"), "set_blend_shapes", "get_blend_shapes");
	ADD_GROUP("Lip Sync", "");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "lip_sync"), "set_lip_sync", "is_lip_sync");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "strength", PROPERTY_HINT_RANGE, "0,2,0.01"), "set_strength", "get_strength");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "anticipation", PROPERTY_HINT_RANGE, "-0.2,0.3,0.001,suffix:s"), "set_anticipation", "get_anticipation");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "attack", PROPERTY_HINT_RANGE, "0,0.5,0.001,suffix:s"), "set_attack", "get_attack");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "release", PROPERTY_HINT_RANGE, "0,0.5,0.001,suffix:s"), "set_release", "get_release");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "level_floor_db", PROPERTY_HINT_RANGE, "-80,0,0.1,suffix:dB"), "set_level_floor_db", "get_level_floor_db");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "level_full_db", PROPERTY_HINT_RANGE, "-80,0,0.1,suffix:dB"), "set_level_full_db", "get_level_full_db");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "max_analysis_seconds", PROPERTY_HINT_RANGE, "1,600,1,suffix:s"), "set_max_analysis_seconds", "get_max_analysis_seconds");
	ADD_GROUP("Blinks", "");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "blinks"), "set_blinks", "is_blinks");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "blink_interval_min", PROPERTY_HINT_RANGE, "0.1,30,0.01,suffix:s"), "set_blink_interval_min", "get_blink_interval_min");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "blink_interval_max", PROPERTY_HINT_RANGE, "0.1,30,0.01,suffix:s"), "set_blink_interval_max", "get_blink_interval_max");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "blink_duration", PROPERTY_HINT_RANGE, "0.05,1,0.001,suffix:s"), "set_blink_duration", "get_blink_duration");
	ADD_GROUP("Gaze", "");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "gaze"), "set_gaze", "is_gaze");
	ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "look_target_path", PROPERTY_HINT_NODE_PATH_VALID_TYPES, "Node3D"), "set_look_target_path", "get_look_target_path");
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR3, "eye_center", PROPERTY_HINT_NONE, "suffix:m"), "set_eye_center", "get_eye_center");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "eye_spacing", PROPERTY_HINT_RANGE, "0,0.5,0.001,suffix:m"), "set_eye_spacing", "get_eye_spacing");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "gaze_range_horizontal", PROPERTY_HINT_RANGE, "1,89,0.1,degrees"), "set_gaze_range_horizontal", "get_gaze_range_horizontal");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "gaze_range_vertical", PROPERTY_HINT_RANGE, "1,89,0.1,degrees"), "set_gaze_range_vertical", "get_gaze_range_vertical");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "gaze_time", PROPERTY_HINT_RANGE, "0,1,0.001,suffix:s"), "set_gaze_time", "get_gaze_time");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "saccades"), "set_saccades", "is_saccades");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "saccade_amplitude", PROPERTY_HINT_RANGE, "0,20,0.1,degrees"), "set_saccade_amplitude", "get_saccade_amplitude");
	ADD_GROUP("Expressions", "");
	ADD_PROPERTY(PropertyInfo(Variant::DICTIONARY, "expressions"), "set_expressions", "get_expressions");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "speech_brows", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_speech_brows", "get_speech_brows");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "speech_emphasis_db", PROPERTY_HINT_RANGE, "1,30,0.1,suffix:dB"), "set_speech_emphasis_db", "get_speech_emphasis_db");
	ADD_GROUP("", "");
	ADD_SIGNAL(MethodInfo("emphasized", PropertyInfo(Variant::FLOAT, "strength")));
	ADD_PROPERTY(PropertyInfo(Variant::INT, "seed"), "set_seed", "get_seed");

	BIND_ENUM_CONSTANT(CHANNEL_JAW_OPEN);
	BIND_ENUM_CONSTANT(CHANNEL_MOUTH_WIDE);
	BIND_ENUM_CONSTANT(CHANNEL_MOUTH_ROUND);
	BIND_ENUM_CONSTANT(CHANNEL_MOUTH_CLOSED);
	BIND_ENUM_CONSTANT(CHANNEL_TEETH);
	BIND_ENUM_CONSTANT(CHANNEL_BLINK_LEFT);
	BIND_ENUM_CONSTANT(CHANNEL_BLINK_RIGHT);
	BIND_ENUM_CONSTANT(CHANNEL_LOOK_OUT_LEFT);
	BIND_ENUM_CONSTANT(CHANNEL_LOOK_IN_LEFT);
	BIND_ENUM_CONSTANT(CHANNEL_LOOK_UP_LEFT);
	BIND_ENUM_CONSTANT(CHANNEL_LOOK_DOWN_LEFT);
	BIND_ENUM_CONSTANT(CHANNEL_LOOK_OUT_RIGHT);
	BIND_ENUM_CONSTANT(CHANNEL_LOOK_IN_RIGHT);
	BIND_ENUM_CONSTANT(CHANNEL_LOOK_UP_RIGHT);
	BIND_ENUM_CONSTANT(CHANNEL_LOOK_DOWN_RIGHT);
	BIND_ENUM_CONSTANT(CHANNEL_BROW_INNER_UP);
	BIND_ENUM_CONSTANT(CHANNEL_BROW_DOWN_LEFT);
	BIND_ENUM_CONSTANT(CHANNEL_BROW_DOWN_RIGHT);
	BIND_ENUM_CONSTANT(CHANNEL_BROW_OUTER_UP_LEFT);
	BIND_ENUM_CONSTANT(CHANNEL_BROW_OUTER_UP_RIGHT);
	BIND_ENUM_CONSTANT(CHANNEL_SMILE_LEFT);
	BIND_ENUM_CONSTANT(CHANNEL_SMILE_RIGHT);
	BIND_ENUM_CONSTANT(CHANNEL_FROWN_LEFT);
	BIND_ENUM_CONSTANT(CHANNEL_FROWN_RIGHT);
	BIND_ENUM_CONSTANT(CHANNEL_CHEEK_SQUINT_LEFT);
	BIND_ENUM_CONSTANT(CHANNEL_CHEEK_SQUINT_RIGHT);
	BIND_ENUM_CONSTANT(CHANNEL_EYE_SQUINT_LEFT);
	BIND_ENUM_CONSTANT(CHANNEL_EYE_SQUINT_RIGHT);
	BIND_ENUM_CONSTANT(CHANNEL_EYE_WIDE_LEFT);
	BIND_ENUM_CONSTANT(CHANNEL_EYE_WIDE_RIGHT);
	BIND_ENUM_CONSTANT(CHANNEL_NOSE_SNEER_LEFT);
	BIND_ENUM_CONSTANT(CHANNEL_NOSE_SNEER_RIGHT);
	BIND_ENUM_CONSTANT(CHANNEL_MAX);
}

FellowFaceAnimator::FellowFaceAnimator() {
	// ARKit's names (Apple's 52 face blend shapes, which many characters' faces carry).
	blend_shapes["jaw_open"] = PackedStringArray({ "jawOpen" });
	blend_shapes["mouth_wide"] = PackedStringArray({ "mouthStretchLeft", "mouthStretchRight" });
	blend_shapes["mouth_round"] = PackedStringArray({ "mouthPucker", "mouthFunnel" });
	blend_shapes["mouth_closed"] = PackedStringArray();
	blend_shapes["teeth"] = PackedStringArray({ "mouthRollLower" });
	blend_shapes["blink_left"] = PackedStringArray({ "eyeBlinkLeft" });
	blend_shapes["blink_right"] = PackedStringArray({ "eyeBlinkRight" });
	blend_shapes["look_out_left"] = PackedStringArray({ "eyeLookOutLeft" });
	blend_shapes["look_in_left"] = PackedStringArray({ "eyeLookInLeft" });
	blend_shapes["look_up_left"] = PackedStringArray({ "eyeLookUpLeft" });
	blend_shapes["look_down_left"] = PackedStringArray({ "eyeLookDownLeft" });
	blend_shapes["look_out_right"] = PackedStringArray({ "eyeLookOutRight" });
	blend_shapes["look_in_right"] = PackedStringArray({ "eyeLookInRight" });
	blend_shapes["look_up_right"] = PackedStringArray({ "eyeLookUpRight" });
	blend_shapes["look_down_right"] = PackedStringArray({ "eyeLookDownRight" });
	blend_shapes["brow_inner_up"] = PackedStringArray({ "browInnerUp" });
	blend_shapes["brow_down_left"] = PackedStringArray({ "browDownLeft" });
	blend_shapes["brow_down_right"] = PackedStringArray({ "browDownRight" });
	blend_shapes["brow_outer_up_left"] = PackedStringArray({ "browOuterUpLeft" });
	blend_shapes["brow_outer_up_right"] = PackedStringArray({ "browOuterUpRight" });
	blend_shapes["smile_left"] = PackedStringArray({ "mouthSmileLeft" });
	blend_shapes["smile_right"] = PackedStringArray({ "mouthSmileRight" });
	blend_shapes["frown_left"] = PackedStringArray({ "mouthFrownLeft" });
	blend_shapes["frown_right"] = PackedStringArray({ "mouthFrownRight" });
	blend_shapes["cheek_squint_left"] = PackedStringArray({ "cheekSquintLeft" });
	blend_shapes["cheek_squint_right"] = PackedStringArray({ "cheekSquintRight" });
	blend_shapes["eye_squint_left"] = PackedStringArray({ "eyeSquintLeft" });
	blend_shapes["eye_squint_right"] = PackedStringArray({ "eyeSquintRight" });
	blend_shapes["eye_wide_left"] = PackedStringArray({ "eyeWideLeft" });
	blend_shapes["eye_wide_right"] = PackedStringArray({ "eyeWideRight" });
	blend_shapes["nose_sneer_left"] = PackedStringArray({ "noseSneerLeft" });
	blend_shapes["nose_sneer_right"] = PackedStringArray({ "noseSneerRight" });
	rng.seed(0);
	saccade_rng.seed(1);
	next_blink = _next_interval();
}
