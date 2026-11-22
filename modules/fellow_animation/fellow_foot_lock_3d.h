/**************************************************************************/
/*  fellow_foot_lock_3d.h                                                 */
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

// FellowEngine (milestone 155, decision 0295): a character's feet kept where they touch the ground while the animation
// under them would slide them — a blend of two gaits at the same moment of their strides (one foot down in one, in
// the air in the other), a turn, a start or a stop. Each foot is locked where its sole comes down (under the heel or
// under the ball, the lower one; its height over the ground: the skeleton's origin's height): the foot is moved across
// the ground by as much as the animation moves that point, so that the heel's and the ball's roll stay the animation's
// and only their slide is taken out — by two-bone IK (the knee in its animated plane, the foot keeping its animated
// turn). It is let go when the animation lifts it or pulls it further than unlock_radius; letting go, it returns to the
// animated foot over blend_halflife.
class FellowFootLock3D : public SkeletonModifier3D {
	GDCLASS(FellowFootLock3D, SkeletonModifier3D);

	struct Foot {
		int upper = -1;
		int lower = -1;
		int foot = -1;
		int toe = -1;
		Vector3 heel_local; // the sole under the heel, in the foot bone's space
		Vector3 ball_local; // the sole under the ball (the toe bone's joint), in the foot bone's space
		bool locked = false;
		bool on_ball = false; // the lock is on the ball (the heel up), else on the heel
		Vector3 lock; // world: where the locked point is held
		Vector3 offset; // world: the shown foot minus the animated (decays to nothing once let go)
		Vector3 offset_velocity;
		bool has_last = false;
		Vector3 last_shown;
		Vector3 last_ankle; // milestone 173: the animated ankle the last pass saw (whether the pose changed)
		float since_pose = 0.0f; // milestone 173: seconds since it last changed
		double slide = 0.0; // metres the shown foot moved while down (get_slide())
	};

	StringName foot_bones[2] = { StringName("LeftFoot"), StringName("RightFoot") };
	StringName toe_bones[2] = { StringName("LeftToes"), StringName("RightToes") };
	float contact_height = 0.03f;
	float heel_offset = 0.05f;
	float unlock_radius = 0.3f;
	float release_extension = 1.0f; // milestone 172: let go when holding needs more of the leg's length (1: never)
	float blend_halflife = 0.06f;
	Foot feet[2];
	bool bones_valid = false;

	void _find_bones();
	static void _two_bone(Skeleton3D *p_skeleton, int p_upper, int p_lower, int p_end, const Vector3 &p_target, const Vector3 &p_pole);

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
	void set_contact_height(float p_height) { contact_height = MAX(0.0f, p_height); }
	void set_heel_offset(float p_offset);
	float get_heel_offset() const { return heel_offset; }
	float get_contact_height() const { return contact_height; }
	void set_release_extension(float p_share) { release_extension = CLAMP(p_share, 0.5f, 1.0f); }
	float get_release_extension() const { return release_extension; }
	void set_unlock_radius(float p_radius) { unlock_radius = MAX(0.01f, p_radius); }
	float get_unlock_radius() const { return unlock_radius; }
	void set_blend_halflife(float p_halflife) { blend_halflife = MAX(0.0f, p_halflife); }
	float get_blend_halflife() const { return blend_halflife; }

	bool is_locked(int p_foot) const;
	double get_slide(int p_foot) const;
	void reset();

	virtual PackedStringArray get_configuration_warnings() const override;
};
