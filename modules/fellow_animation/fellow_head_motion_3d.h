/**************************************************************************/
/*  fellow_head_motion_3d.h                                               */
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
#include "core/templates/local_vector.h"
#include "scene/3d/skeleton_modifier_3d.h"

class FellowFaceAnimator;

// FellowEngine (milestone 104, decision 0212): the head moving with what the face says — a nod on each of the voice's
// stressed syllables (FellowFaceAnimator's `emphasized`), a slow sway that grows while it speaks and a little one at
// rest — over whatever the character's animation plays (a skeleton modifier: after the animation, before the skin).
class FellowHeadMotion3D : public SkeletonModifier3D {
	GDCLASS(FellowHeadMotion3D, SkeletonModifier3D);

	NodePath face_animator_path;
	StringName head_bone = "Head";
	StringName neck_bone = "Neck";
	float neck_share = 0.4f; // of the rotation the neck takes (the head the rest)
	Vector3 forward = Vector3(0, 0, 1); // the face's direction in the skeleton's space (Godot's humanoids: +Z)
	float nod_degrees = 6.0f; // a full-strength emphasis
	float nod_time = 0.12f; // s to the nod's lowest point
	float speech_sway_degrees = 2.0f;
	float idle_sway_degrees = 0.5f;
	float sway_frequency = 0.3f; // Hz
	int64_t seed = 0;
	// Milestone 148 (decision 0278): the head turned towards what the face looks at, the eyes the rest.
	bool follow_gaze = false;
	float gaze_dead_zone = 10.0f; // degrees the eyes turn alone
	float gaze_follow = 0.7f; // of the angle past the dead zone the head takes
	float max_head_yaw = 70.0f;
	float max_head_pitch = 35.0f;
	float gaze_follow_time = 0.3f; // s
	Vector2 gaze_turn; // degrees: yaw (left), pitch (down), eased

	struct Nod {
		double start = 0.0;
		float amplitude = 0.0f; // degrees
	};
	LocalVector<Nod> nods;
	int64_t emphases_seen = -1;
	double clock = 0.0;
	float phases[6] = {};
	Vector3 angles; // degrees: pitch (down), yaw (left), roll, last applied
	uint64_t nod_count = 0;
	uint64_t frames = 0;
	uint64_t usec = 0;

	mutable ObjectID face_cache;
	int head_index = -1;
	int neck_index = -1;
	ObjectID bones_skeleton;
	int bones_version = -1;

	FellowFaceAnimator *_face() const;
	void _find_bones(Skeleton3D *p_skeleton);
	void _reseed();

protected:
	void _notification(int p_what);
	static void _bind_methods();
	virtual void _process_modification(double p_delta) override;

public:
	void set_face_animator_path(const NodePath &p_path);
	NodePath get_face_animator_path() const { return face_animator_path; }
	void set_head_bone(const StringName &p_bone);
	StringName get_head_bone() const { return head_bone; }
	void set_neck_bone(const StringName &p_bone);
	StringName get_neck_bone() const { return neck_bone; }
	void set_neck_share(float p_share) { neck_share = CLAMP(p_share, 0.0f, 1.0f); }
	float get_neck_share() const { return neck_share; }
	void set_forward(const Vector3 &p_forward);
	Vector3 get_forward() const { return forward; }
	void set_nod_degrees(float p_degrees) { nod_degrees = CLAMP(p_degrees, 0.0f, 30.0f); }
	float get_nod_degrees() const { return nod_degrees; }
	void set_nod_time(float p_seconds) { nod_time = CLAMP(p_seconds, 0.03f, 1.0f); }
	float get_nod_time() const { return nod_time; }
	void set_speech_sway_degrees(float p_degrees) { speech_sway_degrees = CLAMP(p_degrees, 0.0f, 20.0f); }
	float get_speech_sway_degrees() const { return speech_sway_degrees; }
	void set_idle_sway_degrees(float p_degrees) { idle_sway_degrees = CLAMP(p_degrees, 0.0f, 20.0f); }
	float get_idle_sway_degrees() const { return idle_sway_degrees; }
	void set_sway_frequency(float p_hz) { sway_frequency = CLAMP(p_hz, 0.01f, 5.0f); }
	float get_sway_frequency() const { return sway_frequency; }
	void set_seed(int64_t p_seed);
	int64_t get_seed() const { return seed; }
	void set_follow_gaze(bool p_enabled) { follow_gaze = p_enabled; }
	bool is_follow_gaze() const { return follow_gaze; }
	void set_gaze_dead_zone(float p_degrees) { gaze_dead_zone = CLAMP(p_degrees, 0.0f, 60.0f); }
	float get_gaze_dead_zone() const { return gaze_dead_zone; }
	void set_gaze_follow(float p_share) { gaze_follow = CLAMP(p_share, 0.0f, 1.0f); }
	float get_gaze_follow() const { return gaze_follow; }
	void set_max_head_yaw(float p_degrees) { max_head_yaw = CLAMP(p_degrees, 0.0f, 120.0f); }
	float get_max_head_yaw() const { return max_head_yaw; }
	void set_max_head_pitch(float p_degrees) { max_head_pitch = CLAMP(p_degrees, 0.0f, 80.0f); }
	float get_max_head_pitch() const { return max_head_pitch; }
	void set_gaze_follow_time(float p_seconds) { gaze_follow_time = CLAMP(p_seconds, 0.0f, 3.0f); }
	float get_gaze_follow_time() const { return gaze_follow_time; }
	Vector2 get_gaze_turn() const { return gaze_turn; }

	void nod(float p_strength = 1.0f);
	Vector3 get_head_angles() const { return angles; }
	Dictionary get_stats() const;

	PackedStringArray get_configuration_warnings() const override;
	virtual bool has_process() const override { return true; } // it moves the head without an animation playing

	FellowHeadMotion3D();
};
