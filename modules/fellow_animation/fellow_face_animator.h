/**************************************************************************/
/*  fellow_face_animator.h                                                */
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

#include "core/math/random_pcg.h"
#include "core/templates/hash_map.h"
#include "core/templates/local_vector.h"
#include "scene/main/node.h"
#include "scene/resources/mesh.h"
#include "servers/audio/audio_stream.h"

class MeshInstance3D;

// FellowEngine (milestone 83, decision 0191): a face that speaks and blinks. The voice a player plays is analysed
// ahead (its stream rendered once, shared by every face: its level and the energy in five bands every 10 ms); each
// frame the mouth's channels — the jaw open, the mouth wide, round, closed, the lower lip under the teeth — are read at
// the player's position (a little ahead: the mouth leads the sound), eased, and set on the face mesh's blend shapes
// (ARKit's names by default); the eyes blink at random intervals.
class FellowFaceAnimator : public Node {
	GDCLASS(FellowFaceAnimator, Node);

public:
	enum Channel {
		CHANNEL_JAW_OPEN,
		CHANNEL_MOUTH_WIDE,
		CHANNEL_MOUTH_ROUND,
		CHANNEL_MOUTH_CLOSED,
		CHANNEL_TEETH,
		CHANNEL_BLINK_LEFT,
		CHANNEL_BLINK_RIGHT,
		// Milestone 89: the gaze (each eye out, in, up, down; "left" the character's).
		CHANNEL_LOOK_OUT_LEFT,
		CHANNEL_LOOK_IN_LEFT,
		CHANNEL_LOOK_UP_LEFT,
		CHANNEL_LOOK_DOWN_LEFT,
		CHANNEL_LOOK_OUT_RIGHT,
		CHANNEL_LOOK_IN_RIGHT,
		CHANNEL_LOOK_UP_RIGHT,
		CHANNEL_LOOK_DOWN_RIGHT,
		CHANNEL_MAX,
	};
	static constexpr int MOUTH_CHANNELS = 5;

private:
	// A voice's analysis, made as it is needed (a little ahead of where it is read): the mouth's channels every hop.
	struct Track;
	static HashMap<ObjectID, Track *> tracks;
	struct GlobalStats {
		uint64_t frames = 0;
		uint64_t usec = 0;
		uint64_t analyses = 0;
		uint64_t analysis_usec = 0;
		double analysed_seconds = 0.0;
	};
	static GlobalStats global_stats;

	NodePath mesh_path;
	NodePath voice_path;
	Ref<AudioStream> voice_stream; // with voice_time >= 0: read at that time instead of a player's
	float voice_time = -1.0f;
	Dictionary blend_shapes; // channel name -> PackedStringArray of blend shape names
	bool lip_sync = true;
	float strength = 1.0f;
	float anticipation = 0.04f; // s: the mouth reads the voice this far ahead
	float attack = 0.04f; // s
	float release = 0.08f; // s
	float level_floor_db = -45.0f;
	float level_full_db = -18.0f;
	float max_analysis_seconds = 60.0f;
	bool blinks = true;
	float blink_interval_min = 2.0f;
	float blink_interval_max = 6.0f;
	float blink_duration = 0.18f;
	// Milestone 89 (decision 0197): the gaze.
	bool gaze = true;
	NodePath look_target_path;
	Vector3 eye_center; // in the face mesh's space
	float gaze_range_horizontal = 30.0f; // degrees: the eyes fully out or in
	float gaze_range_vertical = 25.0f;
	float gaze_time = 0.04f; // s: the eyes' easing (a saccade is fast)
	bool saccades = true;
	float saccade_amplitude = 1.5f; // degrees
	float saccade_interval_min = 0.4f;
	float saccade_interval_max = 1.6f;
	int64_t seed = 0;

	float channels[CHANNEL_MAX] = {};
	RandomPCG rng;
	double next_blink = 0.0;
	double next_saccade = 0.0;
	RandomPCG saccade_rng; // its own: the blinks' intervals the same with or without saccades
	Vector2 saccade; // degrees (yaw, pitch)
	Vector2 gaze_now; // degrees, eased
	double blink_t = -1.0; // s into a blink, < 0: none
	double clock = 0.0;
	ObjectID mesh_id;
	Ref<Mesh> mesh_cache;
	LocalVector<int> shape_index[CHANNEL_MAX];
	LocalVector<float> written; // the blend shapes' values last written (per index)
	uint64_t frames = 0;
	uint64_t usec = 0;

	static const char *channel_name(int p_channel);
	static Track *_track_for(const Ref<AudioStream> &p_stream, float p_max_seconds, float p_floor_db, float p_full_db);
	static void _extend(Track *p_track, float p_until);
	static void _sample(const Track &p_track, float p_time, float r_values[MOUTH_CHANNELS]);
	MeshInstance3D *_mesh();
	void _resolve_shapes(MeshInstance3D *p_mesh);
	void _update(double p_delta);
	double _next_interval();

protected:
	void _notification(int p_what);
	static void _bind_methods();

public:
	void set_mesh_path(const NodePath &p_path);
	NodePath get_mesh_path() const { return mesh_path; }
	void set_voice_path(const NodePath &p_path) { voice_path = p_path; }
	NodePath get_voice_path() const { return voice_path; }
	void set_voice_stream(const Ref<AudioStream> &p_stream) { voice_stream = p_stream; }
	Ref<AudioStream> get_voice_stream() const { return voice_stream; }
	void set_voice_time(float p_time) { voice_time = p_time; }
	float get_voice_time() const { return voice_time; }
	void set_blend_shapes(const Dictionary &p_shapes);
	Dictionary get_blend_shapes() const { return blend_shapes; }
	void set_lip_sync(bool p_enabled) { lip_sync = p_enabled; }
	bool is_lip_sync() const { return lip_sync; }
	void set_strength(float p_strength) { strength = CLAMP(p_strength, 0.0f, 2.0f); }
	float get_strength() const { return strength; }
	void set_anticipation(float p_seconds) { anticipation = CLAMP(p_seconds, -0.2f, 0.3f); }
	float get_anticipation() const { return anticipation; }
	void set_attack(float p_seconds) { attack = MAX(0.0f, p_seconds); }
	float get_attack() const { return attack; }
	void set_release(float p_seconds) { release = MAX(0.0f, p_seconds); }
	float get_release() const { return release; }
	void set_level_floor_db(float p_db) { level_floor_db = p_db; }
	float get_level_floor_db() const { return level_floor_db; }
	void set_level_full_db(float p_db) { level_full_db = p_db; }
	float get_level_full_db() const { return level_full_db; }
	void set_max_analysis_seconds(float p_seconds) { max_analysis_seconds = CLAMP(p_seconds, 1.0f, 600.0f); }
	float get_max_analysis_seconds() const { return max_analysis_seconds; }
	void set_blinks(bool p_enabled) { blinks = p_enabled; }
	bool is_blinks() const { return blinks; }
	void set_blink_interval_min(float p_seconds) { blink_interval_min = MAX(0.1f, p_seconds); }
	float get_blink_interval_min() const { return blink_interval_min; }
	void set_blink_interval_max(float p_seconds) { blink_interval_max = MAX(0.1f, p_seconds); }
	float get_blink_interval_max() const { return blink_interval_max; }
	void set_blink_duration(float p_seconds) { blink_duration = CLAMP(p_seconds, 0.05f, 1.0f); }
	float get_blink_duration() const { return blink_duration; }
	void set_seed(int64_t p_seed);
	int64_t get_seed() const { return seed; }

	void set_gaze(bool p_enabled) { gaze = p_enabled; }
	bool is_gaze() const { return gaze; }
	void set_look_target_path(const NodePath &p_path) { look_target_path = p_path; }
	NodePath get_look_target_path() const { return look_target_path; }
	void set_eye_center(const Vector3 &p_center) { eye_center = p_center; }
	Vector3 get_eye_center() const { return eye_center; }
	void set_gaze_range_horizontal(float p_degrees) { gaze_range_horizontal = CLAMP(p_degrees, 1.0f, 89.0f); }
	float get_gaze_range_horizontal() const { return gaze_range_horizontal; }
	void set_gaze_range_vertical(float p_degrees) { gaze_range_vertical = CLAMP(p_degrees, 1.0f, 89.0f); }
	float get_gaze_range_vertical() const { return gaze_range_vertical; }
	void set_gaze_time(float p_seconds) { gaze_time = MAX(0.0f, p_seconds); }
	float get_gaze_time() const { return gaze_time; }
	void set_saccades(bool p_enabled) { saccades = p_enabled; }
	bool is_saccades() const { return saccades; }
	void set_saccade_amplitude(float p_degrees) { saccade_amplitude = CLAMP(p_degrees, 0.0f, 20.0f); }
	float get_saccade_amplitude() const { return saccade_amplitude; }
	Vector2 get_gaze_angles() const { return gaze_now; }

	void blink();
	float get_channel(Channel p_channel) const;
	Dictionary get_channels() const;
	Dictionary sample_voice(const Ref<AudioStream> &p_stream, float p_time) const;
	Dictionary get_stats() const;
	static Dictionary get_global_stats();
	static void reset_global_stats();
	static void clear_analysis_cache();

	PackedStringArray get_configuration_warnings() const override;

	FellowFaceAnimator();
};

VARIANT_ENUM_CAST(FellowFaceAnimator::Channel);
