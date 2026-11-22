/**************************************************************************/
/*  fellow_foot_lock_3d.cpp                                               */
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

#include "fellow_foot_lock_3d.h"

#include "fellow_leg_ik.h"

#include "core/object/class_db.h"
#include "scene/3d/skeleton_3d.h"

namespace {

// Milestone 156 (decision 0296): a held foot no further from the hip than this share of the leg's reach (or than the
// animation puts it, if further).
const float REACH_SHARE = 0.99f;

void decay(Vector3 &r_x, Vector3 &r_v, float p_halflife, float p_dt) {
	fellow_spring_decay(r_x, r_v, p_halflife, p_dt);
}

} // namespace

void FellowFootLock3D::_find_bones() {
	bones_valid = false;
	Skeleton3D *sk = get_skeleton();
	if (!sk) {
		return;
	}
	bones_valid = true;
	// Milestone 172 (decision 0314): the ground at rest is the skeleton's origin, unless the feet's joints are well
	// below it (a mocap rig resting on its hips, as CMU's BVH: its feet 0.9 m down) — then their lowest joint.
	float rest_ground = 0.0f;
	for (int s = 0; s < 2; s++) {
		const int fb = sk->find_bone(foot_bones[s]);
		const int tb = toe_bones[s] == StringName() ? -1 : sk->find_bone(toe_bones[s]);
		if (fb >= 0) {
			rest_ground = MIN(rest_ground, (float)sk->get_bone_global_rest(fb).origin.y);
		}
		if (tb >= 0) {
			rest_ground = MIN(rest_ground, (float)sk->get_bone_global_rest(tb).origin.y);
		}
	}
	if (rest_ground > -0.1f) {
		rest_ground = 0.0f;
	}
	for (int s = 0; s < 2; s++) {
		Foot &f = feet[s];
		f.foot = sk->find_bone(foot_bones[s]);
		f.lower = f.foot >= 0 ? sk->get_bone_parent(f.foot) : -1;
		f.upper = f.lower >= 0 ? sk->get_bone_parent(f.lower) : -1;
		f.toe = toe_bones[s] == StringName() ? -1 : sk->find_bone(toe_bones[s]);
		if (f.upper < 0) {
			bones_valid = false;
			continue;
		}
		// The sole's points at rest, on the ground under the ankle and the ball (the skeleton's origin is the ground): the
		// heel heel_offset behind the ankle, away from the toe.
		const Transform3D foot_rest = sk->get_bone_global_rest(f.foot);
		const Vector3 ankle = foot_rest.origin;
		const Vector3 toe = f.toe >= 0 ? sk->get_bone_global_rest(f.toe).origin : ankle;
		Vector3 forward = Vector3(toe.x - ankle.x, 0.0f, toe.z - ankle.z);
		forward = forward.length() > 1e-4f ? forward.normalized() : Vector3();
		const Vector3 heel = Vector3(ankle.x, rest_ground, ankle.z) - forward * heel_offset;
		f.heel_local = foot_rest.affine_inverse().xform(heel);
		f.ball_local = foot_rest.affine_inverse().xform(Vector3(toe.x, rest_ground, toe.z));
		f.locked = false;
		f.offset = Vector3();
		f.offset_velocity = Vector3();
		f.has_last = false;
		f.since_pose = 0.0f;
	}
}

void FellowFootLock3D::_skeleton_changed(Skeleton3D *p_old, Skeleton3D *p_new) {
	SkeletonModifier3D::_skeleton_changed(p_old, p_new);
	_find_bones();
}

void FellowFootLock3D::_process_modification(double p_delta) {
	Skeleton3D *sk = get_skeleton();
	if (!sk) {
		return;
	}
	if (!bones_valid) {
		_find_bones();
		if (!bones_valid) {
			return;
		}
	}
	const float dt = float(MAX(p_delta, 0.0));
	// Where the skeleton is drawn (between the physics ticks with physics interpolation on); its origin is the ground.
	const Transform3D xform = sk->get_global_transform_interpolated();
	const Transform3D to_skeleton = xform.affine_inverse();
	const float ground = xform.origin.y;
	for (int s = 0; s < 2; s++) {
		Foot &f = feet[s];
		const Transform3D foot_xform = xform * sk->get_bone_global_pose(f.foot);
		const Vector3 ankle = foot_xform.origin;
		const Vector3 heel = foot_xform.xform(f.heel_local);
		const Vector3 ball = foot_xform.xform(f.ball_local);
		const float heel_h = heel.y - ground;
		const float ball_h = f.toe >= 0 ? ball.y - ground : heel_h;
		const bool contact = MIN(heel_h, ball_h) < contact_height;
		const bool on_ball = f.toe >= 0 && ball_h < heel_h - 0.005f; // the heel up: rolling on the ball
		const Vector3 point = on_ball ? ball : heel; // the animated point that is down
		f.since_pose += dt;
		const bool pose_changed = !f.has_last || !ankle.is_equal_approx(f.last_ankle);
		const float pose_dt = pose_changed ? f.since_pose : 0.0f;
		if (pose_changed) {
			f.since_pose = 0.0f;
		}
		const Vector3 prev_ankle = f.last_ankle;
		f.last_ankle = ankle;
		if (contact && !f.locked) {
			f.locked = true;
			f.on_ball = on_ball;
			f.lock = point + f.offset; // where it is shown now
		} else if (f.locked && contact && on_ball != f.on_ball) {
			// From the heel to the ball (or back): held at where the other point is now shown.
			f.lock += (on_ball ? ball - heel : heel - ball);
			f.on_ball = on_ball;
		}
		Vector3 lock_flat = f.lock;
		lock_flat.y = point.y;
		const bool was_locked = f.locked;
		if (f.locked && (!contact || lock_flat.distance_to(point) > unlock_radius)) {
			f.locked = false; // back to the animation over blend_halflife
		}
		if (f.locked && release_extension < 1.0f) {
			// Milestone 172 (decision 0314): let go before the leg straightens to hold it — near full length the knee's angle
			// swings with every millimetre the hip moves (a body carried away from a held foot: the legs' acceleration 4
			// times the animation's).
			const Vector3 hip_now = xform.xform(sk->get_bone_global_pose(f.upper).origin);
			const Vector3 knee_now = xform.xform(sk->get_bone_global_pose(f.lower).origin);
			const float leg = hip_now.distance_to(knee_now) + knee_now.distance_to(ankle);
			const Vector3 held = ankle + Vector3(f.lock.x - point.x, 0.0f, f.lock.z - point.z);
			if (hip_now.distance_to(held) > leg * release_extension) {
				f.locked = false;
			}
		}
		if (f.locked) {
			// Held: only across the ground (its height is the animation's: the heel and the ball roll as they would).
			// Milestone 173: a height left from letting go (below) eased out as it would have been, not dropped at once.
			Vector3 lift(0.0f, f.offset.y, 0.0f);
			Vector3 lift_velocity(0.0f, f.offset_velocity.y, 0.0f);
			decay(lift, lift_velocity, blend_halflife, dt);
			const Vector3 new_offset(f.lock.x - point.x, lift.y, f.lock.z - point.z);
			// Milestone 173: the held offset's velocity over the time since the pose last changed. A skeleton may process
			// its modifiers twice in a frame, the time passing in one and the pose changing in the other (an AnimationPlayer
			// advanced by a script before the skeleton's own process): measured in the pass with the time, the velocity was
			// always zero, and a foot let go started back from standing still.
			if (pose_changed) {
				if (pose_dt > 0.0f) {
					const Vector3 v = (new_offset - f.offset) / pose_dt;
					f.offset_velocity.x = v.x;
					f.offset_velocity.z = v.z;
				}
			} else if (f.since_pose > 0.1f) {
				f.offset_velocity.x = 0.0f; // the pose still for a while: not moving
				f.offset_velocity.z = 0.0f;
			}
			f.offset_velocity.y = lift_velocity.y;
			f.offset = new_offset;
		} else {
			if (was_locked && f.has_last && f.last_shown.distance_to(prev_ankle + f.offset) > 0.005f) {
				// Milestone 173: a held point out of the leg's reach is shown where the leg reaches (higher and nearer): let go
				// from there, not from the point (the foot jumped back within reach, below, up to 20 cm in a frame). Across the
				// ground from where it was shown; its height the animation's and what the leg's reach lifted it by.
				f.offset = Vector3(f.last_shown.x - ankle.x, f.last_shown.y - prev_ankle.y, f.last_shown.z - ankle.z);
			}
			if (f.offset.length() > unlock_radius * 3.0f) {
				f.offset = Vector3(); // a jump of the body (placed elsewhere): no blend from where it was
				f.offset_velocity = Vector3();
			}
			decay(f.offset, f.offset_velocity, blend_halflife, dt);
		}
		Vector3 shown = ankle;
		if (f.offset.length_squared() > 1e-10f) {
			const Vector3 knee = sk->get_bone_global_pose(f.lower).origin;
			const Vector3 hip = sk->get_bone_global_pose(f.upper).origin;
			const Vector3 foot_now = sk->get_bone_global_pose(f.foot).origin;
			// Milestone 156 (decision 0296): let go, never eased back from further than the leg reaches — a body speeding
			// away (a sprint from standing) had left the foot behind with the knee straight while it came back, then the knee
			// bent 23 degrees in a frame. The way back starts within REACH_SHARE of the leg's reach, or the animated foot's
			// distance if further.
			const Vector3 a_vec = to_skeleton.xform(ankle) - hip;
			const float reach = MAX((hip.distance_to(knee) + knee.distance_to(foot_now)) * REACH_SHARE, a_vec.length());
			const Vector3 o_vec = to_skeleton.basis.xform(f.offset);
			const Vector3 p_vec = a_vec + o_vec;
			const float p_len = p_vec.length();
			if (!f.locked && p_len > reach) {
				// Towards the animated foot until within reach. Milestone 173: unless that moves it more than 2 cm further
				// than straight towards the hip (a foot let go off to the side: towards the animated foot it took up to 17 cm
				// in a frame) — then straight towards the hip, the least move, its velocity away from the hip dropped.
				const float oo = o_vec.length_squared();
				const float ao = a_vec.dot(o_vec);
				const float disc = ao * ao - oo * (a_vec.length_squared() - reach * reach);
				const float k = (oo > 1e-10f && disc >= 0.0f) ? CLAMP((-ao + Math::sqrt(disc)) / oo, 0.0f, 1.0f) : 0.0f;
				const float toward_animated = o_vec.length() * (1.0f - k);
				const float toward_hip = p_len - reach;
				if (toward_animated <= toward_hip + 0.02f) {
					f.offset *= k;
					f.offset_velocity *= k;
				} else {
					const Vector3 dir = xform.basis.xform(p_vec / p_len).normalized();
					f.offset = xform.basis.xform(p_vec * (reach / p_len) - a_vec);
					const float away = f.offset_velocity.dot(dir);
					if (away > 0.0f) {
						f.offset_velocity -= dir * away;
					}
				}
			}
			const Vector3 target = to_skeleton.xform(ankle + f.offset);
			const Vector3 pole = fellow_leg_pole(sk, hip, knee, foot_now, f.toe);
			// The foot keeps its animated turn.
			fellow_two_bone_ik(sk, f.upper, f.lower, f.foot, target, pole, fellow_bone_global_rotation(sk, f.foot));
			shown = xform.xform(sk->get_bone_global_pose(f.foot).origin);
		}
		if (contact && f.has_last) {
			const Vector3 d = shown - f.last_shown;
			f.slide += Vector2(d.x, d.z).length();
		}
		f.last_shown = shown;
		f.has_last = true;
	}
}

void FellowFootLock3D::set_heel_offset(float p_offset) {
	heel_offset = MAX(0.0f, p_offset);
	_find_bones();
}

void FellowFootLock3D::set_left_foot_bone(const StringName &p_name) {
	foot_bones[0] = p_name;
	_find_bones();
}

void FellowFootLock3D::set_right_foot_bone(const StringName &p_name) {
	foot_bones[1] = p_name;
	_find_bones();
}

void FellowFootLock3D::set_left_toe_bone(const StringName &p_name) {
	toe_bones[0] = p_name;
	_find_bones();
}

void FellowFootLock3D::set_right_toe_bone(const StringName &p_name) {
	toe_bones[1] = p_name;
	_find_bones();
}

bool FellowFootLock3D::is_locked(int p_foot) const {
	ERR_FAIL_INDEX_V(p_foot, 2, false);
	return feet[p_foot].locked;
}

double FellowFootLock3D::get_slide(int p_foot) const {
	ERR_FAIL_INDEX_V(p_foot, 2, 0.0);
	return feet[p_foot].slide;
}

void FellowFootLock3D::reset() {
	for (Foot &f : feet) {
		f.locked = false;
		f.offset = Vector3();
		f.offset_velocity = Vector3();
		f.has_last = false;
		f.since_pose = 0.0f;
		f.slide = 0.0;
	}
}

PackedStringArray FellowFootLock3D::get_configuration_warnings() const {
	PackedStringArray warnings = SkeletonModifier3D::get_configuration_warnings();
	const Skeleton3D *sk = get_skeleton();
	if (sk) {
		for (int s = 0; s < 2; s++) {
			const int foot = sk->find_bone(foot_bones[s]);
			if (foot < 0) {
				warnings.push_back(vformat(RTR("The skeleton has no bone named \"%s\" (%s): that foot is not locked."), foot_bones[s], s == 0 ? RTR("left_foot_bone") : RTR("right_foot_bone")));
			} else if (sk->get_bone_parent(foot) < 0 || sk->get_bone_parent(sk->get_bone_parent(foot)) < 0) {
				warnings.push_back(vformat(RTR("The bone \"%s\" needs a parent and a grandparent (the lower and the upper leg)."), foot_bones[s]));
			}
		}
	}
	return warnings;
}

void FellowFootLock3D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_left_foot_bone", "name"), &FellowFootLock3D::set_left_foot_bone);
	ClassDB::bind_method(D_METHOD("get_left_foot_bone"), &FellowFootLock3D::get_left_foot_bone);
	ClassDB::bind_method(D_METHOD("set_right_foot_bone", "name"), &FellowFootLock3D::set_right_foot_bone);
	ClassDB::bind_method(D_METHOD("get_right_foot_bone"), &FellowFootLock3D::get_right_foot_bone);
	ClassDB::bind_method(D_METHOD("set_left_toe_bone", "name"), &FellowFootLock3D::set_left_toe_bone);
	ClassDB::bind_method(D_METHOD("get_left_toe_bone"), &FellowFootLock3D::get_left_toe_bone);
	ClassDB::bind_method(D_METHOD("set_right_toe_bone", "name"), &FellowFootLock3D::set_right_toe_bone);
	ClassDB::bind_method(D_METHOD("get_right_toe_bone"), &FellowFootLock3D::get_right_toe_bone);
	ClassDB::bind_method(D_METHOD("set_contact_height", "height"), &FellowFootLock3D::set_contact_height);
	ClassDB::bind_method(D_METHOD("get_contact_height"), &FellowFootLock3D::get_contact_height);
	ClassDB::bind_method(D_METHOD("set_heel_offset", "offset"), &FellowFootLock3D::set_heel_offset);
	ClassDB::bind_method(D_METHOD("get_heel_offset"), &FellowFootLock3D::get_heel_offset);
	ClassDB::bind_method(D_METHOD("set_release_extension", "share"), &FellowFootLock3D::set_release_extension);
	ClassDB::bind_method(D_METHOD("get_release_extension"), &FellowFootLock3D::get_release_extension);
	ClassDB::bind_method(D_METHOD("set_unlock_radius", "radius"), &FellowFootLock3D::set_unlock_radius);
	ClassDB::bind_method(D_METHOD("get_unlock_radius"), &FellowFootLock3D::get_unlock_radius);
	ClassDB::bind_method(D_METHOD("set_blend_halflife", "halflife"), &FellowFootLock3D::set_blend_halflife);
	ClassDB::bind_method(D_METHOD("get_blend_halflife"), &FellowFootLock3D::get_blend_halflife);
	ClassDB::bind_method(D_METHOD("is_locked", "foot"), &FellowFootLock3D::is_locked);
	ClassDB::bind_method(D_METHOD("get_slide", "foot"), &FellowFootLock3D::get_slide);
	ClassDB::bind_method(D_METHOD("reset"), &FellowFootLock3D::reset);

	ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "left_foot_bone"), "set_left_foot_bone", "get_left_foot_bone");
	ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "right_foot_bone"), "set_right_foot_bone", "get_right_foot_bone");
	ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "left_toe_bone"), "set_left_toe_bone", "get_left_toe_bone");
	ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "right_toe_bone"), "set_right_toe_bone", "get_right_toe_bone");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "contact_height", PROPERTY_HINT_RANGE, "0,0.2,0.001,suffix:m"), "set_contact_height", "get_contact_height");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "heel_offset", PROPERTY_HINT_RANGE, "0,0.2,0.001,suffix:m"), "set_heel_offset", "get_heel_offset");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "unlock_radius", PROPERTY_HINT_RANGE, "0.01,1,0.01,suffix:m"), "set_unlock_radius", "get_unlock_radius");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "release_extension", PROPERTY_HINT_RANGE, "0.5,1,0.001"), "set_release_extension", "get_release_extension");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "blend_halflife", PROPERTY_HINT_RANGE, "0,0.5,0.001,suffix:s"), "set_blend_halflife", "get_blend_halflife");
}
