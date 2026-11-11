/**************************************************************************/
/*  fellow_traversal_pose_3d.cpp                                          */
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

#include "fellow_traversal_pose_3d.h"

#include "core/object/class_db.h"
#include "core/os/os.h"
#include "scene/3d/skeleton_3d.h"

namespace {

const char *ROLE_NAMES[FellowTraversalPose3D::ROLE_MAX] = {
	"hips",
	"spine",
	"chest",
	"left_upper_arm",
	"left_lower_arm",
	"left_hand",
	"right_upper_arm",
	"right_lower_arm",
	"right_hand",
	"left_upper_leg",
	"left_lower_leg",
	"left_foot",
	"right_upper_leg",
	"right_lower_leg",
	"right_foot",
};

// The names a role's bone has in common rigs (Godot's humanoid profile, Mixamo, Unreal's mannequin, Rigify, Blender's
// .L/.R), compared without case, separators and the rig's prefix; the first found wins.
const char *const ROLE_CANDIDATES[FellowTraversalPose3D::ROLE_MAX][8] = {
	{ "hips", "pelvis", "hip", nullptr },
	{ "spine", "spine01", "spine001", nullptr },
	{ "chest", "spine2", "spine03", "spine02", "spine1", "upperchest", "spine002", nullptr },
	{ "leftupperarm", "leftarm", "upperarml", "lupperarm", "arml", "upperarmleft", nullptr },
	{ "leftlowerarm", "leftforearm", "lowerarml", "forearml", "lforearm", "lowerarmleft", nullptr },
	{ "lefthand", "handl", "lhand", "handleft", nullptr },
	{ "rightupperarm", "rightarm", "upperarmr", "rupperarm", "armr", "upperarmright", nullptr },
	{ "rightlowerarm", "rightforearm", "lowerarmr", "forearmr", "rforearm", "lowerarmright", nullptr },
	{ "righthand", "handr", "rhand", "handright", nullptr },
	{ "leftupperleg", "leftupleg", "thighl", "upperlegl", "lthigh", "thighleft", nullptr },
	{ "leftlowerleg", "leftleg", "calfl", "shinl", "lowerlegl", "lcalf", "calfleft", nullptr },
	{ "leftfoot", "footl", "lfoot", "footleft", nullptr },
	{ "rightupperleg", "rightupleg", "thighr", "upperlegr", "rthigh", "thighright", nullptr },
	{ "rightlowerleg", "rightleg", "calfr", "shinr", "lowerlegr", "rcalf", "calfright", nullptr },
	{ "rightfoot", "footr", "rfoot", "footright", nullptr },
};

String normalized_bone_name(const String &p_name) {
	String n = p_name;
	const int colon = n.rfind_char(':');
	if (colon >= 0) {
		n = n.substr(colon + 1);
	}
	n = n.to_lower().remove_chars(" _.-");
	for (const char *prefix : { "mixamorig", "def", "bip001", "bip01" }) {
		if (n.begins_with(prefix) && n.length() > int(strlen(prefix))) {
			n = n.substr(strlen(prefix));
			break;
		}
	}
	return n;
}

void find_role_bones(const Skeleton3D *p_skeleton, const Dictionary &p_names, int r_bones[FellowTraversalPose3D::ROLE_MAX]) {
	HashMap<String, int> by_name;
	for (int b = 0; b < p_skeleton->get_bone_count(); b++) {
		const String n = normalized_bone_name(p_skeleton->get_bone_name(b));
		if (!by_name.has(n)) {
			by_name.insert(n, b);
		}
	}
	for (int r = 0; r < FellowTraversalPose3D::ROLE_MAX; r++) {
		r_bones[r] = -1;
		const Variant given = p_names.get(ROLE_NAMES[r], Variant());
		if (given.get_type() == Variant::STRING || given.get_type() == Variant::STRING_NAME) {
			r_bones[r] = p_skeleton->find_bone(given);
			continue;
		}
		for (int c = 0; c < 8 && ROLE_CANDIDATES[r][c]; c++) {
			const HashMap<String, int>::ConstIterator it = by_name.find(ROLE_CANDIDATES[r][c]);
			if (it) {
				r_bones[r] = it->value;
				break;
			}
		}
	}
	// A spine found as the chest too (a rig with one spine bone): the chest is the spine.
	if (r_bones[FellowTraversalPose3D::ROLE_CHEST] == r_bones[FellowTraversalPose3D::ROLE_SPINE]) {
		r_bones[FellowTraversalPose3D::ROLE_CHEST] = -1;
	}
}

Quaternion global_rotation(const Skeleton3D *p_skeleton, int p_bone) {
	return p_bone < 0 ? Quaternion() : p_skeleton->get_bone_global_pose(p_bone).basis.get_rotation_quaternion();
}

} // namespace

FellowTraversalPose3D::FellowTraversalPose3D() {
	for (int &b : bones) {
		b = -1;
	}
}

const char *FellowTraversalPose3D::get_role_name(Role p_role) {
	ERR_FAIL_INDEX_V(p_role, ROLE_MAX, "");
	return ROLE_NAMES[p_role];
}

FellowTraversal3D *FellowTraversalPose3D::_get_traversal() const {
	if (!traversal_path.is_empty()) {
		return Object::cast_to<FellowTraversal3D>(get_node_or_null(traversal_path));
	}
	FellowTraversal3D *cached = ObjectDB::get_instance<FellowTraversal3D>(traversal_cache);
	if (cached && cached->is_inside_tree() && is_inside_tree() && cached->get_parent() && cached->get_parent()->is_ancestor_of(this)) {
		return cached;
	}
	// The character's traversal: a child of one of the skeleton's ancestors (the body, usually the skeleton's
	// grandparent through the model's root).
	for (Node *n = get_parent(); n; n = n->get_parent()) {
		for (int i = 0; i < n->get_child_count(); i++) {
			if (FellowTraversal3D *t = Object::cast_to<FellowTraversal3D>(n->get_child(i))) {
				traversal_cache = t->get_instance_id();
				return t;
			}
		}
	}
	return nullptr;
}

void FellowTraversalPose3D::_find_bones(Skeleton3D *p_skeleton) {
	if (!bones_dirty && bones_skeleton == p_skeleton->get_instance_id() && bones_count == p_skeleton->get_bone_count()) {
		return;
	}
	find_role_bones(p_skeleton, bone_names, bones);
	bones_skeleton = p_skeleton->get_instance_id();
	bones_count = p_skeleton->get_bone_count();
	bones_dirty = false;
}

int FellowTraversalPose3D::get_role_bone(Role p_role) {
	ERR_FAIL_INDEX_V(p_role, ROLE_MAX, -1);
	Skeleton3D *skeleton = get_skeleton();
	if (!skeleton) {
		return -1;
	}
	_find_bones(skeleton);
	return bones[p_role];
}

void FellowTraversalPose3D::_skeleton_changed(Skeleton3D *p_old, Skeleton3D *p_new) {
	SkeletonModifier3D::_skeleton_changed(p_old, p_new);
	bones_dirty = true;
}

void FellowTraversalPose3D::set_traversal_path(const NodePath &p_path) {
	traversal_path = p_path;
	traversal_cache = ObjectID();
	update_configuration_warnings();
}

void FellowTraversalPose3D::set_bone_names(const Dictionary &p_names) {
	bone_names = p_names;
	bones_dirty = true;
	update_configuration_warnings();
}

bool FellowTraversalPose3D::fellow_processing_is_noop() const {
	if (SkeletonModifier3D::fellow_processing_is_noop()) {
		return true;
	}
	if (weight > 0.0f) {
		return false;
	}
	const FellowTraversal3D *t = _get_traversal();
	return !t || !t->is_active();
}

// The clip's bones at its time (the root's motion since the clip's start taken out: the traversal moves the body).
void FellowTraversalPose3D::_apply_clip(Skeleton3D *p_skeleton, const FellowTraversal3D::PoseHints &p_hints, float p_weight) {
	const Ref<Animation> &clip = p_hints.clip;
	const double t = CLAMP(p_hints.clip_time, 0.0, clip->get_length());
	for (int i = 0; i < clip->get_track_count(); i++) {
		const Animation::TrackType type = clip->track_get_type(i);
		if ((type != Animation::TYPE_POSITION_3D && type != Animation::TYPE_ROTATION_3D && type != Animation::TYPE_SCALE_3D) || !clip->track_is_enabled(i)) {
			continue;
		}
		const StringName name = clip->track_get_path(i).get_concatenated_subnames();
		const int bone = p_skeleton->find_bone(name);
		if (bone < 0) {
			continue;
		}
		if (type == Animation::TYPE_POSITION_3D) {
			Vector3 p;
			if (clip->try_position_track_interpolate(i, t, &p) != OK) {
				continue;
			}
			if (name == p_hints.clip_root_bone) {
				p -= p_hints.clip_root_offset;
			}
			p_skeleton->set_bone_pose_position(bone, p_skeleton->get_bone_pose_position(bone).lerp(p, p_weight));
		} else if (type == Animation::TYPE_ROTATION_3D) {
			Quaternion q;
			if (clip->try_rotation_track_interpolate(i, t, &q) != OK) {
				continue;
			}
			p_skeleton->set_bone_pose_rotation(bone, p_skeleton->get_bone_pose_rotation(bone).slerp(q.normalized(), p_weight));
		} else {
			Vector3 s;
			if (clip->try_scale_track_interpolate(i, t, &s) != OK) {
				continue;
			}
			p_skeleton->set_bone_pose_scale(bone, p_skeleton->get_bone_pose_scale(bone).lerp(s, p_weight));
		}
	}
}

// A bone turned about an axis (skeleton space) by an angle, its children with it.
void FellowTraversalPose3D::_lean(Skeleton3D *p_skeleton, const Vector3 &p_axis, float p_angle) {
	const int spine = bones[ROLE_SPINE];
	const int chest = bones[ROLE_CHEST];
	const int targets[2] = { spine, chest };
	const int count = (spine >= 0) + (chest >= 0);
	if (count == 0 || Math::is_zero_approx(p_angle)) {
		return;
	}
	const Quaternion r(p_axis, p_angle / count);
	for (int b : targets) {
		if (b < 0) {
			continue;
		}
		const Quaternion parent = global_rotation(p_skeleton, p_skeleton->get_bone_parent(b));
		const Quaternion global = global_rotation(p_skeleton, b);
		p_skeleton->set_bone_pose_rotation(b, (parent.inverse() * (r * global)).normalized());
	}
}

// A two-bone chain (the upper bone, the lower, the end) reaching for a target (skeleton space), bending towards a pole;
// the end keeps its rotation from the lower bone. Blended by weight in the bones' local rotations.
void FellowTraversalPose3D::_two_bone(Skeleton3D *p_skeleton, int p_upper, int p_lower, int p_end, const Vector3 &p_target, const Vector3 &p_pole, float p_weight) {
	if (p_upper < 0 || p_lower < 0 || p_end < 0 || p_weight <= 0.001f) {
		return;
	}
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
	d = CLAMP(d, Math::abs(a - b) + 1e-3f, a + b - 1e-3f);
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
	const Quaternion upper_parent = global_rotation(p_skeleton, p_skeleton->get_bone_parent(p_upper));
	const Quaternion lower_parent = global_rotation(p_skeleton, p_skeleton->get_bone_parent(p_lower));
	const Quaternion upper_global = gu.basis.get_rotation_quaternion();
	const Quaternion lower_global = gl.basis.get_rotation_quaternion();
	// Everything under the upper bone turns with it: the lower bone's parent by q1 (it is the upper bone, or a twist
	// bone under it).
	const Quaternion upper_local = (upper_parent.inverse() * (q1 * upper_global)).normalized();
	const Quaternion lower_local = ((q1 * lower_parent).inverse() * (q2 * q1 * lower_global)).normalized();
	p_skeleton->set_bone_pose_rotation(p_upper, p_skeleton->get_bone_pose_rotation(p_upper).slerp(upper_local, p_weight));
	p_skeleton->set_bone_pose_rotation(p_lower, p_skeleton->get_bone_pose_rotation(p_lower).slerp(lower_local, p_weight));
}

void FellowTraversalPose3D::_process_modification(double p_delta) {
	const uint64_t t0 = OS::get_singleton()->get_ticks_usec();
	_pose(p_delta);
	if (weight > 0.0f) {
		FellowTraversal3D::global_stats.poses.increment();
		FellowTraversal3D::global_stats.pose_usec.add(OS::get_singleton()->get_ticks_usec() - t0);
	}
}

void FellowTraversalPose3D::_pose(double p_delta) {
	Skeleton3D *skeleton = get_skeleton();
	FellowTraversal3D *traversal = _get_traversal();
	if (!skeleton || !traversal) {
		weight = 0.0f;
		return;
	}
	const FellowTraversal3D::PoseHints &now = traversal->get_pose_hints();
	if (now.active) {
		last = now;
	}
	const float goal = now.active ? 1.0f : 0.0f;
	weight = blend_time > 0.0f ? Math::move_toward(weight, goal, float(p_delta) / blend_time) : goal;
	hips_drop = 0.0f;
	if (weight <= 0.0f) {
		last.clip.unref();
		return;
	}
	_find_bones(skeleton);
	const FellowTraversal3D::PoseHints &h = last;
	if (h.clip.is_valid()) {
		_apply_clip(skeleton, h, weight);
		return;
	}
	const Transform3D to_skeleton = skeleton->get_global_transform().affine_inverse();
	const Vector3 up = to_skeleton.basis.xform(Vector3(0, 1, 0)).normalized();
	const Vector3 inward = to_skeleton.basis.xform(h.inward).normalized();
	const Vector3 left = to_skeleton.basis.xform(h.left).normalized();
	if (lean && h.lean != 0.0f && left != Vector3()) {
		_lean(skeleton, left, h.lean * weight);
	}
	// The hips lowered (as far as the traversal allows) until the shoulders are within the arms' reach of the hands'
	// targets: hanging from the edge, pushing up from it.
	const float meter = to_skeleton.basis.xform(Vector3(0, 1, 0)).length(); // a meter in the skeleton's units
	float drop = 0.0f;
	const int hips = bones[ROLE_HIPS];
	if (hand_ik && hips >= 0 && h.hips_drop > 0.0f) {
		float need = 0.0f;
		for (int s = 0; s < 2; s++) {
			const int upper = bones[s == 0 ? ROLE_LEFT_UPPER_ARM : ROLE_RIGHT_UPPER_ARM];
			const int lower = bones[s == 0 ? ROLE_LEFT_LOWER_ARM : ROLE_RIGHT_LOWER_ARM];
			const int hand = bones[s == 0 ? ROLE_LEFT_HAND : ROLE_RIGHT_HAND];
			if (upper < 0 || lower < 0 || hand < 0 || h.hand_weight[s] <= 0.0f) {
				continue;
			}
			const Vector3 shoulder = skeleton->get_bone_global_pose(upper).origin;
			const float reach = 0.97f * (shoulder.distance_to(skeleton->get_bone_global_pose(lower).origin) + skeleton->get_bone_global_pose(lower).origin.distance_to(skeleton->get_bone_global_pose(hand).origin));
			const Vector3 d = shoulder - to_skeleton.xform(h.hand[s]);
			const float v = d.dot(up);
			const float across = (d - up * v).length();
			const float v_max = across < reach ? Math::sqrt(reach * reach - across * across) : 0.0f;
			need = MAX(need, MAX(0.0f, v - v_max) * h.hand_weight[s]);
		}
		drop = MIN(need, h.hips_drop * meter) * weight;
		if (drop > 0.0f) {
			const Vector3 lowered = skeleton->get_bone_global_pose(hips).origin - up * drop;
			const int parent = skeleton->get_bone_parent(hips);
			skeleton->set_bone_pose_position(hips, parent >= 0 ? skeleton->get_bone_global_pose(parent).affine_inverse().xform(lowered) : lowered);
		}
	}
	hips_drop = drop / meter;
	if (foot_ik) {
		for (int s = 0; s < 2; s++) {
			const int upper = bones[s == 0 ? ROLE_LEFT_UPPER_LEG : ROLE_RIGHT_UPPER_LEG];
			const int lower = bones[s == 0 ? ROLE_LEFT_LOWER_LEG : ROLE_RIGHT_LOWER_LEG];
			const int foot = bones[s == 0 ? ROLE_LEFT_FOOT : ROLE_RIGHT_FOOT];
			if (upper < 0 || h.foot_weight[s] <= 0.0f) {
				continue;
			}
			// The knee towards the wall.
			const Vector3 hip = skeleton->get_bone_global_pose(upper).origin;
			const Vector3 side = left * (s == 0 ? 1.0f : -1.0f);
			const Quaternion foot_global = global_rotation(skeleton, foot);
			const Vector3 target = to_skeleton.xform(h.foot[s]) - up * (drop * h.foot_follow[s]);
			_two_bone(skeleton, upper, lower, foot, target, hip + (inward * 0.8f + up * 0.2f + side * 0.1f), h.foot_weight[s] * weight);
			// The foot keeps its turn in the world (flat on a top, along the wall).
			if (foot >= 0) {
				const Quaternion parent = global_rotation(skeleton, skeleton->get_bone_parent(foot));
				skeleton->set_bone_pose_rotation(foot, skeleton->get_bone_pose_rotation(foot).slerp((parent.inverse() * foot_global).normalized(), h.foot_weight[s] * weight));
			}
		}
	}
	if (hand_ik) {
		for (int s = 0; s < 2; s++) {
			const int upper = bones[s == 0 ? ROLE_LEFT_UPPER_ARM : ROLE_RIGHT_UPPER_ARM];
			const int lower = bones[s == 0 ? ROLE_LEFT_LOWER_ARM : ROLE_RIGHT_LOWER_ARM];
			const int hand = bones[s == 0 ? ROLE_LEFT_HAND : ROLE_RIGHT_HAND];
			if (upper < 0 || h.hand_weight[s] <= 0.0f) {
				continue;
			}
			// The elbow out to the side, down and back.
			const Vector3 shoulder = skeleton->get_bone_global_pose(upper).origin;
			const Vector3 side = left * (s == 0 ? 1.0f : -1.0f);
			_two_bone(skeleton, upper, lower, hand, to_skeleton.xform(h.hand[s]), shoulder + (side * 0.6f - up * 0.4f - inward * 0.3f), h.hand_weight[s] * weight);
		}
	}
}

PackedStringArray FellowTraversalPose3D::get_configuration_warnings() const {
	PackedStringArray warnings = SkeletonModifier3D::get_configuration_warnings();
	if (!is_inside_tree()) {
		return warnings;
	}
	if (!_get_traversal()) {
		if (traversal_path.is_empty()) {
			warnings.push_back(RTR("A FellowTraversalPose3D poses the body a FellowTraversal3D moves: none was found among the children of this skeleton's ancestors (set Traversal Path)."));
		} else {
			warnings.push_back(vformat(RTR("Traversal Path (%s) leads to no FellowTraversal3D."), String(traversal_path)));
		}
	}
	const Skeleton3D *skeleton = get_skeleton();
	if (skeleton) {
		int found[ROLE_MAX];
		find_role_bones(skeleton, bone_names, found);
		PackedStringArray missing;
		for (int r = ROLE_LEFT_UPPER_ARM; r < ROLE_MAX; r++) {
			if (found[r] < 0) {
				missing.push_back(ROLE_NAMES[r]);
			}
		}
		if (found[ROLE_SPINE] < 0 && found[ROLE_CHEST] < 0) {
			missing.push_back(ROLE_NAMES[ROLE_SPINE]);
		}
		if (!missing.is_empty()) {
			warnings.push_back(vformat(RTR("No bone found for %s: name them in Bone Names (the role's name to the bone's)."), String(", ").join(missing)));
		}
	}
	return warnings;
}

void FellowTraversalPose3D::_notification(int p_what) {
	if (p_what == NOTIFICATION_ENTER_TREE || p_what == NOTIFICATION_EXIT_TREE) {
		traversal_cache = ObjectID();
		weight = 0.0f;
	}
}

void FellowTraversalPose3D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_traversal_path", "path"), &FellowTraversalPose3D::set_traversal_path);
	ClassDB::bind_method(D_METHOD("get_traversal_path"), &FellowTraversalPose3D::get_traversal_path);
	ClassDB::bind_method(D_METHOD("set_blend_time", "seconds"), &FellowTraversalPose3D::set_blend_time);
	ClassDB::bind_method(D_METHOD("get_blend_time"), &FellowTraversalPose3D::get_blend_time);
	ClassDB::bind_method(D_METHOD("set_hand_ik", "enabled"), &FellowTraversalPose3D::set_hand_ik);
	ClassDB::bind_method(D_METHOD("is_hand_ik"), &FellowTraversalPose3D::is_hand_ik);
	ClassDB::bind_method(D_METHOD("set_foot_ik", "enabled"), &FellowTraversalPose3D::set_foot_ik);
	ClassDB::bind_method(D_METHOD("is_foot_ik"), &FellowTraversalPose3D::is_foot_ik);
	ClassDB::bind_method(D_METHOD("set_lean", "enabled"), &FellowTraversalPose3D::set_lean);
	ClassDB::bind_method(D_METHOD("is_lean"), &FellowTraversalPose3D::is_lean);
	ClassDB::bind_method(D_METHOD("set_bone_names", "names"), &FellowTraversalPose3D::set_bone_names);
	ClassDB::bind_method(D_METHOD("get_bone_names"), &FellowTraversalPose3D::get_bone_names);
	ClassDB::bind_method(D_METHOD("get_role_bone", "role"), &FellowTraversalPose3D::get_role_bone);
	ClassDB::bind_method(D_METHOD("get_hips_drop"), &FellowTraversalPose3D::get_hips_drop);
	ClassDB::bind_method(D_METHOD("get_weight"), &FellowTraversalPose3D::get_weight);

	ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "traversal_path", PROPERTY_HINT_NODE_PATH_VALID_TYPES, "FellowTraversal3D"), "set_traversal_path", "get_traversal_path");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "blend_time", PROPERTY_HINT_RANGE, "0,1,0.01,or_greater,suffix:s"), "set_blend_time", "get_blend_time");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "hand_ik"), "set_hand_ik", "is_hand_ik");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "foot_ik"), "set_foot_ik", "is_foot_ik");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "lean"), "set_lean", "is_lean");
	ADD_PROPERTY(PropertyInfo(Variant::DICTIONARY, "bone_names", PROPERTY_HINT_DICTIONARY_TYPE, "String;StringName"), "set_bone_names", "get_bone_names");

	BIND_ENUM_CONSTANT(ROLE_HIPS);
	BIND_ENUM_CONSTANT(ROLE_SPINE);
	BIND_ENUM_CONSTANT(ROLE_CHEST);
	BIND_ENUM_CONSTANT(ROLE_LEFT_UPPER_ARM);
	BIND_ENUM_CONSTANT(ROLE_LEFT_LOWER_ARM);
	BIND_ENUM_CONSTANT(ROLE_LEFT_HAND);
	BIND_ENUM_CONSTANT(ROLE_RIGHT_UPPER_ARM);
	BIND_ENUM_CONSTANT(ROLE_RIGHT_LOWER_ARM);
	BIND_ENUM_CONSTANT(ROLE_RIGHT_HAND);
	BIND_ENUM_CONSTANT(ROLE_LEFT_UPPER_LEG);
	BIND_ENUM_CONSTANT(ROLE_LEFT_LOWER_LEG);
	BIND_ENUM_CONSTANT(ROLE_LEFT_FOOT);
	BIND_ENUM_CONSTANT(ROLE_RIGHT_UPPER_LEG);
	BIND_ENUM_CONSTANT(ROLE_RIGHT_LOWER_LEG);
	BIND_ENUM_CONSTANT(ROLE_RIGHT_FOOT);
	BIND_ENUM_CONSTANT(ROLE_MAX);
}
