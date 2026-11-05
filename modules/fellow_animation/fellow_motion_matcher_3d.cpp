/**************************************************************************/
/*  fellow_motion_matcher_3d.cpp                                          */
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

#include "fellow_motion_matcher_3d.h"

#include "fellow_motion_math.h"

#include "core/object/class_db.h"
#include "core/object/worker_thread_pool.h"
#include "core/os/os.h"
#include "scene/3d/camera_3d.h"
#include "scene/main/viewport.h"

using namespace FellowMotionMath;

FellowMotionMatcher3D::GlobalStats FellowMotionMatcher3D::global_stats;

static inline Vector3 forward_of(const Quaternion &p_q) {
	return p_q.xform(Vector3(0, 0, 1));
}

void FellowMotionMatcher3D::set_database(const Ref<FellowMotionDatabase> &p_database) {
	database = p_database;
	started = false;
	pose_ready = false;
	update_configuration_warnings();
}

// Milestone 52: what the Scene dock's warning icon says (the parent skeleton's is SkeletonModifier3D's).
PackedStringArray FellowMotionMatcher3D::get_configuration_warnings() const {
	PackedStringArray warnings = SkeletonModifier3D::get_configuration_warnings();
	if (database.is_null()) {
		warnings.push_back(RTR("A FellowMotionMatcher3D needs a FellowMotionDatabase (Database) to match motion from."));
	} else if (!database->is_valid()) {
		warnings.push_back(RTR("The FellowMotionDatabase is empty: build it from animations first (FellowMotionDatabase.build())."));
	}
	if (!root_path.is_empty() && is_inside_tree() && !Object::cast_to<Node3D>(get_node_or_null(root_path))) {
		warnings.push_back(vformat(RTR("Root Path (%s) leads to no Node3D."), String(root_path)));
	}
	return warnings;
}

void FellowMotionMatcher3D::set_auto_update(bool p_enable) {
	auto_update = p_enable;
}

void FellowMotionMatcher3D::_skeleton_changed(Skeleton3D *p_old, Skeleton3D *p_new) {
	started = false;
	pose_ready = false;
}

Node3D *FellowMotionMatcher3D::_get_root() const {
	if (!root_path.is_empty()) {
		return Object::cast_to<Node3D>(get_node_or_null(root_path));
	}
	Skeleton3D *skeleton = get_skeleton();
	return skeleton ? Object::cast_to<Node3D>(skeleton->get_parent()) : nullptr;
}

void FellowMotionMatcher3D::_map_bones() {
	Skeleton3D *skeleton = get_skeleton();
	const int bones = database->get_bone_count();
	skeleton_bones.resize(bones);
	parent_rest_globals.resize(bones);
	for (int b = 0; b < bones; b++) {
		skeleton_bones[b] = skeleton ? skeleton->find_bone(database->get_bone_name(b)) : -1;
		parent_rest_globals[b] = Transform3D();
		if (skeleton && skeleton_bones[b] >= 0) {
			const int parent = skeleton->get_bone_parent(skeleton_bones[b]);
			if (parent >= 0) {
				parent_rest_globals[b] = skeleton->get_bone_global_rest(parent);
			}
		}
	}
}

void FellowMotionMatcher3D::_start() {
	if (database.is_null() || !database->is_valid() || !get_skeleton()) {
		return;
	}
	_map_bones();
	const int bones = database->get_bone_count();
	for (LocalVector<Vector3> *v : { &anim_positions, &anim_velocities, &anim_angular_velocities, &offset_positions, &offset_velocities, &offset_angular_velocities, &out_positions }) {
		v->resize(bones);
		for (int b = 0; b < bones; b++) {
			(*v)[b] = Vector3();
		}
	}
	for (LocalVector<Quaternion> *v : { &anim_rotations, &offset_rotations, &out_rotations }) {
		v->resize(bones);
		for (int b = 0; b < bones; b++) {
			(*v)[b] = Quaternion();
		}
	}
	Node3D *root = _get_root();
	Vector3 position;
	Vector3 facing(0, 0, 1);
	if (root && root->is_inside_tree()) {
		const Transform3D t = root->get_global_transform();
		position = t.origin;
		facing = t.basis.get_column(2);
		facing.y = 0.0f;
		facing = facing.length() > 1e-6f ? facing.normalized() : Vector3(0, 0, 1);
	}
	range = 0;
	frame_time = database->get_range_start(0);
	started = true;
	teleport(position, facing);
	{
		// The first frame: the trajectory of standing still at the root's facing, without a pose to continue.
		float query[FellowMotionDatabase::FEATURE_COUNT];
		_build_query(query);
		const int first = database->search_trajectory(query);
		if (first >= 0) {
			frame_time = first;
			range = database->get_range_of(first);
		}
	}
	Vector3 root_velocity;
	float yaw_rate = 0.0f;
	_sample(frame_time, anim_positions, anim_rotations, &anim_velocities, &anim_angular_velocities, &root_velocity, &yaw_rate);
	for (int b = 0; b < bones; b++) {
		out_positions[b] = anim_positions[b];
		out_rotations[b] = anim_rotations[b];
	}
}

void FellowMotionMatcher3D::teleport(const Vector3 &p_position, const Vector3 &p_facing) {
	Vector3 facing(p_facing.x, 0.0f, p_facing.z);
	facing = facing.length() > 1e-6f ? facing.normalized() : Vector3(0, 0, 1);
	sim_position = Vector3(p_position.x, p_position.y, p_position.z);
	sim_velocity = Vector3();
	sim_acceleration = Vector3();
	sim_rotation = yaw_rotation(facing);
	sim_angular_velocity = Vector3();
	char_position = sim_position;
	char_rotation = sim_rotation;
	char_velocity = Vector3();
	char_yaw_rate = 0.0f;
	for (Foot &foot : feet) {
		foot = Foot();
	}
	force_search = true;
}

void FellowMotionMatcher3D::reset() {
	started = false;
	pose_ready = false;
}

void FellowMotionMatcher3D::_sample(double p_frame_time, LocalVector<Vector3> &r_positions, LocalVector<Quaternion> &r_rotations, LocalVector<Vector3> *r_velocities, LocalVector<Vector3> *r_angular_velocities, Vector3 *r_root_velocity, float *r_yaw_rate) const {
	const int r = database->get_range_of(int(p_frame_time));
	const int start = database->get_range_start(r);
	const int stop = database->get_range_stop(r);
	int f0 = CLAMP(int(Math::floor(p_frame_time)), start, stop - 1);
	float alpha = float(p_frame_time - f0);
	int f1 = f0 + 1;
	if (f1 >= stop) {
		f1 = f0;
		alpha = 0.0f;
	}
	// Velocities over a frame pair that exists.
	const int v0 = f1 > f0 ? f0 : MAX(f0 - 1, start);
	const int v1 = f1 > f0 ? f1 : f0;
	const float rate = database->get_sample_rate();
	const int bones = database->get_bone_count();
	for (int b = 0; b < bones; b++) {
		const Vector3 p0 = database->get_bone_position(f0, b);
		r_positions[b] = alpha > 0.0f ? p0.lerp(database->get_bone_position(f1, b), alpha) : p0;
		const Quaternion a = database->get_bone_rotation(f0, b);
		r_rotations[b] = alpha > 0.0f ? a.slerp(database->get_bone_rotation(f1, b), alpha) : a;
		if (r_velocities) {
			(*r_velocities)[b] = (database->get_bone_position(v1, b) - database->get_bone_position(v0, b)) * rate;
		}
		if (r_angular_velocities) {
			(*r_angular_velocities)[b] = quat_to_scaled_angle_axis(quat_abs(database->get_bone_rotation(v1, b) * database->get_bone_rotation(v0, b).inverse())) * rate;
		}
	}
	if (r_root_velocity) {
		const Quaternion rq0 = database->get_root_rotation(v0);
		*r_root_velocity = rq0.inverse().xform(database->get_root_position(v1) - database->get_root_position(v0)) * rate;
		*r_root_velocity = Vector3(r_root_velocity->x, 0.0f, r_root_velocity->z);
		*r_yaw_rate = quat_to_scaled_angle_axis(quat_abs(database->get_root_rotation(v1) * rq0.inverse())).y * rate;
	}
}

void FellowMotionMatcher3D::_transition(int p_frame) {
	const int bones = database->get_bone_count();
	thread_local LocalVector<Vector3> dst_positions, dst_velocities, dst_angular;
	thread_local LocalVector<Quaternion> dst_rotations;
	dst_positions.resize(bones);
	dst_velocities.resize(bones);
	dst_angular.resize(bones);
	dst_rotations.resize(bones);
	Vector3 src_root_velocity, dst_root_velocity;
	float src_yaw = 0.0f, dst_yaw = 0.0f;
	_sample(frame_time, anim_positions, anim_rotations, &anim_velocities, &anim_angular_velocities, &src_root_velocity, &src_yaw);
	_sample(p_frame, dst_positions, dst_rotations, &dst_velocities, &dst_angular, &dst_root_velocity, &dst_yaw);
	if (inertialization) {
		for (int b = 0; b < bones; b++) {
			offset_positions[b] = (anim_positions[b] + offset_positions[b]) - dst_positions[b];
			offset_velocities[b] = (anim_velocities[b] + offset_velocities[b]) - dst_velocities[b];
			offset_rotations[b] = quat_abs((offset_rotations[b] * anim_rotations[b]) * dst_rotations[b].inverse()).normalized();
			offset_angular_velocities[b] = (anim_angular_velocities[b] + offset_angular_velocities[b]) - dst_angular[b];
		}
		offset_root_velocity = (src_root_velocity + offset_root_velocity) - dst_root_velocity;
		offset_yaw_rate = (src_yaw + offset_yaw_rate) - dst_yaw;
	} else {
		for (int b = 0; b < bones; b++) {
			offset_positions[b] = Vector3();
			offset_velocities[b] = Vector3();
			offset_rotations[b] = Quaternion();
			offset_angular_velocities[b] = Vector3();
		}
		offset_root_velocity = Vector3();
		offset_yaw_rate = 0.0f;
	}
	offset_root_velocity_rate = Vector3();
	offset_yaw_rate_rate = 0.0f;
	frame_time = p_frame;
	range = database->get_range_of(p_frame);
	transitioned_this_frame = true;
	transitions++;
	global_stats.transitions.increment();
}

void FellowMotionMatcher3D::_build_query(float *r_query) const {
	const int current = CLAMP(int(Math::round(frame_time)), database->get_range_start(range), database->get_range_stop(range) - 1);
	const float *features = database->get_features(current);
	for (int d = 0; d < 15; d++) {
		r_query[d] = features[d]; // The current pose's features, normalised.
	}
	Vector3 facing = desired_facing.length() > 1e-3f ? desired_facing : (desired_velocity.length() > 0.1f ? desired_velocity : forward_of(sim_rotation));
	facing.y = 0.0f;
	const Quaternion desired_rotation = yaw_rotation(facing.length() > 1e-6f ? facing.normalized() : Vector3(0, 0, 1));
	const Quaternion char_inv = char_rotation.inverse();
	const float rate = database->get_sample_rate();
	for (int k = 0; k < FellowMotionDatabase::TRAJECTORY_POINTS; k++) {
		const float t = database->get_trajectory_frame(k) / rate;
		Vector3 p = sim_position, v = sim_velocity, a = sim_acceleration;
		simulation_position_update(p, v, a, desired_velocity, velocity_halflife, t);
		Quaternion q = sim_rotation;
		Vector3 w = sim_angular_velocity;
		simulation_rotation_update(q, w, desired_rotation, rotation_halflife, t);
		const Vector3 rp = char_inv.xform(p - char_position);
		const Vector3 rd = char_inv.xform(forward_of(q));
		r_query[15 + k * 2 + 0] = rp.x;
		r_query[15 + k * 2 + 1] = rp.z;
		r_query[21 + k * 2 + 0] = rd.x;
		r_query[21 + k * 2 + 1] = rd.z;
	}
	for (int d = 15; d < FellowMotionDatabase::FEATURE_COUNT; d++) {
		r_query[d] = (r_query[d] - database->get_feature_offset(d)) / database->get_feature_scale(d);
	}
}

int FellowMotionMatcher3D::_compute_lod() const {
	if (lod_distances.is_empty() || !is_inside_tree()) {
		return 0;
	}
	Viewport *viewport = get_viewport();
	Camera3D *camera = viewport ? viewport->get_camera_3d() : nullptr;
	if (!camera) {
		return 0;
	}
	const float distance = camera->get_global_position().distance_to(char_position);
	int level = 0;
	for (int i = 0; i < lod_distances.size() && i < 2; i++) {
		if (distance > lod_distances[i]) {
			level = i + 1;
		}
	}
	return level;
}

void FellowMotionMatcher3D::_forward_kinematics(LocalVector<Vector3> &r_gp, LocalVector<Quaternion> &r_gq) const {
	const int bones = database->get_bone_count();
	const int hips = database->get_bone_role(FellowMotionDatabase::ROLE_HIPS);
	r_gp.resize(bones);
	r_gq.resize(bones);
	for (int b = 0; b < bones; b++) {
		const int parent = database->get_bone_parent(b);
		if (b == hips || parent < 0) {
			r_gp[b] = out_positions[b];
			r_gq[b] = out_rotations[b];
		} else {
			r_gp[b] = r_gp[parent] + r_gq[parent].xform(out_positions[b]);
			r_gq[b] = r_gq[parent] * out_rotations[b];
		}
	}
}

void FellowMotionMatcher3D::_solve_feet(float p_dt) {
	thread_local LocalVector<Vector3> gp;
	thread_local LocalVector<Quaternion> gq;
	_forward_kinematics(gp, gq);
	const Transform3D character(Basis(char_rotation), char_position);
	const Transform3D character_inv = character.affine_inverse();
	const int current = CLAMP(int(Math::round(frame_time)), database->get_range_start(range), database->get_range_stop(range) - 1);
	const FellowMotionDatabase::BoneRole roles[2][4] = {
		{ FellowMotionDatabase::ROLE_LEFT_UPPER_LEG, FellowMotionDatabase::ROLE_LEFT_LOWER_LEG, FellowMotionDatabase::ROLE_LEFT_FOOT, FellowMotionDatabase::ROLE_LEFT_TOE },
		{ FellowMotionDatabase::ROLE_RIGHT_UPPER_LEG, FellowMotionDatabase::ROLE_RIGHT_LOWER_LEG, FellowMotionDatabase::ROLE_RIGHT_FOOT, FellowMotionDatabase::ROLE_RIGHT_TOE },
	};
	for (int s = 0; s < 2; s++) {
		Foot &foot = feet[s];
		const int upper = database->get_bone_role(roles[s][0]);
		const int lower = database->get_bone_role(roles[s][1]);
		const int ankle = database->get_bone_role(roles[s][2]);
		const bool contact = database->get_contact(current, s);
		const Vector3 animated = character.xform(gp[ankle]);

		const bool lock = foot_locking && contact;
		if (lock && !foot.locked) {
			foot.locked = true;
			foot.lock_position = animated + foot.offset; // Where the foot is shown now.
		}
		if (foot.locked && (!lock || foot.lock_position.distance_to(animated) > unlock_radius)) {
			foot.locked = false; // Blend back from the locked position to the animation.
			foot.offset = foot.lock_position - animated;
		}
		Vector3 target;
		if (foot.locked) {
			target = foot.lock_position;
			const Vector3 new_offset = target - animated;
			foot.offset_velocity = p_dt > 0.0f ? (new_offset - foot.offset) / p_dt : Vector3();
			foot.offset = new_offset;
		} else {
			decay_spring_damper_exact(foot.offset, foot.offset_velocity, inertialization_halflife, p_dt);
			target = animated + foot.offset;
		}

		Vector3 output = animated;
		if (foot_ik && (foot.locked || foot.offset.length_squared() > 1e-8f)) {
			// Two-bone IK (D. Holden, "Simple Two Joint IK"), in the character's space.
			const Vector3 t = character_inv.xform(target);
			const Vector3 a = gp[upper];
			const Vector3 b = gp[lower];
			const Vector3 c = gp[ankle];
			const float eps = 0.01f;
			const float lab = (b - a).length();
			const float lcb = (b - c).length();
			// A soft limit near full extension (the last 3 %), so that a target out of reach does not snap the knee.
			const float reach = lab + lcb;
			float lat = MAX((t - a).length(), eps);
			const float soft = reach * 0.97f;
			if (lat > soft) {
				lat = soft + (reach - eps - soft) * (1.0f - Math::exp(-(lat - soft) / (reach - eps - soft)));
			}
			auto angle_between = [](const Vector3 &u, const Vector3 &v) {
				return Math::acos(CLAMP(u.normalized().dot(v.normalized()), -1.0f, 1.0f));
			};
			const float ac_ab_0 = angle_between(c - a, b - a);
			const float ba_bc_0 = angle_between(a - b, c - b);
			const float ac_at_0 = angle_between(c - a, t - a);
			const float ac_ab_1 = Math::acos(CLAMP((lcb * lcb - lab * lab - lat * lat) / (-2.0f * lab * lat), -1.0f, 1.0f));
			const float ba_bc_1 = Math::acos(CLAMP((lat * lat - lab * lab - lcb * lcb) / (-2.0f * lab * lcb), -1.0f, 1.0f));
			// The knee's bending axis: the animated knee's side of the hip-ankle line, kept on the side a knee bends to
			// (forward, the character's +Z) — a nearly straight leg's own axis flips from frame to frame.
			const Vector3 leg = c - a;
			Vector3 reference = leg.cross(Vector3(0, 0, 1));
			reference = reference.length() > 1e-6f ? reference.normalized() : Vector3(-1, 0, 0);
			const Vector3 bend = leg.cross(b - a);
			const float bend_sin = bend.length() / MAX(leg.length() * (b - a).length(), 1e-6f);
			Vector3 axis0 = reference;
			if (bend.dot(reference) > 0.0f) {
				const float w = CLAMP(bend_sin / 0.15f, 0.0f, 1.0f);
				axis0 = (reference * (1.0f - w) + bend.normalized() * w).normalized();
			}
			Vector3 axis1 = (c - a).cross(t - a);
			const Quaternion a_gr = gq[upper];
			const Quaternion b_gr = gq[lower];
			const Quaternion r0(a_gr.inverse().xform(axis0), ac_ab_1 - ac_ab_0);
			const Quaternion r1(b_gr.inverse().xform(axis0), ba_bc_1 - ba_bc_0);
			Quaternion r2;
			if (axis1.length() > 1e-6f) {
				r2 = Quaternion(a_gr.inverse().xform(axis1.normalized()), ac_at_0);
			}
			const Quaternion ankle_global = gq[ankle];
			out_rotations[upper] = (out_rotations[upper] * (r0 * r2)).normalized();
			out_rotations[lower] = (out_rotations[lower] * r1).normalized();
			// The chain again, the ankle keeping its animated orientation.
			const int upper_parent = database->get_bone_parent(upper);
			const Quaternion parent_q = upper_parent >= 0 ? gq[upper_parent] : Quaternion();
			const Vector3 parent_p = upper_parent >= 0 ? gp[upper_parent] : Vector3();
			const Quaternion upper_q = parent_q * out_rotations[upper];
			const Vector3 upper_p = upper_parent >= 0 ? parent_p + parent_q.xform(out_positions[upper]) : out_positions[upper];
			const Quaternion lower_q = upper_q * out_rotations[lower];
			const Vector3 lower_p = upper_p + upper_q.xform(out_positions[lower]);
			if (database->get_bone_parent(ankle) == lower) {
				out_rotations[ankle] = (lower_q.inverse() * ankle_global).normalized();
			}
			output = character.xform(lower_p + lower_q.xform(out_positions[ankle]));
		}

		// Foot sliding: how far a foot in contact moves on the ground.
		if (contact && foot.last_contact && foot.has_last) {
			const Vector3 d = output - foot.last_output;
			const double slide = Vector2(d.x, d.z).length();
			foot_slide_distance += slide;
			if (foot.locked) {
				foot_slide_locked += slide;
			}
			foot_contact_time += p_dt;
		}
		foot.last_output = output;
		foot.last_animated = animated;
		foot.last_contact = contact;
		foot.has_last = true;
	}
}

void FellowMotionMatcher3D::advance(double p_delta) {
	if (!started) {
		_start();
		if (!started) {
			return;
		}
	}
	if (p_delta <= 0.0) {
		return;
	}
	_update(float(p_delta), _compute_lod());
	_apply_root();
}

void FellowMotionMatcher3D::_group_update(uint32_t p_index, const LocalVector<FellowMotionMatcher3D *> *p_matchers) {
	FellowMotionMatcher3D *m = (*p_matchers)[p_index];
	m->_update(m->group_dt, m->lod);
}

void FellowMotionMatcher3D::advance_group(const TypedArray<FellowMotionMatcher3D> &p_matchers, double p_delta) {
	if (p_delta <= 0.0) {
		return;
	}
	LocalVector<FellowMotionMatcher3D *> matchers;
	for (int i = 0; i < p_matchers.size(); i++) {
		FellowMotionMatcher3D *m = Object::cast_to<FellowMotionMatcher3D>(p_matchers[i]);
		if (!m) {
			continue;
		}
		if (!m->started) {
			m->_start(); // Reads the scene: here, not on a worker.
		}
		if (!m->started) {
			continue;
		}
		m->lod = m->_compute_lod();
		m->group_dt = float(p_delta);
		matchers.push_back(m);
	}
	if (matchers.is_empty()) {
		return;
	}
	if (matchers.size() == 1) {
		matchers[0]->_update(float(p_delta), matchers[0]->lod);
	} else {
		WorkerThreadPool::GroupID group = WorkerThreadPool::get_singleton()->add_template_group_task(matchers[0], &FellowMotionMatcher3D::_group_update, (const LocalVector<FellowMotionMatcher3D *> *)&matchers, matchers.size(), -1, true, SNAME("FellowMotionMatcher3D"));
		WorkerThreadPool::get_singleton()->wait_for_group_task_completion(group);
	}
	for (FellowMotionMatcher3D *m : matchers) {
		m->_apply_root();
	}
}

void FellowMotionMatcher3D::_apply_root() {
	Node3D *root = _get_root();
	if (root) {
		const Vector3 scale = root->get_global_basis().get_scale();
		root->set_global_transform(Transform3D(Basis(char_rotation).scaled_local(scale), char_position));
	}
}

void FellowMotionMatcher3D::_update(float p_dt, int p_lod) {
	const uint64_t t_start = OS::get_singleton()->get_ticks_usec();
	float dt = p_dt;
	lod = p_lod;
	global_stats.lod_updates[lod].increment();
	if (lod == 2) {
		// A quarter of the rate: in between, the root carries on with its velocity.
		lod_accumulated += dt;
		if (++lod_skip % 4 != 0) {
			char_position += char_velocity * dt;
			sim_position += sim_velocity * dt;
			return;
		}
		dt = lod_accumulated;
		lod_accumulated = 0.0f;
	} else {
		lod_skip = 0;
		lod_accumulated = 0.0f;
	}

	const float rate = database->get_sample_rate();
	const int bones = database->get_bone_count();

	// Search.
	transitioned_this_frame = false;
	search_timer -= dt;
	const int stop = database->get_range_stop(range);
	const bool end_of_range = int(frame_time) + 1 >= stop - 1;
	if ((desired_velocity - searched_velocity).length() > force_search_velocity_change) {
		force_search = true;
	}
	if (force_search || search_timer <= 0.0f || end_of_range) {
		float query[FellowMotionDatabase::FEATURE_COUNT];
		_build_query(query);
		const int current = CLAMP(int(Math::round(frame_time)), database->get_range_start(range), stop - 1);
		const uint64_t s0 = OS::get_singleton()->get_ticks_usec();
		const FellowMotionDatabase::SearchResult result = database->search(query, current, !end_of_range);
		const uint64_t us = OS::get_singleton()->get_ticks_usec() - s0;
		searches++;
		search_usec += us;
		search_usec_max = MAX(search_usec_max, us);
		global_stats.searches.increment();
		global_stats.search_usec.add(us);
		global_stats.search_usec_max.exchange_if_greater(us);
		global_stats.search_histogram[MIN(int(us / 5), 999)].increment();
		global_stats.frames_checked.add(result.frames_checked);
		memcpy(last_query, query, sizeof(query));
		last_result_frame = result.frame;
		last_result_cost = result.cost;
		last_current_frame = current;
		if (result.frame >= 0 && result.frame != current) {
			_transition(result.frame);
		}
		search_timer = search_interval * (lod == 0 ? 1.0f : (lod == 1 ? 2.0f : 4.0f));
		force_search = false;
		searched_velocity = desired_velocity;
	}

	// Playback.
	frame_time += double(dt) * rate;
	const int range_stop = database->get_range_stop(range);
	if (frame_time > range_stop - 1) {
		frame_time = range_stop - 1;
		force_search = true;
	}
	Vector3 root_velocity;
	float yaw_rate = 0.0f;
	_sample(frame_time, anim_positions, anim_rotations, &anim_velocities, &anim_angular_velocities, &root_velocity, &yaw_rate);

	// Inertialization: the offsets from the last transition decay.
	for (int b = 0; b < bones; b++) {
		decay_spring_damper_exact(offset_positions[b], offset_velocities[b], inertialization_halflife, dt);
		Vector3 r = quat_to_scaled_angle_axis(offset_rotations[b]);
		decay_spring_damper_exact(r, offset_angular_velocities[b], inertialization_halflife, dt);
		offset_rotations[b] = quat_from_scaled_angle_axis(r);
		out_positions[b] = anim_positions[b] + offset_positions[b];
		out_rotations[b] = (offset_rotations[b] * anim_rotations[b]).normalized();
	}
	decay_spring_damper_exact(offset_root_velocity, offset_root_velocity_rate, inertialization_halflife, dt);
	decay_spring_damper_exact(offset_yaw_rate, offset_yaw_rate_rate, inertialization_halflife, dt);

	// The simulation.
	Vector3 facing = desired_facing.length() > 1e-3f ? desired_facing : (desired_velocity.length() > 0.1f ? desired_velocity : forward_of(sim_rotation));
	facing.y = 0.0f;
	const Quaternion desired_rotation = yaw_rotation(facing.length() > 1e-6f ? facing.normalized() : Vector3(0, 0, 1));
	simulation_position_update(sim_position, sim_velocity, sim_acceleration, desired_velocity, velocity_halflife, dt);
	simulation_rotation_update(sim_rotation, sim_angular_velocity, desired_rotation, rotation_halflife, dt);

	// The character: the animation's root motion, pulled towards the simulation.
	char_velocity = char_rotation.xform(root_velocity + offset_root_velocity);
	char_yaw_rate = yaw_rate + offset_yaw_rate;
	char_position += char_velocity * dt;
	char_rotation = (quat_from_scaled_angle_axis(Vector3(0, char_yaw_rate * dt, 0)) * char_rotation).normalized();
	if (synchronization) {
		const float k = 1.0f - Math::exp(-0.69314718056f * dt / adjustment_halflife);
		Vector3 diff = sim_position - char_position;
		diff.y = 0.0f;
		Vector3 adjust = diff * k;
		const float max_adjust = adjustment_velocity_ratio * char_velocity.length() * dt;
		if (adjust.length() > max_adjust) {
			adjust = adjust.normalized() * max_adjust;
		}
		char_position += adjust;
		Vector3 rot_diff = quat_to_scaled_angle_axis(quat_abs(sim_rotation * char_rotation.inverse()));
		Vector3 rot_adjust = rot_diff * k;
		const float max_rot = adjustment_velocity_ratio * Math::abs(char_yaw_rate) * dt;
		if (rot_adjust.length() > max_rot) {
			rot_adjust = rot_adjust.normalized() * max_rot;
		}
		char_rotation = (quat_from_scaled_angle_axis(rot_adjust) * char_rotation).normalized();
		// Clamping: never farther than this from the simulation.
		Vector3 away = char_position - sim_position;
		away.y = 0.0f;
		if (away.length() > clamp_distance) {
			char_position = sim_position + away.normalized() * clamp_distance + Vector3(0, char_position.y - sim_position.y, 0);
		}
		const Vector3 rot_away = quat_to_scaled_angle_axis(quat_abs(char_rotation * sim_rotation.inverse()));
		if (rot_away.length() > clamp_angle) {
			char_rotation = (quat_from_scaled_angle_axis(rot_away.normalized() * clamp_angle) * sim_rotation).normalized();
		}
	}
	char_position.y = sim_position.y;
	trajectory_error_sum += Vector2(char_position.x - sim_position.x, char_position.z - sim_position.z).length() * dt;
	trajectory_time += dt;

	if (lod == 0) {
		_solve_feet(dt);
	} else {
		for (Foot &foot : feet) {
			foot.locked = false;
			foot.offset = Vector3();
			foot.offset_velocity = Vector3();
			foot.has_last = false;
		}
	}
	pose_ready = true;
	if (lod == 0) {
		_measure_pop();
	}

	const uint64_t us = OS::get_singleton()->get_ticks_usec() - t_start;
	global_stats.updates.increment();
	global_stats.update_usec.add(us);
	global_stats.update_usec_max.exchange_if_greater(us);
	global_stats.update_histogram[MIN(int(us / 5), 999)].increment();
}

void FellowMotionMatcher3D::_process_modification(double p_delta) {
	if (auto_update) {
		advance(p_delta);
	}
	Skeleton3D *skeleton = get_skeleton();
	if (!skeleton || !pose_ready || !started) {
		return;
	}
	if (lod == 2 && lod_skip % 4 != 0) {
		return; // The pose of the last full update stays.
	}
	const int hips = database->get_bone_role(FellowMotionDatabase::ROLE_HIPS);
	for (uint32_t b = 0; b < skeleton_bones.size(); b++) {
		const int sb = skeleton_bones[b];
		if (sb < 0) {
			continue;
		}
		if (int(b) == hips) {
			const Transform3D t = parent_rest_globals[b].affine_inverse() * Transform3D(Basis(out_rotations[b]), out_positions[b]);
			skeleton->set_bone_pose_position(sb, t.origin);
			skeleton->set_bone_pose_rotation(sb, t.basis.get_rotation_quaternion());
		} else {
			skeleton->set_bone_pose_position(sb, out_positions[b]);
			skeleton->set_bone_pose_rotation(sb, out_rotations[b]);
		}
	}
}

void FellowMotionMatcher3D::_measure_pop() {
	thread_local LocalVector<Vector3> gp;
	thread_local LocalVector<Quaternion> gq;
	_forward_kinematics(gp, gq);
	// The hips and the legs (the role bones): CMU's fingers and thumbs flip in the data itself.
	const int bones = gp.size();
	if (joint_history_count >= 2) {
		float worst = 0.0f;
		int worst_bone = -1;
		for (int r = 0; r < FellowMotionDatabase::ROLE_MAX; r++) {
			const int b = database->get_bone_role(FellowMotionDatabase::BoneRole(r));
			if (b < 0 || b >= bones) {
				continue;
			}
			const float a = (gp[b] - joint_history[0][b] * 2.0f + joint_history[1][b]).length();
			if (a > worst) {
				worst = a;
				worst_bone = b;
			}
		}
		const float mm = worst * 1000.0f;
		pop_histogram[MIN(int(mm * 2.0f), 399)]++;
		pop_frames++;
		if (mm > pop_max) {
			pop_max = mm;
			pop_max_bone = worst_bone;
			pop_max_after_transition = transitioned_this_frame;
		}
	}
	joint_history[1] = joint_history[0];
	joint_history[0] = gp;
	joint_history_count = MIN(joint_history_count + 1, 2);
}

int FellowMotionMatcher3D::get_current_frame() const {
	return started ? int(Math::round(frame_time)) : -1;
}

String FellowMotionMatcher3D::get_current_clip() const {
	return started ? database->get_range_name(range) : String();
}

PackedVector3Array FellowMotionMatcher3D::get_foot_positions() const {
	PackedVector3Array out;
	for (const Foot &foot : feet) {
		out.push_back(foot.last_output);
	}
	return out;
}

Dictionary FellowMotionMatcher3D::get_stats() const {
	Dictionary d;
	d["searches"] = int64_t(searches);
	d["transitions"] = int64_t(transitions);
	d["search_usec"] = int64_t(search_usec);
	d["search_usec_max"] = int64_t(search_usec_max);
	d["foot_slide_distance"] = foot_slide_distance;
	d["foot_contact_time"] = foot_contact_time;
	d["foot_slide_locked"] = foot_slide_locked;
	d["foot_slide_speed"] = foot_contact_time > 0.0 ? foot_slide_distance / foot_contact_time : 0.0;
	d["trajectory_error"] = trajectory_time > 0.0 ? trajectory_error_sum / trajectory_time : 0.0;
	// Popping percentiles (mm a frame²).
	auto percentile = [&](double p_q) {
		if (pop_frames == 0) {
			return 0.0;
		}
		const uint64_t target = uint64_t(Math::ceil(p_q * pop_frames));
		uint64_t sum = 0;
		for (int i = 0; i < 400; i++) {
			sum += pop_histogram[i];
			if (sum >= target) {
				return (i + 0.5) * 0.5;
			}
		}
		return 200.0;
	};
	d["pop_p50_mm"] = percentile(0.5);
	d["pop_p99_mm"] = percentile(0.99);
	d["pop_p999_mm"] = percentile(0.999);
	d["pop_max_mm"] = pop_max;
	d["pop_max_bone"] = pop_max_bone >= 0 ? Variant(database->get_bone_name(pop_max_bone)) : Variant();
	d["pop_max_after_transition"] = pop_max_after_transition;
	d["pop_frames"] = int64_t(pop_frames);
	PackedInt32Array histogram;
	for (int i = 0; i < 400; i++) {
		histogram.push_back(pop_histogram[i]);
	}
	d["pop_histogram"] = histogram;
	d["lod"] = lod;
	d["clip"] = get_current_clip();
	d["frame"] = get_current_frame();
	d["left_locked"] = feet[0].locked;
	d["right_locked"] = feet[1].locked;
	return d;
}

Dictionary FellowMotionMatcher3D::get_last_search() const {
	Dictionary d;
	if (!started || last_result_frame < 0) {
		return d;
	}
	// Features in their units (root space: metres, metres per second, unit directions).
	PackedFloat32Array query, result, current;
	const float *rf = database->get_features(last_result_frame);
	const float *cf = database->get_features(last_current_frame);
	for (int i = 0; i < FellowMotionDatabase::FEATURE_COUNT; i++) {
		const float s = database->get_feature_scale(i);
		const float o = database->get_feature_offset(i);
		query.push_back(last_query[i] * s + o);
		result.push_back(rf[i] * s + o);
		current.push_back(cf[i] * s + o);
	}
	d["query"] = query;
	d["result"] = result;
	d["current"] = current;
	d["frame"] = last_result_frame;
	d["clip"] = database->get_range_name(database->get_range_of(last_result_frame));
	d["cost"] = last_result_cost;
	d["current_frame"] = last_current_frame;
	return d;
}

void FellowMotionMatcher3D::reset_stats() {
	searches = 0;
	transitions = 0;
	search_usec = 0;
	search_usec_max = 0;
	foot_slide_distance = 0.0;
	foot_slide_locked = 0.0;
	foot_contact_time = 0.0;
	trajectory_error_sum = 0.0;
	trajectory_time = 0.0;
	for (uint32_t &h : pop_histogram) {
		h = 0;
	}
	pop_frames = 0;
	pop_max = 0.0f;
	pop_max_bone = -1;
}

Dictionary FellowMotionMatcher3D::get_global_stats() {
	Dictionary d;
	d["updates"] = int64_t(global_stats.updates.get());
	d["update_usec"] = int64_t(global_stats.update_usec.get());
	d["update_usec_max"] = int64_t(global_stats.update_usec_max.get());
	d["searches"] = int64_t(global_stats.searches.get());
	d["search_usec"] = int64_t(global_stats.search_usec.get());
	d["search_usec_max"] = int64_t(global_stats.search_usec_max.get());
	d["frames_checked"] = int64_t(global_stats.frames_checked.get());
	d["transitions"] = int64_t(global_stats.transitions.get());
	d["lod0_updates"] = int64_t(global_stats.lod_updates[0].get());
	d["lod1_updates"] = int64_t(global_stats.lod_updates[1].get());
	d["lod2_updates"] = int64_t(global_stats.lod_updates[2].get());
	auto percentile = [](const SafeNumeric<uint32_t> *p_histogram, uint64_t p_count, double p_q) {
		if (p_count == 0) {
			return 0.0;
		}
		const uint64_t target = uint64_t(Math::ceil(p_q * p_count));
		uint64_t sum = 0;
		for (int i = 0; i < 1000; i++) {
			sum += p_histogram[i].get();
			if (sum >= target) {
				return (i + 0.5) * 5.0;
			}
		}
		return 5000.0;
	};
	d["search_usec_p50"] = percentile(global_stats.search_histogram, global_stats.searches.get(), 0.5);
	d["search_usec_p99"] = percentile(global_stats.search_histogram, global_stats.searches.get(), 0.99);
	d["search_usec_p999"] = percentile(global_stats.search_histogram, global_stats.searches.get(), 0.999);
	d["update_usec_p50"] = percentile(global_stats.update_histogram, global_stats.updates.get(), 0.5);
	d["update_usec_p99"] = percentile(global_stats.update_histogram, global_stats.updates.get(), 0.99);
	d["update_usec_p999"] = percentile(global_stats.update_histogram, global_stats.updates.get(), 0.999);
	return d;
}

void FellowMotionMatcher3D::reset_global_stats() {
	for (SafeNumeric<uint64_t> *v : { &global_stats.updates, &global_stats.update_usec, &global_stats.update_usec_max, &global_stats.searches, &global_stats.search_usec, &global_stats.search_usec_max, &global_stats.frames_checked, &global_stats.transitions, &global_stats.lod_updates[0], &global_stats.lod_updates[1], &global_stats.lod_updates[2] }) {
		v->set(0);
	}
	for (int i = 0; i < 1000; i++) {
		global_stats.search_histogram[i].set(0);
		global_stats.update_histogram[i].set(0);
	}
}

void FellowMotionMatcher3D::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_ENTER_TREE: {
			started = false;
		} break;
	}
}

void FellowMotionMatcher3D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_database", "database"), &FellowMotionMatcher3D::set_database);
	ClassDB::bind_method(D_METHOD("get_database"), &FellowMotionMatcher3D::get_database);
	ClassDB::bind_method(D_METHOD("set_root_path", "path"), &FellowMotionMatcher3D::set_root_path);
	ClassDB::bind_method(D_METHOD("get_root_path"), &FellowMotionMatcher3D::get_root_path);
	ClassDB::bind_method(D_METHOD("set_desired_velocity", "velocity"), &FellowMotionMatcher3D::set_desired_velocity);
	ClassDB::bind_method(D_METHOD("get_desired_velocity"), &FellowMotionMatcher3D::get_desired_velocity);
	ClassDB::bind_method(D_METHOD("set_desired_facing", "facing"), &FellowMotionMatcher3D::set_desired_facing);
	ClassDB::bind_method(D_METHOD("get_desired_facing"), &FellowMotionMatcher3D::get_desired_facing);
	ClassDB::bind_method(D_METHOD("set_search_interval", "seconds"), &FellowMotionMatcher3D::set_search_interval);
	ClassDB::bind_method(D_METHOD("get_search_interval"), &FellowMotionMatcher3D::get_search_interval);
	ClassDB::bind_method(D_METHOD("set_inertialization_halflife", "seconds"), &FellowMotionMatcher3D::set_inertialization_halflife);
	ClassDB::bind_method(D_METHOD("get_inertialization_halflife"), &FellowMotionMatcher3D::get_inertialization_halflife);
	ClassDB::bind_method(D_METHOD("set_velocity_halflife", "seconds"), &FellowMotionMatcher3D::set_velocity_halflife);
	ClassDB::bind_method(D_METHOD("get_velocity_halflife"), &FellowMotionMatcher3D::get_velocity_halflife);
	ClassDB::bind_method(D_METHOD("set_rotation_halflife", "seconds"), &FellowMotionMatcher3D::set_rotation_halflife);
	ClassDB::bind_method(D_METHOD("get_rotation_halflife"), &FellowMotionMatcher3D::get_rotation_halflife);
	ClassDB::bind_method(D_METHOD("set_adjustment_halflife", "seconds"), &FellowMotionMatcher3D::set_adjustment_halflife);
	ClassDB::bind_method(D_METHOD("get_adjustment_halflife"), &FellowMotionMatcher3D::get_adjustment_halflife);
	ClassDB::bind_method(D_METHOD("set_clamp_distance", "distance"), &FellowMotionMatcher3D::set_clamp_distance);
	ClassDB::bind_method(D_METHOD("get_clamp_distance"), &FellowMotionMatcher3D::get_clamp_distance);
	ClassDB::bind_method(D_METHOD("set_unlock_radius", "radius"), &FellowMotionMatcher3D::set_unlock_radius);
	ClassDB::bind_method(D_METHOD("get_unlock_radius"), &FellowMotionMatcher3D::get_unlock_radius);
	ClassDB::bind_method(D_METHOD("set_inertialization", "enable"), &FellowMotionMatcher3D::set_inertialization);
	ClassDB::bind_method(D_METHOD("is_inertialization"), &FellowMotionMatcher3D::is_inertialization);
	ClassDB::bind_method(D_METHOD("set_synchronization", "enable"), &FellowMotionMatcher3D::set_synchronization);
	ClassDB::bind_method(D_METHOD("is_synchronization"), &FellowMotionMatcher3D::is_synchronization);
	ClassDB::bind_method(D_METHOD("set_foot_locking", "enable"), &FellowMotionMatcher3D::set_foot_locking);
	ClassDB::bind_method(D_METHOD("is_foot_locking"), &FellowMotionMatcher3D::is_foot_locking);
	ClassDB::bind_method(D_METHOD("set_foot_ik", "enable"), &FellowMotionMatcher3D::set_foot_ik);
	ClassDB::bind_method(D_METHOD("is_foot_ik"), &FellowMotionMatcher3D::is_foot_ik);
	ClassDB::bind_method(D_METHOD("set_auto_update", "enable"), &FellowMotionMatcher3D::set_auto_update);
	ClassDB::bind_method(D_METHOD("is_auto_update"), &FellowMotionMatcher3D::is_auto_update);
	ClassDB::bind_method(D_METHOD("set_lod_distances", "distances"), &FellowMotionMatcher3D::set_lod_distances);
	ClassDB::bind_method(D_METHOD("get_lod_distances"), &FellowMotionMatcher3D::get_lod_distances);
	ClassDB::bind_method(D_METHOD("advance", "delta"), &FellowMotionMatcher3D::advance);
	ClassDB::bind_static_method("FellowMotionMatcher3D", D_METHOD("advance_group", "matchers", "delta"), &FellowMotionMatcher3D::advance_group);
	ClassDB::bind_method(D_METHOD("reset"), &FellowMotionMatcher3D::reset);
	ClassDB::bind_method(D_METHOD("teleport", "position", "facing"), &FellowMotionMatcher3D::teleport);
	ClassDB::bind_method(D_METHOD("get_simulation_position"), &FellowMotionMatcher3D::get_simulation_position);
	ClassDB::bind_method(D_METHOD("get_simulation_velocity"), &FellowMotionMatcher3D::get_simulation_velocity);
	ClassDB::bind_method(D_METHOD("get_character_position"), &FellowMotionMatcher3D::get_character_position);
	ClassDB::bind_method(D_METHOD("get_current_frame"), &FellowMotionMatcher3D::get_current_frame);
	ClassDB::bind_method(D_METHOD("get_current_clip"), &FellowMotionMatcher3D::get_current_clip);
	ClassDB::bind_method(D_METHOD("get_lod"), &FellowMotionMatcher3D::get_lod);
	ClassDB::bind_method(D_METHOD("get_foot_positions"), &FellowMotionMatcher3D::get_foot_positions);
	ClassDB::bind_method(D_METHOD("get_stats"), &FellowMotionMatcher3D::get_stats);
	ClassDB::bind_method(D_METHOD("reset_stats"), &FellowMotionMatcher3D::reset_stats);
	ClassDB::bind_method(D_METHOD("get_last_search"), &FellowMotionMatcher3D::get_last_search);
	ClassDB::bind_static_method("FellowMotionMatcher3D", D_METHOD("get_global_stats"), &FellowMotionMatcher3D::get_global_stats);
	ClassDB::bind_static_method("FellowMotionMatcher3D", D_METHOD("reset_global_stats"), &FellowMotionMatcher3D::reset_global_stats);

	// Milestone 53: grouped in the inspector.
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "database", PROPERTY_HINT_RESOURCE_TYPE, "FellowMotionDatabase"), "set_database", "get_database");
	ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "root_path", PROPERTY_HINT_NODE_PATH_VALID_TYPES, "Node3D"), "set_root_path", "get_root_path");
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR3, "desired_velocity", PROPERTY_HINT_NONE, "suffix:m/s"), "set_desired_velocity", "get_desired_velocity");
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR3, "desired_facing"), "set_desired_facing", "get_desired_facing");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "auto_update"), "set_auto_update", "is_auto_update");
	ADD_GROUP("Search", "");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "search_interval", PROPERTY_HINT_RANGE, "0,1,0.01,suffix:s"), "set_search_interval", "get_search_interval");
	ADD_PROPERTY(PropertyInfo(Variant::PACKED_FLOAT32_ARRAY, "lod_distances"), "set_lod_distances", "get_lod_distances");
	ADD_GROUP("Blending", "");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "inertialization"), "set_inertialization", "is_inertialization");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "inertialization_halflife", PROPERTY_HINT_RANGE, "0,1,0.01,suffix:s"), "set_inertialization_halflife", "get_inertialization_halflife");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "velocity_halflife", PROPERTY_HINT_RANGE, "0.01,2,0.01,suffix:s"), "set_velocity_halflife", "get_velocity_halflife");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "rotation_halflife", PROPERTY_HINT_RANGE, "0.01,2,0.01,suffix:s"), "set_rotation_halflife", "get_rotation_halflife");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "adjustment_halflife", PROPERTY_HINT_RANGE, "0.01,2,0.01,suffix:s"), "set_adjustment_halflife", "get_adjustment_halflife");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "clamp_distance", PROPERTY_HINT_RANGE, "0,2,0.01,suffix:m"), "set_clamp_distance", "get_clamp_distance");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "synchronization"), "set_synchronization", "is_synchronization");
	ADD_GROUP("Feet", "");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "foot_locking"), "set_foot_locking", "is_foot_locking");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "foot_ik"), "set_foot_ik", "is_foot_ik");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "unlock_radius", PROPERTY_HINT_RANGE, "0,1,0.01,suffix:m"), "set_unlock_radius", "get_unlock_radius");
	ADD_GROUP("", "");
}
