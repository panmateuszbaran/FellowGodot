/**************************************************************************/
/*  fellow_motion_math.h                                                  */
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

#include "core/math/math_funcs.h"
#include "core/math/quaternion.h"
#include "core/math/vector3.h"

// FellowEngine (milestone 48, decision 0149): the springs and quaternion helpers of motion matching, after Daniel
// Holden's "Spring-It-On" and "Code vs Data Driven Displacement" (orangeduck/Motion-Matching, MIT).
namespace FellowMotionMath {

inline float halflife_to_damping(float p_halflife) {
	return (4.0f * 0.69314718056f) / (p_halflife + 1e-5f);
}

inline float negexp(float p_x) {
	return Math::exp(-p_x);
}

inline Quaternion quat_abs(const Quaternion &p_q) {
	return p_q.w < 0.0f ? Quaternion(-p_q.x, -p_q.y, -p_q.z, -p_q.w) : p_q;
}

// 2 log(q): the rotation's axis times its angle.
inline Vector3 quat_to_scaled_angle_axis(const Quaternion &p_q) {
	Quaternion q = quat_abs(p_q);
	const Vector3 v(q.x, q.y, q.z);
	const float length = v.length();
	if (length < 1e-8f) {
		return v * 2.0f;
	}
	const float half_angle = Math::atan2(length, float(q.w)); // float(): unambiguous with precision=double
	return v * (2.0f * half_angle / length);
}

inline Quaternion quat_from_scaled_angle_axis(const Vector3 &p_v) {
	const float angle = p_v.length();
	if (angle < 1e-8f) {
		return Quaternion(p_v.x * 0.5f, p_v.y * 0.5f, p_v.z * 0.5f, 1.0f).normalized();
	}
	const float half = angle * 0.5f;
	const float s = Math::sin(half) / angle;
	return Quaternion(p_v.x * s, p_v.y * s, p_v.z * s, Math::cos(half));
}

// A value and its velocity decaying towards zero (critically damped, exact).
inline void decay_spring_damper_exact(Vector3 &r_x, Vector3 &r_v, float p_halflife, float p_dt) {
	const float y = halflife_to_damping(p_halflife) / 2.0f;
	const Vector3 j1 = r_v + r_x * y;
	const float eydt = negexp(y * p_dt);
	r_x = eydt * (r_x + j1 * p_dt);
	r_v = eydt * (r_v - j1 * y * p_dt);
}

inline void decay_spring_damper_exact(float &r_x, float &r_v, float p_halflife, float p_dt) {
	const float y = halflife_to_damping(p_halflife) / 2.0f;
	const float j1 = r_v + r_x * y;
	const float eydt = negexp(y * p_dt);
	r_x = eydt * (r_x + j1 * p_dt);
	r_v = eydt * (r_v - j1 * y * p_dt);
}

// A position driven by a desired velocity through a critically damped acceleration (the simulated character).
inline void simulation_position_update(Vector3 &r_x, Vector3 &r_v, Vector3 &r_a, const Vector3 &p_desired_v, float p_halflife, float p_dt) {
	const float y = halflife_to_damping(p_halflife) / 2.0f;
	const Vector3 j0 = r_v - p_desired_v;
	const Vector3 j1 = r_a + j0 * y;
	const float eydt = negexp(y * p_dt);
	r_x = eydt * (((-j1) / (y * y)) + ((-j0 - j1 * p_dt) / y)) + (j1 / (y * y)) + j0 / y + p_desired_v * p_dt + r_x;
	r_v = eydt * (j0 + j1 * p_dt) + p_desired_v;
	r_a = eydt * (r_a - j1 * y * p_dt);
}

// A rotation and its angular velocity driven towards a desired rotation.
inline void simulation_rotation_update(Quaternion &r_q, Vector3 &r_w, const Quaternion &p_desired, float p_halflife, float p_dt) {
	const float y = halflife_to_damping(p_halflife) / 2.0f;
	const Vector3 j0 = quat_to_scaled_angle_axis(quat_abs(r_q * p_desired.inverse()));
	const Vector3 j1 = r_w + j0 * y;
	const float eydt = negexp(y * p_dt);
	r_q = quat_from_scaled_angle_axis(eydt * (j0 + j1 * p_dt)) * p_desired;
	r_w = eydt * (r_w - j1 * y * p_dt);
}

inline Quaternion yaw_rotation(const Vector3 &p_forward) {
	return Quaternion(Vector3(0, 1, 0), Math::atan2(p_forward.x, p_forward.z));
}

} //namespace FellowMotionMath
