/**************************************************************************/
/*  fellow_motion_rig.cpp                                                 */
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

#include "fellow_motion_rig.h"

#include "core/math/basis.h"
#include "core/variant/array.h"
#include "core/variant/variant.h"

namespace {

const char *MODEL_JOINT_NAMES[FellowMotionRig::MODEL_JOINTS] = {
	"Hips", "Spine1", "Spine2", "Chest", "Neck1", "Neck2", "Head", "Jaw", "LeftEye", "RightEye",
	"LeftShoulder", "LeftArm", "LeftForeArm", "LeftHand", "LeftHandThumbEnd", "LeftHandMiddleEnd",
	"RightShoulder", "RightArm", "RightForeArm", "RightHand", "RightHandThumbEnd", "RightHandMiddleEnd",
	"LeftLeg", "LeftShin", "LeftFoot", "LeftToeBase", "RightLeg", "RightShin", "RightFoot", "RightToeBase"
};

constexpr float SAMPLE_RATE = 30.0f;

Vector3 v3(const Variant &p_v) {
	Array a = p_v;
	return Vector3(float(a[0]), float(a[1]), float(a[2]));
}

Quaternion q4(const Variant &p_v) {
	Array a = p_v;
	return Quaternion(float(a[0]), float(a[1]), float(a[2]), float(a[3])).normalized();
}

Basis rows3(const float *p_m) {
	return Basis(p_m[0], p_m[1], p_m[2], p_m[3], p_m[4], p_m[5], p_m[6], p_m[7], p_m[8]);
}

Quaternion from_6d(const float *p_c) {
	Vector3 x = Vector3(p_c[0], p_c[1], p_c[2]).normalized();
	Vector3 z = x.cross(Vector3(p_c[3], p_c[4], p_c[5])).normalized();
	Vector3 y = z.cross(x);
	Basis b;
	b.set_column(0, x);
	b.set_column(1, y);
	b.set_column(2, z);
	return b.get_rotation_quaternion();
}

float wrap_angle(float p_a) {
	return Math::atan2(Math::sin(p_a), Math::cos(p_a));
}

} // namespace

int FellowMotionRig::find(const String &p_name) const {
	for (int i = 0; i < joints; i++) {
		if (names[i] == p_name) {
			return i;
		}
	}
	return -1;
}

bool FellowMotionRig::setup(const Dictionary &p_skeleton, const float *p_capture, const float *p_standard, String &r_error) {
	Array nm = p_skeleton.get("names", Array());
	Array pa = p_skeleton.get("parents", Array());
	Array rp = p_skeleton.get("rest_pos", Array());
	Array rr = p_skeleton.get("rest_rot", Array());
	Array rx = p_skeleton.get("relaxed_rot", rr);
	joints = nm.size();
	if (joints < MODEL_JOINTS || pa.size() != joints || rp.size() != joints || rr.size() != joints || rx.size() != joints) {
		r_error = "the model file has no SOMA skeleton (convert it with --usd).";
		return false;
	}
	names.resize(joints);
	parents.resize(joints);
	reference.resize(joints);
	for (int i = 0; i < joints; i++) {
		names[i] = nm[i];
		parents[i] = int(pa[i]);
		if (parents[i] >= i) {
			r_error = "the skeleton's parents must come before their children.";
			return false;
		}
		reference[i].p = v3(rp[i]);
		reference[i].q = q4(rx[i]);
	}
	for (int m = 0; m < MODEL_JOINTS; m++) {
		model_to_pose[m] = find(MODEL_JOINT_NAMES[m]);
		if (model_to_pose[m] < 0) {
			r_error = String("the skeleton has no joint ") + MODEL_JOINT_NAMES[m] + ".";
			return false;
		}
	}
	if (model_to_pose[0] != 0) {
		r_error = "the skeleton's root must be Hips.";
		return false;
	}
	pose_to_model.resize(joints);
	for (int i = 0; i < joints; i++) {
		pose_to_model[i] = -1;
	}
	for (int m = 0; m < MODEL_JOINTS; m++) {
		int pj = model_to_pose[m];
		int p = parents[pj];
		if (p == -1 || pose_to_model[p] >= 0) {
			pose_to_model[pj] = m;
		}
	}
	right_leg = find("RightLeg");
	left_leg = find("LeftLeg");
	memcpy(capture_offsets, p_capture, sizeof(capture_offsets));
	memcpy(standard_offsets, p_standard, sizeof(standard_offsets));
	return true;
}

void FellowMotionRig::world(const Xf *p_local, Xf *r_world) const {
	for (int j = 0; j < joints; j++) {
		int p = parents[j];
		if (p < 0) {
			r_world[j] = p_local[j];
		} else {
			r_world[j].q = r_world[p].q * p_local[j].q;
			r_world[j].p = r_world[p].p + r_world[p].q.xform(p_local[j].p);
		}
	}
}

float FellowMotionRig::heading_of(const Xf *p_world) const {
	float ax = p_world[right_leg].p.x - p_world[left_leg].p.x;
	float az = p_world[right_leg].p.z - p_world[left_leg].p.z;
	float ln = Math::sqrt(ax * ax + az * az);
	if (ln <= 1e-6f) {
		return 0.0f;
	}
	// forward = up x across = (az, 0, -ax)
	return Math::atan2(az / ln, -ax / ln);
}

void FellowMotionRig::relaxed_pose(Xf *r_local, float p_arm_drop, float p_hips_height) const {
	LocalVector<Quaternion> g;
	g.resize(joints);
	LocalVector<bool> is_model;
	is_model.resize(joints);
	for (int j = 0; j < joints; j++) {
		is_model[j] = false;
	}
	for (int m = 0; m < MODEL_JOINTS; m++) {
		is_model[model_to_pose[m]] = true;
	}
	// The T pose of the standard rotations: global capture rotation S C^T for the model's joints.
	for (int j = 0; j < joints; j++) {
		int m = -1;
		for (int k = 0; k < MODEL_JOINTS; k++) {
			if (model_to_pose[k] == j) {
				m = k;
				break;
			}
		}
		if (m >= 0) {
			Basis s = rows3(standard_offsets + m * 9);
			Basis c = rows3(capture_offsets + m * 9);
			g[j] = (s * c.transposed()).get_rotation_quaternion();
		} else {
			g[j] = parents[j] >= 0 ? g[parents[j]] * reference[j].q : reference[j].q;
		}
	}
	const Quaternion left(Vector3(0, 0, 1), -p_arm_drop);
	const Quaternion right(Vector3(0, 0, 1), p_arm_drop);
	for (int j = 0; j < joints; j++) {
		const String &n = names[j];
		if (n.begins_with("LeftArm") || n.begins_with("LeftForeArm") || n.begins_with("LeftHand")) {
			g[j] = left * g[j];
		} else if (n.begins_with("RightArm") || n.begins_with("RightForeArm") || n.begins_with("RightHand")) {
			g[j] = right * g[j];
		}
	}
	for (int j = 0; j < joints; j++) {
		int p = parents[j];
		r_local[j].p = reference[j].p;
		if (p < 0) {
			r_local[j].q = g[j];
			r_local[j].p = Vector3(0, p_hips_height, 0);
		} else if (pose_to_model[j] >= 0) {
			r_local[j].q = (g[p].inverse() * g[j]).normalized();
		} else {
			r_local[j].q = reference[j].q;
		}
	}
}

void FellowMotionRig::encode(const Xf *p_context, const Xf *p_target, int p_target_poses, bool p_target_has_pose, uint32_t p_allowed_bits,
		float *r_grv, int32_t *r_hg, float *r_lrv, int32_t *r_hl, float *r_lp, int32_t *r_hp, int32_t *r_allowed, Xf &r_align) const {
	LocalVector<Xf> w;
	w.resize(8 * joints);
	for (int f = 0; f < 4; f++) {
		world(p_context + f * joints, w.ptr() + f * joints);
		world(p_target + f * joints, w.ptr() + (4 + f) * joints);
	}
	const Xf *last = w.ptr() + 3 * joints;
	Vector3 hips = last[0].p;
	float cur = heading_of(last);
	float desired = Math::atan2(1.0f, 0.0f); // heading alignment (1, 0): the last context pose faces +X
	r_align.p = Vector3(hips.x, 0.0f, hips.z);
	r_align.q = Quaternion(Vector3(0, 1, 0), cur - desired);
	Quaternion inv = r_align.q.inverse();
	for (int i = 0; i < 8 * joints; i++) {
		w[i].p = inv.xform(w[i].p - r_align.p);
		w[i].q = inv * w[i].q;
	}
	float heads[8];
	for (int f = 0; f < 8; f++) {
		heads[f] = heading_of(w.ptr() + f * joints);
	}
	for (int f = 0; f < 8; f++) {
		int nf = f == 3 ? f : MIN(f + 1, 7);
		const Xf *a = w.ptr() + f * joints;
		const Xf *b = w.ptr() + nf * joints;
		Vector3 h = a[0].p;
		Vector3 hn = b[0].p;
		float *g = r_grv + f * 5;
		g[0] = h.x;
		g[1] = h.y;
		g[2] = h.z;
		g[3] = Math::cos(heads[f]);
		g[4] = Math::sin(heads[f]);
		float *l = r_lrv + f * 4;
		l[0] = wrap_angle(heads[nf] - heads[f]) * SAMPLE_RATE;
		l[1] = (hn.x - h.x) * SAMPLE_RATE;
		l[2] = (hn.z - h.z) * SAMPLE_RATE;
		l[3] = h.y;
		Vector3 root(h.x, 0.0f, h.z);
		float *lp = r_lp + f * 267;
		for (int m = 0; m < MODEL_JOINTS; m++) {
			const Xf &jx = a[model_to_pose[m]];
			if (m > 0) {
				Vector3 c = jx.p - root;
				lp[(m - 1) * 3 + 0] = c.x;
				lp[(m - 1) * 3 + 1] = c.y;
				lp[(m - 1) * 3 + 2] = c.z;
			}
			Basis bs(jx.q);
			Vector3 cx = bs.get_column(0);
			Vector3 cy = bs.get_column(1);
			float *r = lp + 87 + m * 6;
			r[0] = cx.x;
			r[1] = cx.y;
			r[2] = cx.z;
			r[3] = cy.x;
			r[4] = cy.y;
			r[5] = cy.z;
		}
	}
	const int ntp = CLAMP(p_target_poses, 1, 4);
	for (int i = 0; i < 8; i++) {
		int v = i < 4 + ntp ? 1 : 0;
		r_hg[i] = v;
		r_hp[i] = i < 4 ? 1 : (p_target_has_pose ? v : 0);
		r_hl[i] = (i == 3 || i == 4 + ntp - 1) ? 0 : v;
	}
	for (int k = 0; k < 11; k++) {
		r_allowed[k] = (p_allowed_bits >> k) & 1;
	}
}

int FellowMotionRig::decode(const float *p_motion, int p_tokens, const Xf &p_align, LocalVector<Xf> &r_frames, LocalVector<float> &r_contacts) const {
	int raw = CLAMP(p_tokens * 4, 0, 64);
	int frames = MAX(0, raw - 2 * BOUNDARY);
	r_frames.resize(frames * joints);
	r_contacts.resize(frames * 4);
	Quaternion rots[MODEL_JOINTS];
	for (int i = 0; i < frames; i++) {
		const float *m = p_motion + (BOUNDARY + i) * 366;
		for (int c = 0; c < 4; c++) {
			r_contacts[i * 4 + c] = CLAMP(m[362 + c], 0.0f, 1.0f);
		}
		for (int k = 0; k < MODEL_JOINTS; k++) {
			rots[k] = from_6d(m + 92 + k * 6);
		}
		Xf *out = r_frames.ptr() + i * joints;
		out[0].p = p_align.p + p_align.q.xform(Vector3(m[0], m[1], m[2]));
		out[0].q = (p_align.q * rots[0]).normalized();
		for (int j = 1; j < joints; j++) {
			int mj = pose_to_model[j];
			if (mj < 0) {
				out[j] = reference[j];
				continue;
			}
			int pm = pose_to_model[parents[j]];
			out[j].p = reference[j].p;
			out[j].q = (rots[pm].inverse() * rots[mj]).normalized();
		}
	}
	return frames;
}

void FellowMotionRig::correct_endpoints(LocalVector<Xf> &r_frames, int p_frames, const Xf *p_start, const Xf *p_end, bool p_correct_end, bool p_end_joints) const {
	if (p_frames < 2) {
		return;
	}
	const Xf *f0 = r_frames.ptr();
	const Xf *fl = r_frames.ptr() + (p_frames - 1) * joints;
	LocalVector<Xf> sd, ed;
	sd.resize(joints);
	ed.resize(joints);
	sd[0].p = p_start[0].p - f0[0].p;
	sd[0].q = (f0[0].q.inverse() * p_start[0].q).normalized();
	ed[0].p = p_correct_end ? p_end[0].p - fl[0].p : Vector3();
	ed[0].q = p_correct_end ? (fl[0].q.inverse() * p_end[0].q).normalized() : Quaternion();
	for (int j = 1; j < joints; j++) {
		Quaternion is = f0[j].q.inverse();
		sd[j].p = is.xform(p_start[j].p - f0[j].p);
		sd[j].q = (is * p_start[j].q).normalized();
		if (p_correct_end && p_end_joints) {
			Quaternion ie = fl[j].q.inverse();
			ed[j].p = ie.xform(p_end[j].p - fl[j].p);
			ed[j].q = (ie * p_end[j].q).normalized();
		} else {
			ed[j].p = Vector3();
			ed[j].q = Quaternion();
		}
	}
	const Quaternion I;
	for (int f = 0; f < p_frames; f++) {
		float we = float(f) / float(p_frames - 1);
		float ws = 1.0f - we;
		Xf *x = r_frames.ptr() + f * joints;
		x[0].q = (x[0].q * I.slerp(sd[0].q, ws) * I.slerp(ed[0].q, we)).normalized();
		x[0].p = x[0].p + sd[0].p * ws + ed[0].p * we;
		for (int j = 1; j < joints; j++) {
			Vector3 p = x[j].p;
			Quaternion q = x[j].q;
			p = p + q.xform(sd[j].p * ws);
			q = q * I.slerp(sd[j].q, ws);
			p = p + q.xform(ed[j].p * we);
			q = q * I.slerp(ed[j].q, we);
			x[j].p = p;
			x[j].q = q.normalized();
		}
	}
}
