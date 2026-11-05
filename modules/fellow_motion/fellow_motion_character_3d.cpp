/**************************************************************************/
/*  fellow_motion_character_3d.cpp                                        */
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

#include "fellow_motion_character_3d.h"

#include "core/config/engine.h"
#include "core/io/file_access.h"
#include "core/object/class_db.h"
#include "scene/3d/mesh_instance_3d.h"
#include "scene/3d/skeleton_3d.h"
#include "scene/resources/3d/primitive_meshes.h"
#include "scene/resources/material.h"
#include "scene/resources/packed_scene.h"

namespace {

constexpr int HISTORY = 32;
constexpr float SAMPLE_RATE = 30.0f;

Mutex body_cache_mutex;
HashMap<String, Ref<PackedScene>> *body_cache = nullptr;

float wrap_angle(float p_a) {
	return Math::fposmod(p_a + float(Math::PI), float(Math::TAU)) - float(Math::PI);
}

// MotionBricks' critically damped spring (math_utils.py), halflife in seconds.
void spring(float &r_x, float &r_v, float p_goal, float p_halflife, float p_dt) {
	float y = (4.0f * 0.69314718f / (p_halflife + 1e-5f)) * 0.5f;
	float j0 = r_x - p_goal;
	float j1 = r_v + j0 * y;
	float e = Math::exp(-y * p_dt);
	float x = e * (j0 + j1 * p_dt) + p_goal;
	float v = e * (r_v - j1 * y * p_dt);
	if (Math::abs(x - p_goal) <= 1e-4f && Math::abs(v) <= 1e-4f) {
		x = p_goal;
		v = 0.0f;
	}
	r_x = x;
	r_v = v;
}

template <typename F>
void each_mesh(Node *p_node, F p_fn) {
	if (MeshInstance3D *mi = Object::cast_to<MeshInstance3D>(p_node)) {
		p_fn(mi);
	}
	for (int i = 0; i < p_node->get_child_count(); i++) {
		each_mesh(p_node->get_child(i), p_fn);
	}
}

Skeleton3D *find_skeleton(Node *p_node) {
	if (Skeleton3D *s = Object::cast_to<Skeleton3D>(p_node)) {
		return s;
	}
	for (int i = 0; i < p_node->get_child_count(); i++) {
		if (Skeleton3D *s = find_skeleton(p_node->get_child(i))) {
			return s;
		}
	}
	return nullptr;
}

} // namespace

void fellow_motion_clear_body_cache() {
	MutexLock lock(body_cache_mutex);
	if (body_cache) {
		memdelete(body_cache);
		body_cache = nullptr;
	}
}

void FellowMotionCharacter3D::set_model_path(const String &p_path) {
	model_path = p_path;
	update_configuration_warnings();
}

void FellowMotionCharacter3D::set_body_path(const String &p_path) {
	body_path = p_path;
	update_configuration_warnings();
}

// Milestone 52: what the Scene dock's warning icon says.
PackedStringArray FellowMotionCharacter3D::get_configuration_warnings() const {
	PackedStringArray warnings = Node3D::get_configuration_warnings();
	if (model_path.is_empty()) {
		warnings.push_back(RTR("Model Path is empty: the character needs a motion model (a .fmb file) to move."));
	} else if (!FileAccess::exists(model_path)) {
		warnings.push_back(vformat(RTR("Model Path (%s) does not exist."), model_path));
	}
	if (!body_path.is_empty() && !FileAccess::exists(body_path)) {
		warnings.push_back(vformat(RTR("Body Path (%s) does not exist."), body_path));
	}
	return warnings;
}

void FellowMotionCharacter3D::set_body_color(const Color &p_c) {
	body_color = p_c;
	if (body) {
		Ref<StandardMaterial3D> m;
		m.instantiate();
		m->set_albedo(body_color);
		m->set_roughness(0.55f);
		each_mesh(body, [&](MeshInstance3D *p_mi) { p_mi->set_material_override(m); });
	}
}

Vector3 FellowMotionCharacter3D::_ground() const {
	return Vector3(pose[0].p.x, 0.0f, pose[0].p.z);
}

float FellowMotionCharacter3D::_heading_of(const Xf *p_local) const {
	return rig->heading_of_local(p_local);
}

// Lying or crawling: the hips or the head well below the relaxed stance's (the model's training data has motion on the
// ground, and a target ahead of a character on the ground keeps it there).
bool FellowMotionCharacter3D::_is_down(const Xf *p_local) const {
	float hips, head;
	rig->hips_head_heights(p_local, hips, head);
	return hips < 0.75f * stand_hips_y || (head_joint >= 0 && head < 0.8f * stand_head_y);
}

void FellowMotionCharacter3D::_start() {
	if (model_path.is_empty()) {
		error = "no model_path.";
		return;
	}
	String err;
	service = FellowMotionService::acquire(model_path, max_batch, err);
	if (service.is_null()) {
		error = err;
		ERR_PRINT("FellowMotionCharacter3D: " + err);
		model_path = String(); // do not retry every frame
		return;
	}
	rig = &service->get_rig();
	const int J = rig->joints;
	relaxed.resize(J);
	rig->relaxed_pose(relaxed.ptr());
	{
		head_joint = rig->find("Head");
		LocalVector<Xf> w;
		w.resize(J);
		rig->world(relaxed.ptr(), w.ptr());
		stand_hips_y = w[0].p.y;
		stand_head_y = head_joint >= 0 ? w[head_joint].p.y : 1.6f;
	}
	// Place the relaxed stance at the node, facing its +Z.
	Transform3D gt = is_inside_tree() ? get_global_transform() : get_transform();
	ground_y = gt.origin.y;
	Vector3 fz = gt.basis.get_column(2);
	float h0 = Math::atan2(fz.x, fz.z);
	Quaternion r(Vector3(0, 1, 0), h0);
	pose = relaxed;
	pose[0].q = r * relaxed[0].q;
	pose[0].p = r.xform(relaxed[0].p) + Vector3(gt.origin.x, 0.0f, gt.origin.z);
	history.resize(HISTORY * J);
	history_head = 0;
	for (int i = 0; i < HISTORY; i++) {
		memcpy((void *)(history.ptr() + i * J), pose.ptr(), J * sizeof(Xf));
		history_dt[i] = 0.0f;
	}
	vel_angle = h0;
	input_angle = h0;
	last_ground = _ground();
	_load_body();
	started = true;
	force_regen = true;
	state = STATE_IDLE;
	_apply_to_skeleton(true);
}

void FellowMotionCharacter3D::_load_body() {
	if (body_path.is_empty()) {
		return;
	}
	Ref<PackedScene> ps;
	{
		MutexLock lock(body_cache_mutex);
		if (!body_cache) {
			body_cache = memnew((HashMap<String, Ref<PackedScene>>));
		}
		if (Ref<PackedScene> *c = body_cache->getptr(body_path)) {
			ps = *c;
		}
	}
	if (ps.is_null()) {
		// glTF at run time (the mannequin tools/fmb_usd.py writes): GLTFDocument through ClassDB, no module dependency.
		Ref<RefCounted> doc = Object::cast_to<RefCounted>(ClassDB::instantiate("GLTFDocument"));
		Ref<RefCounted> st = Object::cast_to<RefCounted>(ClassDB::instantiate("GLTFState"));
		ERR_FAIL_COND_MSG(doc.is_null() || st.is_null(), "FellowMotionCharacter3D: glTF support is not available.");
		Error e = Error(int(doc->call("append_from_file", body_path, st)));
		ERR_FAIL_COND_MSG(e != OK, "FellowMotionCharacter3D: cannot load the body " + body_path + ".");
		Node *scene = Object::cast_to<Node>(doc->call("generate_scene", st));
		ERR_FAIL_NULL_MSG(scene, "FellowMotionCharacter3D: the body has no scene.");
		ps.instantiate();
		ps->pack(scene);
		memdelete(scene);
		MutexLock lock(body_cache_mutex);
		(*body_cache)[body_path] = ps;
	}
	body = Object::cast_to<Node3D>(ps->instantiate());
	ERR_FAIL_NULL(body);
	add_child(body, false, INTERNAL_MODE_FRONT);
	skeleton = find_skeleton(body);
	bones_set = false;
	bone_of_joint.resize(rig->joints);
	for (int j = 0; j < rig->joints; j++) {
		bone_of_joint[j] = skeleton ? skeleton->find_bone(rig->names[j]) : -1;
	}
	set_body_color(body_color);
}

void FellowMotionCharacter3D::_apply_to_skeleton(bool p_all) {
	if (!is_inside_tree()) {
		return;
	}
	Vector3 g = _ground();
	float h = _heading_of(pose.ptr());
	set_global_transform(Transform3D(Basis(Vector3(0, 1, 0), h), Vector3(g.x, ground_y, g.z)));
	if (!skeleton) {
		return;
	}
	Transform3D sk = skeleton->get_global_transform();
	Transform3D hips(Basis(pose[0].q), pose[0].p + Vector3(0, ground_y, 0));
	Transform3D hl = sk.affine_inverse() * hips;
	// Every bone once, then only those the model drives (the others keep the reference pose).
	p_all = p_all || !bones_set;
	bones_set = true;
	const int J = p_all ? rig->joints : int(rig->animated.size());
	for (int i = 0; i < J; i++) {
		int j = p_all ? i : rig->animated[i];
		int b = bone_of_joint[j];
		if (b < 0) {
			continue;
		}
		if (j == 0) {
			skeleton->set_bone_pose_position(b, hl.origin);
			skeleton->set_bone_pose_rotation(b, hl.basis.get_rotation_quaternion());
		} else {
			// Below the hips the joints keep their rest offsets: only the rotations change.
			if (p_all) {
				skeleton->set_bone_pose_position(b, pose[j].p);
			}
			skeleton->set_bone_pose_rotation(b, pose[j].q);
		}
	}
}

void FellowMotionCharacter3D::_update_movement(double p_delta) {
	const float dt = float(p_delta);
	Vector3 v(desired_velocity.x, 0.0f, desired_velocity.z);
	float amount = v.length();
	if (amount > 1e-6f) {
		input_angle = Math::atan2(v.x, v.z);
		input_speed = MIN(amount, max_speed);
	} else {
		input_speed = 0.0f;
	}
	if (input_speed > 0.0f) {
		float goal = vel_angle + wrap_angle(input_angle - vel_angle);
		spring(vel_angle, vel_angle_rate, goal, 0.2f, dt);
		vel_angle = wrap_angle(vel_angle);
	}
	float goal_speed = input_speed;
	Vector3 cur(Math::sin(vel_angle), 0, Math::cos(vel_angle));
	Vector3 in(Math::sin(input_angle), 0, Math::cos(input_angle));
	if (cur.dot(in) < 0.92f) {
		goal_speed *= 1.0f - 0.25f * CLAMP(speed / max_speed, 0.0f, 1.0f);
	}
	spring(speed, speed_rate, goal_speed, speed_halflife, dt);
	if (speed > max_speed) {
		speed = max_speed;
		speed_rate = MIN(speed_rate, 0.0f);
	}
	speed = MAX(speed, 0.0f);
}

bool FellowMotionCharacter3D::_needs_regeneration() const {
	float prev = prev_gen_velocity.length();
	bool started_moving = speed > 0.0f && prev <= 1e-6f;
	float delta = speed - prev;
	float remaining = delta > 0.0f ? max_speed - prev : prev;
	float threshold = MAX(remaining * 0.3f, max_speed * 0.1f);
	if (started_moving || Math::abs(delta) > threshold) {
		return true;
	}
	if (prev <= 1e-6f) {
		return false;
	}
	Vector3 cur(Math::sin(vel_angle), 0, Math::cos(vel_angle));
	return cur.dot(prev_gen_velocity / prev) < 0.92f;
}

void FellowMotionCharacter3D::_context(Xf *r_context) const {
	const int J = rig->joints;
	float times[HISTORY];
	times[HISTORY - 1] = 0.0f;
	for (int i = HISTORY - 2; i >= 0; i--) {
		times[i] = times[i + 1] - history_dt[i];
	}
	for (int k = 0; k < 4; k++) {
		float s = -float(3 - k) / SAMPLE_RATE;
		int upper = HISTORY;
		for (int i = 0; i < HISTORY; i++) {
			if (times[i] > s) {
				upper = i;
				break;
			}
		}
		upper = MIN(upper, HISTORY - 1);
		int lower = MAX(upper - 1, 0);
		float blend = 0.0f;
		float span = times[upper] - times[lower];
		if (span > 0.0f) {
			blend = CLAMP((s - times[lower]) / span, 0.0f, 1.0f);
		} else {
			lower = upper;
		}
		const Xf *a = history.ptr() + ((history_head + lower) % HISTORY) * J;
		const Xf *b = history.ptr() + ((history_head + upper) % HISTORY) * J;
		Xf *o = r_context + k * J;
		// The joints the model does not drive are the same in every recorded pose.
		memcpy((void *)o, (const void *)b, J * sizeof(Xf));
		for (int j : rig->animated) {
			o[j].p = a[j].p.lerp(b[j].p, blend);
			o[j].q = a[j].q.slerp(b[j].q, blend);
		}
	}
}

void FellowMotionCharacter3D::_record(double p_delta) {
	if (p_delta <= 0.0) {
		return;
	}
	const int J = rig->joints;
	for (int i = 0; i < HISTORY - 1; i++) {
		history_dt[i] = history_dt[i + 1];
	}
	history_dt[HISTORY - 2] = float(p_delta);
	history_dt[HISTORY - 1] = 0.0f;
	// A ring: the oldest pose's slot takes the newest.
	history_head = (history_head + 1) % HISTORY;
	memcpy((void *)(history.ptr() + ((history_head + HISTORY - 1) % HISTORY) * J), pose.ptr(), J * sizeof(Xf));
}

void FellowMotionCharacter3D::_generate(bool p_idle) {
	const int J = rig->joints;
	LocalVector<Xf> ctx, tgt;
	ctx.resize(4 * J);
	tgt.resize(4 * J);
	_context(ctx.ptr());
	const Xf *base = ctx.ptr() + 3 * J;
	Vector3 ground(base[0].p.x, 0.0f, base[0].p.z);
	float heading_now = _heading_of(base);
	Vector3 v;
	// On the ground (lying, crawling), a standing pose as the target, a little ahead if moving: the model gets up.
	bool rise = get_up && _is_down(base);
	if (!p_idle && !rise) {
		// Root-only target: lead_time ahead along the smoothed desired velocity, facing it, moving at it.
		v = Vector3(Math::sin(vel_angle), 0.0f, Math::cos(vel_angle)) * speed;
		Vector3 gt = ground + v * lead_time;
		Quaternion turn(Vector3(0, 1, 0), wrap_angle(vel_angle - heading_now));
		// The hips at a standing height for the speed (a little lower running), never the current one: a target at
		// the hips' height of the moment let a crowd's frequent re-planning sink lower each time, down to crawling.
		float hips_y = relaxed[0].p.y - 0.012f * MAX(0.0f, speed - 1.5f);
		for (int k = 0; k < 4; k++) {
			Xf *t = tgt.ptr() + k * J;
			memcpy((void *)t, base, J * sizeof(Xf));
			t[0].q = (turn * base[0].q).normalized();
			t[0].p = Vector3(gt.x + v.x * k / SAMPLE_RATE, hips_y, gt.z + v.z * k / SAMPLE_RATE);
		}
	} else {
		// Pose target: the relaxed stance where the character comes to rest (or stands up), facing as it does (or
		// its way, getting up while moving).
		Vector3 p;
		float facing = heading_now;
		if (rise) {
			bool moving = !p_idle && speed > 0.05f;
			if (moving) {
				facing = vel_angle;
			}
			p = ground + (moving ? Vector3(Math::sin(vel_angle), 0.0f, Math::cos(vel_angle)) * MIN(speed, 1.0f) * 0.5f : Vector3());
		} else {
			p = state == STATE_IDLE && actual_velocity.length() < 0.2f ? ground : ground + actual_velocity * 0.6f;
		}
		Quaternion r(Vector3(0, 1, 0), facing);
		for (int k = 0; k < 4; k++) {
			Xf *t = tgt.ptr() + k * J;
			memcpy((void *)t, relaxed.ptr(), J * sizeof(Xf));
			t[0].q = (r * relaxed[0].q).normalized();
			t[0].p = r.xform(relaxed[0].p) + Vector3(p.x, 0.0f, p.z);
		}
	}
	uint32_t bits = 0;
	for (int c = MIN(min_tokens, max_tokens); c <= MAX(min_tokens, max_tokens); c++) {
		bits |= 1u << (c - 6);
	}
	// The service encodes the poses, and decodes and corrects the prediction (in parallel with the other
	// characters', off this thread when asynchronous).
	FellowMotionService::Request req;
	req.context = std::move(ctx);
	req.target = std::move(tgt);
	req.target_has_pose = p_idle || rise;
	req.allowed_bits = bits;
	req.correct_end = p_idle;
	pending_idle = p_idle;
	pending_get_up = rise;
	if (rise) {
		get_ups++;
	}
	pending_time = 0.0;
	prev_gen_velocity = v;
	force_regen = false;
	generations++;
	if (inference_mode == INFERENCE_IMMEDIATE) {
		FellowMotionService::Result res;
		if (service->run_now(std::move(req), res)) {
			_accept(res);
		}
		return;
	}
	pending_id = service->submit(std::move(req), inference_mode == INFERENCE_ASYNC);
}

void FellowMotionCharacter3D::_accept(FellowMotionService::Result &p_result) {
	pending_id = 0;
	if (p_result.tokens <= 0 || p_result.count < 2) {
		return;
	}
	latency = 0.8 * latency + 0.2 * pending_time;
	latency_sum += pending_time;
	// The old segment keeps its frames for the crossfade; the new one takes the result's.
	SWAP(old_seg, seg);
	fade = (inference_mode == INFERENCE_IMMEDIATE || !old_seg.valid() || crossfade_time <= 0.0f) ? 1.0 : 0.0;
	seg.frames = std::move(p_result.frames);
	seg.contacts = std::move(p_result.contacts);
	seg.count = p_result.count;
	seg.transition_early = pending_idle ? 0 : transition_early_frames;
	seg.get_up = pending_get_up;
	seg.time = pending_time;
}

void FellowMotionCharacter3D::_sample(const Segment &p_seg, Xf *r_pose) const {
	const int J = rig->joints;
	double f = CLAMP(p_seg.time * SAMPLE_RATE, 0.0, double(p_seg.count - 1));
	int a = int(Math::floor(f));
	int b = MIN(a + 1, p_seg.count - 1);
	float w = a < p_seg.count - 1 ? float(CLAMP(f - a, 0.0, 1.0)) : 0.0f;
	const Xf *fa = p_seg.frames.ptr() + a * J;
	const Xf *fb = p_seg.frames.ptr() + b * J;
	// The joints the model drives (the others are the reference pose in every frame and stay as they are).
	for (int j : rig->animated) {
		r_pose[j].p = fa[j].p.lerp(fb[j].p, w);
		r_pose[j].q = fa[j].q.slerp(fb[j].q, w);
	}
}

void FellowMotionCharacter3D::_process(double p_delta) {
	if (!started) {
		if (model_path.is_empty()) {
			return;
		}
		_start();
		if (!started) {
			return;
		}
	}
	if (pending_id) {
		pending_time += p_delta;
		FellowMotionService::Result res;
		if (service->fetch(pending_id, res)) {
			_accept(res);
		}
	}
	_update_movement(p_delta);
	bool entered = false;
	if (state == STATE_LOCOMOTION && input_speed <= 0.0f) {
		state = STATE_IDLE;
		entered = true;
	} else if (state == STATE_IDLE && input_speed > 0.0f) {
		state = STATE_LOCOMOTION;
		entered = true;
		// The target at once at the desired speed (the model accelerates over the segment); the direction from the
		// motion if there is any.
		Vector3 av(actual_velocity.x, 0.0f, actual_velocity.z);
		speed = input_speed;
		vel_angle = av.length() > 0.2f ? Math::atan2(av.x, av.z) : input_angle;
		speed_rate = 0.0f;
		vel_angle_rate = 0.0f;
	}
	if (entered) {
		force_regen = true;
		if (pending_id) {
			service->cancel(pending_id);
			pending_id = 0;
		}
	}
	double lookahead = inference_mode == INFERENCE_IMMEDIATE ? p_delta : latency + p_delta;
	if (!pending_id) {
		if (state == STATE_LOCOMOTION) {
			// Catching up: while the shown speed is far from the target's (accelerating, braking), re-plan every 0.33 s
			// instead of at the transition point (the model approaches a target over its whole segment).
			// Getting up plays to its transition point.
			bool rising = seg.valid() && seg.get_up && seg.time_to_transition() > lookahead;
			bool behind = catch_up && !rising && seg.valid() && seg.time > 0.33 && Math::abs(shown_speed - speed) > MAX(0.25f * speed, 0.3f);
			if (force_regen || !seg.valid() || seg.time_to_transition() <= lookahead || (!rising && _needs_regeneration()) || behind) {
				_generate(false);
			}
		} else if (force_regen || (idle_motion && (!seg.valid() || seg.time_to_transition() <= lookahead))) {
			_generate(true);
		}
	}
	seg.time += p_delta;
	old_seg.time += p_delta;
	if (seg.valid()) {
		const int J = rig->joints;
		_sample(seg, pose.ptr());
		if (fade < 1.0 && old_seg.valid()) {
			fade = MIN(1.0, fade + p_delta / MAX(1e-3, double(crossfade_time)));
			float w = float(fade * fade * (3.0 - 2.0 * fade));
			fade_pose.resize(J);
			Xf *o = fade_pose.ptr();
			_sample(old_seg, o);
			for (int j : rig->animated) {
				pose[j].p = o[j].p.lerp(pose[j].p, w);
				pose[j].q = o[j].q.slerp(pose[j].q, w);
			}
		}
	}
	_record(p_delta);
	Vector3 g = _ground();
	actual_velocity = p_delta > 0.0 ? (g - last_ground) / p_delta : Vector3();
	last_ground = g;
	// The speed shown, smoothed over 0.2 s (a stride's sway is not a speed error).
	shown_speed += (actual_velocity.length() - shown_speed) * float(1.0 - Math::exp(-p_delta / 0.2));
	_apply_to_skeleton();
}

PackedVector3Array FellowMotionCharacter3D::get_joint_positions() const {
	PackedVector3Array r;
	if (!rig) {
		return r;
	}
	LocalVector<Xf> w;
	w.resize(rig->joints);
	rig->world(pose.ptr(), w.ptr());
	r.resize(rig->joints);
	for (int j = 0; j < rig->joints; j++) {
		r.set(j, w[j].p + Vector3(0, ground_y, 0));
	}
	return r;
}

Dictionary FellowMotionCharacter3D::get_stats() const {
	Dictionary d;
	d["generations"] = int64_t(generations);
	d["latency_msec"] = latency * 1000.0;
	d["avg_latency_msec"] = generations ? latency_sum / double(generations) * 1000.0 : 0.0;
	d["state"] = int(state);
	d["speed"] = speed;
	d["segment_frames"] = seg.count;
	d["error"] = error;
	d["get_ups"] = int64_t(get_ups);
	return d;
}

Dictionary FellowMotionCharacter3D::get_service_stats(const String &p_model_path) {
	return FellowMotionService::get_stats_for(p_model_path);
}

void FellowMotionCharacter3D::flush_requests(const String &p_model_path) {
	String err;
	Ref<FellowMotionService> s = FellowMotionService::acquire(p_model_path, 32, err);
	if (s.is_valid()) {
		s->flush();
	}
}

void FellowMotionCharacter3D::set_auto_process(bool p_v) {
	auto_process = p_v;
	if (is_inside_tree() && !Engine::get_singleton()->is_editor_hint()) {
		set_process(auto_process);
	}
}

void FellowMotionCharacter3D::advance(double p_delta) {
	_process(p_delta);
}

void FellowMotionCharacter3D::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_READY: {
			if (!Engine::get_singleton()->is_editor_hint()) {
				set_process(auto_process);
			}
		} break;
		case NOTIFICATION_PROCESS: {
			_process(get_process_delta_time());
		} break;
		case NOTIFICATION_EXIT_TREE: {
			if (pending_id && service.is_valid()) {
				service->cancel(pending_id);
				pending_id = 0;
			}
		} break;
	}
}

void FellowMotionCharacter3D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_model_path", "path"), &FellowMotionCharacter3D::set_model_path);
	ClassDB::bind_method(D_METHOD("get_model_path"), &FellowMotionCharacter3D::get_model_path);
	ClassDB::bind_method(D_METHOD("set_body_path", "path"), &FellowMotionCharacter3D::set_body_path);
	ClassDB::bind_method(D_METHOD("get_body_path"), &FellowMotionCharacter3D::get_body_path);
	ClassDB::bind_method(D_METHOD("set_desired_velocity", "velocity"), &FellowMotionCharacter3D::set_desired_velocity);
	ClassDB::bind_method(D_METHOD("get_desired_velocity"), &FellowMotionCharacter3D::get_desired_velocity);
	ClassDB::bind_method(D_METHOD("set_max_speed", "speed"), &FellowMotionCharacter3D::set_max_speed);
	ClassDB::bind_method(D_METHOD("get_max_speed"), &FellowMotionCharacter3D::get_max_speed);
	ClassDB::bind_method(D_METHOD("set_transition_early_frames", "frames"), &FellowMotionCharacter3D::set_transition_early_frames);
	ClassDB::bind_method(D_METHOD("get_transition_early_frames"), &FellowMotionCharacter3D::get_transition_early_frames);
	ClassDB::bind_method(D_METHOD("set_min_tokens", "tokens"), &FellowMotionCharacter3D::set_min_tokens);
	ClassDB::bind_method(D_METHOD("get_min_tokens"), &FellowMotionCharacter3D::get_min_tokens);
	ClassDB::bind_method(D_METHOD("set_max_tokens", "tokens"), &FellowMotionCharacter3D::set_max_tokens);
	ClassDB::bind_method(D_METHOD("get_max_tokens"), &FellowMotionCharacter3D::get_max_tokens);
	ClassDB::bind_method(D_METHOD("set_catch_up", "enable"), &FellowMotionCharacter3D::set_catch_up);
	ClassDB::bind_method(D_METHOD("is_catch_up"), &FellowMotionCharacter3D::is_catch_up);
	ClassDB::bind_method(D_METHOD("set_speed_halflife", "seconds"), &FellowMotionCharacter3D::set_speed_halflife);
	ClassDB::bind_method(D_METHOD("get_speed_halflife"), &FellowMotionCharacter3D::get_speed_halflife);
	ClassDB::bind_method(D_METHOD("set_lead_time", "seconds"), &FellowMotionCharacter3D::set_lead_time);
	ClassDB::bind_method(D_METHOD("get_lead_time"), &FellowMotionCharacter3D::get_lead_time);
	ClassDB::bind_method(D_METHOD("set_inference_mode", "mode"), &FellowMotionCharacter3D::set_inference_mode);
	ClassDB::bind_method(D_METHOD("get_inference_mode"), &FellowMotionCharacter3D::get_inference_mode);
	ClassDB::bind_method(D_METHOD("set_auto_process", "enable"), &FellowMotionCharacter3D::set_auto_process);
	ClassDB::bind_method(D_METHOD("is_auto_process"), &FellowMotionCharacter3D::is_auto_process);
	ClassDB::bind_method(D_METHOD("advance", "delta"), &FellowMotionCharacter3D::advance);
	ClassDB::bind_static_method("FellowMotionCharacter3D", D_METHOD("flush_requests", "model_path"), &FellowMotionCharacter3D::flush_requests);
	ClassDB::bind_method(D_METHOD("set_crossfade_time", "seconds"), &FellowMotionCharacter3D::set_crossfade_time);
	ClassDB::bind_method(D_METHOD("get_crossfade_time"), &FellowMotionCharacter3D::get_crossfade_time);
	ClassDB::bind_method(D_METHOD("set_max_batch", "requests"), &FellowMotionCharacter3D::set_max_batch);
	ClassDB::bind_method(D_METHOD("get_max_batch"), &FellowMotionCharacter3D::get_max_batch);
	ClassDB::bind_method(D_METHOD("set_body_color", "color"), &FellowMotionCharacter3D::set_body_color);
	ClassDB::bind_method(D_METHOD("get_body_color"), &FellowMotionCharacter3D::get_body_color);
	ClassDB::bind_method(D_METHOD("set_idle_motion", "enable"), &FellowMotionCharacter3D::set_idle_motion);
	ClassDB::bind_method(D_METHOD("is_idle_motion"), &FellowMotionCharacter3D::is_idle_motion);
	ClassDB::bind_method(D_METHOD("set_get_up", "enable"), &FellowMotionCharacter3D::set_get_up);
	ClassDB::bind_method(D_METHOD("is_get_up"), &FellowMotionCharacter3D::is_get_up);
	ClassDB::bind_method(D_METHOD("get_actual_velocity"), &FellowMotionCharacter3D::get_actual_velocity);
	ClassDB::bind_method(D_METHOD("get_state"), &FellowMotionCharacter3D::get_state);
	ClassDB::bind_method(D_METHOD("is_ready"), &FellowMotionCharacter3D::is_ready);
	ClassDB::bind_method(D_METHOD("get_error"), &FellowMotionCharacter3D::get_error);
	ClassDB::bind_method(D_METHOD("get_joint_positions"), &FellowMotionCharacter3D::get_joint_positions);
	ClassDB::bind_method(D_METHOD("get_stats"), &FellowMotionCharacter3D::get_stats);
	ClassDB::bind_static_method("FellowMotionCharacter3D", D_METHOD("get_service_stats", "model_path"), &FellowMotionCharacter3D::get_service_stats);

	// Milestone 53: grouped in the inspector.
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "model_path", PROPERTY_HINT_FILE, "*.fmb"), "set_model_path", "get_model_path");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "body_path", PROPERTY_HINT_FILE, "*.glb,*.gltf"), "set_body_path", "get_body_path");
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "body_color"), "set_body_color", "get_body_color");
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR3, "desired_velocity", PROPERTY_HINT_NONE, "suffix:m/s"), "set_desired_velocity", "get_desired_velocity");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "max_speed", PROPERTY_HINT_RANGE, "0.1,8,0.01,suffix:m/s"), "set_max_speed", "get_max_speed");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "auto_process"), "set_auto_process", "is_auto_process");
	ADD_GROUP("Motion", "");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "idle_motion"), "set_idle_motion", "is_idle_motion");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "get_up"), "set_get_up", "is_get_up");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "catch_up"), "set_catch_up", "is_catch_up");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "speed_halflife", PROPERTY_HINT_RANGE, "0.01,2,0.01,suffix:s"), "set_speed_halflife", "get_speed_halflife");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "crossfade_time", PROPERTY_HINT_RANGE, "0,1,0.01,suffix:s"), "set_crossfade_time", "get_crossfade_time");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "lead_time", PROPERTY_HINT_RANGE, "0.1,3,0.01,suffix:s"), "set_lead_time", "get_lead_time");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "transition_early_frames", PROPERTY_HINT_RANGE, "0,20,1"), "set_transition_early_frames", "get_transition_early_frames");
	ADD_GROUP("Inference", "");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "inference_mode", PROPERTY_HINT_ENUM, "Async,Flush,Immediate"), "set_inference_mode", "get_inference_mode");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "min_tokens", PROPERTY_HINT_RANGE, "6,16,1"), "set_min_tokens", "get_min_tokens");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "max_tokens", PROPERTY_HINT_RANGE, "6,16,1"), "set_max_tokens", "get_max_tokens");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "max_batch", PROPERTY_HINT_RANGE, "1,256,1"), "set_max_batch", "get_max_batch");
	ADD_GROUP("", "");

	BIND_ENUM_CONSTANT(STATE_IDLE);
	BIND_ENUM_CONSTANT(STATE_LOCOMOTION);
	BIND_ENUM_CONSTANT(INFERENCE_ASYNC);
	BIND_ENUM_CONSTANT(INFERENCE_FLUSH);
	BIND_ENUM_CONSTANT(INFERENCE_IMMEDIATE);
}

FellowMotionCharacter3D::FellowMotionCharacter3D() {
}

FellowMotionCharacter3D::~FellowMotionCharacter3D() {
	if (pending_id && service.is_valid()) {
		service->cancel(pending_id);
	}
}
