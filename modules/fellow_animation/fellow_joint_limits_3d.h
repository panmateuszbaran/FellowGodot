/**************************************************************************/
/*  fellow_joint_limits_3d.h                                              */
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

#include "scene/3d/skeleton_modifier_3d.h"

// FellowEngine (milestone 202, decision 0344): a person's joints kept within their range of motion, after everything
// else on the skeleton (the animation, the IK, a look-at) — the elbows and knees bent in their hinges' plane (the upper
// arm or thigh turned about itself until its hinge lies in the bend, as a person's does: an IK that places the elbow
// leaves the arm's turn as the animation had it, the elbow bent sideways), the ankles, the wrists and the neck brought
// back within a person's range. The limits are an adult's active range of motion (degrees); the rest pose is taken as
// a T or an A (the thumbs and the toes forwards).
class FellowJointLimits3D : public SkeletonModifier3D {
	GDCLASS(FellowJointLimits3D, SkeletonModifier3D);

public:
	enum Role {
		ROLE_HIPS,
		ROLE_CHEST,
		ROLE_NECK,
		ROLE_HEAD,
		ROLE_LEFT_UPPER_ARM,
		ROLE_LEFT_LOWER_ARM,
		ROLE_LEFT_HAND,
		ROLE_RIGHT_UPPER_ARM,
		ROLE_RIGHT_LOWER_ARM,
		ROLE_RIGHT_HAND,
		ROLE_LEFT_UPPER_LEG,
		ROLE_LEFT_LOWER_LEG,
		ROLE_LEFT_FOOT,
		ROLE_LEFT_TOES,
		ROLE_RIGHT_UPPER_LEG,
		ROLE_RIGHT_LOWER_LEG,
		ROLE_RIGHT_FOOT,
		ROLE_RIGHT_TOES,
		ROLE_MAX,
	};

	// What the last pose corrected (get_stats()).
	struct Stats {
		uint64_t frames = 0;
		uint64_t hinges = 0; // hinges turned into their bend
		uint64_t ankles = 0;
		uint64_t wrists = 0;
		uint64_t necks = 0;
		float hinge_max = 0.0f; // degrees, the largest turn
		float ankle_max = 0.0f;
		float wrist_max = 0.0f;
		float neck_max = 0.0f;
		uint64_t usec = 0;
	};

private:
	bool hinge_alignment = true;
	float hinge_max_turn = 120.0f; // degrees: a bend further round than this (the hinge bent the wrong way) left as it is
	bool limit_ankles = true;
	Vector2 ankle_range = Vector2(-30.0f, 55.0f); // dorsiflexion (−), plantarflexion (+), from the rest pose
	bool limit_wrists = true;
	Vector2 wrist_flexion_range = Vector2(-85.0f, 75.0f); // flexion (−), extension (+)
	Vector2 wrist_deviation_range = Vector2(-40.0f, 25.0f); // ulnar (−), radial (+)
	bool wrist_pivot = true; // a wrist's correction turns the hand about its knuckles (the arm brought to the wrist)
	bool limit_neck = true;
	Vector2 neck_flexion_range = Vector2(-70.0f, 60.0f); // extension (−), flexion (+), the head against the chest
	float neck_bend_limit = 45.0f;
	float neck_turn_limit = 85.0f;
	Dictionary bone_names; // role name -> bone name, over the names found by themselves
	float max_correction_speed = 720.0f; // degrees a second: each correction turns no faster (0: at once)

	int bones[ROLE_MAX];
	int fingers[2][3]; // per hand: the index, middle and little fingers' first bones
	ObjectID bones_skeleton;
	int bones_count = -1;
	bool bones_dirty = true;
	// From the rest pose (each in its bone's own frame): the hinges' bend, the bones' axes; the hand's frame against the
	// forearm, its axis, its back and its thumb's side; the ankles' rest angle; the head's frame against the chest and the
	// body's axes in each.
	Vector3 flex_local[4]; // the upper arms', the thighs': left arm, right arm, left leg, right leg
	Vector3 axis_local[4];
	Basis hand_rest[2];
	Vector3 hand_axis[2], hand_back[2], hand_thumb[2];
	float ankle_rest[2] = { 0.0f, 0.0f };
	Basis head_rest;
	Vector3 chest_up, chest_forward, chest_left, head_up, head_forward;
	bool rest_valid = false;
	Stats stats;
	Stats last_stats;
	// Each corrected bone's correction, in its own frame (the upper arms, the thighs, the hands, the feet, the head):
	// eased towards what this pose needs at max_correction_speed — on and off (a correction that jumped, a hinge's turn as
	// the limb straightened, a wrist bent past its axis, had turned the bone over in a frame).
	enum Slot {
		SLOT_LEFT_UPPER_ARM,
		SLOT_RIGHT_UPPER_ARM,
		SLOT_LEFT_UPPER_LEG,
		SLOT_RIGHT_UPPER_LEG,
		SLOT_LEFT_HAND,
		SLOT_RIGHT_HAND,
		SLOT_LEFT_FOOT,
		SLOT_RIGHT_FOOT,
		SLOT_HEAD,
		SLOT_MAX,
	};
	Quaternion correction[SLOT_MAX];
	double step = 0.0; // this pose's time step (s)
	Quaternion _ease_correction(int p_slot, const Quaternion &p_target, float p_speed = 1.0f);

	void _find_bones(Skeleton3D *p_skeleton);
	void _read_rest(Skeleton3D *p_skeleton);
	void _set_global_rotation(Skeleton3D *p_skeleton, int p_bone, const Basis &p_global);
	float _align_hinge(Skeleton3D *p_skeleton, int p_index, int p_upper, int p_lower, int p_end);
	float _limit_ankle(Skeleton3D *p_skeleton, int p_side);
	float _limit_wrist(Skeleton3D *p_skeleton, int p_side);
	void _reach(Skeleton3D *p_skeleton, int p_upper, int p_lower, int p_end, const Vector3 &p_target);
	float _limit_neck(Skeleton3D *p_skeleton);

protected:
	static void _bind_methods();
	virtual void _process_modification(double p_delta) override;

public:
	void set_hinge_alignment(bool p_enabled) { hinge_alignment = p_enabled; }
	bool is_hinge_alignment() const { return hinge_alignment; }
	void set_hinge_max_turn(float p_degrees) { hinge_max_turn = CLAMP(p_degrees, 0.0f, 180.0f); }
	float get_hinge_max_turn() const { return hinge_max_turn; }
	void set_limit_ankles(bool p_enabled) { limit_ankles = p_enabled; }
	bool is_limit_ankles() const { return limit_ankles; }
	void set_ankle_range(const Vector2 &p_range) { ankle_range = p_range; }
	Vector2 get_ankle_range() const { return ankle_range; }
	void set_limit_wrists(bool p_enabled) { limit_wrists = p_enabled; }
	bool is_limit_wrists() const { return limit_wrists; }
	void set_wrist_flexion_range(const Vector2 &p_range) { wrist_flexion_range = Vector2(CLAMP(p_range.x, -89.0f, 89.0f), CLAMP(p_range.y, -89.0f, 89.0f)); }
	Vector2 get_wrist_flexion_range() const { return wrist_flexion_range; }
	void set_wrist_deviation_range(const Vector2 &p_range) { wrist_deviation_range = Vector2(CLAMP(p_range.x, -89.0f, 89.0f), CLAMP(p_range.y, -89.0f, 89.0f)); }
	Vector2 get_wrist_deviation_range() const { return wrist_deviation_range; }
	void set_wrist_pivot(bool p_enabled) { wrist_pivot = p_enabled; }
	bool is_wrist_pivot() const { return wrist_pivot; }
	void set_limit_neck(bool p_enabled) { limit_neck = p_enabled; }
	bool is_limit_neck() const { return limit_neck; }
	void set_neck_flexion_range(const Vector2 &p_range) { neck_flexion_range = p_range; }
	Vector2 get_neck_flexion_range() const { return neck_flexion_range; }
	void set_neck_bend_limit(float p_degrees) { neck_bend_limit = CLAMP(p_degrees, 0.0f, 180.0f); }
	float get_neck_bend_limit() const { return neck_bend_limit; }
	void set_neck_turn_limit(float p_degrees) { neck_turn_limit = CLAMP(p_degrees, 0.0f, 180.0f); }
	float get_neck_turn_limit() const { return neck_turn_limit; }
	void set_max_correction_speed(float p_degrees) { max_correction_speed = MAX(0.0f, p_degrees); }
	float get_max_correction_speed() const { return max_correction_speed; }
	void set_bone_names(const Dictionary &p_names);
	Dictionary get_bone_names() const { return bone_names; }
	Dictionary get_stats() const;
	Dictionary get_bones() const;

	FellowJointLimits3D();
};

VARIANT_ENUM_CAST(FellowJointLimits3D::Role);
