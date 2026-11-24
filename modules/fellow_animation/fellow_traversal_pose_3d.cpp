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
	if (pole_follow > 0.0f) {
		// Milestone 156 (decision 0296): the bend also follows the joint's own (the clip's): where the target passes the
		// pole's line its projection turns over, and the joint with it (an elbow 150 degrees in four frames, hanging).
		Vector3 own = (e - s) - dir * (e - s).dot(dir);
		const float bl = bend.length();
		const float ol = own.length();
		bend = (bl > 1e-5f ? bend / bl : Vector3()) + (ol > 1e-5f ? own / ol * pole_follow : Vector3());
	}
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
	const Vector3 drive = -accel * body_sway;
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
	hips_drop = 0.0f;
	// Milestone 153: the spring follows the body even while it is off (the body's speed known when it comes on).
	// Milestone 154 (decision 0292): the skeleton where it is drawn — between two physics ticks with physics interpolation
	// on (the body moved at the ticks, the frame drawn between them: the hands would slide on their holds by the
	// difference); the same as its transform with it off.
	const Transform3D sk_xform = skeleton->get_global_transform_interpolated();
	_sway_step(sk_xform.origin + (now.active ? now.body_offset : Vector3()), p_delta, now.active && !now.clip.is_valid());
	if (weight <= 0.0f) {
		last.clip.unref();
		limbs_valid = false;
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
			limb_w[s] *= Math::lerp(hand_flight_weight, 1.0f, hand_flight[s]);
		}
	}
	if (limb_smoothing > 0.0f) {
		const float k = limbs_valid ? 1.0f - Math::exp(-float(p_delta) / limb_smoothing) : 1.0f;
		for (int i = 0; i < 4; i++) {
			limb_at[i] = limb_at[i].lerp(limb_target[i], k);
			limb_weight[i] = Math::lerp(limb_weight[i], limb_w[i], k);
			limb_target[i] = limb_at[i];
			limb_w[i] = limb_weight[i];
		}
		limbs_valid = true;
	}
	const float meter = to_skeleton.basis.xform(Vector3(0, 1, 0)).length(); // a meter in the skeleton's units
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
		fit = fit.lerp(fit_goal, 1.0f - Math::exp(-float(p_delta) / 0.1f));
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
			_two_bone(skeleton, upper, lower, foot, target, hip + (inward * 0.8f + up * 0.2f + side * 0.1f), fw * weight);
			// The foot keeps its turn in the world (flat on a top, along the wall).
			if (foot >= 0) {
				const Quaternion parent = global_rotation(skeleton, skeleton->get_bone_parent(foot));
				skeleton->set_bone_pose_rotation(foot, skeleton->get_bone_pose_rotation(foot).slerp((parent.inverse() * foot_global).normalized(), fw * weight));
			}
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
			// Milestone 196 (decision 0338): hanging from one ledge, along its edge where the animation's hand is.
			if (hand_slide > 0.0f && h.kind == FellowTraversal3D::KIND_HANG && h.slide > 0.0f && hand >= 0 && left != Vector3()) {
				const Vector3 animated = skeleton->get_bone_global_pose(hand).origin;
				target += left * ((animated - target).dot(left) * hand_slide * h.slide);
			}
			_two_bone(skeleton, upper, lower, hand, target, shoulder + (side * 0.6f - up * 0.4f - inward * 0.3f), hw * weight);
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
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "limb_smoothing", PROPERTY_HINT_RANGE, "0,0.5,0.005,suffix:s"), "set_limb_smoothing", "get_limb_smoothing");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "hang_fit"), "set_hang_fit", "is_hang_fit");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "soft_ik", PROPERTY_HINT_RANGE, "0,0.999,0.001"), "set_soft_ik", "get_soft_ik");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "clip_blend", PROPERTY_HINT_RANGE, "0,1,0.01,suffix:s"), "set_clip_blend", "get_clip_blend");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "pole_follow", PROPERTY_HINT_RANGE, "0,4,0.01"), "set_pole_follow", "get_pole_follow");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "max_joint_speed", PROPERTY_HINT_RANGE, "0,5000,1,suffix:°/s"), "set_max_joint_speed", "get_max_joint_speed");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "hand_flight_weight", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_hand_flight_weight", "get_hand_flight_weight");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "hand_slide", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_hand_slide", "get_hand_slide");

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
