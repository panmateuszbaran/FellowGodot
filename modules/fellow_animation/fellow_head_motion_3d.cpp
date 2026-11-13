/**************************************************************************/
/*  fellow_head_motion_3d.cpp                                             */
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

#include "fellow_head_motion_3d.h"

#include "fellow_face_animator.h"

#include "core/object/class_db.h"
#include "core/os/os.h"
#include "scene/3d/skeleton_3d.h"

FellowFaceAnimator *FellowHeadMotion3D::_face() const {
	if (face_cache.is_valid()) {
		if (FellowFaceAnimator *f = ObjectDB::get_instance<FellowFaceAnimator>(face_cache)) {
			return f;
		}
	}
	face_cache = ObjectID();
	if (face_animator_path.is_empty() || !is_inside_tree()) {
		return nullptr;
	}
	FellowFaceAnimator *f = Object::cast_to<FellowFaceAnimator>(get_node_or_null(face_animator_path));
	if (f) {
		face_cache = f->get_instance_id();
	}
	return f;
}

void FellowHeadMotion3D::_find_bones(Skeleton3D *p_skeleton) {
	if (bones_skeleton == p_skeleton->get_instance_id() && bones_version == int(p_skeleton->get_version())) {
		return;
	}
	bones_skeleton = p_skeleton->get_instance_id();
	bones_version = int(p_skeleton->get_version());
	head_index = head_bone == StringName() ? -1 : p_skeleton->find_bone(head_bone);
	neck_index = neck_bone == StringName() ? -1 : p_skeleton->find_bone(neck_bone);
	if (neck_index == head_index) {
		neck_index = -1;
	}
}

void FellowHeadMotion3D::_reseed() {
	RandomPCG rng(uint64_t(seed) * 2654435761u + 77u);
	for (float &p : phases) {
		p = rng.randf() * float(Math::TAU);
	}
}

// A bone's rotation in the skeleton's space, from its pose and its ancestors'.
static Quaternion _fellow_head_global_rotation(const Skeleton3D *p_skeleton, int p_bone) {
	Quaternion q;
	for (int b = p_bone; b >= 0; b = p_skeleton->get_bone_parent(b)) {
		q = p_skeleton->get_bone_pose_rotation(b) * q;
	}
	return q;
}

// A bone turned by p_rotation in the skeleton's space, its children with it.
static void _fellow_head_turn(Skeleton3D *p_skeleton, int p_bone, const Quaternion &p_rotation) {
	const Quaternion parent = _fellow_head_global_rotation(p_skeleton, p_skeleton->get_bone_parent(p_bone));
	const Quaternion global = parent * p_skeleton->get_bone_pose_rotation(p_bone);
	p_skeleton->set_bone_pose_rotation(p_bone, (parent.inverse() * (p_rotation * global)).normalized());
}

void FellowHeadMotion3D::_process_modification(double p_delta) {
	Skeleton3D *skeleton = get_skeleton();
	if (!skeleton) {
		return;
	}
	const uint64_t t0 = OS::get_singleton()->get_ticks_usec();
	clock += p_delta;
	float speech = 0.0f;
	if (FellowFaceAnimator *face = _face()) {
		const int64_t count = face->get_emphasis_count();
		if (emphases_seen < 0) {
			emphases_seen = count; // the ones before this node watched are not nodded
		} else if (count > emphases_seen) {
			for (int64_t i = emphases_seen; i < count; i++) {
				nod(face->get_last_emphasis_strength());
			}
			emphases_seen = count;
		}
		speech = face->get_speech_activity();
	}
	// The nods: each a critically damped pulse, its lowest point (the amplitude) nod_time after it starts.
	float pitch = 0.0f;
	for (int i = int(nods.size()) - 1; i >= 0; i--) {
		const double t = clock - nods[i].start;
		if (t > nod_time * 12.0) {
			nods.remove_at(i);
			continue;
		}
		if (t > 0.0) {
			const float x = float(t / nod_time);
			pitch += nods[i].amplitude * x * Math::exp(1.0f - x);
		}
	}
	// The sway: two sines an axis at frequencies that do not repeat together, larger while the face speaks.
	float yaw = 0.0f;
	float roll = 0.0f;
	const float a = idle_sway_degrees + speech_sway_degrees * speech;
	if (a > 0.0f) {
		const double w = Math::TAU * sway_frequency * clock;
		pitch += a * float(0.6 * Math::sin(w + phases[0]) + 0.4 * Math::sin(1.618 * w + phases[1]));
		yaw = a * float(0.6 * Math::sin(0.83 * w + phases[2]) + 0.4 * Math::sin(2.13 * w + phases[3]));
		roll = a * 0.5f * float(0.6 * Math::sin(0.71 * w + phases[4]) + 0.4 * Math::sin(1.37 * w + phases[5]));
	}
	angles = Vector3(pitch, yaw, roll);
	frames++;
	if (pitch == 0.0f && yaw == 0.0f && roll == 0.0f) {
		usec += OS::get_singleton()->get_ticks_usec() - t0;
		return; // the animation's pose as it is
	}
	_find_bones(skeleton);
	if (head_index < 0) {
		return;
	}
	const Vector3 up(0, 1, 0);
	Vector3 fwd = forward.normalized();
	Vector3 right = up.cross(fwd);
	if (right.length_squared() < 1e-6f) {
		right = Vector3(1, 0, 0);
	}
	right.normalize();
	const Quaternion r = Quaternion(up, Math::deg_to_rad(yaw)) * Quaternion(right, Math::deg_to_rad(pitch)) * Quaternion(fwd, Math::deg_to_rad(roll));
	if (neck_index >= 0 && neck_share > 0.0f) {
		_fellow_head_turn(skeleton, neck_index, Quaternion().slerp(r, neck_share));
		_fellow_head_turn(skeleton, head_index, Quaternion().slerp(r, 1.0f - neck_share));
	} else {
		_fellow_head_turn(skeleton, head_index, r);
	}
	usec += OS::get_singleton()->get_ticks_usec() - t0;
}

void FellowHeadMotion3D::nod(float p_strength) {
	Nod n;
	n.start = clock;
	n.amplitude = nod_degrees * CLAMP(p_strength, 0.0f, 2.0f);
	if (nods.size() >= 8) {
		nods.remove_at(0);
	}
	nods.push_back(n);
	nod_count++;
}

void FellowHeadMotion3D::set_face_animator_path(const NodePath &p_path) {
	face_animator_path = p_path;
	face_cache = ObjectID();
	emphases_seen = -1;
	update_configuration_warnings();
}

void FellowHeadMotion3D::set_head_bone(const StringName &p_bone) {
	head_bone = p_bone;
	bones_version = -1;
	update_configuration_warnings();
}

void FellowHeadMotion3D::set_neck_bone(const StringName &p_bone) {
	neck_bone = p_bone;
	bones_version = -1;
}

void FellowHeadMotion3D::set_forward(const Vector3 &p_forward) {
	forward = p_forward.is_zero_approx() ? Vector3(0, 0, 1) : p_forward.normalized();
}

void FellowHeadMotion3D::set_seed(int64_t p_seed) {
	seed = p_seed;
	_reseed();
}

Dictionary FellowHeadMotion3D::get_stats() const {
	Dictionary d;
	d["frames"] = frames;
	d["usec"] = usec;
	d["nods"] = nod_count;
	d["nods_active"] = nods.size();
	d["head_bone"] = head_index;
	d["neck_bone"] = neck_index;
	return d;
}

PackedStringArray FellowHeadMotion3D::get_configuration_warnings() const {
	PackedStringArray warnings = SkeletonModifier3D::get_configuration_warnings();
	Skeleton3D *skeleton = get_skeleton();
	if (skeleton && (head_bone == StringName() || skeleton->find_bone(head_bone) < 0)) {
		warnings.push_back(RTR("The head bone is not in the skeleton: set head_bone to its name."));
	}
	if (!face_animator_path.is_empty() && is_inside_tree() && !Object::cast_to<FellowFaceAnimator>(get_node_or_null(face_animator_path))) {
		warnings.push_back(RTR("face_animator_path does not point to a FellowFaceAnimator: the head sways, but does not nod with the voice."));
	}
	return warnings;
}

void FellowHeadMotion3D::_notification(int p_what) {
	if (p_what == NOTIFICATION_ENTER_TREE) {
		face_cache = ObjectID();
		bones_version = -1;
	}
}

void FellowHeadMotion3D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_face_animator_path", "path"), &FellowHeadMotion3D::set_face_animator_path);
	ClassDB::bind_method(D_METHOD("get_face_animator_path"), &FellowHeadMotion3D::get_face_animator_path);
	ClassDB::bind_method(D_METHOD("set_head_bone", "bone"), &FellowHeadMotion3D::set_head_bone);
	ClassDB::bind_method(D_METHOD("get_head_bone"), &FellowHeadMotion3D::get_head_bone);
	ClassDB::bind_method(D_METHOD("set_neck_bone", "bone"), &FellowHeadMotion3D::set_neck_bone);
	ClassDB::bind_method(D_METHOD("get_neck_bone"), &FellowHeadMotion3D::get_neck_bone);
	ClassDB::bind_method(D_METHOD("set_neck_share", "share"), &FellowHeadMotion3D::set_neck_share);
	ClassDB::bind_method(D_METHOD("get_neck_share"), &FellowHeadMotion3D::get_neck_share);
	ClassDB::bind_method(D_METHOD("set_forward", "forward"), &FellowHeadMotion3D::set_forward);
	ClassDB::bind_method(D_METHOD("get_forward"), &FellowHeadMotion3D::get_forward);
	ClassDB::bind_method(D_METHOD("set_nod_degrees", "degrees"), &FellowHeadMotion3D::set_nod_degrees);
	ClassDB::bind_method(D_METHOD("get_nod_degrees"), &FellowHeadMotion3D::get_nod_degrees);
	ClassDB::bind_method(D_METHOD("set_nod_time", "seconds"), &FellowHeadMotion3D::set_nod_time);
	ClassDB::bind_method(D_METHOD("get_nod_time"), &FellowHeadMotion3D::get_nod_time);
	ClassDB::bind_method(D_METHOD("set_speech_sway_degrees", "degrees"), &FellowHeadMotion3D::set_speech_sway_degrees);
	ClassDB::bind_method(D_METHOD("get_speech_sway_degrees"), &FellowHeadMotion3D::get_speech_sway_degrees);
	ClassDB::bind_method(D_METHOD("set_idle_sway_degrees", "degrees"), &FellowHeadMotion3D::set_idle_sway_degrees);
	ClassDB::bind_method(D_METHOD("get_idle_sway_degrees"), &FellowHeadMotion3D::get_idle_sway_degrees);
	ClassDB::bind_method(D_METHOD("set_sway_frequency", "hz"), &FellowHeadMotion3D::set_sway_frequency);
	ClassDB::bind_method(D_METHOD("get_sway_frequency"), &FellowHeadMotion3D::get_sway_frequency);
	ClassDB::bind_method(D_METHOD("set_seed", "seed"), &FellowHeadMotion3D::set_seed);
	ClassDB::bind_method(D_METHOD("get_seed"), &FellowHeadMotion3D::get_seed);
	ClassDB::bind_method(D_METHOD("nod", "strength"), &FellowHeadMotion3D::nod, DEFVAL(1.0f));
	ClassDB::bind_method(D_METHOD("get_head_angles"), &FellowHeadMotion3D::get_head_angles);
	ClassDB::bind_method(D_METHOD("get_stats"), &FellowHeadMotion3D::get_stats);

	ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "face_animator_path", PROPERTY_HINT_NODE_PATH_VALID_TYPES, "FellowFaceAnimator"), "set_face_animator_path", "get_face_animator_path");
	ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "head_bone"), "set_head_bone", "get_head_bone");
	ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "neck_bone"), "set_neck_bone", "get_neck_bone");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "neck_share", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_neck_share", "get_neck_share");
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR3, "forward"), "set_forward", "get_forward");
	ADD_GROUP("Nods", "nod_");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "nod_degrees", PROPERTY_HINT_RANGE, "0,30,0.1,degrees"), "set_nod_degrees", "get_nod_degrees");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "nod_time", PROPERTY_HINT_RANGE, "0.03,1,0.01,suffix:s"), "set_nod_time", "get_nod_time");
	ADD_GROUP("Sway", "");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "speech_sway_degrees", PROPERTY_HINT_RANGE, "0,20,0.1,degrees"), "set_speech_sway_degrees", "get_speech_sway_degrees");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "idle_sway_degrees", PROPERTY_HINT_RANGE, "0,20,0.1,degrees"), "set_idle_sway_degrees", "get_idle_sway_degrees");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "sway_frequency", PROPERTY_HINT_RANGE, "0.01,5,0.01,suffix:Hz"), "set_sway_frequency", "get_sway_frequency");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "seed"), "set_seed", "get_seed");
}

FellowHeadMotion3D::FellowHeadMotion3D() {
	_reseed();
}
