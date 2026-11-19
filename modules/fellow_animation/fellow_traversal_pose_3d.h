/**************************************************************************/
/*  fellow_traversal_pose_3d.h                                            */
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

#include "core/templates/local_vector.h"
#include "fellow_traversal_3d.h"

#include "scene/3d/skeleton_modifier_3d.h"

// FellowEngine (milestone 72, decision 0180): a character's pose while its FellowTraversal3D mantles or vaults — the
// hands reaching for the edge and holding it, the feet on the wall's face and stepping onto the top, the torso tipped
// towards the wall (two-bone IK over whatever the character's animation plays) — or, when the traversal has a clip for
// the kind, that clip played across the mantle with its root's motion taken out (the traversal moves the body).
class FellowTraversalPose3D : public SkeletonModifier3D {
	GDCLASS(FellowTraversalPose3D, SkeletonModifier3D);

public:
	enum Role {
		ROLE_HIPS,
		ROLE_SPINE,
		ROLE_CHEST,
		ROLE_LEFT_UPPER_ARM,
		ROLE_LEFT_LOWER_ARM,
		ROLE_LEFT_HAND,
		ROLE_RIGHT_UPPER_ARM,
		ROLE_RIGHT_LOWER_ARM,
		ROLE_RIGHT_HAND,
		ROLE_LEFT_UPPER_LEG,
		ROLE_LEFT_LOWER_LEG,
		ROLE_LEFT_FOOT,
		ROLE_RIGHT_UPPER_LEG,
		ROLE_RIGHT_LOWER_LEG,
		ROLE_RIGHT_FOOT,
		ROLE_MAX,
	};

private:
	NodePath traversal_path;
	float blend_time = 0.15f;
	bool hand_ik = true;
	bool foot_ik = true;
	bool lean = true;
	Dictionary bone_names; // role name -> bone name, over the names found by themselves
	// Milestone 153 (decision 0290): the body's weight — the hips on a damped spring behind where the traversal puts
	// them (they keep the body's speed when it is stopped or started, and swing back), the torso leaning against them;
	// the hands' and feet's targets eased (no jump where the hints change).
	float body_sway = 0.0f; // 0: off; 1: the hips keep all of the body's speed
	float sway_frequency = 1.6f; // Hz
	float sway_damping = 0.35f; // of critical
	float sway_max = 0.15f; // m
	float limb_smoothing = 0.0f; // s: the time constant of the limbs' targets (0: off)
	// Milestone 153 (decision 0291): hanging, the body moved until the shoulders are within the arms' reach of the holds
	// the hands grip (the hands on the stone whatever the clip's pose and the capsule's place).
	bool hang_fit = false;
	float soft_ik = 0.0f; // milestone 155 (decision 0295): past this share of a limb's reach its target is eased (0: off)
	Vector3 fit; // world: the body's move, eased
	// Milestone 156 (decision 0296): a clip (a mantle from a hang) fades in from the pose the limbs' IK gave the frame
	// before over clip_blend seconds (0: at once — the braced legs had snapped to the clip's first pose, 74 degrees in a
	// frame).
	float clip_blend = 0.0f;
	float pole_follow = 0.0f; // milestone 156: the limbs' bend pulled towards the clip's own by this much against the pole's
	float clip_fade = 1.0f;
	bool clip_on = false;
	bool snap_valid = false;
	LocalVector<Quaternion> snap_rot;
	LocalVector<Vector3> snap_pos;
	// Milestone 156 (decision 0296): no joint turned faster than this (degrees a second) from this node's last pose (0:
	// off).
	float max_joint_speed = 0.0f;
	LocalVector<Quaternion> speed_rot;
	bool speed_valid = false;

	// The skeleton's bones for each role (-1: not found).
	int bones[ROLE_MAX];
	ObjectID bones_skeleton;
	int bones_count = -1;
	bool bones_dirty = true;

	// The blend in and out (a clip's last pose held while it fades).
	float weight = 0.0f;
	float hips_drop = 0.0f; // the last pose's, in meters
	FellowTraversal3D::PoseHints last;
	mutable ObjectID traversal_cache;

	// The spring's state (world space): the hips' offset and its rate; the point they hang behind, its last place and
	// speed.
	Vector3 sway;
	Vector3 sway_rate;
	Vector3 track_last;
	Vector3 track_speed;
	int track_frames = 0; // frames tracked in a row (two before the speed, three before the acceleration)
	// The limbs' eased targets (world) and weights: the left hand, the right, the left foot, the right.
	Vector3 limb_at[4];
	float limb_weight[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	bool limbs_valid = false;

	void _sway_step(const Vector3 &p_point, double p_delta, bool p_on);

	FellowTraversal3D *_get_traversal() const;
	void _find_bones(Skeleton3D *p_skeleton);
	void _apply_clip(Skeleton3D *p_skeleton, const FellowTraversal3D::PoseHints &p_hints, float p_weight);
	void _lean(Skeleton3D *p_skeleton, const Vector3 &p_axis, float p_angle);
	void _pose(double p_delta);
	void _two_bone(Skeleton3D *p_skeleton, int p_upper, int p_lower, int p_end, const Vector3 &p_target, const Vector3 &p_pole, float p_weight);
	void _snapshot(Skeleton3D *p_skeleton);
	void _limit_speed(Skeleton3D *p_skeleton, double p_delta);

protected:
	void _notification(int p_what);
	static void _bind_methods();
	virtual void _process_modification(double p_delta) override;
	virtual void _skeleton_changed(Skeleton3D *p_old, Skeleton3D *p_new) override;

public:
	virtual PackedStringArray get_configuration_warnings() const override;
	virtual bool fellow_processing_is_noop() const override;

	static const char *get_role_name(Role p_role);
	int get_role_bone(Role p_role);
	float get_hips_drop() const { return hips_drop; }
	float get_weight() const { return weight; }

	void set_traversal_path(const NodePath &p_path);
	NodePath get_traversal_path() const { return traversal_path; }
	void set_blend_time(float p_seconds) { blend_time = MAX(0.0f, p_seconds); }
	float get_blend_time() const { return blend_time; }
	void set_clip_blend(float p_seconds) { clip_blend = MAX(0.0f, p_seconds); }
	void set_pole_follow(float p_amount) { pole_follow = MAX(0.0f, p_amount); }
	void set_max_joint_speed(float p_degrees_per_second) { max_joint_speed = MAX(0.0f, p_degrees_per_second); }
	float get_max_joint_speed() const { return max_joint_speed; }
	float get_pole_follow() const { return pole_follow; }
	float get_clip_blend() const { return clip_blend; }
	void set_hand_ik(bool p_enabled) { hand_ik = p_enabled; }
	bool is_hand_ik() const { return hand_ik; }
	void set_foot_ik(bool p_enabled) { foot_ik = p_enabled; }
	bool is_foot_ik() const { return foot_ik; }
	void set_lean(bool p_enabled) { lean = p_enabled; }
	bool is_lean() const { return lean; }
	void set_bone_names(const Dictionary &p_names);
	Dictionary get_bone_names() const { return bone_names; }
	void set_body_sway(float p_amount) { body_sway = CLAMP(p_amount, 0.0f, 1.0f); }
	float get_body_sway() const { return body_sway; }
	void set_sway_frequency(float p_hz) { sway_frequency = CLAMP(p_hz, 0.2f, 8.0f); }
	float get_sway_frequency() const { return sway_frequency; }
	void set_sway_damping(float p_ratio) { sway_damping = CLAMP(p_ratio, 0.0f, 2.0f); }
	float get_sway_damping() const { return sway_damping; }
	void set_sway_max(float p_meters) { sway_max = CLAMP(p_meters, 0.0f, 0.5f); }
	float get_sway_max() const { return sway_max; }
	void set_hang_fit(bool p_enabled) { hang_fit = p_enabled; }
	void set_soft_ik(float p_share) { soft_ik = p_share <= 0.0f ? 0.0f : CLAMP(p_share, 0.8f, 0.999f); }
	float get_soft_ik() const { return soft_ik; }
	bool is_hang_fit() const { return hang_fit; }
	Vector3 get_hang_fit_offset() const { return fit; }
	void set_limb_smoothing(float p_seconds) { limb_smoothing = CLAMP(p_seconds, 0.0f, 0.5f); }
	float get_limb_smoothing() const { return limb_smoothing; }
	Vector3 get_sway() const { return sway; }

	FellowTraversalPose3D();
};

VARIANT_ENUM_CAST(FellowTraversalPose3D::Role);
