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

#include "core/variant/typed_array.h"
#include "core/config/engine.h"
#include "core/config/project_settings.h"
#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "core/os/os.h"
#include "scene/3d/physics/collision_shape_3d.h"
#include "scene/3d/physics/physical_bone_3d.h"
#include "scene/3d/physics/physical_bone_simulator_3d.h"
#include "scene/3d/skeleton_3d.h"
#include "scene/resources/3d/capsule_shape_3d.h"
#include "scene/resources/3d/sphere_shape_3d.h"
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
	// Going limp (strength under a half) or back: the joints made anew for it, from the pose now.
	if ((strength < 0.5f) != limp) {
		limp = strength < 0.5f;
		_rebuild_joints();
	}
	for (Body &b : bodies) {
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
		const float s = strength * (1.0f - b.weakness);
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
	b.node->apply_impulse(p_impulse, p_position - b.node->get_global_position());
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
}
