/**************************************************************************/
/*  fellow_foot_placement_3d.cpp                                          */
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

#include "fellow_foot_placement_3d.h"

#include "fellow_leg_ik.h"

#include "core/object/class_db.h"
#include "core/os/os.h"
#include "scene/3d/physics/collision_object_3d.h"
#include "scene/3d/skeleton_3d.h"
#include "scene/resources/3d/world_3d.h"
#include "servers/physics_3d/physics_server_3d.h"

namespace {

// A leg reaches its placed foot within this share of its length (or as far as the animation stretches it): further,
// the hips come down.
const float REACH_SHARE = 0.97f;

// Ground offsets smaller than this are none (a body resting a millimetre over the floor does not bend its legs).
const float DEAD_ZONE = 0.002f;

// In the air, the ground is looked at where the ball will be this many seconds ahead (no further than LOOKAHEAD_MAX).
const float LOOKAHEAD = 0.05f;
const float LOOKAHEAD_MAX = 0.15f;
const float SWING_SPEED = 0.5f;

// A goal moving faster than this (m/s) jumped (a step, the body set down a stair): eased, not followed.
const float JUMP_SPEED = 3.0f;

// The velocity of a goal measured, when the pose changes, over the time since it last did (a skeleton may process its
// modifiers twice in a frame, the time passing in one pass and the pose changing in the other).
void track_goal(float p_goal, bool p_pose_changed, float p_since_pose, float &r_prev, float &r_velocity, bool &r_has) {
	if (!r_has) {
		r_prev = p_goal;
		r_velocity = 0.0f;
		r_has = true;
		return;
	}
	if (p_pose_changed) {
		const float v = p_since_pose > 0.0f ? (p_goal - r_prev) / p_since_pose : 0.0f;
		r_velocity = Math::abs(v) > JUMP_SPEED ? 0.0f : v;
		r_prev = p_goal;
	} else if (p_since_pose > 0.1f) {
		r_velocity = 0.0f; // still for a while
	}
}

float smoothstep01(float p_x) {
	const float x = CLAMP(p_x, 0.0f, 1.0f);
	return x * x * (3.0f - 2.0f * x);
}

} // namespace

void FellowFootPlacement3D::_find_bones() {
	bones_valid = false;
	hips = -1;
	Skeleton3D *sk = get_skeleton();
	if (!sk) {
		return;
	}
	bones_valid = true;
	// The ground at rest, as FellowFootLock3D's (decision 0314): the skeleton's origin, or the feet's lowest joint on a
	// rig resting on its hips.
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
		const Transform3D foot_rest = sk->get_bone_global_rest(f.foot);
		const Vector3 ankle = foot_rest.origin;
		const Vector3 toe = f.toe >= 0 ? sk->get_bone_global_rest(f.toe).origin : ankle;
		Vector3 forward = Vector3(toe.x - ankle.x, 0.0f, toe.z - ankle.z);
		forward = forward.length() > 1e-4f ? forward.normalized() : Vector3();
		const Vector3 heel = Vector3(ankle.x, rest_ground, ankle.z) - forward * heel_offset;
		f.heel_local = foot_rest.affine_inverse().xform(heel);
		f.ball_local = foot_rest.affine_inverse().xform(Vector3(toe.x, rest_ground, toe.z));
	}
	if (!bones_valid) {
		return;
	}
	// The hips: the bone both legs hang from.
	for (int a = sk->get_bone_parent(feet[0].upper); a >= 0 && hips < 0; a = sk->get_bone_parent(a)) {
		for (int b = sk->get_bone_parent(feet[1].upper); b >= 0; b = sk->get_bone_parent(b)) {
			if (a == b) {
				hips = a;
				break;
			}
		}
	}
	reset();
}

void FellowFootPlacement3D::_skeleton_changed(Skeleton3D *p_old, Skeleton3D *p_new) {
	SkeletonModifier3D::_skeleton_changed(p_old, p_new);
	_find_bones();
}

void FellowFootPlacement3D::_process_modification(double p_delta) {
	Skeleton3D *sk = get_skeleton();
	if (!sk || !sk->is_inside_tree()) {
		return;
	}
	if (!bones_valid) {
		_find_bones();
		if (!bones_valid) {
			return;
		}
	}
	Ref<World3D> world = sk->get_world_3d();
	PhysicsDirectSpaceState3D *space = world.is_valid() ? world->get_direct_space_state() : nullptr;
	if (!space) {
		return;
	}
	const uint64_t t0 = OS::get_singleton()->get_ticks_usec();
	const float dt = float(MAX(p_delta, 0.0));
	// Where the skeleton is drawn; its origin is the character's ground (the animation's feet stand on it).
	const Transform3D xform = sk->get_global_transform_interpolated();
	const Transform3D to_skeleton = xform.affine_inverse();
	const Vector3 up(0.0f, 1.0f, 0.0f);
	const float ground = xform.origin.y;
	// The character's own bodies are not its ground.
	HashSet<RID> exclude;
	for (Node *n = sk; n; n = n->get_parent()) {
		CollisionObject3D *co = Object::cast_to<CollisionObject3D>(n);
		if (co) {
			exclude.insert(co->get_rid());
		}
	}
	PhysicsDirectSpaceState3D::RayParameters ray;
	ray.collision_mask = collision_mask;
	ray.exclude = exclude;
	ray.collide_with_areas = false;
	ray.hit_back_faces = false;
	const float cos_limit = Math::cos(Math::deg_to_rad(max_alignment_angle));
	// Whether the pose (or the body) moved since the last pass, and the time since it last did.
	since_pose += dt;
	const Vector3 pose_key = xform.xform(sk->get_bone_global_pose(feet[0].foot).origin) + xform.xform(sk->get_bone_global_pose(feet[1].foot).origin) * 2.0f;
	const bool pose_changed = !has_pose_key || !pose_key.is_equal_approx(last_pose_key);
	const float pose_dt = since_pose;
	if (pose_changed) {
		since_pose = 0.0f;
	}
	last_pose_key = pose_key;
	has_pose_key = true;

	Vector3 ankles[2];
	Quaternion foot_globals[2];
	for (int s = 0; s < 2; s++) {
		Foot &f = feet[s];
		const Transform3D foot_pose = sk->get_bone_global_pose(f.foot);
		foot_globals[s] = foot_pose.basis.get_rotation_quaternion();
		const Transform3D foot_xform = xform * foot_pose;
		const Vector3 ankle = foot_xform.origin;
		ankles[s] = ankle;
		const Vector3 heel = foot_xform.xform(f.heel_local);
		const Vector3 ball = f.toe >= 0 ? foot_xform.xform(f.ball_local) : heel;
		const float h_heel = heel.y - ground;
		const float h_ball = ball.y - ground;
		// Down (its sole within contact_height of the animation's ground) to in the swing (swing_height above it).
		const float down = 1.0f - smoothstep01((MIN(h_heel, h_ball) - contact_height) / swing_height);
		// The ground under the heel and the ball: from probe_up above (no higher: not onto a table over the foot) to
		// probe_down below the character's ground.
		const Vector3 points[2] = { heel, ball };
		float g[2] = { 0.0f, 0.0f };
		bool hit[2] = { false, false };
		Vector3 normal;
		for (int k = 0; k < (f.toe >= 0 ? 2 : 1); k++) {
			ray.from = Vector3(points[k].x, MAX(points[k].y, ground) + probe_up, points[k].z);
			ray.to = Vector3(points[k].x, ground - probe_down, points[k].z);
			PhysicsDirectSpaceState3D::RayResult r;
			rays++;
			if (space->intersect_ray(ray, r)) {
				hit[k] = true;
				g[k] = r.position.y - ground;
				normal += r.normal;
			}
		}
		if (f.toe < 0) {
			hit[1] = hit[0];
			g[1] = g[0];
		}
		// A point with no ground under it (over an edge) takes the other's.
		if (hit[0] != hit[1]) {
			const int k = hit[0] ? 0 : 1;
			g[1 - k] = g[k];
		}
		f.hit = hit[0] || hit[1];
		// In the air, the ground a little ahead of the ball too (where the foot is going): a step's edge seen before the
		// toe reaches it (seen only once under the ball, the toe was in the step for the frames the foot took to rise).
		if (pose_changed && pose_dt > 0.0f && f.has_ankle) {
			f.velocity = (ankle - f.last_ankle) / pose_dt;
		}
		f.last_ankle = ankle;
		f.has_ankle = true;
		float g_ahead = -1e9f;
		const Vector3 ahead_step = Vector3(f.velocity.x, 0.0f, f.velocity.z) * LOOKAHEAD;
		// Swinging: moving across the ground faster than SWING_SPEED.
		const bool swinging = Vector2(f.velocity.x, f.velocity.z).length() > SWING_SPEED;
		if (swinging && f.toe >= 0) {
			const Vector3 ahead = ball + ahead_step.limit_length(LOOKAHEAD_MAX);
			ray.from = Vector3(ahead.x, MAX(ahead.y, ground) + probe_up, ahead.z);
			ray.to = Vector3(ahead.x, ground - probe_down, ahead.z);
			PhysicsDirectSpaceState3D::RayResult r;
			rays++;
			if (space->intersect_ray(ray, r)) {
				g_ahead = r.position.y - ground;
			}
		}
		// The sole turned to the ground while the foot is down (no further than max_alignment_angle).
		Vector3 n_goal = up;
		if (f.hit && normal.length_squared() > 1e-8f) {
			n_goal = normal.normalized();
			if (n_goal.dot(up) < cos_limit) {
				const Vector3 side = up.cross(n_goal).normalized();
				n_goal = Quaternion(side, Math::deg_to_rad(max_alignment_angle)).xform(up);
			}
			n_goal = up.slerp(n_goal, down * foot_alignment).normalized();
		}
		Vector3 nx = f.normal - n_goal;
		fellow_spring_decay(nx, f.normal_velocity, lower_halflife, dt);
		f.normal = (n_goal + nx).normalized();
		const Quaternion turn(up, f.normal);
		// Raised so that each point of the sole is as high over the ground under it as the animation has it over the
		// character's ground.
		float goal = 0.0f;
		if (f.hit) {
			const float r_heel = (ankle + turn.xform(heel - ankle)).y - ground;
			const float r_ball = (ankle + turn.xform(ball - ankle)).y - ground;
			goal = MAX(g[0] + h_heel - r_heel, g[1] + h_ball - r_ball);
			goal = MAX(goal, g_ahead + h_ball - r_ball);
			goal = CLAMP(goal, -probe_down, probe_up);
			goal -= CLAMP(goal, -DEAD_ZONE, DEAD_ZONE);
		}
		// Eased in the world: the height the foot stands at (the animation's ground under it raised by the goal), so that a
		// foot held down stays where it is while the body climbs over it (eased relative to the body, it lagged behind:
		// floating on the way up). Up onto higher ground faster than down (a foot landing on a step must not sink into it).
		// Followed with the goal's velocity (along a slope the swinging foot's ground rises as it goes: eased without it,
		// the toe dug into the slope).
		const float base_goal = ground + goal;
		const bool had = f.has_base;
		track_goal(base_goal, pose_changed, pose_dt, f.goal_prev, f.goal_velocity, f.has_base);
		if (!had) {
			f.base = base_goal;
			f.base_velocity = 0.0f;
		}
		// (Down slowly only while the foot is down: in the air it comes down onto a lower step as fast as up onto a higher.)
		const float halflife = (f.base < base_goal || swinging) ? raise_halflife : lower_halflife;
		fellow_spring_follow(f.base, f.base_velocity, base_goal, f.goal_velocity, halflife, dt);
		f.offset = CLAMP(f.base - ground, -probe_down, probe_up);
	}

	// The hips: down as far as the lower foot goes down (eased), and at once as far as a leg needs to reach its foot (the
	// feet's offsets are eased: no jump; eased again, the hips lagged behind the foot left behind on the way up a slope
	// and the leg straightened).
	float drop_goal = MIN(0.0f, MIN(feet[0].offset, feet[1].offset));
	float reach_drop = 0.0f;
	for (int s = 0; s < 2; s++) {
		const Foot &f = feet[s];
		const Vector3 hip = xform.xform(sk->get_bone_global_pose(f.upper).origin);
		const Vector3 knee = xform.xform(sk->get_bone_global_pose(f.lower).origin);
		const Vector3 target = ankles[s] + up * f.offset;
		const float reach = MAX((hip.distance_to(knee) + knee.distance_to(ankles[s])) * REACH_SHARE, hip.distance_to(ankles[s]));
		const float dh = Vector2(target.x - hip.x, target.z - hip.z).length();
		if (dh < reach) {
			const float dv = hip.y - target.y;
			reach_drop = MIN(reach_drop, Math::sqrt(reach * reach - dh * dh) - dv);
		}
	}
	drop_goal = CLAMP(drop_goal, -max_hips_drop, 0.0f);
	{
		// Eased in the world as the feet (the body set down a stair, or onto a stone, does not jolt the hips), following
		// the goal's velocity (no lag up a slope). The hips may be above the animation's for a while (the body set down).
		const float hips_goal = ground + drop_goal;
		const bool had = hips_has;
		track_goal(hips_goal, pose_changed, pose_dt, hips_goal_prev, hips_goal_velocity, hips_has);
		if (!had) {
			hips_world = hips_goal;
			hips_world_velocity = 0.0f;
		}
		fellow_spring_follow(hips_world, hips_world_velocity, hips_goal, hips_goal_velocity, hips_halflife, dt);
		hips_drop = CLAMP(MIN(hips_world - ground, reach_drop), -max_hips_drop, max_hips_drop);
	}

	const bool hips_moved = hips >= 0 && Math::abs(hips_drop) > 1e-5f;
	if (hips_moved) {
		const Vector3 delta = to_skeleton.basis.xform(up * hips_drop);
		const int parent = sk->get_bone_parent(hips);
		const Basis parent_basis = parent >= 0 ? sk->get_bone_global_pose(parent).basis : Basis();
		sk->set_bone_pose_position(hips, sk->get_bone_pose_position(hips) + parent_basis.inverse().xform(delta));
	}
	const Quaternion to_skeleton_rotation = to_skeleton.basis.get_rotation_quaternion();
	const Quaternion xform_rotation = xform.basis.get_rotation_quaternion();
	for (int s = 0; s < 2; s++) {
		const Foot &f = feet[s];
		const bool turned = f.normal.dot(up) < 0.99999f;
		if (!hips_moved && Math::abs(f.offset) < 1e-4f && !turned) {
			continue;
		}
		const Vector3 target = to_skeleton.xform(ankles[s] + up * f.offset);
		const Quaternion foot_global = turned ? (to_skeleton_rotation * Quaternion(up, f.normal) * xform_rotation * foot_globals[s]).normalized() : foot_globals[s];
		const Vector3 hip = sk->get_bone_global_pose(f.upper).origin;
		const Vector3 knee = sk->get_bone_global_pose(f.lower).origin;
		const Vector3 foot_now = sk->get_bone_global_pose(f.foot).origin;
		fellow_two_bone_ik(sk, f.upper, f.lower, f.foot, target, fellow_leg_pole(sk, hip, knee, foot_now, f.toe), foot_global);
	}
	frames++;
	usec += OS::get_singleton()->get_ticks_usec() - t0;
}

void FellowFootPlacement3D::set_left_foot_bone(const StringName &p_name) {
	foot_bones[0] = p_name;
	_find_bones();
}

void FellowFootPlacement3D::set_right_foot_bone(const StringName &p_name) {
	foot_bones[1] = p_name;
	_find_bones();
}

void FellowFootPlacement3D::set_left_toe_bone(const StringName &p_name) {
	toe_bones[0] = p_name;
	_find_bones();
}

void FellowFootPlacement3D::set_right_toe_bone(const StringName &p_name) {
	toe_bones[1] = p_name;
	_find_bones();
}

void FellowFootPlacement3D::set_heel_offset(float p_offset) {
	heel_offset = MAX(0.0f, p_offset);
	_find_bones();
}

float FellowFootPlacement3D::get_foot_offset(int p_foot) const {
	ERR_FAIL_INDEX_V(p_foot, 2, 0.0f);
	return feet[p_foot].offset;
}

Dictionary FellowFootPlacement3D::get_stats() const {
	Dictionary d;
	d["frames"] = (int64_t)frames;
	d["rays"] = (int64_t)rays;
	d["usec_per_frame"] = frames > 0 ? double(usec) / double(frames) : 0.0;
	return d;
}

void FellowFootPlacement3D::reset() {
	for (Foot &f : feet) {
		f.offset = 0.0f;
		f.base = 0.0f;
		f.base_velocity = 0.0f;
		f.has_base = false;
		f.goal_prev = 0.0f;
		f.goal_velocity = 0.0f;
		f.velocity = Vector3();
		f.has_ankle = false;
		f.normal = Vector3(0, 1, 0);
		f.normal_velocity = Vector3();
		f.hit = false;
	}
	hips_drop = 0.0f;
	hips_world = 0.0f;
	hips_world_velocity = 0.0f;
	hips_has = false;
	hips_goal_prev = 0.0f;
	hips_goal_velocity = 0.0f;
	since_pose = 0.0f;
	has_pose_key = false;
}

PackedStringArray FellowFootPlacement3D::get_configuration_warnings() const {
	PackedStringArray warnings = SkeletonModifier3D::get_configuration_warnings();
	const Skeleton3D *sk = get_skeleton();
	if (sk) {
		for (int s = 0; s < 2; s++) {
			const int foot = sk->find_bone(foot_bones[s]);
			if (foot < 0) {
				warnings.push_back(vformat(RTR("The skeleton has no bone named \"%s\" (%s): that foot is not placed."), foot_bones[s], s == 0 ? RTR("left_foot_bone") : RTR("right_foot_bone")));
			} else if (sk->get_bone_parent(foot) < 0 || sk->get_bone_parent(sk->get_bone_parent(foot)) < 0) {
				warnings.push_back(vformat(RTR("The bone \"%s\" needs a parent and a grandparent (the lower and the upper leg)."), foot_bones[s]));
			}
		}
		if (bones_valid && hips < 0) {
			warnings.push_back(RTR("The legs hang from no common bone: the hips are not lowered."));
		}
	}
	if (collision_mask == 0) {
		warnings.push_back(RTR("collision_mask is empty: no ground is found."));
	}
	return warnings;
}

void FellowFootPlacement3D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_left_foot_bone", "name"), &FellowFootPlacement3D::set_left_foot_bone);
	ClassDB::bind_method(D_METHOD("get_left_foot_bone"), &FellowFootPlacement3D::get_left_foot_bone);
	ClassDB::bind_method(D_METHOD("set_right_foot_bone", "name"), &FellowFootPlacement3D::set_right_foot_bone);
	ClassDB::bind_method(D_METHOD("get_right_foot_bone"), &FellowFootPlacement3D::get_right_foot_bone);
	ClassDB::bind_method(D_METHOD("set_left_toe_bone", "name"), &FellowFootPlacement3D::set_left_toe_bone);
	ClassDB::bind_method(D_METHOD("get_left_toe_bone"), &FellowFootPlacement3D::get_left_toe_bone);
	ClassDB::bind_method(D_METHOD("set_right_toe_bone", "name"), &FellowFootPlacement3D::set_right_toe_bone);
	ClassDB::bind_method(D_METHOD("get_right_toe_bone"), &FellowFootPlacement3D::get_right_toe_bone);
	ClassDB::bind_method(D_METHOD("set_collision_mask", "mask"), &FellowFootPlacement3D::set_collision_mask);
	ClassDB::bind_method(D_METHOD("get_collision_mask"), &FellowFootPlacement3D::get_collision_mask);
	ClassDB::bind_method(D_METHOD("set_probe_up", "height"), &FellowFootPlacement3D::set_probe_up);
	ClassDB::bind_method(D_METHOD("get_probe_up"), &FellowFootPlacement3D::get_probe_up);
	ClassDB::bind_method(D_METHOD("set_probe_down", "depth"), &FellowFootPlacement3D::set_probe_down);
	ClassDB::bind_method(D_METHOD("get_probe_down"), &FellowFootPlacement3D::get_probe_down);
	ClassDB::bind_method(D_METHOD("set_heel_offset", "offset"), &FellowFootPlacement3D::set_heel_offset);
	ClassDB::bind_method(D_METHOD("get_heel_offset"), &FellowFootPlacement3D::get_heel_offset);
	ClassDB::bind_method(D_METHOD("set_contact_height", "height"), &FellowFootPlacement3D::set_contact_height);
	ClassDB::bind_method(D_METHOD("get_contact_height"), &FellowFootPlacement3D::get_contact_height);
	ClassDB::bind_method(D_METHOD("set_swing_height", "height"), &FellowFootPlacement3D::set_swing_height);
	ClassDB::bind_method(D_METHOD("get_swing_height"), &FellowFootPlacement3D::get_swing_height);
	ClassDB::bind_method(D_METHOD("set_max_hips_drop", "drop"), &FellowFootPlacement3D::set_max_hips_drop);
	ClassDB::bind_method(D_METHOD("get_max_hips_drop"), &FellowFootPlacement3D::get_max_hips_drop);
	ClassDB::bind_method(D_METHOD("set_foot_alignment", "weight"), &FellowFootPlacement3D::set_foot_alignment);
	ClassDB::bind_method(D_METHOD("get_foot_alignment"), &FellowFootPlacement3D::get_foot_alignment);
	ClassDB::bind_method(D_METHOD("set_max_alignment_angle", "degrees"), &FellowFootPlacement3D::set_max_alignment_angle);
	ClassDB::bind_method(D_METHOD("get_max_alignment_angle"), &FellowFootPlacement3D::get_max_alignment_angle);
	ClassDB::bind_method(D_METHOD("set_raise_halflife", "halflife"), &FellowFootPlacement3D::set_raise_halflife);
	ClassDB::bind_method(D_METHOD("get_raise_halflife"), &FellowFootPlacement3D::get_raise_halflife);
	ClassDB::bind_method(D_METHOD("set_lower_halflife", "halflife"), &FellowFootPlacement3D::set_lower_halflife);
	ClassDB::bind_method(D_METHOD("get_lower_halflife"), &FellowFootPlacement3D::get_lower_halflife);
	ClassDB::bind_method(D_METHOD("set_hips_halflife", "halflife"), &FellowFootPlacement3D::set_hips_halflife);
	ClassDB::bind_method(D_METHOD("get_hips_halflife"), &FellowFootPlacement3D::get_hips_halflife);
	ClassDB::bind_method(D_METHOD("get_foot_offset", "foot"), &FellowFootPlacement3D::get_foot_offset);
	ClassDB::bind_method(D_METHOD("get_hips_drop"), &FellowFootPlacement3D::get_hips_drop);
	ClassDB::bind_method(D_METHOD("get_stats"), &FellowFootPlacement3D::get_stats);
	ClassDB::bind_method(D_METHOD("reset"), &FellowFootPlacement3D::reset);

	ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "left_foot_bone"), "set_left_foot_bone", "get_left_foot_bone");
	ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "right_foot_bone"), "set_right_foot_bone", "get_right_foot_bone");
	ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "left_toe_bone"), "set_left_toe_bone", "get_left_toe_bone");
	ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "right_toe_bone"), "set_right_toe_bone", "get_right_toe_bone");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "collision_mask", PROPERTY_HINT_LAYERS_3D_PHYSICS), "set_collision_mask", "get_collision_mask");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "probe_up", PROPERTY_HINT_RANGE, "0,2,0.01,suffix:m"), "set_probe_up", "get_probe_up");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "probe_down", PROPERTY_HINT_RANGE, "0,2,0.01,suffix:m"), "set_probe_down", "get_probe_down");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "heel_offset", PROPERTY_HINT_RANGE, "0,0.2,0.001,suffix:m"), "set_heel_offset", "get_heel_offset");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "contact_height", PROPERTY_HINT_RANGE, "0,0.3,0.001,suffix:m"), "set_contact_height", "get_contact_height");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "swing_height", PROPERTY_HINT_RANGE, "0.01,1,0.01,suffix:m"), "set_swing_height", "get_swing_height");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "max_hips_drop", PROPERTY_HINT_RANGE, "0,1,0.01,suffix:m"), "set_max_hips_drop", "get_max_hips_drop");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "foot_alignment", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_foot_alignment", "get_foot_alignment");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "max_alignment_angle", PROPERTY_HINT_RANGE, "0,89,0.1,degrees"), "set_max_alignment_angle", "get_max_alignment_angle");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "raise_halflife", PROPERTY_HINT_RANGE, "0,0.5,0.001,suffix:s"), "set_raise_halflife", "get_raise_halflife");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "lower_halflife", PROPERTY_HINT_RANGE, "0,0.5,0.001,suffix:s"), "set_lower_halflife", "get_lower_halflife");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "hips_halflife", PROPERTY_HINT_RANGE, "0,0.5,0.001,suffix:s"), "set_hips_halflife", "get_hips_halflife");
}
