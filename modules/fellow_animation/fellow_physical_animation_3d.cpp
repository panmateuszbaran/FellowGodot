/**************************************************************************/
/*  fellow_physical_animation_3d.cpp                                      */
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

#include "fellow_physical_animation_3d.h"

#include "fellow_motion_database.h"
#include "fellow_motion_matcher_3d.h"

#include "core/config/engine.h"
#include "core/config/project_settings.h"
#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "core/os/os.h"
#include "core/variant/typed_array.h"
#include "scene/3d/physics/collision_shape_3d.h"
#include "scene/3d/physics/physical_bone_3d.h"
#include "scene/3d/physics/physical_bone_simulator_3d.h"
#include "scene/3d/skeleton_3d.h"
#include "scene/resources/3d/capsule_shape_3d.h"
#include "scene/resources/3d/sphere_shape_3d.h"
#include "scene/resources/3d/world_3d.h"
#include "servers/physics_3d/physics_server_3d.h"

static Vector3 _rotation_vector(const Basis &p_basis) {
	Quaternion q = p_basis.get_rotation_quaternion();
	if (q.w < 0.0f) {
		q = -q;
	}
	const real_t s = Math::sqrt(MAX(1.0 - q.w * q.w, 0.0));
	if (s < 1e-6) {
		return Vector3(q.x, q.y, q.z) * 2.0;
	}
	const real_t angle = 2.0 * Math::acos(CLAMP(q.w, (real_t)-1.0, (real_t)1.0));
	return Vector3(q.x, q.y, q.z) / s * angle;
}

FellowPhysicalAnimation3D::FellowPhysicalAnimation3D() {
}

PhysicalBoneSimulator3D *FellowPhysicalAnimation3D::_get_simulator() const {
	return ObjectDB::get_instance<PhysicalBoneSimulator3D>(simulator_id);
}

void FellowPhysicalAnimation3D::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_READY: {
			set_physics_process_internal(true);
			if (!Engine::get_singleton()->is_editor_hint() && !built) {
				callable_mp(this, &FellowPhysicalAnimation3D::_auto_build).call_deferred();
			}
		} break;
		case NOTIFICATION_INTERNAL_PHYSICS_PROCESS: {
			_drive(get_physics_process_delta_time());
		} break;
		case NOTIFICATION_EXIT_TREE: {
			// The bodies go with the skeleton; nothing of ours to keep.
		} break;
	}
}

PackedStringArray FellowPhysicalAnimation3D::get_configuration_warnings() const {
	PackedStringArray warnings = SkeletonModifier3D::get_configuration_warnings();
	const Skeleton3D *skeleton = get_skeleton();
	if (skeleton && skeleton->get_modifier_callback_mode_process() != Skeleton3D::MODIFIER_CALLBACK_MODE_PROCESS_PHYSICS) {
		warnings.push_back(RTR("The skeleton updates its modifiers in the idle frame: set its Modifier Callback Mode Process to Physics, so that the bodies follow each physics step's pose."));
	}
	if (String(GLOBAL_GET("physics/3d/physics_engine")) != "Jolt Physics") {
		warnings.push_back(RTR("The physics engine is not Jolt Physics: Godot Physics' joints jitter in a ragdoll (strength under 0.5). Driven bodies and hits work with either."));
	}
	return warnings;
}

void FellowPhysicalAnimation3D::_auto_build() {
	if (!built) {
		build();
	}
}

PackedFloat32Array FellowPhysicalAnimation3D::get_body_errors() const {
	// Each body's distance from where the animation has it (metres).
	PackedFloat32Array e;
	for (const Body &b : bodies) {
		e.push_back(b.has_target ? float((b.target.origin - b.node->get_global_transform().origin).length()) : 0.0f);
	}
	return e;
}

TypedArray<Transform3D> FellowPhysicalAnimation3D::get_body_targets() const {
	// Where the animation has each body (world).
	TypedArray<Transform3D> t;
	for (const Body &b : bodies) {
		t.push_back(b.target);
	}
	return t;
}

PackedFloat32Array FellowPhysicalAnimation3D::get_body_weakness() const {
	PackedFloat32Array w;
	for (const Body &b : bodies) {
		w.push_back(b.weakness);
	}
	return w;
}

PackedStringArray FellowPhysicalAnimation3D::get_body_bones() const {
	PackedStringArray names;
	const Skeleton3D *skeleton = get_skeleton();
	for (const Body &b : bodies) {
		names.push_back(skeleton ? skeleton->get_bone_name(b.bone) : String());
	}
	return names;
}

void FellowPhysicalAnimation3D::clear() {
	PhysicalBoneSimulator3D *sim = _get_simulator();
	if (sim) {
		sim->physical_bones_stop_simulation();
		sim->queue_free();
	}
	simulator = nullptr;
	simulator_id = ObjectID();
	bodies.clear();
	built = false;
}

void FellowPhysicalAnimation3D::build() {
	// The bodies from the skeleton's rest pose (as the editor's "Create Physical Skeleton" makes them, by code): a
	// capsule along each bone long enough (to its farthest child), a sphere for the root and for a leaf bone named like
	// a head; masses by volume, together `total_mass`; each joined to the nearest body up the bones by a cone joint.
	clear();
	Skeleton3D *skeleton = get_skeleton();
	ERR_FAIL_NULL_MSG(skeleton, "FellowPhysicalAnimation3D must be a child of a Skeleton3D.");
	PhysicalBoneSimulator3D *sim = memnew(PhysicalBoneSimulator3D);
	sim->set_name("FellowPhysicalBones");
	skeleton->add_child(sim);
	skeleton->move_child(sim, get_index() + 1);
	simulator = sim;
	simulator_id = sim->get_instance_id();

	const int bone_count = skeleton->get_bone_count();
	LocalVector<float> volumes;
	LocalVector<int> body_of_bone;
	body_of_bone.resize(bone_count);
	// Each bone's farthest child (the way the bone goes) and the rest pose's heights.
	LocalVector<Vector3> to_child_of;
	LocalVector<Vector3> rest_global;
	to_child_of.resize(bone_count);
	rest_global.resize(bone_count);
	for (int i = 0; i < bone_count; i++) {
		body_of_bone[i] = -1;
		Vector3 to_child;
		for (int c : skeleton->get_bone_children(i)) {
			const Vector3 o = skeleton->get_bone_rest(c).origin;
			if (o.length() > to_child.length()) {
				to_child = o;
			}
		}
		to_child_of[i] = to_child;
		rest_global[i] = skeleton->get_bone_global_rest(i).origin;
	}
	// The trunk: the chain from the root to the topmost bone (the head's end), below 80 % of its height — its bodies
	// as thick as a torso.
	int top = 0;
	for (int i = 0; i < bone_count; i++) {
		if (rest_global[i].y > rest_global[top].y) {
			top = i;
		}
	}
	LocalVector<bool> trunk;
	trunk.resize(bone_count);
	for (int i = 0; i < bone_count; i++) {
		trunk[i] = false;
	}
	const float root_y = rest_global[0].y;
	for (int b = top; b >= 0; b = skeleton->get_bone_parent(b)) {
		if (rest_global[b].y - root_y < 0.8f * (rest_global[top].y - root_y)) {
			trunk[b] = true;
		}
	}
	// Which bones get a body: long enough; the root; then every bone between two bodies (no gap in a chain, or a
	// joint's anchor would move as the bone in the gap turns); a leaf head with no body above it.
	LocalVector<int> kind; // 0 none, 1 capsule, 2 sphere
	kind.resize(bone_count);
	for (int b = 0; b < bone_count; b++) {
		kind[b] = to_child_of[b].length() >= min_bone_length ? 1 : (skeleton->get_bone_parent(b) < 0 ? 2 : 0);
	}
	auto has_body_below = [&](int p_bone) {
		LocalVector<int> stack;
		for (int c : skeleton->get_bone_children(p_bone)) {
			stack.push_back(c);
		}
		while (!stack.is_empty()) {
			const int x = stack[stack.size() - 1];
			stack.resize(stack.size() - 1);
			if (kind[x] != 0) {
				return true;
			}
			for (int c : skeleton->get_bone_children(x)) {
				stack.push_back(c);
			}
		}
		return false;
	};
	for (int b = 0; b < bone_count; b++) {
		if (kind[b] != 0) {
			continue;
		}
		bool above = false;
		for (int p = skeleton->get_bone_parent(b); p >= 0; p = skeleton->get_bone_parent(p)) {
			if (kind[p] != 0) {
				above = true;
				break;
			}
		}
		if (above && has_body_below(b)) {
			kind[b] = to_child_of[b].length() > 0.01f ? 1 : 2;
		} else if (skeleton->get_bone_children(b).is_empty() && skeleton->get_bone_name(b).containsn("head") && skeleton->get_bone_parent(b) >= 0 && kind[skeleton->get_bone_parent(b)] == 0) {
			kind[b] = 2;
		}
	}
	for (int b = 0; b < bone_count; b++) {
		if (kind[b] == 0) {
			continue;
		}
		const Vector3 to_child = to_child_of[b];
		const float len = to_child.length();
		const String name = skeleton->get_bone_name(b);
		Body body;
		body.bone = b;
		Ref<Shape3D> shape;
		Transform3D shape_transform;
		Transform3D joint_offset;
		if (kind[b] == 1) {
			const float r = trunk[b] ? 0.11f : CLAMP(len * 0.18f, 0.025f, 0.11f);
			Ref<CapsuleShape3D> capsule;
			capsule.instantiate();
			capsule->set_radius(r);
			capsule->set_height(MAX(len, 2.0f * r + 0.01f));
			shape = capsule;
			// The capsule's axis (Y) along the body's -Z, which looks down the bone to its child.
			shape_transform.basis.rows[0] = Vector3(1, 0, 0);
			shape_transform.basis.rows[1] = Vector3(0, 0, 1);
			shape_transform.basis.rows[2] = Vector3(0, -1, 0);
			Vector3 up(0, 1, 0);
			if (up.cross(to_child).is_zero_approx()) {
				up = Vector3(0, 0, 1);
			}
			body.offset.basis = Basis::looking_at(to_child, up);
			body.offset.origin = body.offset.basis.xform(Vector3(0, 0, -len * 0.5f));
			joint_offset.origin = Vector3(0, 0, len * 0.5f);
			body.radius = r;
			body.length = len;
			volumes.push_back(Math::PI * r * r * len + 4.0f / 3.0f * Math::PI * r * r * r);
		} else {
			const bool root = skeleton->get_bone_parent(b) < 0;
			const bool head = skeleton->get_bone_children(b).is_empty();
			const float r = root ? 0.12f : (head ? 0.1f : 0.03f);
			Ref<SphereShape3D> sphere;
			sphere.instantiate();
			sphere->set_radius(r);
			shape = sphere;
			if (head) {
				// The head's sphere on above the neck: along the way from the parent to this bone.
				Vector3 dir = skeleton->get_bone_rest(b).basis.xform_inv(skeleton->get_bone_rest(b).origin);
				dir = dir.length() > 1e-5f ? dir.normalized() : Vector3(0, 1, 0);
				body.offset.origin = dir * (r * 0.9f);
				joint_offset.origin = -body.offset.origin;
			}
			body.radius = r;
			body.length = 0.0f;
			volumes.push_back(4.0f / 3.0f * Math::PI * r * r * r);
		}
		PhysicalBone3D *pb = memnew(PhysicalBone3D);
		pb->set_name("Body_" + name);
		CollisionShape3D *cs = memnew(CollisionShape3D);
		cs->set_name("Shape");
		cs->set_shape(shape);
		cs->set_transform(shape_transform);
		pb->add_child(cs);
		pb->set_bone_name(name);
		pb->set_body_offset(body.offset);
		pb->set_joint_offset(joint_offset);
		pb->set_collision_layer(collision_layer);
		pb->set_collision_mask(collision_mask);
		pb->set_friction(0.8f);
		pb->set_linear_damp(0.1f);
		pb->set_angular_damp(0.5f);
		pb->set_can_sleep(false);
		body.node = pb;
		body_of_bone[b] = bodies.size();
		bodies.push_back(body);
	}
	float volume = 0.0f;
	for (float v : volumes) {
		volume += v;
	}
	for (uint32_t i = 0; i < bodies.size(); i++) {
		Body &body = bodies[i];
		for (int p = skeleton->get_bone_parent(body.bone); p >= 0; p = skeleton->get_bone_parent(p)) {
			if (body_of_bone[p] >= 0) {
				body.parent = body_of_bone[p];
				break;
			}
		}
		body.node->set_mass(MAX(total_mass * volumes[i] / MAX(volume, 1e-6f), 0.05f));
		if (body.parent >= 0) {
			body.node->set_joint_type(PhysicalBone3D::JOINT_TYPE_CONE);
		}
		sim->add_child(body.node);
		if (body.parent >= 0) {
			// The twist axis (the joint's X) down the bone (set once: setting it puts the body at its rest pose).
			body.node->set_joint_rotation(Vector3(0, Math::PI * 0.5f, 0));
		}
	}
	built = true;
	// The bodies are put where the animation has their bones, and simulated, at the next pose (now the skeleton may
	// still hold its rest pose, its feet under the ground).
	pending_start = true;
	update_configuration_warnings();
}

void FellowPhysicalAnimation3D::_apply_simulate() {
	PhysicalBoneSimulator3D *sim = _get_simulator();
	if (!sim) {
		return;
	}
	if (simulate) {
		sim->physical_bones_start_simulation_on(TypedArray<StringName>());
	} else {
		sim->physical_bones_stop_simulation();
	}
}

void FellowPhysicalAnimation3D::set_simulate(bool p_simulate) {
	if (simulate == p_simulate) {
		return;
	}
	simulate = p_simulate;
	_apply_simulate();
	if (simulate) {
		reset_bodies();
	}
}

void FellowPhysicalAnimation3D::reset_bodies() {
	// Every body where the animation (or the rest pose, before it has one) has its bone, at rest.
	Skeleton3D *skeleton = get_skeleton();
	if (!skeleton) {
		return;
	}
	const Transform3D sk = skeleton->get_global_transform();
	for (Body &b : bodies) {
		const Transform3D t = b.has_target ? b.target : sk * skeleton->get_bone_global_pose(b.bone) * b.offset;
		b.node->set_global_transform(t.orthonormalized());
		PhysicsServer3D::get_singleton()->body_set_state(b.node->get_rid(), PhysicsServer3D::BODY_STATE_TRANSFORM, t.orthonormalized());
		b.node->set_linear_velocity(Vector3());
		b.node->set_angular_velocity(Vector3());
		b.has_previous = false;
		b.weakness = 0.0f;
	}
	_rebuild_joints();
}

void FellowPhysicalAnimation3D::_rebuild_joints() {
	// The joints made anew from the bodies' places now (a joint's anchors are fixed in both bodies when it is made:
	// made before the bodies were put where the animation has them, they would pull the bodies apart).
	// Milestone 183: first each body put back on its parent's joint (its bone where the parent's bone has it at rest;
	// parents first) — a joint pulled open as the body went limp or got up (a hit, an impact: 3-6 cm) was made anew
	// that far open, and stayed so.
	Skeleton3D *skeleton = get_skeleton();
	if (skeleton && balance) {
		for (Body &b : bodies) {
			if (b.parent < 0 || skeleton->get_bone_parent(b.bone) != bodies[b.parent].bone) {
				continue;
			}
			const Body &p = bodies[b.parent];
			const Vector3 rest = (skeleton->get_bone_global_rest(p.bone).affine_inverse() * skeleton->get_bone_global_rest(b.bone)).origin;
			const Transform3D parent_bone = p.node->get_global_transform() * p.offset.affine_inverse();
			Transform3D t = b.node->get_global_transform();
			const Vector3 gap = parent_bone.xform(rest) - (t * b.offset.affine_inverse()).origin;
			if (gap.length() > 0.002f) {
				t.origin += gap;
				b.node->set_global_transform(t);
				PhysicsServer3D::get_singleton()->body_set_state(b.node->get_rid(), PhysicsServer3D::BODY_STATE_TRANSFORM, t);
			}
		}
	}
	for (Body &b : bodies) {
		if (b.parent < 0) {
			continue;
		}
		b.node->set_joint_type(PhysicalBone3D::JOINT_TYPE_NONE);
		if (limp) {
			// Limp (a ragdoll): cones about the pose it went limp in.
			b.node->set_joint_type(PhysicalBone3D::JOINT_TYPE_CONE);
			b.node->set("joint_constraints/swing_span", Math::rad_to_deg(swing_limit));
			b.node->set("joint_constraints/twist_span", Math::rad_to_deg(twist_limit));
		} else {
			// Driven: pins (the bones held together, their angles the animation's — a limit about one pose would fight
			// every other one).
			b.node->set_joint_type(PhysicalBone3D::JOINT_TYPE_PIN);
		}
	}
}

void FellowPhysicalAnimation3D::_process_modification(double p_delta) {
	// The animation's pose (the modifiers before this one): each body's target.
	Skeleton3D *skeleton = get_skeleton();
	if (!skeleton || !built) {
		return;
	}
	const Transform3D sk = skeleton->get_global_transform();
	for (Body &b : bodies) {
		b.target = sk * skeleton->get_bone_global_pose(b.bone) * b.offset;
		b.target.orthonormalize();
		b.has_target = true;
	}
	// Milestone 183 (decision 0325): the balance's states put their own targets in place of the animation's.
	if (state == STATE_FALLING && state_strength > 0.0f && fall_targets.size() == bodies.size()) {
		// The stiff fall: the pose it began in, turned about the feet.
		const Basis r(fall_axis, fall_angle);
		for (uint32_t i = 0; i < bodies.size(); i++) {
			bodies[i].target = Transform3D(r * fall_targets[i].basis, fall_pivot + r.xform(fall_targets[i].origin - fall_pivot));
		}
	} else if (state == STATE_GETTING_UP && gu_clip >= 0) {
		FellowMotionMatcher3D *mm = _get_matcher();
		const Ref<FellowMotionDatabase> db = mm ? mm->get_database() : Ref<FellowMotionDatabase>();
		if (db.is_valid()) {
			const GetUpClip &clip = get_up_clips[gu_clip];
			const int f = CLAMP(int(gu_frame), clip.start, clip.stop);
			_clip_pose(db.ptr(), f);
			// From the ragdoll's pose into the clip's (smoothstep over get_up_blend).
			const float x = CLAMP(state_time / get_up_blend, 0.0f, 1.0f);
			const float w = x * x * (3.0f - 2.0f * x);
			for (uint32_t i = 0; i < bodies.size(); i++) {
				Transform3D t = gu_anchor * clip_globals[bodies[i].bone] * bodies[i].offset;
				t.orthonormalize();
				if (w < 1.0f && i < gu_snapshot.size()) {
					t = gu_snapshot[i].interpolate_with(t, w);
				}
				bodies[i].target = t;
			}
		}
	} else if (handing_over && handover_from.size() == bodies.size()) {
		// From the clip's last pose to the motion matcher's.
		const float x = CLAMP(handover_time / handover_blend, 0.0f, 1.0f);
		const float w = x * x * (3.0f - 2.0f * x);
		for (uint32_t i = 0; i < bodies.size(); i++) {
			bodies[i].target = handover_from[i].interpolate_with(bodies[i].target, w);
		}
	}
	targets_fresh = true;
	if (pending_start) {
		pending_start = false;
		_apply_simulate();
		reset_bodies();
	}
}

void FellowPhysicalAnimation3D::_drive(double p_delta) {
	if (!built || !simulate || p_delta <= 0.0) {
		return;
	}
	const uint64_t t0 = OS::get_singleton()->get_ticks_usec();
	const float dt = float(p_delta);
	const float decay = Math::exp(-0.69314718056f * dt / recovery_halflife);
	if (balance) {
		_update_balance(dt);
	}
	const float drive_strength = strength * state_strength;
	// Going limp (strength under a half) or back: the joints made anew for it, from the pose now.
	if ((drive_strength < 0.5f) != limp) {
		limp = drive_strength < 0.5f;
		_rebuild_joints();
	}
	for (uint32_t bi = 0; bi < bodies.size(); bi++) {
		Body &b = bodies[bi];
		b.weakness *= decay;
		if (!b.has_target) {
			continue;
		}
		PhysicalBone3D *pb = b.node;
		const Transform3D cur = pb->get_global_transform().orthonormalized();
		// Feed-forward: the animation's own motion since the last step (none while the pose has not changed).
		Vector3 v_ff;
		Vector3 w_ff;
		if (b.has_previous && targets_fresh) {
			v_ff = (b.target.origin - b.previous_target.origin) / dt;
			w_ff = _rotation_vector(b.target.basis * b.previous_target.basis.inverse()) / dt;
		}
		const Vector3 v_goal = v_ff + (b.target.origin - cur.origin) * (position_gain / dt);
		const Vector3 w_goal = w_ff + _rotation_vector(b.target.basis * cur.basis.inverse()) * (rotation_gain / dt);
		const float s = drive_strength * (1.0f - b.weakness);
		// The body's velocity as the physics server has it now (a hit's impulse in it; the node's copy is the last
		// step's), less the gravity the step will add as far as the drive holds the body (else it would hang low).
		Vector3 gravity;
		Vector3 v_now = pb->get_linear_velocity();
		Vector3 w_now = pb->get_angular_velocity();
		PhysicsDirectBodyState3D *state = PhysicsServer3D::get_singleton()->body_get_direct_state(pb->get_rid());
		if (state) {
			gravity = state->get_total_gravity();
			v_now = state->get_linear_velocity();
			w_now = state->get_angular_velocity();
		}
		// The drive's share of the step: the strength cubed, so a weakened body is truly let go of (a straight share
		// of 0.15 a step at 60 Hz still takes back most of a hit within a tenth of a second).
		const float f = s * s * s;
		const Vector3 v = v_now.lerp(v_goal - gravity * dt, f);
		const Vector3 w = w_now.lerp(w_goal, f);
		pb->set_linear_velocity(v);
		pb->set_angular_velocity(w);
		// A body held fast far from its bone (caught behind a wall, under the ground) is put back on it.
		if (s > 0.5f && (b.target.origin - cur.origin).length() > rescue_distance) {
			PhysicsServer3D::get_singleton()->body_set_state(pb->get_rid(), PhysicsServer3D::BODY_STATE_TRANSFORM, b.target);
			pb->set_linear_velocity(v_ff);
			pb->set_angular_velocity(w_ff);
			rescues++;
		}
		speed_max = MAX(speed_max, float(v.length()));
		if (s > 0.99f && b.has_previous) {
			const float e = (b.target.origin - cur.origin).length();
			error_sum += e;
			error_angle_sum += _rotation_vector(b.target.basis * cur.basis.inverse()).length();
			error_samples += 1.0;
			error_max = MAX(error_max, e);
		}
		if (targets_fresh) {
			b.previous_target = b.target;
			b.has_previous = true;
		}
	}
	targets_fresh = false;
	drive_usec += OS::get_singleton()->get_ticks_usec() - t0;
	drive_steps++;
}

void FellowPhysicalAnimation3D::apply_hit(const Vector3 &p_position, const Vector3 &p_impulse) {
	// The body nearest the point (by its capsule's axis) takes the impulse there; it and its neighbours give way for a
	// moment (the struck bone by `hit_weakening`, each link away half as much, two links at most).
	int best = -1;
	float best_d = 1e20f;
	for (uint32_t i = 0; i < bodies.size(); i++) {
		const Transform3D t = bodies[i].node->get_global_transform();
		const Vector3 a = t.xform(Vector3(0, 0, -bodies[i].length * 0.5f));
		const Vector3 c = t.xform(Vector3(0, 0, bodies[i].length * 0.5f));
		const Vector3 ab = c - a;
		const float u = ab.length_squared() > 1e-8f ? CLAMP((p_position - a).dot(ab) / ab.length_squared(), 0.0f, 1.0f) : 0.0f;
		const float d = (p_position - (a + ab * u)).length() - bodies[i].radius;
		if (d < best_d) {
			best_d = d;
			best = i;
		}
	}
	if (best < 0) {
		return;
	}
	Body &b = bodies[best];
	stat_hits++;
	if (balance && (state == STATE_STANDING || state == STATE_STAGGERING) && strength >= 0.5f) {
		// Milestone 183 (decision 0325): where the centre of mass would have to be stood over to stop — the capture
		// point (Pratt 2006): its offset from the animation's plus its velocity, the hit's share of the whole body's
		// mass added, over sqrt(g / h). Near: the drive takes the hit back where it stands; further: a stagger, the
		// motion matcher pushed that way for as long as an inverted pendulum takes to be caught (about two steps);
		// beyond a step's reach: a fall.
		Vector3 com, vel, tgt;
		float feet_y = 0.0f;
		_centre_of_mass(com, vel, tgt, feet_y);
		float mass = 0.0f;
		for (const Body &c : bodies) {
			mass += c.node->get_mass();
		}
		const float h = MAX(com.y - feet_y, 0.3f);
		const float omega = Math::sqrt(9.81f / h);
		Vector3 dv = p_impulse / MAX(mass, 1.0f);
		dv.y = 0.0f;
		Vector3 offset = com - tgt;
		offset.y = 0.0f;
		const Vector3 capture = offset + dv / omega;
		const float c = capture.length();
		stat_capture_offset = c;
		// The push moves the whole body (the joints carry it, as a person's held body does): spread over every body by
		// its mass — all of it for a fall (the body topples whole); for a stagger or a flinch the struck part a share
		// more, as much as kicks it 1.5 m/s faster than the rest (at most 30 %). All of it on one body (a chest's 10 kg at
		// 23 m/s from 230 N·s) tore the ragdoll's joints 11 cm apart; 30 % of 130 N·s, 13 cm.
		const bool falls = c > fall_threshold;
		const float local = falls ? 0.0f : MIN(0.3f, 1.5f * b.node->get_mass() / MAX(float(p_impulse.length()), 1e-3f));
		if (local > 0.0f) {
			b.node->apply_impulse(p_impulse * local, p_position - b.node->get_global_position());
		}
		for (Body &e : bodies) {
			e.node->apply_central_impulse(p_impulse * ((1.0f - local) * e.node->get_mass() / MAX(mass, 1e-3f)));
		}
		if (falls) {
			_fall(capture / c, dv.length());
			return;
		}
		if (c > step_threshold) {
			FellowMotionMatcher3D *mm = _get_matcher();
			if (mm) {
				// The steps carry the body about as far as the capture point, decaying over a little more than twice
				// the pendulum's time.
				const float duration = 2.6f / omega;
				mm->push(capture / c * (2.0f * c / duration * 0.8f), duration);
				stagger_end = duration;
			}
			_set_state(STATE_STAGGERING);
			stat_staggers++;
			emit_signal(SNAME("staggered"), capture / c, c);
		}
	} else {
		b.node->apply_impulse(p_impulse, p_position - b.node->get_global_position());
	}
	b.weakness = MAX(b.weakness, hit_weakening);
	// Up the chain and down to the children.
	float w = hit_weakening * 0.5f;
	for (int p = b.parent, links = 0; p >= 0 && links < 2; p = bodies[p].parent, links++, w *= 0.5f) {
		bodies[p].weakness = MAX(bodies[p].weakness, w);
	}
	for (Body &c : bodies) {
		if (c.parent == best) {
			c.weakness = MAX(c.weakness, hit_weakening * 0.5f);
			for (Body &g : bodies) {
				if (g.parent >= 0 && &bodies[g.parent] == &c) {
					g.weakness = MAX(g.weakness, hit_weakening * 0.25f);
				}
			}
		}
	}
}

void FellowPhysicalAnimation3D::apply_hit_to_bone(const StringName &p_bone, const Vector3 &p_impulse) {
	const Skeleton3D *skeleton = get_skeleton();
	ERR_FAIL_NULL(skeleton);
	const int bone = skeleton->find_bone(p_bone);
	for (const Body &b : bodies) {
		if (b.bone == bone) {
			apply_hit(b.node->get_global_position(), p_impulse);
			return;
		}
	}
	ERR_FAIL_MSG(vformat("No body for the bone \"%s\".", String(p_bone)));
}

// Milestone 183 (decision 0325): balance, falls and getting up.

FellowMotionMatcher3D *FellowPhysicalAnimation3D::_get_matcher() const {
	if (motion_matcher_path.is_empty() || !is_inside_tree()) {
		return nullptr;
	}
	return Object::cast_to<FellowMotionMatcher3D>(get_node_or_null(motion_matcher_path));
}

void FellowPhysicalAnimation3D::_set_state(State p_state) {
	state = p_state;
	state_time = 0.0f;
	still_time = 0.0f;
	state_strength = (state == STATE_DOWN) ? 0.0f : 1.0f;
}

void FellowPhysicalAnimation3D::_centre_of_mass(Vector3 &r_position, Vector3 &r_velocity, Vector3 &r_target, float &r_feet_y) const {
	// The bodies' centre of mass and its velocity (the physics server's), the animation's, and the lowest body's
	// bottom (the feet on the ground).
	float mass = 0.0f;
	r_position = Vector3();
	r_velocity = Vector3();
	r_target = Vector3();
	r_feet_y = 1e20f;
	for (const Body &b : bodies) {
		const float m = b.node->get_mass();
		const Vector3 p = b.node->get_global_transform().origin;
		Vector3 v = b.node->get_linear_velocity();
		PhysicsDirectBodyState3D *st = PhysicsServer3D::get_singleton()->body_get_direct_state(b.node->get_rid());
		if (st) {
			v = st->get_linear_velocity();
		}
		r_position += p * m;
		r_velocity += v * m;
		r_target += (b.has_target ? b.target.origin : p) * m;
		r_feet_y = MIN(r_feet_y, (b.has_target ? b.target.origin.y : p.y) - b.radius);
		mass += m;
	}
	if (mass > 0.0f) {
		r_position /= mass;
		r_velocity /= mass;
		r_target /= mass;
	}
	if (r_feet_y > 1e19f) {
		r_feet_y = r_position.y - 1.0f;
	}
}

void FellowPhysicalAnimation3D::_fall(const Vector3 &p_direction, float p_speed) {
	// A stiff fall: the pose the body stands in now, turned about its feet the way it was pushed as an inverted
	// pendulum falls (its angle's rate from the push's speed over its height, then gravity's), the drive holding the
	// body to it; past `topple_angle` it goes limp and lands a ragdoll. The motion matcher stops.
	Vector3 dir(p_direction.x, 0.0f, p_direction.z);
	if (dir.length() < 1e-4f) {
		dir = Vector3(0, 0, 1);
	}
	dir.normalize();
	Vector3 com, vel, tgt;
	float feet_y = 0.0f;
	_centre_of_mass(com, vel, tgt, feet_y);
	fall_targets.resize(bodies.size());
	Vector3 pivot;
	float pivot_mass = 0.0f;
	for (uint32_t i = 0; i < bodies.size(); i++) {
		fall_targets[i] = bodies[i].has_target ? bodies[i].target : bodies[i].node->get_global_transform();
		// The feet: the bodies near the ground.
		if (fall_targets[i].origin.y - bodies[i].radius < feet_y + 0.15f) {
			pivot += fall_targets[i].origin;
			pivot_mass += 1.0f;
		}
	}
	fall_pivot = pivot_mass > 0.0f ? pivot / pivot_mass : Vector3(com.x, feet_y, com.z);
	fall_pivot.y = feet_y;
	// Pushed the way of the fall the feet would trip over: the pivot at their edge on that side.
	fall_pivot += dir * 0.08f;
	fall_axis = Vector3(0, 1, 0).cross(dir).normalized();
	fall_direction = dir;
	fall_height = MAX(com.y - feet_y, 0.3f);
	fall_angle = 0.02f;
	fall_rate = MAX(p_speed, 0.3f) / fall_height;
	// The whole body topples alike: no part given way (a part held weaker than its neighbours is pulled from its joint
	// as they are driven).
	for (Body &b : bodies) {
		b.weakness = 0.0f;
	}
	_set_state(STATE_FALLING);
	stat_falls++;
	FellowMotionMatcher3D *mm = _get_matcher();
	if (mm) {
		mm->push(Vector3(), 1e6f); // standing still until it gets up
	}
	emit_signal(SNAME("fell"), dir);
}

void FellowPhysicalAnimation3D::knock_down(const Vector3 &p_velocity) {
	ERR_FAIL_COND_MSG(!built, "FellowPhysicalAnimation3D.knock_down(): the bodies are not built yet.");
	_fall(p_velocity, p_velocity.length());
}

int FellowPhysicalAnimation3D::_top_bone() const {
	const Skeleton3D *skeleton = get_skeleton();
	int top = -1;
	float top_y = -1e20f;
	for (int i = 0; skeleton && i < skeleton->get_bone_count(); i++) {
		const float y = skeleton->get_bone_global_rest(i).origin.y;
		if (y > top_y) {
			top_y = y;
			top = i;
		}
	}
	return top;
}

int FellowPhysicalAnimation3D::_face_of(const Vector3 &p_hips, const Vector3 &p_top, const Vector3 &p_left, const Vector3 &p_right) const {
	// The trunk's front: up the spine crossed with the way from the left hip to the right.
	const Vector3 front = (p_top - p_hips).cross(p_right - p_left).normalized();
	return front.y > 0.5f ? FACE_UP : (front.y < -0.5f ? FACE_DOWN : FACE_SIDE);
}

void FellowPhysicalAnimation3D::_clip_pose(const FellowMotionDatabase *p_db, int p_frame) {
	// A database frame's bones in the skeleton's space (the hips' in the clip's: as the motion matcher writes them).
	Skeleton3D *skeleton = get_skeleton();
	const int count = skeleton->get_bone_count();
	clip_globals.resize(count);
	const int hips = p_db->get_bone_role(FellowMotionDatabase::ROLE_HIPS);
	LocalVector<bool> done;
	done.resize(count);
	for (int i = 0; i < count; i++) {
		done[i] = false;
	}
	for (int i = 0; i < count; i++) {
		// Parents first (a bone's chain walked up to the first one done).
		LocalVector<int> chain;
		for (int b = i; b >= 0 && !done[b]; b = skeleton->get_bone_parent(b)) {
			chain.push_back(b);
		}
		for (int k = int(chain.size()) - 1; k >= 0; k--) {
			const int b = chain[k];
			const int d = db_bone_of[b];
			const int parent = skeleton->get_bone_parent(b);
			Transform3D local;
			if (d >= 0) {
				local = Transform3D(Basis(p_db->get_bone_rotation(p_frame, d)), p_db->get_bone_position(p_frame, d));
				if (d == hips && parent >= 0) {
					local = skeleton->get_bone_global_rest(parent).affine_inverse() * local;
				}
			} else {
				local = skeleton->get_bone_rest(b);
			}
			clip_globals[b] = parent >= 0 ? clip_globals[parent] * local : local;
			done[b] = true;
		}
	}
}

bool FellowPhysicalAnimation3D::_scan_get_up_clips() {
	// The motion matcher's database's clips tagged get_up_tag: from a fifth of a second before the hips start rising
	// to 0.6 s after they reach standing height; the face each starts on. The matcher leaves them out of its search.
	FellowMotionMatcher3D *mm = _get_matcher();
	Skeleton3D *skeleton = get_skeleton();
	const Ref<FellowMotionDatabase> db = mm ? mm->get_database() : Ref<FellowMotionDatabase>();
	if (db.is_null() || !db->is_valid() || !skeleton) {
		return false;
	}
	if (scanned_database == db->get_instance_id()) {
		return !get_up_clips.is_empty();
	}
	scanned_database = db->get_instance_id();
	get_up_clips.clear();
	db_bone_of.resize(skeleton->get_bone_count());
	for (int b = 0; b < skeleton->get_bone_count(); b++) {
		db_bone_of[b] = -1;
	}
	for (int d = 0; d < db->get_bone_count(); d++) {
		const int b = skeleton->find_bone(db->get_bone_name(d));
		if (b >= 0) {
			db_bone_of[b] = d;
		}
	}
	PackedStringArray tag;
	tag.push_back(get_up_tag);
	const PackedStringArray names = db->get_tag_names();
	if (!names.has(String(get_up_tag))) {
		return false;
	}
	const uint32_t mask = db->get_tag_mask(tag);
	PackedStringArray excluded = mm->get_tags_excluded();
	if (!excluded.has(String(get_up_tag))) {
		excluded.push_back(get_up_tag);
		mm->set_tags_excluded(excluded);
	}
	const int hips = db->get_bone_role(FellowMotionDatabase::ROLE_HIPS);
	const int left_db = db->get_bone_role(FellowMotionDatabase::ROLE_LEFT_UPPER_LEG);
	const int right_db = db->get_bone_role(FellowMotionDatabase::ROLE_RIGHT_UPPER_LEG);
	const int hips_bone = skeleton->find_bone(db->get_bone_name(hips));
	const int left = left_db >= 0 ? skeleton->find_bone(db->get_bone_name(left_db)) : -1;
	const int right = right_db >= 0 ? skeleton->find_bone(db->get_bone_name(right_db)) : -1;
	const int top = _top_bone();
	const float rate = db->get_sample_rate();
	for (int r = 0; r < db->get_range_count(); r++) {
		const int start = db->get_range_start(r);
		const int stop = db->get_range_stop(r);
		if (stop - start < 10 || (db->get_frame_tags(start) & mask) != mask) {
			continue;
		}
		float lo = 1e20f;
		float hi = -1e20f;
		for (int f = start; f < stop; f++) {
			const float y = db->get_bone_position(f, hips).y;
			lo = MIN(lo, y);
			hi = MAX(hi, y);
		}
		int stand = start;
		while (stand < stop - 1 && db->get_bone_position(stand, hips).y < lo + 0.85f * (hi - lo)) {
			stand++;
		}
		GetUpClip clip;
		clip.range = r;
		clip.name = db->get_range_name(r);
		// The face it lies on, at its first frame; it starts a tenth of a second before any body's bone has moved 5 cm
		// from there (the clips lie still for a second or three first).
		_clip_pose(db.ptr(), start);
		if (hips_bone >= 0 && left >= 0 && right >= 0 && top >= 0) {
			clip.face = _face_of(clip_globals[hips_bone].origin, clip_globals[top].origin, clip_globals[left].origin, clip_globals[right].origin);
		}
		LocalVector<Vector3> first;
		for (const Body &b : bodies) {
			first.push_back(clip_globals[b.bone].origin);
		}
		int moved = start;
		for (; moved < stand; moved++) {
			_clip_pose(db.ptr(), moved);
			float most = 0.0f;
			for (uint32_t i = 0; i < bodies.size(); i++) {
				most = MAX(most, (clip_globals[bodies[i].bone].origin - first[i]).length());
			}
			if (most > 0.05f) {
				break;
			}
		}
		clip.start = MAX(start, moved - int(0.1f * rate));
		clip.stop = MIN(stop - 1, stand + int(0.6f * rate));
		get_up_clips.push_back(clip);
	}
	return !get_up_clips.is_empty();
}

bool FellowPhysicalAnimation3D::get_up() {
	// Lying (down, or limp after a fall): the get-up clip whose first pose fits the body's best — each clip's start
	// turned about the vertical and moved over the ground to fit the bodies (a least-squares fit of their places seen
	// from above, by mass), the one with the least distance left wins — played from the ragdoll's pose.
	if (!built || (state != STATE_DOWN && state != STATE_FALLING) || !_scan_get_up_clips()) {
		return false;
	}
	FellowMotionMatcher3D *mm = _get_matcher();
	const Ref<FellowMotionDatabase> db = mm->get_database();
	// The bones where the bodies have them, and the ground under the lying body.
	LocalVector<Vector3> bone_world;
	LocalVector<float> weight;
	bone_world.resize(bodies.size());
	weight.resize(bodies.size());
	float ground = 1e20f;
	for (uint32_t i = 0; i < bodies.size(); i++) {
		const Transform3D t = bodies[i].node->get_global_transform();
		bone_world[i] = (t * bodies[i].offset.affine_inverse()).origin;
		weight[i] = bodies[i].node->get_mass();
		ground = MIN(ground, t.origin.y - bodies[i].radius);
	}
	// The ground under the hips' body (a ray down through the world the bodies collide with; else their lowest).
	if (!bodies.is_empty() && bodies[0].node->get_world_3d().is_valid()) {
		PhysicsDirectSpaceState3D *space = PhysicsServer3D::get_singleton()->space_get_direct_state(bodies[0].node->get_world_3d()->get_space());
		if (space) {
			PhysicsDirectSpaceState3D::RayParameters ray;
			const Vector3 from = bodies[0].node->get_global_transform().origin;
			ray.from = from + Vector3(0, 0.5f, 0);
			ray.to = from - Vector3(0, 2.0f, 0);
			ray.collision_mask = collision_mask;
			for (const Body &b : bodies) {
				ray.exclude.insert(b.node->get_rid());
			}
			PhysicsDirectSpaceState3D::RayResult hit;
			if (space->intersect_ray(ray, hit)) {
				ground = hit.position.y;
			}
		}
	}
	float best_error = 1e20f;
	int best = -1;
	Transform3D best_anchor;
	// The clips that start on the face the body lies on (seen from above, lying on the back and on a side fit alike);
	// any if none does.
	bool any_face = false;
	for (const GetUpClip &clip : get_up_clips) {
		any_face = any_face || clip.face == landed_face;
	}
	for (uint32_t c = 0; c < get_up_clips.size(); c++) {
		if (any_face && get_up_clips[c].face != landed_face) {
			continue;
		}
		_clip_pose(db.ptr(), get_up_clips[c].start);
		// Seen from above: the clip's bones (a) onto the bodies' (b), by a turn about the vertical and a shift.
		Vector3 ca, cb;
		float wsum = 0.0f;
		for (uint32_t i = 0; i < bodies.size(); i++) {
			ca += clip_globals[bodies[i].bone].origin * weight[i];
			cb += bone_world[i] * weight[i];
			wsum += weight[i];
		}
		ca /= wsum;
		cb /= wsum;
		float C = 0.0f;
		float S = 0.0f;
		for (uint32_t i = 0; i < bodies.size(); i++) {
			const Vector3 a = clip_globals[bodies[i].bone].origin - ca;
			const Vector3 b = bone_world[i] - cb;
			C += weight[i] * (a.x * b.x + a.z * b.z);
			S += weight[i] * (b.x * a.z - b.z * a.x);
		}
		const Basis yaw(Vector3(0, 1, 0), Math::atan2(S, C));
		Transform3D anchor(yaw, Vector3());
		const Vector3 moved = yaw.xform(ca);
		anchor.origin = Vector3(cb.x - moved.x, ground, cb.z - moved.z);
		float error = 0.0f;
		for (uint32_t i = 0; i < bodies.size(); i++) {
			error += weight[i] * (anchor.xform(clip_globals[bodies[i].bone].origin) - bone_world[i]).length();
		}
		error /= wsum;
		if (error < best_error) {
			best_error = error;
			best = c;
			best_anchor = anchor;
		}
	}
	if (best < 0) {
		return false;
	}
	gu_clip = best;
	gu_anchor = best_anchor;
	gu_frame = get_up_clips[best].start;
	gu_snapshot.resize(bodies.size());
	for (uint32_t i = 0; i < bodies.size(); i++) {
		gu_snapshot[i] = bodies[i].node->get_global_transform().orthonormalized();
		// The targets from the bodies on (the drive takes hold in this step, before the next pose), no feed-forward from
		// the standing pose they last had: else the bodies were yanked there (and put back, 100 m/s).
		bodies[i].target = gu_snapshot[i];
		bodies[i].has_previous = false;
	}
	stat_match_error = best_error;
	stat_blend_speed_max = 0.0f;
	stat_get_up_clip = get_up_clips[best].name;
	handing_over = false;
	_set_state(STATE_GETTING_UP);
	return true;
}

void FellowPhysicalAnimation3D::_update_balance(float p_dt) {
	// The get-up clips found (and left out of the motion matcher's search) as soon as its database is there: a lying
	// clip's still frames match standing still better than any.
	FellowMotionMatcher3D *matcher = _get_matcher();
	if (matcher && matcher->get_database().is_valid() && scanned_database != matcher->get_database()->get_instance_id()) {
		_scan_get_up_clips();
	}
	state_time += p_dt;
	if (handing_over) {
		handover_time += p_dt;
		handing_over = handover_time < handover_blend;
	}
	switch (state) {
		case STATE_STANDING:
			break;
		case STATE_STAGGERING: {
			if (state_time >= stagger_end) {
				_set_state(STATE_STANDING);
			}
		} break;
		case STATE_FALLING: {
			if (state_strength > 0.0f) {
				// The stiff fall: an inverted pendulum (angle'' = g / h sin angle) until it goes limp.
				fall_rate += 9.81f / fall_height * Math::sin(fall_angle) * p_dt;
				fall_angle += fall_rate * p_dt;
				if (fall_angle >= topple_angle) {
					state_strength = 0.0f;
				}
				break;
			}
			// Limp: lying still a moment (the bodies slower than 0.25 m/s on average for 0.3 s) — down.
			float mean = 0.0f;
			for (const Body &b : bodies) {
				mean += float(b.node->get_linear_velocity().length());
			}
			mean /= MAX(int(bodies.size()), 1);
			still_time = mean < 0.25f ? still_time + p_dt : 0.0f;
			if (still_time >= 0.3f && state_time > 0.6f) {
				stat_fall_seconds = state_time;
				// The face it lies on: the trunk's front from the bodies (the hips', the topmost bone's, the legs').
				FellowMotionMatcher3D *mm = _get_matcher();
				Skeleton3D *skeleton = get_skeleton();
				const Ref<FellowMotionDatabase> db = mm ? mm->get_database() : Ref<FellowMotionDatabase>();
				landed_face = FACE_SIDE;
				if (db.is_valid() && skeleton) {
					auto bone_at = [&](int p_bone) {
						for (int b = p_bone; b >= 0; b = skeleton->get_bone_parent(b)) {
							for (const Body &body : bodies) {
								if (body.bone == b) {
									return (body.node->get_global_transform() * body.offset.affine_inverse()).origin;
								}
							}
						}
						return Vector3();
					};
					const int hips = skeleton->find_bone(db->get_bone_name(db->get_bone_role(FellowMotionDatabase::ROLE_HIPS)));
					const int left = skeleton->find_bone(db->get_bone_name(db->get_bone_role(FellowMotionDatabase::ROLE_LEFT_UPPER_LEG)));
					const int right = skeleton->find_bone(db->get_bone_name(db->get_bone_role(FellowMotionDatabase::ROLE_RIGHT_UPPER_LEG)));
					const int top = _top_bone();
					if (hips >= 0 && left >= 0 && right >= 0 && top >= 0) {
						landed_face = _face_of(bone_at(hips), bone_at(top), bone_at(left), bone_at(right));
					}
				}
				_set_state(STATE_DOWN);
				emit_signal(SNAME("landed"), landed_face);
			}
		} break;
		case STATE_DOWN: {
			if (auto_get_up && state_time >= get_up_delay) {
				get_up();
			}
		} break;
		case STATE_GETTING_UP: {
			FellowMotionMatcher3D *mm = _get_matcher();
			const Ref<FellowMotionDatabase> db = mm ? mm->get_database() : Ref<FellowMotionDatabase>();
			if (db.is_null() || gu_clip < 0) {
				_set_state(STATE_DOWN);
				break;
			}
			if (state_time < get_up_blend + 0.2f) {
				for (const Body &b : bodies) {
					stat_blend_speed_max = MAX(stat_blend_speed_max, float(b.node->get_linear_velocity().length()));
				}
			}
			gu_frame += double(p_dt) * db->get_sample_rate();
			const GetUpClip &clip = get_up_clips[gu_clip];
			if (gu_frame >= clip.stop) {
				// Standing: the motion matcher put where the clip ends, facing its way; its pose blended in.
				const Transform3D root = gu_anchor * Transform3D(Basis(db->get_root_rotation(clip.stop)), db->get_root_position(clip.stop));
				mm->teleport(Vector3(root.origin.x, gu_anchor.origin.y, root.origin.z), root.basis.xform(Vector3(0, 0, 1)));
				handover_from.resize(bodies.size());
				for (uint32_t i = 0; i < bodies.size(); i++) {
					handover_from[i] = bodies[i].target;
				}
				handing_over = true;
				handover_time = 0.0f;
				stat_get_up_seconds = state_time;
				stat_get_ups++;
				gu_clip = -1;
				_set_state(STATE_STANDING);
				emit_signal(SNAME("got_up"));
			}
		} break;
	}
}

Dictionary FellowPhysicalAnimation3D::get_balance_stats() const {
	Dictionary d;
	d["state"] = int(state);
	d["hits"] = stat_hits;
	d["staggers"] = stat_staggers;
	d["falls"] = stat_falls;
	d["get_ups"] = stat_get_ups;
	d["capture_offset_m"] = stat_capture_offset;
	d["landed_face"] = landed_face;
	d["fall_seconds"] = stat_fall_seconds;
	d["get_up_clip"] = stat_get_up_clip;
	d["get_up_match_error_m"] = stat_match_error;
	d["get_up_blend_speed_max_m_s"] = stat_blend_speed_max;
	d["get_up_seconds"] = stat_get_up_seconds;
	Array clips;
	for (const GetUpClip &c : get_up_clips) {
		Dictionary e;
		e["name"] = c.name;
		e["face"] = c.face;
		e["frames"] = c.stop - c.start;
		clips.push_back(e);
	}
	d["get_up_clips"] = clips;
	return d;
}

Dictionary FellowPhysicalAnimation3D::get_stats() const {
	Dictionary d;
	d["bodies"] = int(bodies.size());
	d["tracking_error_mean_m"] = error_samples > 0.0 ? error_sum / error_samples : 0.0;
	d["tracking_angle_mean_rad"] = error_samples > 0.0 ? error_angle_sum / error_samples : 0.0;
	d["tracking_error_max_m"] = error_max;
	d["speed_max_m_s"] = speed_max;
	d["drive_usec_per_step"] = drive_steps > 0 ? double(drive_usec) / double(drive_steps) : 0.0;
	float weakness = 0.0f;
	for (const Body &b : bodies) {
		weakness = MAX(weakness, b.weakness);
	}
	d["weakness_max"] = weakness;
	d["rescues"] = rescues;
	return d;
}

void FellowPhysicalAnimation3D::reset_stats() {
	error_sum = 0.0;
	error_angle_sum = 0.0;
	error_samples = 0.0;
	error_max = 0.0f;
	speed_max = 0.0f;
	drive_usec = 0;
	drive_steps = 0;
	rescues = 0;
}

void FellowPhysicalAnimation3D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("build"), &FellowPhysicalAnimation3D::build);
	ClassDB::bind_method(D_METHOD("clear"), &FellowPhysicalAnimation3D::clear);
	ClassDB::bind_method(D_METHOD("is_built"), &FellowPhysicalAnimation3D::is_built);
	ClassDB::bind_method(D_METHOD("get_body_count"), &FellowPhysicalAnimation3D::get_body_count);
	ClassDB::bind_method(D_METHOD("get_body_bones"), &FellowPhysicalAnimation3D::get_body_bones);
	ClassDB::bind_method(D_METHOD("reset_bodies"), &FellowPhysicalAnimation3D::reset_bodies);
	ClassDB::bind_method(D_METHOD("get_body_errors"), &FellowPhysicalAnimation3D::get_body_errors);
	ClassDB::bind_method(D_METHOD("get_body_weakness"), &FellowPhysicalAnimation3D::get_body_weakness);
	ClassDB::bind_method(D_METHOD("get_body_targets"), &FellowPhysicalAnimation3D::get_body_targets);
	ClassDB::bind_method(D_METHOD("apply_hit", "position", "impulse"), &FellowPhysicalAnimation3D::apply_hit);
	ClassDB::bind_method(D_METHOD("apply_hit_to_bone", "bone", "impulse"), &FellowPhysicalAnimation3D::apply_hit_to_bone);
	ClassDB::bind_method(D_METHOD("get_stats"), &FellowPhysicalAnimation3D::get_stats);
	ClassDB::bind_method(D_METHOD("reset_stats"), &FellowPhysicalAnimation3D::reset_stats);

	ClassDB::bind_method(D_METHOD("set_simulate", "simulate"), &FellowPhysicalAnimation3D::set_simulate);
	ClassDB::bind_method(D_METHOD("is_simulating"), &FellowPhysicalAnimation3D::is_simulating);
	ClassDB::bind_method(D_METHOD("set_strength", "strength"), &FellowPhysicalAnimation3D::set_strength);
	ClassDB::bind_method(D_METHOD("get_strength"), &FellowPhysicalAnimation3D::get_strength);
	ClassDB::bind_method(D_METHOD("set_position_gain", "gain"), &FellowPhysicalAnimation3D::set_position_gain);
	ClassDB::bind_method(D_METHOD("get_position_gain"), &FellowPhysicalAnimation3D::get_position_gain);
	ClassDB::bind_method(D_METHOD("set_rotation_gain", "gain"), &FellowPhysicalAnimation3D::set_rotation_gain);
	ClassDB::bind_method(D_METHOD("get_rotation_gain"), &FellowPhysicalAnimation3D::get_rotation_gain);
	ClassDB::bind_method(D_METHOD("set_recovery_halflife", "seconds"), &FellowPhysicalAnimation3D::set_recovery_halflife);
	ClassDB::bind_method(D_METHOD("get_recovery_halflife"), &FellowPhysicalAnimation3D::get_recovery_halflife);
	ClassDB::bind_method(D_METHOD("set_hit_weakening", "weakening"), &FellowPhysicalAnimation3D::set_hit_weakening);
	ClassDB::bind_method(D_METHOD("get_hit_weakening"), &FellowPhysicalAnimation3D::get_hit_weakening);
	ClassDB::bind_method(D_METHOD("set_total_mass", "mass"), &FellowPhysicalAnimation3D::set_total_mass);
	ClassDB::bind_method(D_METHOD("get_total_mass"), &FellowPhysicalAnimation3D::get_total_mass);
	ClassDB::bind_method(D_METHOD("set_min_bone_length", "length"), &FellowPhysicalAnimation3D::set_min_bone_length);
	ClassDB::bind_method(D_METHOD("get_min_bone_length"), &FellowPhysicalAnimation3D::get_min_bone_length);
	ClassDB::bind_method(D_METHOD("set_collision_layer", "layer"), &FellowPhysicalAnimation3D::set_collision_layer);
	ClassDB::bind_method(D_METHOD("get_collision_layer"), &FellowPhysicalAnimation3D::get_collision_layer);
	ClassDB::bind_method(D_METHOD("set_collision_mask", "mask"), &FellowPhysicalAnimation3D::set_collision_mask);
	ClassDB::bind_method(D_METHOD("get_collision_mask"), &FellowPhysicalAnimation3D::get_collision_mask);
	ClassDB::bind_method(D_METHOD("set_swing_limit", "radians"), &FellowPhysicalAnimation3D::set_swing_limit);
	ClassDB::bind_method(D_METHOD("get_swing_limit"), &FellowPhysicalAnimation3D::get_swing_limit);
	ClassDB::bind_method(D_METHOD("set_twist_limit", "radians"), &FellowPhysicalAnimation3D::set_twist_limit);
	ClassDB::bind_method(D_METHOD("get_twist_limit"), &FellowPhysicalAnimation3D::get_twist_limit);
	ClassDB::bind_method(D_METHOD("set_balance", "balance"), &FellowPhysicalAnimation3D::set_balance);
	ClassDB::bind_method(D_METHOD("is_balance"), &FellowPhysicalAnimation3D::is_balance);
	ClassDB::bind_method(D_METHOD("set_motion_matcher_path", "path"), &FellowPhysicalAnimation3D::set_motion_matcher_path);
	ClassDB::bind_method(D_METHOD("get_motion_matcher_path"), &FellowPhysicalAnimation3D::get_motion_matcher_path);
	ClassDB::bind_method(D_METHOD("set_step_threshold", "metres"), &FellowPhysicalAnimation3D::set_step_threshold);
	ClassDB::bind_method(D_METHOD("get_step_threshold"), &FellowPhysicalAnimation3D::get_step_threshold);
	ClassDB::bind_method(D_METHOD("set_fall_threshold", "metres"), &FellowPhysicalAnimation3D::set_fall_threshold);
	ClassDB::bind_method(D_METHOD("get_fall_threshold"), &FellowPhysicalAnimation3D::get_fall_threshold);
	ClassDB::bind_method(D_METHOD("set_topple_angle", "radians"), &FellowPhysicalAnimation3D::set_topple_angle);
	ClassDB::bind_method(D_METHOD("get_topple_angle"), &FellowPhysicalAnimation3D::get_topple_angle);
	ClassDB::bind_method(D_METHOD("set_get_up_tag", "tag"), &FellowPhysicalAnimation3D::set_get_up_tag);
	ClassDB::bind_method(D_METHOD("get_get_up_tag"), &FellowPhysicalAnimation3D::get_get_up_tag);
	ClassDB::bind_method(D_METHOD("set_auto_get_up", "auto_get_up"), &FellowPhysicalAnimation3D::set_auto_get_up);
	ClassDB::bind_method(D_METHOD("is_auto_get_up"), &FellowPhysicalAnimation3D::is_auto_get_up);
	ClassDB::bind_method(D_METHOD("set_get_up_delay", "seconds"), &FellowPhysicalAnimation3D::set_get_up_delay);
	ClassDB::bind_method(D_METHOD("get_get_up_delay"), &FellowPhysicalAnimation3D::get_get_up_delay);
	ClassDB::bind_method(D_METHOD("set_get_up_blend", "seconds"), &FellowPhysicalAnimation3D::set_get_up_blend);
	ClassDB::bind_method(D_METHOD("get_get_up_blend"), &FellowPhysicalAnimation3D::get_get_up_blend);
	ClassDB::bind_method(D_METHOD("get_state"), &FellowPhysicalAnimation3D::get_state);
	ClassDB::bind_method(D_METHOD("get_landed_face"), &FellowPhysicalAnimation3D::get_landed_face);
	ClassDB::bind_method(D_METHOD("knock_down", "velocity"), &FellowPhysicalAnimation3D::knock_down);
	ClassDB::bind_method(D_METHOD("get_up"), &FellowPhysicalAnimation3D::get_up);
	ClassDB::bind_method(D_METHOD("get_balance_stats"), &FellowPhysicalAnimation3D::get_balance_stats);

	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "simulate"), "set_simulate", "is_simulating");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "strength", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_strength", "get_strength");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "position_gain", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_position_gain", "get_position_gain");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "rotation_gain", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_rotation_gain", "get_rotation_gain");
	ADD_GROUP("Hits", "");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "hit_weakening", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_hit_weakening", "get_hit_weakening");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "recovery_halflife", PROPERTY_HINT_RANGE, "0.01,3,0.01,suffix:s"), "set_recovery_halflife", "get_recovery_halflife");
	ADD_GROUP("Bodies", "");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "total_mass", PROPERTY_HINT_RANGE, "0.1,300,0.1,suffix:kg"), "set_total_mass", "get_total_mass");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "min_bone_length", PROPERTY_HINT_RANGE, "0,0.5,0.001,suffix:m"), "set_min_bone_length", "get_min_bone_length");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "collision_layer", PROPERTY_HINT_LAYERS_3D_PHYSICS), "set_collision_layer", "get_collision_layer");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "collision_mask", PROPERTY_HINT_LAYERS_3D_PHYSICS), "set_collision_mask", "get_collision_mask");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "swing_limit", PROPERTY_HINT_RANGE, "0,180,0.1,radians_as_degrees"), "set_swing_limit", "get_swing_limit");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "twist_limit", PROPERTY_HINT_RANGE, "0,180,0.1,radians_as_degrees"), "set_twist_limit", "get_twist_limit");
	ADD_GROUP("Balance", "");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "balance"), "set_balance", "is_balance");
	ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "motion_matcher_path", PROPERTY_HINT_NODE_PATH_VALID_TYPES, "FellowMotionMatcher3D"), "set_motion_matcher_path", "get_motion_matcher_path");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "step_threshold", PROPERTY_HINT_RANGE, "0,1,0.01,suffix:m"), "set_step_threshold", "get_step_threshold");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "fall_threshold", PROPERTY_HINT_RANGE, "0,3,0.01,suffix:m"), "set_fall_threshold", "get_fall_threshold");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "topple_angle", PROPERTY_HINT_RANGE, "0,90,0.1,radians_as_degrees"), "set_topple_angle", "get_topple_angle");
	ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "get_up_tag"), "set_get_up_tag", "get_get_up_tag");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "auto_get_up"), "set_auto_get_up", "is_auto_get_up");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "get_up_delay", PROPERTY_HINT_RANGE, "0,10,0.01,suffix:s"), "set_get_up_delay", "get_get_up_delay");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "get_up_blend", PROPERTY_HINT_RANGE, "0.01,2,0.01,suffix:s"), "set_get_up_blend", "get_get_up_blend");

	ADD_SIGNAL(MethodInfo("staggered", PropertyInfo(Variant::VECTOR3, "direction"), PropertyInfo(Variant::FLOAT, "distance")));
	ADD_SIGNAL(MethodInfo("fell", PropertyInfo(Variant::VECTOR3, "direction")));
	ADD_SIGNAL(MethodInfo("landed", PropertyInfo(Variant::INT, "face")));
	ADD_SIGNAL(MethodInfo("got_up"));

	BIND_ENUM_CONSTANT(STATE_STANDING);
	BIND_ENUM_CONSTANT(STATE_STAGGERING);
	BIND_ENUM_CONSTANT(STATE_FALLING);
	BIND_ENUM_CONSTANT(STATE_DOWN);
	BIND_ENUM_CONSTANT(STATE_GETTING_UP);
	BIND_ENUM_CONSTANT(FACE_UP);
	BIND_ENUM_CONSTANT(FACE_DOWN);
	BIND_ENUM_CONSTANT(FACE_SIDE);
}
