/**************************************************************************/
/*  fellow_foot_placement_3d.h                                            */
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

// FellowEngine (milestone 174, decision 0316): a character's feet on the ground under them — slopes, steps, roofs —
// with an animation made on flat ground. Rays under each foot's heel and ball find the ground; each foot is raised or
// lowered by as much as the ground there is above or below the character's (the skeleton's origin), its sole turned to
// the ground while it is down, and the hips lowered as far as the lower foot must reach down (the leg over the higher
// ground bends rather than the lower one overstretching), all eased by springs — by two-bone IK on the legs (shared
// with FellowFootLock3D; after it, the held feet are placed where they are held).
class FellowFootPlacement3D : public SkeletonModifier3D {
	GDCLASS(FellowFootPlacement3D, SkeletonModifier3D);

	struct Foot {
		int upper = -1;
		int lower = -1;
		int foot = -1;
		int toe = -1;
		Vector3 heel_local; // the sole under the heel, in the foot bone's space
		Vector3 ball_local; // the sole under the ball (the toe bone's joint)
		float offset = 0.0f; // m: the foot raised (lowered below zero) over the animation's
		float base = 0.0f; // world: the height of the ground the foot stands on, eased
		float base_velocity = 0.0f;
		bool has_base = false;
		float goal_prev = 0.0f; // the base's goal when the pose last changed, and its velocity since
		float goal_velocity = 0.0f;
		Vector3 last_ankle; // world, when the pose last changed, and the ankle's velocity since
		Vector3 velocity;
		bool has_ankle = false;
		Vector3 normal = Vector3(0, 1, 0); // the ground's under it, eased (world)
		Vector3 normal_velocity;
		bool hit = false;
	};

	StringName foot_bones[2] = { StringName("LeftFoot"), StringName("RightFoot") };
	StringName toe_bones[2] = { StringName("LeftToes"), StringName("RightToes") };
	uint32_t collision_mask = 1;
	float probe_up = 0.5f;
	float probe_down = 0.6f;
	float heel_offset = 0.05f;
	float contact_height = 0.05f;
	float swing_height = 0.2f;
	float max_hips_drop = 0.45f;
	float foot_alignment = 1.0f;
	float max_alignment_angle = 40.0f;
	float raise_halflife = 0.04f;
	float lower_halflife = 0.1f;
	float hips_halflife = 0.08f;
	Foot feet[2];
	int hips = -1;
	float hips_drop = 0.0f; // m: the hips over the animation's (below zero: lowered)
	float hips_world = 0.0f; // world: the height the hips' drop is measured from, eased
	float hips_world_velocity = 0.0f;
	bool hips_has = false;
	float hips_goal_prev = 0.0f;
	float hips_goal_velocity = 0.0f;
	float since_pose = 0.0f; // seconds since the pose (or the body) last moved
	Vector3 last_pose_key;
	bool has_pose_key = false;
	bool bones_valid = false;
	uint64_t frames = 0;
	uint64_t usec = 0;
	uint64_t rays = 0;

	void _find_bones();

protected:
	static void _bind_methods();
	virtual void _process_modification(double p_delta) override;
	virtual void _skeleton_changed(Skeleton3D *p_old, Skeleton3D *p_new) override;

public:
	void set_left_foot_bone(const StringName &p_name);
	StringName get_left_foot_bone() const { return foot_bones[0]; }
	void set_right_foot_bone(const StringName &p_name);
	StringName get_right_foot_bone() const { return foot_bones[1]; }
	void set_left_toe_bone(const StringName &p_name);
	StringName get_left_toe_bone() const { return toe_bones[0]; }
	void set_right_toe_bone(const StringName &p_name);
	StringName get_right_toe_bone() const { return toe_bones[1]; }
	void set_collision_mask(uint32_t p_mask) { collision_mask = p_mask; }
	uint32_t get_collision_mask() const { return collision_mask; }
	void set_probe_up(float p_height) { probe_up = MAX(0.0f, p_height); }
	float get_probe_up() const { return probe_up; }
	void set_probe_down(float p_depth) { probe_down = MAX(0.0f, p_depth); }
	float get_probe_down() const { return probe_down; }
	void set_heel_offset(float p_offset);
	float get_heel_offset() const { return heel_offset; }
	void set_contact_height(float p_height) { contact_height = MAX(0.0f, p_height); }
	float get_contact_height() const { return contact_height; }
	void set_swing_height(float p_height) { swing_height = MAX(0.01f, p_height); }
	float get_swing_height() const { return swing_height; }
	void set_max_hips_drop(float p_drop) { max_hips_drop = MAX(0.0f, p_drop); }
	float get_max_hips_drop() const { return max_hips_drop; }
	void set_foot_alignment(float p_weight) { foot_alignment = CLAMP(p_weight, 0.0f, 1.0f); }
	float get_foot_alignment() const { return foot_alignment; }
	void set_max_alignment_angle(float p_degrees) { max_alignment_angle = CLAMP(p_degrees, 0.0f, 89.0f); }
	float get_max_alignment_angle() const { return max_alignment_angle; }
	void set_raise_halflife(float p_halflife) { raise_halflife = MAX(0.0f, p_halflife); }
	float get_raise_halflife() const { return raise_halflife; }
	void set_lower_halflife(float p_halflife) { lower_halflife = MAX(0.0f, p_halflife); }
	float get_lower_halflife() const { return lower_halflife; }
	void set_hips_halflife(float p_halflife) { hips_halflife = MAX(0.0f, p_halflife); }
	float get_hips_halflife() const { return hips_halflife; }

	float get_foot_offset(int p_foot) const;
	float get_hips_drop() const { return hips_drop; }
	Dictionary get_stats() const;
	void reset();

	virtual PackedStringArray get_configuration_warnings() const override;
};
