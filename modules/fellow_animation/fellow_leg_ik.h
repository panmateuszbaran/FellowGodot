/**************************************************************************/
/*  fellow_leg_ik.h                                                       */
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

#include "scene/3d/skeleton_3d.h"

// FellowEngine: a leg's two-bone IK, shared by FellowFootLock3D (milestone 155) and FellowFootPlacement3D (milestone 174).

// The exact decay of a critically damped spring towards zero over a step (D. Holden, "Spring-It-On").
template <typename T>
inline void fellow_spring_decay(T &r_x, T &r_v, float p_halflife, float p_dt) {
	if (p_halflife <= 0.0f) {
		r_x = T();
		r_v = T();
		return;
	}
	const float y = 4.0f * 0.69314718056f / (p_halflife + 1e-5f) / 2.0f;
	const T j1 = r_v + r_x * y;
	const float e = Math::exp(-y * p_dt);
	r_x = (r_x + j1 * p_dt) * e;
	r_v = (r_v - j1 * y * p_dt) * e;
}

// A critically damped spring following a goal that moves at p_goal_velocity: no lag behind a goal moving steadily (a
// foot's ground along a slope), the half-life easing what the velocity does not say (a step).
inline void fellow_spring_follow(float &r_x, float &r_v, float p_goal, float p_goal_velocity, float p_halflife, float p_dt) {
	if (p_halflife <= 0.0f) {
		r_x = p_goal;
		r_v = p_goal_velocity;
		return;
	}
	const float y = 4.0f * 0.69314718056f / (p_halflife + 1e-5f) / 2.0f;
	const float c = p_goal + p_goal_velocity * (2.0f / y);
	const float j0 = r_x - c;
	const float j1 = r_v + j0 * y;
	const float e = Math::exp(-y * p_dt);
	r_x = (j0 + j1 * p_dt) * e + c;
	r_v = (r_v - j1 * y * p_dt) * e;
}

inline Quaternion fellow_bone_global_rotation(const Skeleton3D *p_skeleton, int p_bone) {
	return p_bone < 0 ? Quaternion() : p_skeleton->get_bone_global_pose(p_bone).basis.get_rotation_quaternion();
}

// Milestone 173 (decision 0315): where the knee bends (a point in the skeleton's space): from the animated knee's offset
// from the hip-foot line where the leg is bent, from the foot's forward (towards the toe) where it is nearly straight —
// the animated knee then lies on that line, and against a target elsewhere its side flips (the knee swung round the leg
// in a frame).
inline Vector3 fellow_leg_pole(const Skeleton3D *p_skeleton, const Vector3 &p_hip, const Vector3 &p_knee, const Vector3 &p_foot, int p_toe) {
	Vector3 pole = p_knee + (p_knee - (p_hip + p_foot) * 0.5f) * 4.0f;
	const float leg_len = p_hip.distance_to(p_knee) + p_knee.distance_to(p_foot);
	const Vector3 line = (p_foot - p_hip).normalized();
	const Vector3 knee_off = (p_knee - p_hip) - line * (p_knee - p_hip).dot(line);
	const float w = CLAMP(knee_off.length() / (0.05f * leg_len), 0.0f, 1.0f);
	Vector3 fwd = p_toe >= 0 ? p_skeleton->get_bone_global_pose(p_toe).origin - p_foot : Vector3();
	fwd -= line * fwd.dot(line);
	Vector3 bend_dir = knee_off.length() > 1e-6f ? knee_off.normalized() * w : Vector3();
	if (fwd.length() > 1e-6f) {
		bend_dir += fwd.normalized() * (1.0f - w);
	}
	if (bend_dir.length() > 1e-6f) {
		pole = p_knee + bend_dir.normalized() * leg_len;
	}
	return pole;
}

// A two-bone chain reaching for a target (skeleton space), bending towards a pole, with a soft limit in the last 0.2 % of
// its reach (a target out of reach does not snap the knee straight; one the animation reaches is reached — an earlier
// soft limit would bend every leg the animation stretches near full length). The end bone is given p_end_global, its
// rotation in the skeleton's space (its animated one keeps the foot's turn).
inline void fellow_two_bone_ik(Skeleton3D *p_skeleton, int p_upper, int p_lower, int p_end, const Vector3 &p_target, const Vector3 &p_pole, const Quaternion &p_end_global) {
	const Transform3D gu = p_skeleton->get_bone_global_pose(p_upper);
	const Transform3D gl = p_skeleton->get_bone_global_pose(p_lower);
	const Transform3D ge = p_skeleton->get_bone_global_pose(p_end);
	const Vector3 s = gu.origin;
	const Vector3 e = gl.origin;
	const Vector3 h = ge.origin;
	const float a = s.distance_to(e);
	const float b = e.distance_to(h);
	const Vector3 to = p_target - s;
	float d = to.length();
	if (a < 1e-4f || b < 1e-4f || d < 1e-4f) {
		return;
	}
	const Vector3 dir = to / d;
	const float reach = a + b - 1e-3f;
	const float soft = reach * 0.998f;
	if (d > soft) {
		d = soft + (reach - soft) * (1.0f - Math::exp(-(d - soft) / (reach - soft)));
	}
	d = MAX(d, Math::abs(a - b) + 1e-3f);
	const float cos_a = CLAMP((a * a + d * d - b * b) / (2.0f * a * d), -1.0f, 1.0f);
	const float sin_a = Math::sqrt(MAX(0.0f, 1.0f - cos_a * cos_a));
	Vector3 bend = p_pole - s;
	bend -= dir * bend.dot(dir);
	if (bend.length() < 1e-4f) {
		bend = (e - s) - dir * (e - s).dot(dir);
	}
	if (bend.length() < 1e-4f) {
		return;
	}
	bend.normalize();
	const Vector3 e2 = s + dir * (a * cos_a) + bend * (a * sin_a);
	const Vector3 h2 = s + dir * d;
	const Quaternion q1((e - s).normalized(), (e2 - s).normalized());
	const Quaternion q2(q1.xform(h - e).normalized(), (h2 - e2).normalized());
	const Quaternion upper_parent = fellow_bone_global_rotation(p_skeleton, p_skeleton->get_bone_parent(p_upper));
	const Quaternion lower_parent = fellow_bone_global_rotation(p_skeleton, p_skeleton->get_bone_parent(p_lower));
	const Quaternion upper_local = (upper_parent.inverse() * (q1 * gu.basis.get_rotation_quaternion())).normalized();
	const Quaternion lower_local = ((q1 * lower_parent).inverse() * (q2 * q1 * gl.basis.get_rotation_quaternion())).normalized();
	p_skeleton->set_bone_pose_rotation(p_upper, upper_local);
	p_skeleton->set_bone_pose_rotation(p_lower, lower_local);
	const Quaternion lower_now = fellow_bone_global_rotation(p_skeleton, p_lower);
	p_skeleton->set_bone_pose_rotation(p_end, (lower_now.inverse() * p_end_global).normalized());
}
