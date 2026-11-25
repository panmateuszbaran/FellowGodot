/**************************************************************************/
/*  fellow_joint_limits_3d.cpp                                            */
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

#include "fellow_joint_limits_3d.h"

#include "core/object/class_db.h"
#include "core/os/os.h"
#include "scene/3d/skeleton_3d.h"

namespace {

const char *const ROLE_NAMES[FellowJointLimits3D::ROLE_MAX] = {
	"hips", "chest", "neck", "head",
	"left_upper_arm", "left_lower_arm", "left_hand", "right_upper_arm", "right_lower_arm", "right_hand",
	"left_upper_leg", "left_lower_leg", "left_foot", "left_toes", "right_upper_leg", "right_lower_leg", "right_foot", "right_toes"
};

const char *const ROLE_CANDIDATES[FellowJointLimits3D::ROLE_MAX][8] = {
	{ "hips", "pelvis", "hip", nullptr },
	{ "upperchest", "chest", "spine2", "spine03", "spine02", "spine1", "spine002", nullptr },
	{ "neck", "neck01", "neck1", nullptr },
	{ "head", nullptr },
	{ "leftupperarm", "leftarm", "upperarml", "lupperarm", "arml", "upperarmleft", nullptr },
	{ "leftlowerarm", "leftforearm", "lowerarml", "forearml", "lforearm", "lowerarmleft", nullptr },
	{ "lefthand", "handl", "lhand", "handleft", nullptr },
	{ "rightupperarm", "rightarm", "upperarmr", "rupperarm", "armr", "upperarmright", nullptr },
	{ "rightlowerarm", "rightforearm", "lowerarmr", "forearmr", "rforearm", "lowerarmright", nullptr },
	{ "righthand", "handr", "rhand", "handright", nullptr },
	{ "leftupperleg", "leftupleg", "thighl", "upperlegl", "lthigh", "thighleft", nullptr },
	{ "leftlowerleg", "leftleg", "calfl", "shinl", "lowerlegl", "lcalf", "calfleft", nullptr },
	{ "leftfoot", "footl", "lfoot", "footleft", nullptr },
	{ "lefttoes", "lefttoebase", "toel", "toesl", "balll", "ltoe", "toeleft", nullptr },
	{ "rightupperleg", "rightupleg", "thighr", "upperlegr", "rthigh", "thighright", nullptr },
	{ "rightlowerleg", "rightleg", "calfr", "shinr", "lowerlegr", "rcalf", "calfright", nullptr },
	{ "rightfoot", "footr", "rfoot", "footright", nullptr },
	{ "righttoes", "righttoebase", "toer", "toesr", "ballr", "rtoe", "toeright", nullptr },
};

String limits_bone_name(const String &p_name) {
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

float smooth01(float p_x) {
	const float x = CLAMP(p_x, 0.0f, 1.0f);
	return x * x * (3.0f - 2.0f * x);
}

Vector3 square_to(const Vector3 &p_v, const Vector3 &p_axis) {
	return p_v - p_axis * p_axis.dot(p_v);
}

} // namespace

FellowJointLimits3D::FellowJointLimits3D() {
	for (int r = 0; r < ROLE_MAX; r++) {
		bones[r] = -1;
	}
	for (int s = 0; s < 2; s++) {
		for (int f = 0; f < 3; f++) {
			fingers[s][f] = -1;
		}
	}
}

void FellowJointLimits3D::set_bone_names(const Dictionary &p_names) {
	bone_names = p_names;
	bones_dirty = true;
}

void FellowJointLimits3D::_find_bones(Skeleton3D *p_skeleton) {
	if (!bones_dirty && bones_skeleton == p_skeleton->get_instance_id() && bones_count == p_skeleton->get_bone_count()) {
		return;
	}
	HashMap<String, int> by_name;
	for (int b = 0; b < p_skeleton->get_bone_count(); b++) {
		const String n = limits_bone_name(p_skeleton->get_bone_name(b));
		if (!by_name.has(n)) {
			by_name.insert(n, b);
		}
	}
	for (int r = 0; r < ROLE_MAX; r++) {
		bones[r] = -1;
		const Variant given = bone_names.get(ROLE_NAMES[r], Variant());
		if (given.get_type() == Variant::STRING || given.get_type() == Variant::STRING_NAME) {
			bones[r] = p_skeleton->find_bone(given);
			continue;
		}
		for (int c = 0; c < 8 && ROLE_CANDIDATES[r][c]; c++) {
			const HashMap<String, int>::ConstIterator it = by_name.find(ROLE_CANDIDATES[r][c]);
			if (it) {
				bones[r] = it->value;
				break;
			}
		}
	}
	// The hands' index, middle and little fingers (their first bones: the hand's children named for them).
	static const char *const names[3][2] = { { "index", nullptr }, { "middle", nullptr }, { "little", "pinky" } };
	for (int s = 0; s < 2; s++) {
		for (int f = 0; f < 3; f++) {
			fingers[s][f] = -1;
		}
		const int hand = bones[s == 0 ? ROLE_LEFT_HAND : ROLE_RIGHT_HAND];
		if (hand < 0) {
			continue;
		}
		for (const int c : p_skeleton->get_bone_children(hand)) {
			const String n = limits_bone_name(p_skeleton->get_bone_name(c));
			for (int f = 0; f < 3; f++) {
				if (fingers[s][f] < 0 && (n.contains(names[f][0]) || (names[f][1] && n.contains(names[f][1])))) {
					fingers[s][f] = c;
					break;
				}
			}
		}
	}
	bones_skeleton = p_skeleton->get_instance_id();
	bones_count = p_skeleton->get_bone_count();
	bones_dirty = false;
	rest_valid = false;
}

// The rest pose: the body's forward the toes' way from the ankles (+Z where there are none), up +Y.
void FellowJointLimits3D::_read_rest(Skeleton3D *p_skeleton) {
	auto rest = [&](int p_bone) { return p_skeleton->get_bone_global_rest(p_bone); };
	const Vector3 up(0, 1, 0);
	Vector3 forward;
	for (int s = 0; s < 2; s++) {
		const int foot = bones[s == 0 ? ROLE_LEFT_FOOT : ROLE_RIGHT_FOOT];
		const int toes = bones[s == 0 ? ROLE_LEFT_TOES : ROLE_RIGHT_TOES];
		if (foot >= 0 && toes >= 0) {
			forward += square_to(rest(toes).origin - rest(foot).origin, up);
		}
	}
	forward = forward.length() > 1e-5f ? forward.normalized() : Vector3(0, 0, 1);
	const Vector3 left = up.cross(forward).normalized();
	// The hinges: the elbows bend forwards, the knees back (in a T or an A).
	static const Role hinge[4][2] = { { ROLE_LEFT_UPPER_ARM, ROLE_LEFT_LOWER_ARM }, { ROLE_RIGHT_UPPER_ARM, ROLE_RIGHT_LOWER_ARM }, { ROLE_LEFT_UPPER_LEG, ROLE_LEFT_LOWER_LEG }, { ROLE_RIGHT_UPPER_LEG, ROLE_RIGHT_LOWER_LEG } };
	for (int i = 0; i < 4; i++) {
		flex_local[i] = Vector3();
		axis_local[i] = Vector3();
		const int upper = bones[hinge[i][0]];
		const int lower = bones[hinge[i][1]];
		if (upper < 0 || lower < 0) {
			continue;
		}
		const Basis ub = rest(upper).basis.orthonormalized();
		const Vector3 axis = (rest(lower).origin - rest(upper).origin).normalized();
		axis_local[i] = ub.xform_inv(axis).normalized();
		const Vector3 flex = square_to(i < 2 ? forward : -forward, axis);
		flex_local[i] = flex.length() > 1e-5f ? ub.xform_inv(flex.normalized()).normalized() : Vector3();
	}
	// The hands: their frame against the forearm's, their axis (to the middle finger's knuckle), the thumb's side (from
	// the little finger's knuckle to the index finger's) and the back (the thumbs forward: the left's thumb-side × axis).
	for (int s = 0; s < 2; s++) {
		const int lower = bones[s == 0 ? ROLE_LEFT_LOWER_ARM : ROLE_RIGHT_LOWER_ARM];
		const int hand = bones[s == 0 ? ROLE_LEFT_HAND : ROLE_RIGHT_HAND];
		hand_axis[s] = Vector3();
		if (lower < 0 || hand < 0) {
			continue;
		}
		const Basis hb = rest(hand).basis.orthonormalized();
		hand_rest[s] = rest(lower).basis.orthonormalized().inverse() * hb;
		Vector3 axis = fingers[s][1] >= 0 ? rest(fingers[s][1]).origin - rest(hand).origin : rest(hand).origin - rest(lower).origin;
		axis.normalize();
		Vector3 thumb = (fingers[s][0] >= 0 && fingers[s][2] >= 0) ? rest(fingers[s][0]).origin - rest(fingers[s][2]).origin : forward;
		thumb = square_to(thumb, axis);
		if (thumb.length() < 1e-5f) {
			continue;
		}
		thumb.normalize();
		const Vector3 back = thumb.cross(axis).normalized() * (s == 0 ? 1.0f : -1.0f);
		hand_axis[s] = hb.xform_inv(axis).normalized();
		hand_thumb[s] = hb.xform_inv(thumb).normalized();
		hand_back[s] = hb.xform_inv(back).normalized();
	}
	for (int s = 0; s < 2; s++) {
		const int lower = bones[s == 0 ? ROLE_LEFT_LOWER_LEG : ROLE_RIGHT_LOWER_LEG];
		const int foot = bones[s == 0 ? ROLE_LEFT_FOOT : ROLE_RIGHT_FOOT];
		const int toes = bones[s == 0 ? ROLE_LEFT_TOES : ROLE_RIGHT_TOES];
		if (lower >= 0 && foot >= 0 && toes >= 0) {
			ankle_rest[s] = Math::rad_to_deg((rest(foot).origin - rest(lower).origin).angle_to(rest(toes).origin - rest(foot).origin));
		}
	}
	const int chest = bones[ROLE_CHEST];
	const int head = bones[ROLE_HEAD];
	if (chest >= 0 && head >= 0) {
		const Basis cb = rest(chest).basis.orthonormalized();
		const Basis hb = rest(head).basis.orthonormalized();
		chest_up = cb.xform_inv(up);
		chest_forward = cb.xform_inv(forward);
		chest_left = cb.xform_inv(left);
		head_up = hb.xform_inv(up);
		head_forward = hb.xform_inv(forward);
	}
	rest_valid = true;
}

void FellowJointLimits3D::_set_global_rotation(Skeleton3D *p_skeleton, int p_bone, const Basis &p_global) {
	const int parent = p_skeleton->get_bone_parent(p_bone);
	const Basis parent_global = parent >= 0 ? p_skeleton->get_bone_global_pose(parent).basis.orthonormalized() : Basis();
	p_skeleton->set_bone_pose_rotation(p_bone, (parent_global.inverse() * p_global.orthonormalized()).get_rotation_quaternion());
}

// A correction (in its bone's own frame) eased towards what the pose needs, no faster than max_correction_speed.
Quaternion FellowJointLimits3D::_ease_correction(int p_slot, const Quaternion &p_target, float p_speed) {
	Quaternion &c = correction[p_slot];
	if (max_correction_speed <= 0.0f || p_speed <= 0.0f) {
		c = p_target;
	} else if (step > 0.0) {
		const float max_angle = Math::deg_to_rad(max_correction_speed * p_speed) * float(step);
		const float angle = c.angle_to(p_target);
		c = angle <= max_angle ? p_target : c.slerp(p_target, max_angle / angle).normalized();
	}
	return c;
}

// A hinge (the elbow, the knee) bent in its plane: the upper bone turned about itself (its line from joint to joint, so
// that nothing moves but its turn) until its hinge's bend lies in the bend the limb has; the lower bone kept where it
// is. From 8° of bend (a straight limb has no plane) to 20° the turn comes in; a bend further round than
// hinge_max_turn (the hinge bent the wrong way) is left, fading out over the last 15°. Returns the turn (degrees).
float FellowJointLimits3D::_align_hinge(Skeleton3D *p_skeleton, int p_index, int p_upper, int p_lower, int p_end) {
	if (p_upper < 0 || p_lower < 0 || p_end < 0 || p_skeleton->get_bone_parent(p_lower) != p_upper || flex_local[p_index] == Vector3()) {
		return 0.0f;
	}
	const Transform3D U = p_skeleton->get_bone_global_pose(p_upper);
	const Transform3D L = p_skeleton->get_bone_global_pose(p_lower);
	const Vector3 E = p_skeleton->get_bone_global_pose(p_end).origin;
	Vector3 a = L.origin - U.origin;
	Vector3 f = E - L.origin;
	if (a.length() < 1e-6f || f.length() < 1e-6f) {
		return 0.0f;
	}
	a.normalize();
	f.normalize();
	const Basis ub = U.basis.orthonormalized();
	const Vector3 axis = ub.xform_inv(a).normalized();
	float turn = 0.0f;
	float w = 0.0f;
	Vector3 bend = square_to(f, a);
	const float bend_deg = Math::rad_to_deg(Math::atan2(bend.length(), a.dot(f)));
	Vector3 fd = square_to(ub.xform(flex_local[p_index]), a);
	if (bend_deg >= 8.0f && fd.length() > 1e-5f) {
		bend.normalize();
		fd.normalize();
		const float theta = Math::atan2(fd.cross(bend).dot(a), fd.dot(bend));
		const float theta_deg = Math::rad_to_deg(Math::abs(theta));
		if (theta_deg < hinge_max_turn) {
			turn = theta;
			w = smooth01((bend_deg - 8.0f) / 12.0f) * smooth01((hinge_max_turn - theta_deg) / 15.0f);
		}
	}
	// (in the bone's own frame: a turn about its own line. A limb straight, or bent the wrong way, keeps the turn it had —
	// its plane no longer says which: let go, the turn had swung back and forth by up to 120° as an arm straightened and
	// bent again)
	const Quaternion held = correction[p_index];
	const Vector3 held_axis = held.get_axis();
	const float held_angle = held.get_angle() * (held_axis.dot(axis) < 0.0f ? -1.0f : 1.0f);
	// (the turn's own speed less as the limb straightens: chasing a plane that is going, as an elbow straightened at
	// 1,000°/s, it had added its own to the elbow's)
	const Quaternion c = _ease_correction(p_index, Quaternion(axis, Math::lerp(held_angle, turn, w)), 0.25f + 0.75f * smooth01((bend_deg - 8.0f) / 40.0f));
	if (c.angle_to(Quaternion()) < 1e-5f) {
		return Math::rad_to_deg(Math::abs(turn));
	}
	const Basis lb = L.basis.orthonormalized();
	_set_global_rotation(p_skeleton, p_upper, ub * Basis(c));
	_set_global_rotation(p_skeleton, p_lower, lb);
	p_skeleton->set_bone_pose_position(p_lower, p_skeleton->get_bone_global_pose(p_upper).affine_inverse().xform(L.origin));
	return Math::rad_to_deg(Math::abs(turn));
}

// An ankle within ankle_range: the foot turned about the ankle in the plane of the shin and the foot (the toes with it).
float FellowJointLimits3D::_limit_ankle(Skeleton3D *p_skeleton, int p_side) {
	const int lower = bones[p_side == 0 ? ROLE_LEFT_LOWER_LEG : ROLE_RIGHT_LOWER_LEG];
	const int foot = bones[p_side == 0 ? ROLE_LEFT_FOOT : ROLE_RIGHT_FOOT];
	const int toes = bones[p_side == 0 ? ROLE_LEFT_TOES : ROLE_RIGHT_TOES];
	if (lower < 0 || foot < 0 || toes < 0) {
		return 0.0f;
	}
	const Transform3D F = p_skeleton->get_bone_global_pose(foot);
	const Vector3 shin = (F.origin - p_skeleton->get_bone_global_pose(lower).origin).normalized();
	const Vector3 v = (p_skeleton->get_bone_global_pose(toes).origin - F.origin).normalized();
	const float pf = Math::rad_to_deg(shin.angle_to(v)) - ankle_rest[p_side];
	float d = pf < ankle_range.x ? ankle_range.x - pf : (pf > ankle_range.y ? ankle_range.y - pf : 0.0f);
	const Vector3 n = shin.cross(v);
	if (n.length() < 1e-5f) {
		d = 0.0f;
	}
	const Basis fb = F.basis.orthonormalized();
	const Quaternion c = _ease_correction(SLOT_LEFT_FOOT + p_side, d == 0.0f ? Quaternion() : Quaternion(fb.xform_inv(n).normalized(), Math::deg_to_rad(d)));
	if (c.angle_to(Quaternion()) >= 1e-5f) {
		_set_global_rotation(p_skeleton, foot, fb * Basis(c));
	}
	return Math::abs(d);
}

// A wrist within its ranges: the hand's axis against where it would be with the wrist at rest — towards its back
// (extension) and its thumb's side (radial deviation) — turned back the least way into them.
float FellowJointLimits3D::_limit_wrist(Skeleton3D *p_skeleton, int p_side) {
	const int lower = bones[p_side == 0 ? ROLE_LEFT_LOWER_ARM : ROLE_RIGHT_LOWER_ARM];
	const int hand = bones[p_side == 0 ? ROLE_LEFT_HAND : ROLE_RIGHT_HAND];
	if (lower < 0 || hand < 0 || hand_axis[p_side] == Vector3()) {
		return 0.0f;
	}
	const Basis r0 = p_skeleton->get_bone_global_pose(lower).basis.orthonormalized() * hand_rest[p_side];
	const Vector3 axis0 = r0.xform(hand_axis[p_side]).normalized();
	const Vector3 back0 = r0.xform(hand_back[p_side]).normalized();
	const Vector3 thumb0 = r0.xform(hand_thumb[p_side]).normalized();
	const Basis hb = p_skeleton->get_bone_global_pose(hand).basis.orthonormalized();
	const Vector3 h = hb.xform(hand_axis[p_side]).normalized();
	// The hand's bend from its axis at rest (beta) and which way (phi: from its back towards its thumb's side); the
	// range an ellipse through the four limits, the hand brought back to it along the same way (no side to choose
	// between: clamped on each axis apart, a hand bent past 90° had flipped from one limit to the other in a frame).
	const float beta = Math::acos(CLAMP(h.dot(axis0), -1.0f, 1.0f));
	const float phi = Math::atan2(h.dot(thumb0), h.dot(back0));
	const float a = Math::deg_to_rad(Math::cos(phi) >= 0.0f ? wrist_flexion_range.y : -wrist_flexion_range.x);
	const float b = Math::deg_to_rad(Math::sin(phi) >= 0.0f ? wrist_deviation_range.y : -wrist_deviation_range.x);
	const float ca = b * Math::cos(phi);
	const float sb = a * Math::sin(phi);
	const float beta_max = (a > 0.0f && b > 0.0f) ? a * b / Math::sqrt(ca * ca + sb * sb) : 0.0f;
	Quaternion target;
	float over = 0.0f;
	if (beta > beta_max) {
		const Vector3 h2 = axis0 * Math::cos(beta_max) + (back0 * Math::cos(phi) + thumb0 * Math::sin(phi)) * Math::sin(beta_max);
		target = (hb.inverse() * Basis(Quaternion(h, h2.normalized())) * hb).get_rotation_quaternion();
		over = Math::rad_to_deg(beta - beta_max);
	}
	// (at once: what a wrist's correction undoes is the hand's own turn past its range, as fast as that comes — eased,
	// the wrist had stayed bent past it for a tenth of a second)
	const Quaternion c = _ease_correction(SLOT_LEFT_HAND + p_side, target, 0.0f);
	if (c.angle_to(Quaternion()) < 1e-5f) {
		return over;
	}
	const Basis hb2 = hb * Basis(c);
	// The hand turned about its knuckles, not its wrist (wrist_pivot): the arm brought to the wrist's new place — a hand
	// on a hold stays on it, turning on it as a person's does (turned about the wrist, its fingers had left the edge).
	const int upper = bones[p_side == 0 ? ROLE_LEFT_UPPER_ARM : ROLE_RIGHT_UPPER_ARM];
	const int middle = fingers[p_side][1];
	if (wrist_pivot && upper >= 0 && middle >= 0 && p_skeleton->get_bone_parent(lower) == upper && p_skeleton->get_bone_parent(hand) == lower) {
		const Vector3 w = p_skeleton->get_bone_global_pose(hand).origin;
		const Vector3 k = p_skeleton->get_bone_global_pose(middle).origin;
		const Vector3 k2 = w + hb2.xform(hb.xform_inv(k - w));
		const Vector3 w2 = w + (k - k2);
		if (w2.distance_to(w) > 1e-3f) {
			_reach(p_skeleton, upper, lower, hand, w2);
		}
	}
	_set_global_rotation(p_skeleton, hand, hb2);
	return over;
}

// An arm's (or a leg's) end brought to p_target, the bend kept in the plane it is in (the hinge's alignment kept): the
// upper bone and the lower turned the shortest way.
void FellowJointLimits3D::_reach(Skeleton3D *p_skeleton, int p_upper, int p_lower, int p_end, const Vector3 &p_target) {
	const Transform3D U = p_skeleton->get_bone_global_pose(p_upper);
	const Transform3D L = p_skeleton->get_bone_global_pose(p_lower);
	const Vector3 s = U.origin;
	const Vector3 e = L.origin;
	const Vector3 w = p_skeleton->get_bone_global_pose(p_end).origin;
	const float a = s.distance_to(e);
	const float b = e.distance_to(w);
	Vector3 n = (e - s).cross(w - s);
	Vector3 to = p_target - s;
	float d = to.length();
	if (a < 1e-5f || b < 1e-5f || d < 1e-5f || n.length() < 1e-6f) {
		return;
	}
	n.normalize();
	const Vector3 u = to / d;
	d = CLAMP(d, Math::abs(a - b) + 1e-4f, a + b - 1e-4f);
	const float cos_a = CLAMP((a * a + d * d - b * b) / (2.0f * a * d), -1.0f, 1.0f);
	const Vector3 v = n.cross(u).normalized();
	// (the elbow on the side of the line it was on)
	const float side = (e - s).dot(v) >= 0.0f ? 1.0f : -1.0f;
	const Vector3 e2 = s + (u * cos_a + v * side * Math::sqrt(MAX(0.0f, 1.0f - cos_a * cos_a))) * a;
	const Vector3 w2 = s + u * d;
	const Basis ub = U.basis.orthonormalized();
	const Basis lb = L.basis.orthonormalized();
	const Basis ub2 = Basis(Quaternion((e - s).normalized(), (e2 - s).normalized())) * ub;
	const Basis lb2 = Basis(Quaternion((w - e).normalized(), (w2 - e2).normalized())) * lb;
	_set_global_rotation(p_skeleton, p_upper, ub2);
	_set_global_rotation(p_skeleton, p_lower, lb2);
}

// The neck within its ranges: the head against the chest — its flexion forwards, its bend to the side, its turn —
// turned back about the chest's axes by as much as each goes past.
float FellowJointLimits3D::_limit_neck(Skeleton3D *p_skeleton) {
	const int chest = bones[ROLE_CHEST];
	const int head = bones[ROLE_HEAD];
	if (chest < 0 || head < 0 || chest_up == Vector3()) {
		return 0.0f;
	}
	const Basis cb = p_skeleton->get_bone_global_pose(chest).basis.orthonormalized();
	const Basis hb = p_skeleton->get_bone_global_pose(head).basis.orthonormalized();
	const Vector3 up_c = cb.xform(chest_up).normalized();
	const Vector3 fwd_c = cb.xform(chest_forward).normalized();
	const Vector3 left_c = cb.xform(chest_left).normalized();
	const Vector3 up_h = hb.xform(head_up).normalized();
	const Vector3 fwd_h = hb.xform(head_forward).normalized();
	const float flex = Math::rad_to_deg(Math::atan2(up_h.dot(fwd_c), up_h.dot(up_c)));
	const float bend = Math::rad_to_deg(Math::asin(CLAMP(up_h.dot(left_c), -1.0f, 1.0f)));
	const float turn = Math::rad_to_deg(Math::atan2(fwd_h.dot(left_c), fwd_h.dot(fwd_c)));
	const float df = CLAMP(flex, neck_flexion_range.x, neck_flexion_range.y) - flex;
	const float db = CLAMP(bend, -neck_bend_limit, neck_bend_limit) - bend;
	const float dt = CLAMP(turn, -neck_turn_limit, neck_turn_limit) - turn;
	Quaternion target;
	if (df != 0.0f || db != 0.0f || dt != 0.0f) {
		// (left, up, forward: a right-handed frame — flexion about the left, the bend to the left about forward the other
		// way, the turn to the left about up)
		const Quaternion q = Quaternion(left_c, Math::deg_to_rad(df)) * Quaternion(fwd_c, Math::deg_to_rad(-db)) * Quaternion(up_c, Math::deg_to_rad(dt));
		target = (hb.inverse() * Basis(q) * hb).get_rotation_quaternion();
	}
	const Quaternion c = _ease_correction(SLOT_HEAD, target);
	if (c.angle_to(Quaternion()) >= 1e-5f) {
		_set_global_rotation(p_skeleton, head, hb * Basis(c));
	}
	return MAX(Math::abs(df), MAX(Math::abs(db), Math::abs(dt)));
}

void FellowJointLimits3D::_process_modification(double p_delta) {
	Skeleton3D *skeleton = get_skeleton();
	if (!skeleton) {
		return;
	}
	const uint64_t t0 = OS::get_singleton()->get_ticks_usec();
	_find_bones(skeleton);
	if (!rest_valid) {
		_read_rest(skeleton);
	}
	step = p_delta;
	Stats s;
	s.frames = 1;
	if (hinge_alignment) {
		static const Role hinge[4][3] = { { ROLE_LEFT_UPPER_ARM, ROLE_LEFT_LOWER_ARM, ROLE_LEFT_HAND }, { ROLE_RIGHT_UPPER_ARM, ROLE_RIGHT_LOWER_ARM, ROLE_RIGHT_HAND }, { ROLE_LEFT_UPPER_LEG, ROLE_LEFT_LOWER_LEG, ROLE_LEFT_FOOT }, { ROLE_RIGHT_UPPER_LEG, ROLE_RIGHT_LOWER_LEG, ROLE_RIGHT_FOOT } };
		for (int i = 0; i < 4; i++) {
			const float t = _align_hinge(skeleton, SLOT_LEFT_UPPER_ARM + i, bones[hinge[i][0]], bones[hinge[i][1]], bones[hinge[i][2]]);
			if (t > 0.5f) {
				s.hinges++;
				s.hinge_max = MAX(s.hinge_max, t);
			}
		}
	}
	for (int side = 0; side < 2; side++) {
		if (limit_wrists) {
			const float w = _limit_wrist(skeleton, side);
			if (w > 0.0f) {
				s.wrists++;
				s.wrist_max = MAX(s.wrist_max, w);
			}
		}
		if (limit_ankles) {
			const float a = _limit_ankle(skeleton, side);
			if (a > 0.0f) {
				s.ankles++;
				s.ankle_max = MAX(s.ankle_max, a);
			}
		}
	}
	if (limit_neck) {
		const float n = _limit_neck(skeleton);
		if (n > 0.0f) {
			s.necks++;
			s.neck_max = n;
		}
	}
	s.usec = OS::get_singleton()->get_ticks_usec() - t0;
	last_stats = s;
	stats.frames++;
	stats.hinges += s.hinges;
	stats.ankles += s.ankles;
	stats.wrists += s.wrists;
	stats.necks += s.necks;
	stats.hinge_max = MAX(stats.hinge_max, s.hinge_max);
	stats.ankle_max = MAX(stats.ankle_max, s.ankle_max);
	stats.wrist_max = MAX(stats.wrist_max, s.wrist_max);
	stats.neck_max = MAX(stats.neck_max, s.neck_max);
	stats.usec += s.usec;
}

Dictionary FellowJointLimits3D::get_stats() const {
	Dictionary d;
	d["frames"] = (int64_t)stats.frames;
	d["hinges"] = (int64_t)stats.hinges;
	d["ankles"] = (int64_t)stats.ankles;
	d["wrists"] = (int64_t)stats.wrists;
	d["necks"] = (int64_t)stats.necks;
	d["hinge_max"] = stats.hinge_max;
	d["ankle_max"] = stats.ankle_max;
	d["wrist_max"] = stats.wrist_max;
	d["neck_max"] = stats.neck_max;
	d["usec"] = (int64_t)stats.usec;
	d["last_usec"] = (int64_t)last_stats.usec;
	return d;
}

Dictionary FellowJointLimits3D::get_bones() const {
	Dictionary d;
	const Skeleton3D *skeleton = get_skeleton();
	for (int r = 0; r < ROLE_MAX; r++) {
		d[ROLE_NAMES[r]] = (skeleton && bones[r] >= 0) ? Variant(skeleton->get_bone_name(bones[r])) : Variant();
	}
	return d;
}

void FellowJointLimits3D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_hinge_alignment", "enabled"), &FellowJointLimits3D::set_hinge_alignment);
	ClassDB::bind_method(D_METHOD("is_hinge_alignment"), &FellowJointLimits3D::is_hinge_alignment);
	ClassDB::bind_method(D_METHOD("set_hinge_max_turn", "degrees"), &FellowJointLimits3D::set_hinge_max_turn);
	ClassDB::bind_method(D_METHOD("get_hinge_max_turn"), &FellowJointLimits3D::get_hinge_max_turn);
	ClassDB::bind_method(D_METHOD("set_limit_ankles", "enabled"), &FellowJointLimits3D::set_limit_ankles);
	ClassDB::bind_method(D_METHOD("is_limit_ankles"), &FellowJointLimits3D::is_limit_ankles);
	ClassDB::bind_method(D_METHOD("set_ankle_range", "range"), &FellowJointLimits3D::set_ankle_range);
	ClassDB::bind_method(D_METHOD("get_ankle_range"), &FellowJointLimits3D::get_ankle_range);
	ClassDB::bind_method(D_METHOD("set_limit_wrists", "enabled"), &FellowJointLimits3D::set_limit_wrists);
	ClassDB::bind_method(D_METHOD("is_limit_wrists"), &FellowJointLimits3D::is_limit_wrists);
	ClassDB::bind_method(D_METHOD("set_wrist_flexion_range", "range"), &FellowJointLimits3D::set_wrist_flexion_range);
	ClassDB::bind_method(D_METHOD("get_wrist_flexion_range"), &FellowJointLimits3D::get_wrist_flexion_range);
	ClassDB::bind_method(D_METHOD("set_wrist_deviation_range", "range"), &FellowJointLimits3D::set_wrist_deviation_range);
	ClassDB::bind_method(D_METHOD("get_wrist_deviation_range"), &FellowJointLimits3D::get_wrist_deviation_range);
	ClassDB::bind_method(D_METHOD("set_wrist_pivot", "enabled"), &FellowJointLimits3D::set_wrist_pivot);
	ClassDB::bind_method(D_METHOD("is_wrist_pivot"), &FellowJointLimits3D::is_wrist_pivot);
	ClassDB::bind_method(D_METHOD("set_limit_neck", "enabled"), &FellowJointLimits3D::set_limit_neck);
	ClassDB::bind_method(D_METHOD("is_limit_neck"), &FellowJointLimits3D::is_limit_neck);
	ClassDB::bind_method(D_METHOD("set_neck_flexion_range", "range"), &FellowJointLimits3D::set_neck_flexion_range);
	ClassDB::bind_method(D_METHOD("get_neck_flexion_range"), &FellowJointLimits3D::get_neck_flexion_range);
	ClassDB::bind_method(D_METHOD("set_neck_bend_limit", "degrees"), &FellowJointLimits3D::set_neck_bend_limit);
	ClassDB::bind_method(D_METHOD("get_neck_bend_limit"), &FellowJointLimits3D::get_neck_bend_limit);
	ClassDB::bind_method(D_METHOD("set_neck_turn_limit", "degrees"), &FellowJointLimits3D::set_neck_turn_limit);
	ClassDB::bind_method(D_METHOD("get_neck_turn_limit"), &FellowJointLimits3D::get_neck_turn_limit);
	ClassDB::bind_method(D_METHOD("set_max_correction_speed", "degrees"), &FellowJointLimits3D::set_max_correction_speed);
	ClassDB::bind_method(D_METHOD("get_max_correction_speed"), &FellowJointLimits3D::get_max_correction_speed);
	ClassDB::bind_method(D_METHOD("set_bone_names", "names"), &FellowJointLimits3D::set_bone_names);
	ClassDB::bind_method(D_METHOD("get_bone_names"), &FellowJointLimits3D::get_bone_names);
	ClassDB::bind_method(D_METHOD("get_stats"), &FellowJointLimits3D::get_stats);
	ClassDB::bind_method(D_METHOD("get_bones"), &FellowJointLimits3D::get_bones);

	ADD_GROUP("Hinges", "");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "hinge_alignment"), "set_hinge_alignment", "is_hinge_alignment");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "hinge_max_turn", PROPERTY_HINT_RANGE, "0,180,0.1,suffix:°"), "set_hinge_max_turn", "get_hinge_max_turn");
	ADD_GROUP("Ankles", "");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "limit_ankles"), "set_limit_ankles", "is_limit_ankles");
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR2, "ankle_range", PROPERTY_HINT_NONE, "suffix:°"), "set_ankle_range", "get_ankle_range");
	ADD_GROUP("Wrists", "");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "limit_wrists"), "set_limit_wrists", "is_limit_wrists");
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR2, "wrist_flexion_range", PROPERTY_HINT_NONE, "suffix:°"), "set_wrist_flexion_range", "get_wrist_flexion_range");
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR2, "wrist_deviation_range", PROPERTY_HINT_NONE, "suffix:°"), "set_wrist_deviation_range", "get_wrist_deviation_range");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "wrist_pivot"), "set_wrist_pivot", "is_wrist_pivot");
	ADD_GROUP("Neck", "");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "limit_neck"), "set_limit_neck", "is_limit_neck");
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR2, "neck_flexion_range", PROPERTY_HINT_NONE, "suffix:°"), "set_neck_flexion_range", "get_neck_flexion_range");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "neck_bend_limit", PROPERTY_HINT_RANGE, "0,180,0.1,suffix:°"), "set_neck_bend_limit", "get_neck_bend_limit");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "neck_turn_limit", PROPERTY_HINT_RANGE, "0,180,0.1,suffix:°"), "set_neck_turn_limit", "get_neck_turn_limit");
	ADD_GROUP("", "");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "max_correction_speed", PROPERTY_HINT_RANGE, "0,5000,1,suffix:°/s"), "set_max_correction_speed", "get_max_correction_speed");
	ADD_PROPERTY(PropertyInfo(Variant::DICTIONARY, "bone_names"), "set_bone_names", "get_bone_names");

	BIND_ENUM_CONSTANT(ROLE_HIPS);
	BIND_ENUM_CONSTANT(ROLE_CHEST);
	BIND_ENUM_CONSTANT(ROLE_NECK);
	BIND_ENUM_CONSTANT(ROLE_HEAD);
	BIND_ENUM_CONSTANT(ROLE_LEFT_UPPER_ARM);
	BIND_ENUM_CONSTANT(ROLE_LEFT_LOWER_ARM);
	BIND_ENUM_CONSTANT(ROLE_LEFT_HAND);
	BIND_ENUM_CONSTANT(ROLE_RIGHT_UPPER_ARM);
	BIND_ENUM_CONSTANT(ROLE_RIGHT_LOWER_ARM);
	BIND_ENUM_CONSTANT(ROLE_RIGHT_HAND);
	BIND_ENUM_CONSTANT(ROLE_LEFT_UPPER_LEG);
	BIND_ENUM_CONSTANT(ROLE_LEFT_LOWER_LEG);
	BIND_ENUM_CONSTANT(ROLE_LEFT_FOOT);
	BIND_ENUM_CONSTANT(ROLE_LEFT_TOES);
	BIND_ENUM_CONSTANT(ROLE_RIGHT_UPPER_LEG);
	BIND_ENUM_CONSTANT(ROLE_RIGHT_LOWER_LEG);
	BIND_ENUM_CONSTANT(ROLE_RIGHT_FOOT);
	BIND_ENUM_CONSTANT(ROLE_RIGHT_TOES);
	BIND_ENUM_CONSTANT(ROLE_MAX);
}
