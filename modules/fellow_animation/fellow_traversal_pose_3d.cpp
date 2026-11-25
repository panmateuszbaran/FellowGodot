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

// Milestone 198 (decision 0340): a hand held on a ledge (ledge_grip). The palm this far from level towards up, against
// the face under the edge; the middle finger's knuckle this far out from the face and over the edge (along the top's
// normal); the fingers' first two bones this far below the top's plane, over it; the grip turned to and from in about
// GRIP_EASE.
const float GRIP_PITCH = 70.0f; // degrees
const float GRIP_KNUCKLE_OUT = 0.01f; // m
const float GRIP_KNUCKLE_UP = 0.02f; // m
const float GRIP_EASE = 0.12f; // s
const float GRIP_HAND_SMOOTHING = 0.015f; // s
const float GRIP_TURN = 0.08f; // s: the palm's way from the knuckles to the wrist eased over this
const float GRIP_ELBOW_TURN = 720.0f; // degrees a second
const float GRIP_PALM_CLEAR = 0.024f; // m: the palm's half thickness and a little
// Milestone 200 (decision 0342): a hand on its hold — its fingers over the edge eased in over this; the hang's fit
// following its goal's steady drift at once, up to GRIP_FIT_FOLLOW.
const float GRIP_HOLD_EASE = 0.04f; // s
const float GRIP_FIT_FOLLOW = 1.2f; // m/s
// Milestone 201 (decision 0343): a foot the clip puts into the stone (the traversal's foot_clear) put on its face, the
// ankle this far off it, the knee this far.
const float FOOT_CLEAR = 0.1f; // m
const float FOOT_KNEE_CLEAR = 0.08f; // m: and the knee (more than _clear_wall's: its turn not needed)
const float GRIP_EASE_AIR = 0.03f; // s: the grip's shape in, reaching for a hold from the air
// A step just under the edge (its top no more than GRIP_STEP under it, standing out at least GRIP_STEP_OUT): held by its
// own edge instead; the fingers on it.
const float GRIP_STEP = 0.04f; // m
const float GRIP_STEP_OUT = 0.06f; // m
// The fingers' middle joints and tips this far in over the edge and this far over the top.
const float GRIP_FINGER_IN[2] = { 0.037f, 0.08f }; // m
const float GRIP_FINGER_UP[2] = { 0.016f, 0.01f }; // m
const float GRIP_THUMB_IN = 0.025f; // m: the thumb's tip in over the edge
const float GRIP_THUMB_UP = 0.012f; // m: over the top

float smooth01(float p_x) {
	const float x = CLAMP(p_x, 0.0f, 1.0f);
	return x * x * (3.0f - 2.0f * x);
}

// An orthonormal frame whose first column is along p_along, its second p_across made square to it.
Basis frame_of(const Vector3 &p_along, const Vector3 &p_across) {
	const Vector3 f = p_along.normalized();
	Vector3 a = p_across - f * p_across.dot(f);
	if (f == Vector3() || a.length() < 1e-5f) {
		return Basis();
	}
	a.normalize();
	Basis b;
	b.set_columns(f, a, f.cross(a));
	return b;
}

// A bone turned (its parent's pose kept) the least way that points it along p_dir (skeleton space), by p_weight. Its
// axis: to its first child, or (an end bone) its parent's continued (a finger's bones straight at rest).
void aim_bone(Skeleton3D *p_skeleton, int p_bone, const Vector3 &p_dir, float p_weight) {
	if (p_bone < 0 || p_weight <= 0.0f) {
		return;
	}
	const Vector<int> children = p_skeleton->get_bone_children(p_bone);
	Vector3 axis;
	if (!children.is_empty()) {
		axis = p_skeleton->get_bone_pose_position(children[0]);
	} else {
		const Transform3D rest = p_skeleton->get_bone_rest(p_bone);
		axis = rest.basis.inverse().xform(rest.origin);
	}
	if (axis.length() < 1e-6f) {
		return;
	}
	const Quaternion now = global_rotation(p_skeleton, p_bone);
	const Vector3 from = now.xform(axis.normalized());
	const Vector3 to = p_dir.normalized();
	if (from.dot(to) > 0.99999f) {
		return;
	}
	const Quaternion swing = from.dot(to) < -0.9999f ? Quaternion(from.get_any_perpendicular(), Math::PI) : Quaternion(from, to);
	const Quaternion parent = global_rotation(p_skeleton, p_skeleton->get_bone_parent(p_bone));
	p_skeleton->set_bone_pose_rotation(p_bone, (parent.inverse() * (Quaternion().slerp(swing, p_weight) * now)).normalized());
}

} // namespace

FellowTraversalPose3D::FellowTraversalPose3D() {
	for (int &b : bones) {
		b = -1;
	}
	for (int s = 0; s < 2; s++) {
		for (int f = 0; f < 5; f++) {
			fingers[s][f][0] = fingers[s][f][1] = -1;
		}
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
	_find_fingers(p_skeleton);
	bones_skeleton = p_skeleton->get_instance_id();
	bones_count = p_skeleton->get_bone_count();
	bones_dirty = false;
}

// Milestone 198 (decision 0340): each hand's fingers — the hand bone's children named for the thumb, the index, middle,
// ring and little finger (or pinky), and each one's first child.
void FellowTraversalPose3D::_find_fingers(Skeleton3D *p_skeleton) {
	static const char *const names[5][2] = { { "thumb", nullptr }, { "index", nullptr }, { "middle", nullptr }, { "ring", nullptr }, { "little", "pinky" } };
	for (int s = 0; s < 2; s++) {
		for (int f = 0; f < 5; f++) {
			fingers[s][f][0] = fingers[s][f][1] = -1;
		}
		const int hand = bones[s == 0 ? ROLE_LEFT_HAND : ROLE_RIGHT_HAND];
		if (hand < 0) {
			continue;
		}
		for (const int c : p_skeleton->get_bone_children(hand)) {
			const String n = normalized_bone_name(p_skeleton->get_bone_name(c));
			for (int f = 0; f < 5; f++) {
				if (fingers[s][f][0] < 0 && (n.contains(names[f][0]) || (names[f][1] && n.contains(names[f][1])))) {
					fingers[s][f][0] = c;
					const Vector<int> next = p_skeleton->get_bone_children(c);
					fingers[s][f][1] = next.is_empty() ? -1 : next[0];
					break;
				}
			}
		}
	}
}

// The hand's length from the wrist to the middle finger's knuckle (the skeleton's units; 0: no such finger).
float FellowTraversalPose3D::_palm_length(Skeleton3D *p_skeleton, int p_side) const {
	const int middle = fingers[p_side][2][0];
	const int index = fingers[p_side][1][0];
	const int little = fingers[p_side][4][0];
	if (middle < 0 || index < 0 || little < 0) {
		return 0.0f;
	}
	return p_skeleton->get_bone_pose_position(middle).length();
}

// Milestone 198 (decision 0340): the hand turned to hold a ledge (skeleton space: p_palm from the wrist to the knuckles,
// p_edge a point on the edge held, p_in into the wall, p_normal the top's, p_side_dir the hand's outer side along the
// edge) — its palm against the face under the edge, its fingers over the edge and on the top, the thumb along the palm
// by the index finger.
void FellowTraversalPose3D::_shape_grip(Skeleton3D *p_skeleton, int p_side, const Vector3 &p_palm, const Vector3 &p_edge, const Vector3 &p_in, const Vector3 &p_normal, const Vector3 &p_side_dir, float p_meter, float p_weight, bool p_letting_go, float p_hold) {
	const int hand = bones[p_side == 0 ? ROLE_LEFT_HAND : ROLE_RIGHT_HAND];
	const int index = fingers[p_side][1][0];
	const int middle = fingers[p_side][2][0];
	const int little = fingers[p_side][4][0];
	if (hand < 0 || index < 0 || middle < 0 || little < 0 || p_weight <= 0.0f) {
		return;
	}
	// The hand's own frame: along the palm to the middle finger's knuckle, across the knuckles from the index finger's to
	// the little finger's — turned so that the palm goes up the face and the little finger is on the outer side (which
	// faces the palm to the wall, whatever the rig's axes).
	// (the knuckles' row level along the edge: a hand's slopes back from the index finger's to the little finger's, which
	// had been 1.9 cm lower — under the top, its finger left standing up)
	const Basis own = frame_of(p_skeleton->get_bone_pose_position(little) - p_skeleton->get_bone_pose_position(index), p_skeleton->get_bone_pose_position(middle));
	const Basis want = frame_of(p_side_dir, p_palm);
	if (own == Basis() || want == Basis()) {
		return;
	}
	const Quaternion hand_rot(want * own.transposed());
	const Quaternion parent = global_rotation(p_skeleton, p_skeleton->get_bone_parent(hand));
	// Letting go, the wrist's last bend held against the forearm while it eases into the animation's (held in the world,
	// the wrist had turned as fast as the arm swung off the hold).
	if (!p_letting_go || !grip_local_valid[p_side]) {
		grip_local[p_side] = (parent.inverse() * hand_rot).normalized();
		grip_local_valid[p_side] = true;
	}
	p_skeleton->set_bone_pose_rotation(hand, p_skeleton->get_bone_pose_rotation(hand).slerp(grip_local[p_side], p_weight));
	// The fingers over the edge and on the top: each one's middle joint and tip aimed at their places by its knuckle —
	// as far as the knuckles are over the top: below it (the arm not yet up to the hold, the body still rising to it), the
	// fingers open, up the face and a little out from it (not curled into the stone). Milestone 200 (decision 0342): a
	// hand on its hold (p_hold) keeps them over the edge wherever its knuckles are (they had sprung open, both hands' at
	// once, wherever an arm fell short of its hold by a centimetre).
	Vector3 over = p_in - p_normal * p_in.dot(p_normal);
	Vector3 along = p_normal.cross(over);
	if (over.length() < 1e-4f || along.length() < 1e-4f) {
		return;
	}
	over.normalize();
	along.normalize();
	Vector3 open = p_normal.cross(along).cross(along); // (up the face)
	open = (open.dot(p_normal) < 0.0f ? -open : open) - over * 0.15f;
	open.normalize();
	// (by the middle finger's knuckle: the hand's)
	const float curl = MAX(p_hold, smooth01((p_skeleton->get_bone_global_pose(middle).origin - p_edge).dot(p_normal) / (GRIP_KNUCKLE_UP * p_meter)));
	for (int f = 1; f < 5; f++) {
		const int first = fingers[p_side][f][0];
		if (first < 0) {
			continue;
		}
		const Vector3 knuckle = p_skeleton->get_bone_global_pose(first).origin;
		const Vector3 base = p_edge + along * (knuckle - p_edge).dot(along);
		for (int j = 0; j < 2; j++) {
			const int b = fingers[p_side][f][j];
			if (b < 0) {
				break;
			}
			const Vector3 goal = base + (over * GRIP_FINGER_IN[j] + p_normal * GRIP_FINGER_UP[j]) * p_meter;
			const Vector3 aim = (goal - p_skeleton->get_bone_global_pose(b).origin).normalized();
			aim_bone(p_skeleton, b, open.slerp(aim, curl), p_weight);
		}
	}
	// The thumb along the palm by the index finger, its tip over the edge onto the top beside it (up the face while the
	// knuckles are under the top; it had stood up off the edge).
	aim_bone(p_skeleton, fingers[p_side][0][0], p_palm * 0.8f - p_side_dir * 0.55f - p_in * 0.2f, p_weight);
	const int thumb = fingers[p_side][0][1];
	if (thumb >= 0) {
		const Vector3 at = p_skeleton->get_bone_global_pose(thumb).origin;
		const Vector3 goal = p_edge + along * (at - p_edge).dot(along) + (over * GRIP_THUMB_IN + p_normal * GRIP_THUMB_UP) * p_meter;
		const float curl = MAX(p_hold, smooth01(((at - p_edge).dot(p_normal) + GRIP_KNUCKLE_UP * p_meter) / (GRIP_KNUCKLE_UP * p_meter)));
		aim_bone(p_skeleton, thumb, open.slerp((goal - at).normalized(), curl), p_weight);
	}
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
void FellowTraversalPose3D::_two_bone(Skeleton3D *p_skeleton, int p_upper, int p_lower, int p_end, const Vector3 &p_target, const Vector3 &p_pole, float p_weight, Vector3 *r_bend, float p_max_turn, float p_follow) {
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
	if (soft_ik > 0.0f) {
		// Milestone 155 (decision 0295): past soft_ik of the reach the target eased towards it (a limb never snaps straight:
		// a mantle's legs had been straight a quarter of the time).
		const float reach = a + b - 1e-3f;
		const float soft = reach * soft_ik;
		if (d > soft) {
			d = soft + (reach - soft) * (1.0f - Math::exp(-(d - soft) / (reach - soft)));
		}
	}
	d = CLAMP(d, Math::abs(a - b) + 1e-3f, a + b - 1e-3f);
	const float cos_a = CLAMP((a * a + d * d - b * b) / (2.0f * a * d), -1.0f, 1.0f);
	const float sin_a = Math::sqrt(MAX(0.0f, 1.0f - cos_a * cos_a));
	Vector3 bend = p_pole - s;
	bend -= dir * bend.dot(dir);
	const float follow = p_follow >= 0.0f ? p_follow : pole_follow;
	if (follow > 0.0f) {
		// Milestone 156 (decision 0296): the bend also follows the joint's own (the clip's): where the target passes the
		// pole's line its projection turns over, and the joint with it (an elbow 150 degrees in four frames, hanging).
		Vector3 own = (e - s) - dir * (e - s).dot(dir);
		const float bl = bend.length();
		const float ol = own.length();
		bend = (bl > 1e-5f ? bend / bl : Vector3()) + (ol > 1e-5f ? own / ol * follow : Vector3());
	}
	if (bend.length() < 1e-4f) {
		bend = (e - s) - dir * (e - s).dot(dir);
	}
	if (bend.length() < 1e-4f) {
		return;
	}
	bend.normalize();
	// Milestone 198 (decision 0340): the bend turned no faster than p_max_turn from the last (the elbow had turned over in
	// a frame where the pole's and the clip's bends crossed).
	if (r_bend) {
		Vector3 last_bend = *r_bend - dir * r_bend->dot(dir);
		if (p_max_turn > 0.0f && last_bend.length() > 1e-4f) {
			last_bend.normalize();
			const float turn = last_bend.angle_to(bend);
			if (turn > p_max_turn) {
				Vector3 axis = last_bend.cross(bend);
				axis = axis.length() > 1e-5f ? axis.normalized() : dir;
				bend = last_bend.rotated(axis, p_max_turn).normalized();
			}
		}
		*r_bend = bend;
	}
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
	Skeleton3D *skeleton = get_skeleton();
	if (skeleton && weight > 0.0f && max_joint_speed > 0.0f) {
		_limit_speed(skeleton, p_delta);
	} else {
		speed_valid = false;
	}
	// Milestone 156 (decision 0296): the pose a clip fades in from — this node's last before the clip began.
	if (skeleton && weight > 0.0f && clip_blend > 0.0f && !clip_on) {
		_snapshot(skeleton);
	}
	if (weight > 0.0f) {
		FellowTraversal3D::global_stats.poses.increment();
		FellowTraversal3D::global_stats.pose_usec.add(OS::get_singleton()->get_ticks_usec() - t0);
	}
}

// Milestone 153 (decision 0290): the hips on a damped spring behind p_point (world; where the traversal shows the body):
// driven by its acceleration (the hips keep body_sway of its speed when it changes), pulled back at sway_frequency,
// damped by sway_damping, never further than sway_max. Off (p_on false): at rest, still following the point.
void FellowTraversalPose3D::_sway_step(const Vector3 &p_point, double p_delta, bool p_on) {
	const float dt = float(p_delta);
	if (dt <= 0.0f) {
		return;
	}
	Vector3 accel;
	if (track_frames > 0 && p_point.distance_to(track_last) > 1.0f) {
		track_frames = 0; // a metre in a frame: put there, not moved
	}
	if (track_frames > 0) {
		const Vector3 speed = (p_point - track_last) / dt;
		if (track_frames > 1) {
			accel = (speed - track_speed) / dt;
		}
		track_speed = speed;
	}
	track_last = p_point;
	track_frames = MIN(track_frames + 1, 3);
	if (!p_on || body_sway <= 0.0f || sway_max <= 0.0f) {
		sway = Vector3();
		sway_rate = Vector3();
		return;
	}
	const float omega = float(Math::TAU) * sway_frequency;
	const int steps = MAX(1, int(Math::ceil(dt * 240.0f)));
	const float h = dt / steps;
	Vector3 drive = -accel * body_sway;
	if (sway_vertical < 1.0f) {
		// (milestone 203: the skeleton's up is the world's for a body hanging upright)
		drive.y *= sway_vertical;
	}
	for (int i = 0; i < steps; i++) {
		sway_rate += (drive - sway * (omega * omega) - sway_rate * (2.0f * sway_damping * omega)) * h;
		sway += sway_rate * h;
		const float len = sway.length();
		if (len > sway_max) {
			const Vector3 dir = sway / len;
			sway = dir * sway_max;
			const float out = sway_rate.dot(dir);
			if (out > 0.0f) {
				sway_rate -= dir * out;
			}
		}
	}
}

void FellowTraversalPose3D::_pose(double p_delta) {
	Skeleton3D *skeleton = get_skeleton();
	FellowTraversal3D *traversal = _get_traversal();
	if (!skeleton || !traversal) {
		weight = 0.0f;
		track_frames = 0;
		limbs_valid = false;
		return;
	}
	const FellowTraversal3D::PoseHints &now = traversal->get_pose_hints();
	if (now.active) {
		last = now;
	}
	const float goal = now.active ? 1.0f : 0.0f;
	weight = blend_time > 0.0f ? Math::move_toward(weight, goal, float(p_delta) / blend_time) : goal;
	// Milestone 198 (decision 0340): with ledge_grip, the hands on their holds twice as fast as the rest of the
	// pose comes in (a hold caught from the air: the hands had reached it a sixth of a second after the body).
	hand_fade = blend_time > 0.0f ? Math::move_toward(hand_fade, goal, float(p_delta) / (blend_time * 0.5f)) : goal;
	hips_drop = 0.0f;
	// Milestone 153: the spring follows the body even while it is off (the body's speed known when it comes on).
	// Milestone 154 (decision 0292): the skeleton where it is drawn — between two physics ticks with physics interpolation
	// on (the body moved at the ticks, the frame drawn between them: the hands would slide on their holds by the
	// difference); the same as its transform with it off.
	const Transform3D sk_xform = skeleton->get_global_transform_interpolated();
	// (Milestone 201, decision 0343: not while the hands reach for a hold from the air — the body flying is the
	// controller's, falling at g: no swing.)
	_sway_step(sk_xform.origin + (now.active ? now.body_offset : Vector3()), p_delta, now.active && !now.clip.is_valid() && !now.air_reach);
	if (weight <= 0.0f) {
		last.clip.unref();
		limbs_valid = false;
		// Milestone 198 (decision 0340): a hold caught when the pose comes in again has the hands on it (a reach between
		// holds left the grip's ease part way).
		hand_flight[0] = hand_flight[1] = 1.0f;
		fit = Vector3();
		clip_on = false;
		snap_valid = false;
		return;
	}
	_find_bones(skeleton);
	const FellowTraversal3D::PoseHints &h = last;
	if (h.clip.is_valid()) {
		limbs_valid = false;
		if (!clip_on) {
			clip_on = true;
			clip_fade = (clip_blend > 0.0f && snap_valid && int(snap_rot.size()) == skeleton->get_bone_count()) ? 0.0f : 1.0f;
		}
		_apply_clip(skeleton, h, weight);
		// Milestone 153: a clip from a hang (climbing up onto the ledge) starts where the fitted body was, the fit eased out.
		if (fit != Vector3()) {
			fit = fit.lerp(Vector3(), 1.0f - Math::exp(-float(p_delta) / 0.2f));
			const int hips_bone = bones[ROLE_HIPS];
			if (hips_bone >= 0) {
				const Transform3D to_sk = sk_xform.affine_inverse();
				const Vector3 moved = skeleton->get_bone_global_pose(hips_bone).origin + to_sk.basis.xform(fit) * weight;
				const int parent = skeleton->get_bone_parent(hips_bone);
				skeleton->set_bone_pose_position(hips_bone, parent >= 0 ? skeleton->get_bone_global_pose(parent).affine_inverse().xform(moved) : moved);
			}
		}
		// Milestone 156 (decision 0296): faded in from the limbs' last pose.
		if (clip_fade < 1.0f) {
			clip_fade = MIN(1.0f, clip_fade + float(p_delta) / clip_blend);
			const float e = clip_fade * clip_fade * (3.0f - 2.0f * clip_fade);
			for (int b = 0; b < int(snap_rot.size()); b++) {
				skeleton->set_bone_pose_rotation(b, snap_rot[b].slerp(skeleton->get_bone_pose_rotation(b), e).normalized());
				skeleton->set_bone_pose_position(b, snap_pos[b].lerp(skeleton->get_bone_pose_position(b), e));
			}
		}
		snap_valid = false;
		return;
	}
	clip_on = false;
	const Transform3D to_skeleton = sk_xform.affine_inverse();
	const Vector3 up = to_skeleton.basis.xform(Vector3(0, 1, 0)).normalized();
	const Vector3 inward = to_skeleton.basis.xform(h.inward).normalized();
	const Vector3 left = to_skeleton.basis.xform(h.left).normalized();
	if (lean && h.lean != 0.0f && left != Vector3()) {
		_lean(skeleton, left, h.lean * weight);
	}
	// Milestone 153 (decision 0290): the limbs' targets eased (limb_smoothing); the hips moved by the traversal's offset
	// and the spring's, the torso leaning back against the spring's.
	Vector3 limb_target[4] = { h.hand[0], h.hand[1], h.foot[0], h.foot[1] };
	float limb_w[4] = { h.hand_weight[0], h.hand_weight[1], h.foot_weight[0], h.foot_weight[1] };
	// Milestone 196 (decision 0338): between holds, a hand off its hold reached for by hand_flight_weight of the IK (the
	// clip's arm carries it), the grip eased in and out over a tenth of a second.
	if (hand_flight_weight < 1.0f) {
		const float k = 1.0f - Math::exp(-float(p_delta) / 0.1f);
		for (int s = 0; s < 2; s++) {
			const float grip = h.kind == FellowTraversal3D::KIND_HANG ? h.hand_grip[s] : 1.0f;
			hand_flight[s] = Math::lerp(hand_flight[s], grip, k);
			// Milestone 198 (decision 0340): homing in on the next hold over the second half of its way — on it as the
			// move catches it (the grip's ease alone had left it short of the hold for a fifth of a second, holding the
			// air).
			if (h.kind == FellowTraversal3D::KIND_HANG && h.hand_grip[s] <= 0.0f) {
				hand_flight[s] = MAX(hand_flight[s], smooth01((h.hand_reach[s] - 0.5f) / 0.5f));
			}
			limb_w[s] *= Math::lerp(hand_flight_weight, 1.0f, hand_flight[s]);
		}
	}
	if (limb_smoothing > 0.0f) {
		const float k = limbs_valid ? 1.0f - Math::exp(-float(p_delta) / limb_smoothing) : 1.0f;
		// Milestone 198 (decision 0340): with ledge_grip, hanging, the hands' targets eased over at most GRIP_HAND_SMOOTHING
		// (their ways between holds are smooth already: eased over limb_smoothing, a hand reached its hold some 3 cm short,
		// a twentieth of a second late; not at all, they stepped at the physics' rate where frames are drawn faster).
		const bool sharp_hands = ledge_grip > 0.0f && h.kind == FellowTraversal3D::KIND_HANG;
		const float k_hand = limbs_valid ? 1.0f - Math::exp(-float(p_delta) / MIN(limb_smoothing, GRIP_HAND_SMOOTHING)) : 1.0f;
		for (int i = 0; i < 4; i++) {
			limb_at[i] = limb_at[i].lerp(limb_target[i], (sharp_hands && i < 2) ? k_hand : k);
			limb_weight[i] = Math::lerp(limb_weight[i], limb_w[i], k);
			limb_target[i] = limb_at[i];
			limb_w[i] = limb_weight[i];
		}
		limbs_valid = true;
	}
	const float meter = to_skeleton.basis.xform(Vector3(0, 1, 0)).length(); // a meter in the skeleton's units
	const float hand_fade_now = (ledge_grip > 0.0f && h.kind == FellowTraversal3D::KIND_HANG) ? MAX(weight, hand_fade) : weight;
	const int hips = bones[ROLE_HIPS];
	const Vector3 shift_world = (h.body_offset + sway) * weight;
	float shift_down = 0.0f; // the skeleton's units the shift moved the hips down by
	if (hips >= 0 && shift_world != Vector3()) {
		const Vector3 shift = to_skeleton.basis.xform(shift_world);
		shift_down = MAX(0.0f, -shift.dot(up));
		const Vector3 moved = skeleton->get_bone_global_pose(hips).origin + shift;
		const int parent = skeleton->get_bone_parent(hips);
		skeleton->set_bone_pose_position(hips, parent >= 0 ? skeleton->get_bone_global_pose(parent).affine_inverse().xform(moved) : moved);
		if (lean && sway != Vector3()) {
			const Vector3 across = to_skeleton.basis.xform(Vector3(sway.x, 0.0f, sway.z)) * weight;
			const Vector3 tilted = (up * (0.55f * meter) - across).normalized();
			const Vector3 axis = up.cross(tilted);
			if (axis.length() > 1e-5f) {
				_lean(skeleton, axis.normalized(), up.angle_to(tilted));
			}
		}
	}
	// Milestone 196 (decision 0338): hanging from one ledge, the hands along its edge where the animation's are.
	// (Milestone 198: before the body is fitted to the hands' reach — a hand slid out of the arm's reach had fallen short
	// of the edge.)
	if (hand_slide > 0.0f && h.kind == FellowTraversal3D::KIND_HANG && h.slide > 0.0f && h.left != Vector3()) {
		const Vector3 edge_left = h.left.normalized();
		for (int s = 0; s < 2; s++) {
			const int hand = bones[s == 0 ? ROLE_LEFT_HAND : ROLE_RIGHT_HAND];
			if (hand >= 0) {
				const Vector3 animated = sk_xform.xform(skeleton->get_bone_global_pose(hand).origin);
				limb_target[s] += edge_left * ((animated - limb_target[s]).dot(edge_left) * hand_slide * h.slide);
			}
		}
	}
	// Milestone 198 (decision 0340): a hand on a ledge held as a climber's — the knuckles over the edge, the palm down
	// the face from them to the wrist (GRIP_PITCH below level, less where the stone under the edge stands out: the palm
	// over it), the hand turned to it after the arm's IK; turned to it over the last 65 % of its way to a hold, from it as
	// it lets go. The body is fitted to the wrist's place (below).
	float grip_w[2] = { 0.0f, 0.0f }; // the hand's shape (turned, the fingers over the edge)
	float grip_at[2] = { 0.0f, 0.0f }; // its place (the wrist by the knuckles over the edge: on a hold and between holds)
	Vector3 grip_hint[2]; // world: the hand's target before the grip
	Vector3 grip_edge[2]; // world: the edge held (a step's under it)
	Vector3 grip_knuckle[2]; // world: the middle finger's knuckle
	Vector3 grip_down[2]; // world: from the knuckles to the wrist
	if (ledge_grip > 0.0f && hand_ik) {
		const Vector3 world_up(0, 1, 0);
		for (int s = 0; s < 2; s++) {
			float shape = 0.0f;
			if (h.kind == FellowTraversal3D::KIND_HANG) {
				shape = h.hand_grip[s] > 0.0f ? 1.0f : smooth01((h.hand_reach[s] - 0.35f) / 0.65f);
			}
			// (let go of over twice as long: the hand unwinding into the animation's as the arm swings off)
			grip_letting_go[s] = shape < grip_shape[s];
			// (milestone 201, decision 0343: reaching for a hold from the air, in at once — the arm swung up from a clip's arm
			// hanging down, what the grip left of its hand was turned half round from it, the wrist flicking over as the
			// shortest way between them changed sides)
			const float ease = grip_letting_go[s] ? 2.0f * GRIP_EASE : (h.air_reach ? GRIP_EASE_AIR : GRIP_EASE);
			grip_shape[s] = Math::lerp(grip_shape[s], shape, 1.0f - Math::exp(-float(p_delta) / ease));
			// Milestone 200 (decision 0342): on its hold, the fingers over the edge (eased over GRIP_HOLD_EASE).
			const float holding = (h.kind == FellowTraversal3D::KIND_HANG && h.hand_grip[s] > 0.0f) ? 1.0f : 0.0f;
			grip_hold[s] = Math::lerp(grip_hold[s], holding, 1.0f - Math::exp(-float(p_delta) / GRIP_HOLD_EASE));
			const float palm = _palm_length(skeleton, s) / meter;
			if (h.kind != FellowTraversal3D::KIND_HANG || palm <= 0.0f || h.inward == Vector3()) {
				continue;
			}
			// The place by the knuckles all the way between holds too (the hand's way from one hold's grip to the next's,
			// out of the stone: blended from the hold's target to the grip's, it had cut through the edge's corner); what
			// depends on the hold under it (a step, the stone under the edge) with the shape (its hold changes mid-way).
			grip_at[s] = ledge_grip;
			grip_w[s] = grip_shape[s] * ledge_grip;
			const Vector3 in = h.inward.normalized();
			Vector3 edge = limb_target[s] - in * FellowTraversal3D::HOLD_IN - world_up * FellowTraversal3D::HOLD_UP;
			// (the hold's own edge: between holds the hand's target is on its way to it)
			Vector3 hold_edge = h.hold[s] - in * FellowTraversal3D::HOLD_IN - world_up * FellowTraversal3D::HOLD_UP;
			Vector2 lip = h.hold_lip[s];
			if (lip.x >= GRIP_STEP_OUT && lip.y <= GRIP_STEP) {
				const Vector3 step = (-in * lip.x - world_up * lip.y) * grip_shape[s];
				edge += step;
				hold_edge += step;
				lip = Vector2();
			}
			const Vector3 knuckle = edge - in * GRIP_KNUCKLE_OUT + h.hold_normal[s] * GRIP_KNUCKLE_UP;
			// The steepest palm (from GRIP_PITCH down to level, 5 degrees at a time) clear of the stone's outer top under
			// the edge (its palm-side within GRIP_PALM_CLEAR of it).
			float pitch = GRIP_PITCH;
			if (lip.x > 0.0f) {
				const Vector3 corner = edge - in * lip.x - world_up * lip.y;
				for (; pitch > 0.0f; pitch -= 5.0f) {
					const float a = Math::deg_to_rad(pitch);
					const Vector3 down = -(in * Math::cos(a) + world_up * Math::sin(a));
					const Vector3 palm_side = in * Math::sin(a) - world_up * Math::cos(a);
					const float t = (corner - knuckle).dot(down);
					if (t < 0.0f || t > palm || (corner - knuckle).dot(palm_side) >= GRIP_PALM_CLEAR) {
						break;
					}
				}
				pitch = Math::lerp(GRIP_PITCH, MAX(pitch, 0.0f), grip_shape[s]);
			}
			const float a = Math::deg_to_rad(pitch);
			grip_hint[s] = limb_target[s];
			grip_edge[s] = hold_edge;
			grip_knuckle[s] = knuckle;
			grip_down[s] = -(in * Math::cos(a) + world_up * Math::sin(a));
			limb_target[s] = limb_target[s].lerp(knuckle + grip_down[s] * palm, grip_at[s]);
		}
	} else {
		grip_shape[0] = grip_shape[1] = 0.0f;
	}
	for (int s = 0; s < 2; s++) {
		if (grip_at[s] <= 0.0f) {
			grip_dir_valid[s] = false;
		}
		if (grip_w[s] <= 0.0f) {
			grip_local_valid[s] = false;
		}
	}
	// Milestone 153 (decision 0291): hanging, the body moved (up to 0.75 m, eased over 0.1 s) until the shoulders are
	// within the arms' reach of the holds the hands grip — a hand that takes a hold pulls the body up to it.
	Vector3 fit_goal;
	if (hang_fit && hand_ik && hips >= 0 && h.kind == FellowTraversal3D::KIND_HANG) {
		Vector3 move;
		for (int iter = 0; iter < 4; iter++) {
			Vector3 need;
			float count = 0.0f;
			for (int s = 0; s < 2; s++) {
				const int upper = bones[s == 0 ? ROLE_LEFT_UPPER_ARM : ROLE_RIGHT_UPPER_ARM];
				const int lower = bones[s == 0 ? ROLE_LEFT_LOWER_ARM : ROLE_RIGHT_LOWER_ARM];
				const int hand = bones[s == 0 ? ROLE_LEFT_HAND : ROLE_RIGHT_HAND];
				const float grip = h.hand_grip[s] * MIN(1.0f, limb_w[s]);
				if (upper < 0 || lower < 0 || hand < 0 || grip <= 0.01f) {
					continue;
				}
				const Vector3 shoulder = skeleton->get_bone_global_pose(upper).origin + move;
				const Vector3 elbow = skeleton->get_bone_global_pose(lower).origin;
				const float reach = 0.97f * (skeleton->get_bone_global_pose(upper).origin.distance_to(elbow) + elbow.distance_to(skeleton->get_bone_global_pose(hand).origin));
				const Vector3 d = to_skeleton.xform(limb_target[s]) - shoulder;
				const float len = d.length();
				if (len > reach) {
					need += d / len * ((len - reach) * grip);
				}
				count += 1.0f;
			}
			if (need == Vector3()) {
				break;
			}
			move += need / MAX(count, 1.0f);
		}
		move = move.limit_length(0.75f * meter);
		fit_goal = sk_xform.basis.xform(move);
	}
	if (hang_fit) {
		// Milestone 200 (decision 0342): with ledge_grip, the goal's steady drift (no faster than GRIP_FIT_FOLLOW) followed
		// at once, its jumps (a hand taking a hold far above) eased over 0.1 s as before: eased alone, the fit had lagged 8
		// cm behind a body settling into the hang after a leap, the arms short of the holds.
		if (ledge_grip > 0.0f && fit_goal_valid && (fit_goal - fit_goal_last).length() <= GRIP_FIT_FOLLOW * float(p_delta)) {
			fit += fit_goal - fit_goal_last;
		}
		fit_goal_last = fit_goal;
		fit_goal_valid = true;
		const Vector3 fit_was = fit;
		fit = fit.lerp(fit_goal, 1.0f - Math::exp(-float(p_delta) / 0.1f));
		if (fit_max_speed > 0.0f && p_delta > 0.0) {
			// Milestone 203 (decision 0345): no faster than a person's arms move the body, its speed changing by no more
			// than FIT_MAX_ACCEL (a speed limit alone had started it at that speed in a frame).
			const float dt = float(p_delta);
			const Vector3 want = ((fit - fit_was) / dt).limit_length(fit_max_speed);
			fit_rate = fit_rate.move_toward(want, FIT_MAX_ACCEL * dt);
			fit = fit_was + fit_rate * dt;
		}
		if (hips >= 0 && fit != Vector3()) {
			const Vector3 moved = skeleton->get_bone_global_pose(hips).origin + to_skeleton.basis.xform(fit) * weight;
			const int parent = skeleton->get_bone_parent(hips);
			skeleton->set_bone_pose_position(hips, parent >= 0 ? skeleton->get_bone_global_pose(parent).affine_inverse().xform(moved) : moved);
		}
	}
	// The hips lowered (as far as the traversal allows) until the shoulders are within the arms' reach of the hands'
	// targets: hanging from the edge, pushing up from it.
	float drop = 0.0f;
	if (hand_ik && hips >= 0 && h.hips_drop > 0.0f) {
		float need = 0.0f;
		for (int s = 0; s < 2; s++) {
			const int upper = bones[s == 0 ? ROLE_LEFT_UPPER_ARM : ROLE_RIGHT_UPPER_ARM];
			const int lower = bones[s == 0 ? ROLE_LEFT_LOWER_ARM : ROLE_RIGHT_LOWER_ARM];
			const int hand = bones[s == 0 ? ROLE_LEFT_HAND : ROLE_RIGHT_HAND];
			if (upper < 0 || lower < 0 || hand < 0 || limb_w[s] <= 0.0f) {
				continue;
			}
			const Vector3 shoulder = skeleton->get_bone_global_pose(upper).origin;
			const float reach = 0.97f * (shoulder.distance_to(skeleton->get_bone_global_pose(lower).origin) + skeleton->get_bone_global_pose(lower).origin.distance_to(skeleton->get_bone_global_pose(hand).origin));
			const Vector3 d = shoulder - to_skeleton.xform(limb_target[s]);
			const float v = d.dot(up);
			const float across = (d - up * v).length();
			const float v_max = across < reach ? Math::sqrt(reach * reach - across * across) : 0.0f;
			need = MAX(need, MAX(0.0f, v - v_max) * limb_w[s]);
		}
		drop = MIN(need, h.hips_drop * meter) * weight;
		if (drop > 0.0f) {
			const Vector3 lowered = skeleton->get_bone_global_pose(hips).origin - up * drop;
			const int parent = skeleton->get_bone_parent(hips);
			skeleton->set_bone_pose_position(hips, parent >= 0 ? skeleton->get_bone_global_pose(parent).affine_inverse().xform(lowered) : lowered);
		}
	}
	hips_drop = drop / meter;
	// Milestone 153: the hips raised back (as far as the shift lowered them) where it took the shoulders out of the arms'
	// reach of the hands' targets above them — hanging, the body cannot swing lower than its arms let it.
	if (hand_ik && hips >= 0 && shift_down > 0.0f) {
		float raise = 0.0f;
		for (int s = 0; s < 2; s++) {
			const int upper = bones[s == 0 ? ROLE_LEFT_UPPER_ARM : ROLE_RIGHT_UPPER_ARM];
			const int lower = bones[s == 0 ? ROLE_LEFT_LOWER_ARM : ROLE_RIGHT_LOWER_ARM];
			const int hand = bones[s == 0 ? ROLE_LEFT_HAND : ROLE_RIGHT_HAND];
			if (upper < 0 || lower < 0 || hand < 0 || limb_w[s] <= 0.0f) {
				continue;
			}
			const Vector3 shoulder = skeleton->get_bone_global_pose(upper).origin;
			const float reach = 0.97f * (shoulder.distance_to(skeleton->get_bone_global_pose(lower).origin) + skeleton->get_bone_global_pose(lower).origin.distance_to(skeleton->get_bone_global_pose(hand).origin));
			const Vector3 d = to_skeleton.xform(limb_target[s]) - shoulder;
			const float v = d.dot(up);
			const float across = (d - up * v).length();
			const float v_max = across < reach ? Math::sqrt(reach * reach - across * across) : 0.0f;
			raise = MAX(raise, MAX(0.0f, v - v_max) * MIN(1.0f, limb_w[s]));
		}
		raise = MIN(raise, shift_down);
		if (raise > 0.0f) {
			const Vector3 raised = skeleton->get_bone_global_pose(hips).origin + up * raise;
			const int parent = skeleton->get_bone_parent(hips);
			skeleton->set_bone_pose_position(hips, parent >= 0 ? skeleton->get_bone_global_pose(parent).affine_inverse().xform(raised) : raised);
		}
	}
	// Milestone 204 (decision 0346): the hips kept at the height off the body they had as the hold began (a leap's push and
	// flight): the clip's own crouch and rise taken out, and the fit's — the body's arc is the traversal's alone.
	if (hips >= 0 && h.hips_hold_weight > 0.0f) {
		const Vector3 at = skeleton->get_bone_global_pose(hips).origin;
		const float rel = at.dot(up);
		if (!hips_hold_active) {
			hips_hold_active = true;
			hips_hold_rel = rel;
		}
		const float shift = (hips_hold_rel - rel) * h.hips_hold_weight * weight;
		if (shift != 0.0f) {
			const Vector3 held = at + up * shift;
			const int parent = skeleton->get_bone_parent(hips);
			skeleton->set_bone_pose_position(hips, parent >= 0 ? skeleton->get_bone_global_pose(parent).affine_inverse().xform(held) : held);
		}
	} else {
		hips_hold_active = false;
	}
	if (foot_ik) {
		for (int s = 0; s < 2; s++) {
			const int upper = bones[s == 0 ? ROLE_LEFT_UPPER_LEG : ROLE_RIGHT_UPPER_LEG];
			const int lower = bones[s == 0 ? ROLE_LEFT_LOWER_LEG : ROLE_RIGHT_LOWER_LEG];
			const int foot = bones[s == 0 ? ROLE_LEFT_FOOT : ROLE_RIGHT_FOOT];
			const float fw = MIN(1.0f, limb_w[2 + s]);
			if (upper < 0 || fw <= 0.0f) {
				continue;
			}
			// The knee towards the wall.
			const Vector3 hip = skeleton->get_bone_global_pose(upper).origin;
			const Vector3 side = left * (s == 0 ? 1.0f : -1.0f);
			const Quaternion foot_global = global_rotation(skeleton, foot);
			const Vector3 target = to_skeleton.xform(limb_target[2 + s]) - up * (drop * h.foot_follow[s]);
			// (milestone 203: or the move's own way, the clip's bend followed the less)
			const float pw = CLAMP(h.foot_pole_weight[s], 0.0f, 1.0f);
			const Vector3 pole_dir = (inward * 0.8f + up * 0.2f + side * 0.1f).lerp(to_skeleton.basis.xform(h.foot_pole[s]), pw);
			_two_bone(skeleton, upper, lower, foot, target, hip + pole_dir, fw * weight, nullptr, 0.0f, pole_follow * (1.0f - pw));
			// The foot keeps its turn in the world (flat on a top, along the wall).
			if (foot >= 0) {
				const Quaternion parent = global_rotation(skeleton, skeleton->get_bone_parent(foot));
				skeleton->set_bone_pose_rotation(foot, skeleton->get_bone_pose_rotation(foot).slerp((parent.inverse() * foot_global).normalized(), fw * weight));
			}
		}
	}
	// Milestone 201 (decision 0343): reaching for a hold from the air and settling from its catch, a foot the clip puts
	// into the stone (Mixamo's step up a wall) put on its face instead, the ankle FOOT_CLEAR off it and the knee
	// FOOT_KNEE_CLEAR.
	if (foot_ik && h.foot_clear) {
		for (int s = 0; s < 2; s++) {
			const int upper = bones[s == 0 ? ROLE_LEFT_UPPER_LEG : ROLE_RIGHT_UPPER_LEG];
			const int lower = bones[s == 0 ? ROLE_LEFT_LOWER_LEG : ROLE_RIGHT_LOWER_LEG];
			const int foot = bones[s == 0 ? ROLE_LEFT_FOOT : ROLE_RIGHT_FOOT];
			if (upper < 0 || lower < 0 || foot < 0 || h.wall_weight[s] <= 0.0f) {
				continue;
			}
			const Vector3 out = -inward;
			const Vector3 wall = to_skeleton.xform(h.wall[s]);
			const Vector3 ankle = skeleton->get_bone_global_pose(foot).origin;
			const Vector3 knee = skeleton->get_bone_global_pose(lower).origin;
			// (the knee off it too, the leg moved out: turned about its line to clear it, a step's knee had swung across
			// the body and back)
			const float need = MAX(FOOT_CLEAR * meter - (ankle - wall).dot(out), FOOT_KNEE_CLEAR * meter - (knee - wall).dot(out));
			if (need <= 0.0f) {
				continue;
			}
			// (the knee kept where the leg's own plane has it)
			const Quaternion foot_global = global_rotation(skeleton, foot);
			_two_bone(skeleton, upper, lower, foot, ankle + out * need, knee + (knee - (skeleton->get_bone_global_pose(upper).origin + ankle) * 0.5f), weight * h.wall_weight[s]);
			const Quaternion parent = global_rotation(skeleton, skeleton->get_bone_parent(foot));
			skeleton->set_bone_pose_rotation(foot, skeleton->get_bone_pose_rotation(foot).slerp((parent.inverse() * foot_global).normalized(), weight * h.wall_weight[s]));
		}
	}
	// Milestone 196 (decision 0338): hanging and between holds, the knees and the toes kept off the stone the feet brace on
	// — whatever the feet's weights (a leap's legs swing free of the IK, not through the wall).
	if (h.kind == FellowTraversal3D::KIND_HANG) {
		for (int s = 0; s < 2; s++) {
			const int upper = bones[s == 0 ? ROLE_LEFT_UPPER_LEG : ROLE_RIGHT_UPPER_LEG];
			const int lower = bones[s == 0 ? ROLE_LEFT_LOWER_LEG : ROLE_RIGHT_LOWER_LEG];
			const int foot = bones[s == 0 ? ROLE_LEFT_FOOT : ROLE_RIGHT_FOOT];
			if (upper >= 0 && lower >= 0 && foot >= 0 && h.wall_weight[s] > 0.0f) {
				_clear_wall(skeleton, upper, lower, foot, to_skeleton.xform(h.wall[s]), -inward, left * (s == 0 ? 1.0f : -1.0f), meter, weight * h.wall_weight[s]);
			}
			// (milestone 203: a foot standing on a top, its ball on it)
			if (upper >= 0 && lower >= 0 && foot >= 0 && h.sole_weight[s] > 0.0f) {
				_clear_wall(skeleton, upper, lower, foot, to_skeleton.xform(h.sole[s]), up, left * (s == 0 ? 1.0f : -1.0f), meter, weight * h.sole_weight[s]);
			}
		}
	}
	if (hand_ik) {
		for (int s = 0; s < 2; s++) {
			const int upper = bones[s == 0 ? ROLE_LEFT_UPPER_ARM : ROLE_RIGHT_UPPER_ARM];
			const int lower = bones[s == 0 ? ROLE_LEFT_LOWER_ARM : ROLE_RIGHT_LOWER_ARM];
			const int hand = bones[s == 0 ? ROLE_LEFT_HAND : ROLE_RIGHT_HAND];
			const float hw = MIN(1.0f, limb_w[s]);
			if (upper < 0 || hw <= 0.0f) {
				continue;
			}
			// The elbow out to the side, down and back.
			const Vector3 shoulder = skeleton->get_bone_global_pose(upper).origin;
			const Vector3 side = left * (s == 0 ? 1.0f : -1.0f);
			Vector3 target = to_skeleton.xform(limb_target[s]);
			// Milestone 198 (decision 0340): the grip's wrist — the knuckles held on the edge, the palm turned towards the
			// shoulder as far as the arm needs to reach it (a straighter wrist: an arm stretched by a move had let the
			// hand fall short of the edge, its fingers into the stone).
			Vector3 palm_up;
			if (grip_at[s] > 0.0f) {
				const float palm = _palm_length(skeleton, s);
				const Vector3 knuckle = to_skeleton.xform(grip_knuckle[s]);
				Vector3 down = to_skeleton.basis.xform(grip_down[s]).normalized();
				const int lower_bone = bones[s == 0 ? ROLE_LEFT_LOWER_ARM : ROLE_RIGHT_LOWER_ARM];
				if (lower_bone >= 0 && hand >= 0) {
					const Vector3 elbow = skeleton->get_bone_global_pose(lower_bone).origin;
					const float reach = 0.95f * (shoulder.distance_to(elbow) + elbow.distance_to(skeleton->get_bone_global_pose(hand).origin));
					const Vector3 to_shoulder = (shoulder - knuckle).normalized();
					if ((knuckle + down * palm).distance_to(shoulder) > reach && to_shoulder != Vector3()) {
						float lo = 0.0f;
						float hi = 1.0f;
						for (int it = 0; it < 8; it++) {
							const float mid = 0.5f * (lo + hi);
							if ((knuckle + down.slerp(to_shoulder, mid) * palm).distance_to(shoulder) > reach) {
								lo = mid;
							} else {
								hi = mid;
							}
						}
						down = down.slerp(to_shoulder, hi).normalized();
					}
				}
				// (eased: the body swinging across under a hand out of its arm's reach had twisted the wrist at 2,000 degrees a
				// second)
				const Vector3 down_world = sk_xform.basis.xform(down).normalized();
				grip_dir[s] = grip_dir_valid[s] ? grip_dir[s].slerp(down_world, 1.0f - Math::exp(-float(p_delta) / GRIP_TURN)).normalized() : down_world;
				grip_dir_valid[s] = true;
				down = to_skeleton.basis.xform(grip_dir[s]).normalized();
				target = to_skeleton.xform(grip_hint[s]).lerp(knuckle + down * palm, grip_at[s]);

				palm_up = -down;
			}
			// (milestone 198, with ledge_grip hanging: the elbow's bend turned at most GRIP_ELBOW_TURN a second)
			const bool steady = ledge_grip > 0.0f && h.kind == FellowTraversal3D::KIND_HANG;
			if (!steady) {
				arm_bend[s] = Vector3();
			}
			// (milestone 203: or the move's own way, the clip's bend followed the less)
			const float pw = CLAMP(h.hand_pole_weight[s], 0.0f, 1.0f);
			const Vector3 pole_dir = (side * 0.6f - up * 0.4f - inward * 0.3f).lerp(to_skeleton.basis.xform(h.hand_pole[s]), pw);
			_two_bone(skeleton, upper, lower, hand, target, shoulder + pole_dir, hw * hand_fade_now, steady ? &arm_bend[s] : nullptr, Math::deg_to_rad(GRIP_ELBOW_TURN) * float(p_delta), pole_follow * (1.0f - pw));
			if (grip_w[s] > 0.0f) {
				_shape_grip(skeleton, s, palm_up, to_skeleton.xform(grip_edge[s]), inward, to_skeleton.basis.xform(h.hold_normal[s]).normalized(), side, meter, grip_w[s] * hw * hand_fade_now, grip_letting_go[s], grip_hold[s]);
			}
		}
	}
}

// Milestone 196 (decision 0338): a braced leg kept off the stone its foot is on (p_wall: a point on it, p_out: out of it;
// skeleton space). The knee: where it is within KNEE_CLEAR of the stone or behind it, the leg turned about the line from
// the hip to the ankle (the foot stays) the least way that clears it, to the side first (a climber's knee turns out). The
// toes: where the ball of the foot is behind the stone, the foot pitched about the ankle until it is on it.
void FellowTraversalPose3D::_clear_wall(Skeleton3D *p_skeleton, int p_upper, int p_lower, int p_foot, const Vector3 &p_wall, const Vector3 &p_out, const Vector3 &p_side, float p_meter, float p_weight) {
	if (p_weight <= 0.0f) {
		return;
	}
	const float clear = 0.04f * p_meter;
	const Vector3 hip = p_skeleton->get_bone_global_pose(p_upper).origin;
	const Vector3 knee = p_skeleton->get_bone_global_pose(p_lower).origin;
	const Vector3 ankle = p_skeleton->get_bone_global_pose(p_foot).origin;
	const Quaternion foot_global = global_rotation(p_skeleton, p_foot);
	const Vector3 axis = ankle - hip;
	if ((knee - p_wall).dot(p_out) < clear && axis.length() > 1e-4f) {
		const Vector3 a = axis.normalized();
		const Vector3 centre = hip + a * (knee - hip).dot(a);
		const Vector3 arm = knee - centre;
		// The turn that clears it, the side's way first: the knee's offset from the leg's line turned by t.
		const float out_way = (a.cross(arm)).dot(p_side) >= 0.0f ? 1.0f : -1.0f;
		float best = 0.0f;
		float best_d = (knee - p_wall).dot(p_out);
		for (int k = 1; k <= 24 && best_d < clear; k++) {
			for (int sgn = 0; sgn < 2; sgn++) {
				const float t = Math::deg_to_rad(5.0f * k) * (sgn == 0 ? out_way : -out_way);
				const float d = (centre + Quaternion(a, t).xform(arm) - p_wall).dot(p_out);
				if (d > best_d + 1e-5f) {
					best_d = d;
					best = t;
					if (d >= clear) {
						break;
					}
				}
			}
		}
		if (best != 0.0f) {
			const Quaternion turn(a, best * p_weight);
			const int parent = p_skeleton->get_bone_parent(p_upper);
			const Quaternion parent_rot = global_rotation(p_skeleton, parent);
			const Quaternion upper_global = global_rotation(p_skeleton, p_upper);
			p_skeleton->set_bone_pose_rotation(p_upper, (parent_rot.inverse() * (turn * upper_global)).normalized());
			// (the foot's turn in the world kept)
			const Quaternion foot_parent = global_rotation(p_skeleton, p_skeleton->get_bone_parent(p_foot));
			p_skeleton->set_bone_pose_rotation(p_foot, (foot_parent.inverse() * foot_global).normalized());
		}
	}
	// The toes: the ball of the foot (the foot's first child) onto the stone's face.
	const Vector<int> children = p_skeleton->get_bone_children(p_foot);
	if (children.is_empty()) {
		return;
	}
	const Vector3 ankle_now = p_skeleton->get_bone_global_pose(p_foot).origin;
	const Vector3 ball = p_skeleton->get_bone_global_pose(children[0]).origin;
	const Vector3 v = ball - ankle_now;
	const float len = v.length();
	const float behind = (ball - p_wall).dot(p_out);
	if (len < 1e-4f || behind >= 0.0f) {
		return;
	}
	const float a0 = Math::asin(CLAMP(v.dot(p_out) / len, -1.0f, 1.0f));
	const float a1 = Math::asin(CLAMP(((p_wall - ankle_now).dot(p_out)) / len, -1.0f, 1.0f));
	const Vector3 pitch_axis = v.cross(p_out);
	if (pitch_axis.length() < 1e-5f || a1 <= a0) {
		return;
	}
	// (turning v about v × out raises its part along out)
	const Quaternion pitch(pitch_axis.normalized(), (a1 - a0) * p_weight);
	const Quaternion foot_parent = global_rotation(p_skeleton, p_skeleton->get_bone_parent(p_foot));
	p_skeleton->set_bone_pose_rotation(p_foot, (foot_parent.inverse() * (pitch * global_rotation(p_skeleton, p_foot))).normalized());
}

// Milestone 156 (decision 0296): no joint turned faster than max_joint_speed from this node's last pose — where the
// traversal's hints, a clip's start or an IK solution turns a joint over in a frame (a forearm 53 degrees about its
// own axis in one frame of a leap), the turn is spread over the frames it needs.
void FellowTraversalPose3D::_limit_speed(Skeleton3D *p_skeleton, double p_delta) {
	const int n = p_skeleton->get_bone_count();
	const float step = Math::deg_to_rad(max_joint_speed) * float(p_delta);
	if (!speed_valid || int(speed_rot.size()) != n) {
		speed_rot.resize(n);
		for (int b = 0; b < n; b++) {
			speed_rot[b] = p_skeleton->get_bone_pose_rotation(b);
		}
		speed_valid = true;
		return;
	}
	for (int b = 0; b < n; b++) {
		Quaternion q = p_skeleton->get_bone_pose_rotation(b);
		const Quaternion &was = speed_rot[b];
		const float angle = was.angle_to(q);
		if (angle > step && angle > 1e-5f) {
			q = was.slerp(q, step / angle).normalized();
			p_skeleton->set_bone_pose_rotation(b, q);
		}
		speed_rot[b] = q;
	}
}

// The pose this frame (every bone's), for a clip to fade in from.
void FellowTraversalPose3D::_snapshot(Skeleton3D *p_skeleton) {
	const int n = p_skeleton->get_bone_count();
	snap_rot.resize(n);
	snap_pos.resize(n);
	for (int b = 0; b < n; b++) {
		snap_rot[b] = p_skeleton->get_bone_pose_rotation(b);
		snap_pos[b] = p_skeleton->get_bone_pose_position(b);
	}
	snap_valid = true;
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
		sway = Vector3();
		sway_rate = Vector3();
		track_frames = 0;
		limbs_valid = false;
		fit = Vector3();
		clip_on = false;
		snap_valid = false;
		speed_valid = false;
	}
}

void FellowTraversalPose3D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_traversal_path", "path"), &FellowTraversalPose3D::set_traversal_path);
	ClassDB::bind_method(D_METHOD("get_traversal_path"), &FellowTraversalPose3D::get_traversal_path);
	ClassDB::bind_method(D_METHOD("set_blend_time", "seconds"), &FellowTraversalPose3D::set_blend_time);
	ClassDB::bind_method(D_METHOD("get_blend_time"), &FellowTraversalPose3D::get_blend_time);
	ClassDB::bind_method(D_METHOD("set_hand_slide", "amount"), &FellowTraversalPose3D::set_hand_slide);
	ClassDB::bind_method(D_METHOD("get_hand_slide"), &FellowTraversalPose3D::get_hand_slide);
	ClassDB::bind_method(D_METHOD("set_ledge_grip", "amount"), &FellowTraversalPose3D::set_ledge_grip);
	ClassDB::bind_method(D_METHOD("get_ledge_grip"), &FellowTraversalPose3D::get_ledge_grip);
	ClassDB::bind_method(D_METHOD("set_hand_flight_weight", "weight"), &FellowTraversalPose3D::set_hand_flight_weight);
	ClassDB::bind_method(D_METHOD("get_hand_flight_weight"), &FellowTraversalPose3D::get_hand_flight_weight);
	ClassDB::bind_method(D_METHOD("set_max_joint_speed", "degrees_per_second"), &FellowTraversalPose3D::set_max_joint_speed);
	ClassDB::bind_method(D_METHOD("get_max_joint_speed"), &FellowTraversalPose3D::get_max_joint_speed);
	ClassDB::bind_method(D_METHOD("set_pole_follow", "amount"), &FellowTraversalPose3D::set_pole_follow);
	ClassDB::bind_method(D_METHOD("get_pole_follow"), &FellowTraversalPose3D::get_pole_follow);
	ClassDB::bind_method(D_METHOD("set_clip_blend", "seconds"), &FellowTraversalPose3D::set_clip_blend);
	ClassDB::bind_method(D_METHOD("get_clip_blend"), &FellowTraversalPose3D::get_clip_blend);
	ClassDB::bind_method(D_METHOD("set_hand_ik", "enabled"), &FellowTraversalPose3D::set_hand_ik);
	ClassDB::bind_method(D_METHOD("is_hand_ik"), &FellowTraversalPose3D::is_hand_ik);
	ClassDB::bind_method(D_METHOD("set_foot_ik", "enabled"), &FellowTraversalPose3D::set_foot_ik);
	ClassDB::bind_method(D_METHOD("is_foot_ik"), &FellowTraversalPose3D::is_foot_ik);
	ClassDB::bind_method(D_METHOD("set_lean", "enabled"), &FellowTraversalPose3D::set_lean);
	ClassDB::bind_method(D_METHOD("is_lean"), &FellowTraversalPose3D::is_lean);
	ClassDB::bind_method(D_METHOD("set_bone_names", "names"), &FellowTraversalPose3D::set_bone_names);
	ClassDB::bind_method(D_METHOD("get_bone_names"), &FellowTraversalPose3D::get_bone_names);
	ClassDB::bind_method(D_METHOD("set_body_sway", "amount"), &FellowTraversalPose3D::set_body_sway);
	ClassDB::bind_method(D_METHOD("get_body_sway"), &FellowTraversalPose3D::get_body_sway);
	ClassDB::bind_method(D_METHOD("set_sway_frequency", "hz"), &FellowTraversalPose3D::set_sway_frequency);
	ClassDB::bind_method(D_METHOD("get_sway_frequency"), &FellowTraversalPose3D::get_sway_frequency);
	ClassDB::bind_method(D_METHOD("set_sway_damping", "ratio"), &FellowTraversalPose3D::set_sway_damping);
	ClassDB::bind_method(D_METHOD("get_sway_damping"), &FellowTraversalPose3D::get_sway_damping);
	ClassDB::bind_method(D_METHOD("set_sway_max", "meters"), &FellowTraversalPose3D::set_sway_max);
	ClassDB::bind_method(D_METHOD("get_sway_max"), &FellowTraversalPose3D::get_sway_max);
	ClassDB::bind_method(D_METHOD("set_limb_smoothing", "seconds"), &FellowTraversalPose3D::set_limb_smoothing);
	ClassDB::bind_method(D_METHOD("get_limb_smoothing"), &FellowTraversalPose3D::get_limb_smoothing);
	ClassDB::bind_method(D_METHOD("get_sway"), &FellowTraversalPose3D::get_sway);
	ClassDB::bind_method(D_METHOD("set_hang_fit", "enabled"), &FellowTraversalPose3D::set_hang_fit);
	ClassDB::bind_method(D_METHOD("set_sway_vertical", "share"), &FellowTraversalPose3D::set_sway_vertical);
	ClassDB::bind_method(D_METHOD("get_sway_vertical"), &FellowTraversalPose3D::get_sway_vertical);
	ClassDB::bind_method(D_METHOD("set_fit_max_speed", "speed"), &FellowTraversalPose3D::set_fit_max_speed);
	ClassDB::bind_method(D_METHOD("get_fit_max_speed"), &FellowTraversalPose3D::get_fit_max_speed);
	ClassDB::bind_method(D_METHOD("is_hang_fit"), &FellowTraversalPose3D::is_hang_fit);
	ClassDB::bind_method(D_METHOD("get_hang_fit_offset"), &FellowTraversalPose3D::get_hang_fit_offset);
	ClassDB::bind_method(D_METHOD("set_soft_ik", "share"), &FellowTraversalPose3D::set_soft_ik);
	ClassDB::bind_method(D_METHOD("get_soft_ik"), &FellowTraversalPose3D::get_soft_ik);
	ClassDB::bind_method(D_METHOD("get_role_bone", "role"), &FellowTraversalPose3D::get_role_bone);
	ClassDB::bind_method(D_METHOD("get_hips_drop"), &FellowTraversalPose3D::get_hips_drop);
	ClassDB::bind_method(D_METHOD("get_weight"), &FellowTraversalPose3D::get_weight);

	ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "traversal_path", PROPERTY_HINT_NODE_PATH_VALID_TYPES, "FellowTraversal3D"), "set_traversal_path", "get_traversal_path");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "blend_time", PROPERTY_HINT_RANGE, "0,1,0.01,or_greater,suffix:s"), "set_blend_time", "get_blend_time");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "hand_ik"), "set_hand_ik", "is_hand_ik");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "foot_ik"), "set_foot_ik", "is_foot_ik");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "lean"), "set_lean", "is_lean");
	ADD_PROPERTY(PropertyInfo(Variant::DICTIONARY, "bone_names", PROPERTY_HINT_DICTIONARY_TYPE, "String;StringName"), "set_bone_names", "get_bone_names");
	ADD_GROUP("Dynamics", "");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "body_sway", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_body_sway", "get_body_sway");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "sway_frequency", PROPERTY_HINT_RANGE, "0.2,8,0.01,suffix:Hz"), "set_sway_frequency", "get_sway_frequency");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "sway_damping", PROPERTY_HINT_RANGE, "0,2,0.01"), "set_sway_damping", "get_sway_damping");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "sway_max", PROPERTY_HINT_RANGE, "0,0.5,0.01,suffix:m"), "set_sway_max", "get_sway_max");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "sway_vertical", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_sway_vertical", "get_sway_vertical");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "limb_smoothing", PROPERTY_HINT_RANGE, "0,0.5,0.005,suffix:s"), "set_limb_smoothing", "get_limb_smoothing");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "hang_fit"), "set_hang_fit", "is_hang_fit");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "fit_max_speed", PROPERTY_HINT_RANGE, "0,5,0.05,or_greater,suffix:m/s"), "set_fit_max_speed", "get_fit_max_speed");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "soft_ik", PROPERTY_HINT_RANGE, "0,0.999,0.001"), "set_soft_ik", "get_soft_ik");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "clip_blend", PROPERTY_HINT_RANGE, "0,1,0.01,suffix:s"), "set_clip_blend", "get_clip_blend");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "pole_follow", PROPERTY_HINT_RANGE, "0,4,0.01"), "set_pole_follow", "get_pole_follow");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "max_joint_speed", PROPERTY_HINT_RANGE, "0,5000,1,suffix:°/s"), "set_max_joint_speed", "get_max_joint_speed");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "hand_flight_weight", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_hand_flight_weight", "get_hand_flight_weight");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "hand_slide", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_hand_slide", "get_hand_slide");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "ledge_grip", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_ledge_grip", "get_ledge_grip");

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
