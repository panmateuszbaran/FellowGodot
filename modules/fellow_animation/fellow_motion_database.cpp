/**************************************************************************/
/*  fellow_motion_database.cpp                                            */
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

#include "fellow_motion_database.h"

#include "fellow_motion_math.h"

#include "core/io/file_access.h"
#include "core/object/class_db.h"
#include "scene/3d/skeleton_3d.h"

#include <cfloat>

using namespace FellowMotionMath;

namespace {

// Feature layout: left and right foot positions (6), their velocities (6), the hips' velocity (3), the trajectory's
// positions (6) and directions (6) on the ground, all in the root's space.
enum FeatureGroup {
	GROUP_FOOT_POSITION,
	GROUP_FOOT_VELOCITY,
	GROUP_HIPS_VELOCITY,
	GROUP_TRAJECTORY_POSITION,
	GROUP_TRAJECTORY_DIRECTION,
	GROUP_MAX,
};

const int group_offset[GROUP_MAX] = { 0, 6, 12, 15, 21 };
const int group_size[GROUP_MAX] = { 6, 6, 3, 6, 6 };
const float default_weights[GROUP_MAX] = { 0.75f, 1.0f, 1.0f, 1.0f, 1.5f };

const char *role_option[FellowMotionDatabase::ROLE_MAX] = { "hips", "left_upper_leg", "left_lower_leg", "left_foot", "left_toe", "right_upper_leg", "right_lower_leg", "right_foot", "right_toe" };

// Names tried for each role when the options do not give one: the CMU/MotionBuilder names and Godot's humanoid profile.
const char *role_defaults[FellowMotionDatabase::ROLE_MAX][3] = {
	{ "Hips", "hips", "pelvis" },
	{ "LeftUpLeg", "LeftUpperLeg", "thigh_l" },
	{ "LeftLeg", "LeftLowerLeg", "calf_l" },
	{ "LeftFoot", "LeftFoot", "foot_l" },
	{ "LeftToeBase", "LeftToes", "ball_l" },
	{ "RightUpLeg", "RightUpperLeg", "thigh_r" },
	{ "RightLeg", "RightLowerLeg", "calf_r" },
	{ "RightFoot", "RightFoot", "foot_r" },
	{ "RightToeBase", "RightToes", "ball_r" },
};

inline Vector3 root_pos_of(const PackedVector3Array &p_positions, int p_frame) {
	return p_positions[p_frame];
}

inline Quaternion read_quat(const float *p) {
	return Quaternion(p[0], p[1], p[2], p[3]);
}

inline void write_quat(float *p, const Quaternion &q) {
	p[0] = q.x;
	p[1] = q.y;
	p[2] = q.z;
	p[3] = q.w;
}

// Gaussian smoothing of a sequence of vectors, clamped at the ends.
void smooth(LocalVector<Vector3> &r_values, float p_sigma_frames) {
	if (p_sigma_frames <= 0.0f || r_values.size() < 3) {
		return;
	}
	const int radius = int(Math::ceil(p_sigma_frames * 3.0f));
	LocalVector<float> kernel;
	kernel.resize(radius * 2 + 1);
	for (int k = -radius; k <= radius; k++) {
		kernel[k + radius] = Math::exp(-(k * k) / (2.0f * p_sigma_frames * p_sigma_frames));
	}
	LocalVector<Vector3> out;
	out.resize(r_values.size());
	const int n = r_values.size();
	for (int i = 0; i < n; i++) {
		Vector3 sum;
		float wsum = 0.0f;
		for (int k = -radius; k <= radius; k++) {
			const int j = CLAMP(i + k, 0, n - 1);
			sum += r_values[j] * kernel[k + radius];
			wsum += kernel[k + radius];
		}
		out[i] = sum / wsum;
	}
	r_values = out;
}

} //namespace

FellowMotionDatabase::FellowMotionDatabase() {
	for (int i = 0; i < ROLE_MAX; i++) {
		bone_roles[i] = -1;
	}
}

Quaternion FellowMotionDatabase::get_bone_rest_rotation(int p_bone) const {
	return read_quat(bone_rest_rotations.ptr() + p_bone * 4);
}

Quaternion FellowMotionDatabase::get_root_rotation(int p_frame) const {
	return read_quat(root_rotations.ptr() + p_frame * 4);
}

int FellowMotionDatabase::get_range_of(int p_frame) const {
	// Ranges are in order: the last one starting at or before the frame.
	int lo = 0;
	int hi = range_starts.size() - 1;
	while (lo < hi) {
		const int mid = (lo + hi + 1) / 2;
		if (range_starts[mid] <= p_frame) {
			lo = mid;
		} else {
			hi = mid - 1;
		}
	}
	return lo;
}

Error FellowMotionDatabase::build(Object *p_skeleton, const TypedArray<Animation> &p_animations, const Dictionary &p_options) {
	Skeleton3D *skeleton = Object::cast_to<Skeleton3D>(p_skeleton);
	ERR_FAIL_NULL_V_MSG(skeleton, ERR_INVALID_PARAMETER, "FellowMotionDatabase.build(): a Skeleton3D is needed.");
	const int bone_count = skeleton->get_bone_count();
	ERR_FAIL_COND_V(bone_count == 0, ERR_INVALID_PARAMETER);

	sample_rate = float(p_options.get("sample_rate", 60.0));
	ERR_FAIL_COND_V(sample_rate <= 0.0f, ERR_INVALID_PARAMETER);
	for (int i = 0; i < TRAJECTORY_POINTS; i++) {
		trajectory_frames[i] = int(Math::round(sample_rate * (i + 1) / 3.0f));
	}
	// Only frames whose whole trajectory lies within their clip can be found (a trajectory clamped at a clip's end
	// looks like stopping, and short clips' last second would match every slowing request).
	ignore_range_end = int(p_options.get("ignore_range_end", trajectory_frames[TRAJECTORY_POINTS - 1]));
	const float facing_smoothing = float(p_options.get("facing_smoothing", 0.15)); // Seconds (sigma).
	const float root_smoothing = float(p_options.get("root_smoothing", 0.05));
	const float contact_speed = float(p_options.get("contact_speed", 0.3)); // m/s
	const float contact_margin = float(p_options.get("contact_height_margin", 0.04)); // m above the feet's low point.

	weights.resize(GROUP_MAX);
	const char *weight_option[GROUP_MAX] = { "weight_foot_position", "weight_foot_velocity", "weight_hips_velocity", "weight_trajectory_position", "weight_trajectory_direction" };
	for (int g = 0; g < GROUP_MAX; g++) {
		weights.set(g, float(p_options.get(weight_option[g], default_weights[g])));
	}

	bone_names.resize(bone_count);
	bone_parents.resize(bone_count);
	bone_rest_positions.resize(bone_count);
	bone_rest_rotations.resize(bone_count * 4);
	HashMap<String, int> bone_index;
	for (int b = 0; b < bone_count; b++) {
		bone_names.set(b, skeleton->get_bone_name(b));
		bone_parents.set(b, skeleton->get_bone_parent(b));
		const Transform3D rest = skeleton->get_bone_rest(b);
		bone_rest_positions.set(b, rest.origin);
		write_quat(bone_rest_rotations.ptrw() + b * 4, rest.basis.get_rotation_quaternion());
		bone_index[bone_names[b]] = b;
		ERR_FAIL_COND_V_MSG(bone_parents[b] >= b, ERR_INVALID_DATA, "FellowMotionDatabase.build(): bones must come after their parents.");
	}
	for (int r = 0; r < ROLE_MAX; r++) {
		bone_roles[r] = -1;
		if (p_options.has(role_option[r])) {
			const String role_name = p_options[role_option[r]];
			bone_roles[r] = bone_index.has(role_name) ? bone_index[role_name] : -1;
		} else {
			for (int c = 0; c < 3 && bone_roles[r] < 0; c++) {
				if (bone_index.has(role_defaults[r][c])) {
					bone_roles[r] = bone_index[role_defaults[r][c]];
				}
			}
		}
		ERR_FAIL_COND_V_MSG(bone_roles[r] < 0 && r != ROLE_LEFT_TOE && r != ROLE_RIGHT_TOE, ERR_INVALID_DATA, vformat("FellowMotionDatabase.build(): no bone for \"%s\" (set it in the options).", role_option[r]));
	}
	const int hips = bone_roles[ROLE_HIPS];

	// Sample every clip.
	LocalVector<Vector3> all_positions;
	LocalVector<Quaternion> all_rotations;
	LocalVector<int> starts;
	LocalVector<int> stops;
	Vector<String> names;
	const int min_frames = MAX(trajectory_frames[TRAJECTORY_POINTS - 1], ignore_range_end) + 2;
	for (int a = 0; a < p_animations.size(); a++) {
		Ref<Animation> anim = p_animations[a];
		if (anim.is_null()) {
			continue;
		}
		LocalVector<int> rotation_track;
		LocalVector<int> position_track;
		rotation_track.resize(bone_count);
		position_track.resize(bone_count);
		for (int b = 0; b < bone_count; b++) {
			rotation_track[b] = -1;
			position_track[b] = -1;
		}
		for (int t = 0; t < anim->get_track_count(); t++) {
			const String sub = anim->track_get_path(t).get_concatenated_subnames();
			if (!bone_index.has(sub)) {
				continue;
			}
			const int b = bone_index[sub];
			if (anim->track_get_type(t) == Animation::TYPE_ROTATION_3D) {
				rotation_track[b] = t;
			} else if (anim->track_get_type(t) == Animation::TYPE_POSITION_3D) {
				position_track[b] = t;
			}
		}
		const int n = int(Math::floor(anim->get_length() * sample_rate + 1e-4)) + 1;
		if (n < min_frames) {
			continue;
		}
		starts.push_back(all_positions.size() / bone_count);
		for (int i = 0; i < n; i++) {
			const double t = MIN(double(i) / sample_rate, anim->get_length());
			for (int b = 0; b < bone_count; b++) {
				Vector3 p = bone_rest_positions[b];
				Quaternion q = get_bone_rest_rotation(b);
				if (position_track[b] >= 0) {
					anim->try_position_track_interpolate(position_track[b], t, &p);
				}
				if (rotation_track[b] >= 0) {
					anim->try_rotation_track_interpolate(rotation_track[b], t, &q);
				}
				all_positions.push_back(p);
				all_rotations.push_back(q.normalized());
			}
		}
		stops.push_back(all_positions.size() / bone_count);
		names.push_back(anim->get_name().is_empty() ? anim->get_path().get_file().get_basename() : anim->get_name());
	}
	frame_count = all_positions.size() / bone_count;
	ERR_FAIL_COND_V_MSG(frame_count == 0, ERR_INVALID_DATA, "FellowMotionDatabase.build(): no clip long enough.");

	// Forward kinematics of the bones the features need, in the skeleton's space.
	const int key_bones[] = { hips, bone_roles[ROLE_LEFT_UPPER_LEG], bone_roles[ROLE_RIGHT_UPPER_LEG], bone_roles[ROLE_LEFT_FOOT], bone_roles[ROLE_RIGHT_FOOT], bone_roles[ROLE_LEFT_TOE] >= 0 ? bone_roles[ROLE_LEFT_TOE] : bone_roles[ROLE_LEFT_FOOT], bone_roles[ROLE_RIGHT_TOE] >= 0 ? bone_roles[ROLE_RIGHT_TOE] : bone_roles[ROLE_RIGHT_FOOT] };
	enum { K_HIPS,
		K_LUL,
		K_RUL,
		K_LF,
		K_RF,
		K_LT,
		K_RT,
		K_MAX };
	LocalVector<Vector3> key_pos;
	LocalVector<Quaternion> hips_rot;
	key_pos.resize(frame_count * K_MAX);
	hips_rot.resize(frame_count);
	{
		LocalVector<Vector3> gp;
		LocalVector<Quaternion> gq;
		gp.resize(bone_count);
		gq.resize(bone_count);
		for (int f = 0; f < frame_count; f++) {
			for (int b = 0; b < bone_count; b++) {
				const Vector3 &lp = all_positions[f * bone_count + b];
				const Quaternion &lq = all_rotations[f * bone_count + b];
				const int parent = bone_parents[b];
				if (parent < 0) {
					gp[b] = lp;
					gq[b] = lq;
				} else {
					gp[b] = gp[parent] + gq[parent].xform(lp);
					gq[b] = gq[parent] * lq;
				}
			}
			for (int k = 0; k < K_MAX; k++) {
				key_pos[f * K_MAX + k] = gp[key_bones[k]];
			}
			hips_rot[f] = gq[hips];
		}
	}

	// The root: on the ground under the hips, facing the way the pelvis faces (the right leg's hip to the left's).
	root_positions.resize(frame_count);
	root_rotations.resize(frame_count * 4);
	for (uint32_t r = 0; r < starts.size(); r++) {
		LocalVector<Vector3> pos;
		LocalVector<Vector3> dir;
		for (int f = starts[r]; f < stops[r]; f++) {
			const Vector3 h = key_pos[f * K_MAX + K_HIPS];
			pos.push_back(Vector3(h.x, 0.0f, h.z));
			Vector3 right = key_pos[f * K_MAX + K_RUL] - key_pos[f * K_MAX + K_LUL];
			right.y = 0.0f;
			Vector3 forward = Vector3(0, 1, 0).cross(right);
			dir.push_back(forward.length() > 1e-6f ? forward.normalized() : Vector3(0, 0, 1));
		}
		smooth(pos, root_smoothing * sample_rate);
		smooth(dir, facing_smoothing * sample_rate);
		for (int f = starts[r]; f < stops[r]; f++) {
			const int i = f - starts[r];
			Vector3 d = dir[i];
			d.y = 0.0f;
			d = d.length() > 1e-6f ? d.normalized() : Vector3(0, 0, 1);
			root_positions.set(f, pos[i]);
			write_quat(root_rotations.ptrw() + f * 4, yaw_rotation(d));
		}
	}

	// The poses, the hips relative to the root (the bones above the hips are taken at rest).
	for (int f = 0; f < frame_count; f++) {
		const Quaternion root_inv = get_root_rotation(f).inverse();
		all_positions[f * bone_count + hips] = root_inv.xform(key_pos[f * K_MAX + K_HIPS] - root_pos_of(root_positions, f));
		all_rotations[f * bone_count + hips] = (root_inv * hips_rot[f]).normalized();
	}
	position_slots.resize(bone_count);
	rotation_slots.resize(bone_count);
	position_slot_count = 0;
	rotation_slot_count = 0;
	for (int b = 0; b < bone_count; b++) {
		bool moves_p = b == hips;
		bool moves_q = b == hips;
		const Quaternion rest_q = get_bone_rest_rotation(b);
		for (int f = 0; f < frame_count && !(moves_p && moves_q); f++) {
			moves_p = moves_p || !all_positions[f * bone_count + b].is_equal_approx(bone_rest_positions[b]);
			moves_q = moves_q || Math::abs(all_rotations[f * bone_count + b].dot(rest_q)) < 1.0f - 1e-7f;
		}
		position_slots.set(b, moves_p ? position_slot_count++ : -1);
		rotation_slots.set(b, moves_q ? rotation_slot_count++ : -1);
	}
	positions.resize(frame_count * position_slot_count);
	rotations.resize(frame_count * rotation_slot_count * 4 * sizeof(int16_t));
	{
		Vector3 *wp = positions.ptrw();
		int16_t *wq = reinterpret_cast<int16_t *>(rotations.ptrw());
		for (int f = 0; f < frame_count; f++) {
			for (int b = 0; b < bone_count; b++) {
				if (position_slots[b] >= 0) {
					wp[f * position_slot_count + position_slots[b]] = all_positions[f * bone_count + b];
				}
				if (rotation_slots[b] >= 0) {
					const Quaternion q = all_rotations[f * bone_count + b];
					int16_t *o = wq + (f * rotation_slot_count + rotation_slots[b]) * 4;
					o[0] = int16_t(Math::round(CLAMP(q.x, -1.0f, 1.0f) * 32767.0f));
					o[1] = int16_t(Math::round(CLAMP(q.y, -1.0f, 1.0f) * 32767.0f));
					o[2] = int16_t(Math::round(CLAMP(q.z, -1.0f, 1.0f) * 32767.0f));
					o[3] = int16_t(Math::round(CLAMP(q.w, -1.0f, 1.0f) * 32767.0f));
				}
			}
		}
	}

	range_starts.resize(starts.size());
	range_stops.resize(stops.size());
	range_names.resize(names.size());
	for (uint32_t r = 0; r < starts.size(); r++) {
		range_starts.set(r, starts[r]);
		range_stops.set(r, stops[r]);
		range_names.set(r, names[r]);
	}

	// Contacts: a toe low and slow.
	contacts.resize(frame_count * 2);
	{
		float low[2] = { FLT_MAX, FLT_MAX };
		LocalVector<float> heights[2];
		for (int f = 0; f < frame_count; f++) {
			for (int s = 0; s < 2; s++) {
				heights[s].push_back(key_pos[f * K_MAX + K_LT + s].y);
			}
		}
		for (int s = 0; s < 2; s++) {
			LocalVector<float> sorted(heights[s]);
			sorted.sort();
			low[s] = sorted[sorted.size() / 20]; // The 5th percentile.
		}
		const float threshold_h[2] = { float(p_options.get("contact_height", low[0] + contact_margin)), float(p_options.get("contact_height", low[1] + contact_margin)) };
		for (uint32_t r = 0; r < starts.size(); r++) {
			for (int f = starts[r]; f < stops[r]; f++) {
				const int f0 = f < stops[r] - 1 ? f : f - 1;
				for (int s = 0; s < 2; s++) {
					const Vector3 v = (key_pos[(f0 + 1) * K_MAX + K_LT + s] - key_pos[f0 * K_MAX + K_LT + s]) * sample_rate;
					const bool c = key_pos[f * K_MAX + K_LT + s].y < threshold_h[s] && v.length() < contact_speed;
					contacts.set(f * 2 + s, c ? 1 : 0);
				}
			}
		}
	}

	// Features.
	features.resize(frame_count * FEATURE_COUNT);
	{
		float *w = features.ptrw();
		for (uint32_t r = 0; r < starts.size(); r++) {
			for (int f = starts[r]; f < stops[r]; f++) {
				const int f0 = f < stops[r] - 1 ? f : f - 1;
				const Quaternion root_inv = get_root_rotation(f).inverse();
				const Vector3 root_pos = root_positions[f];
				float *x = w + f * FEATURE_COUNT;
				const Vector3 lf = root_inv.xform(key_pos[f * K_MAX + K_LF] - root_pos);
				const Vector3 rf = root_inv.xform(key_pos[f * K_MAX + K_RF] - root_pos);
				const Vector3 lv = root_inv.xform((key_pos[(f0 + 1) * K_MAX + K_LF] - key_pos[f0 * K_MAX + K_LF]) * sample_rate);
				const Vector3 rv = root_inv.xform((key_pos[(f0 + 1) * K_MAX + K_RF] - key_pos[f0 * K_MAX + K_RF]) * sample_rate);
				const Vector3 hv = root_inv.xform((key_pos[(f0 + 1) * K_MAX + K_HIPS] - key_pos[f0 * K_MAX + K_HIPS]) * sample_rate);
				const Vector3 values[] = { lf, rf, lv, rv };
				for (int i = 0; i < 4; i++) {
					x[i * 3 + 0] = values[i].x;
					x[i * 3 + 1] = values[i].y;
					x[i * 3 + 2] = values[i].z;
				}
				x[12] = hv.x;
				x[13] = hv.y;
				x[14] = hv.z;
				for (int t = 0; t < TRAJECTORY_POINTS; t++) {
					const int g = MIN(f + trajectory_frames[t], stops[r] - 1);
					const Vector3 tp = root_inv.xform(root_positions[g] - root_pos);
					const Vector3 td = root_inv.xform(get_root_rotation(g).xform(Vector3(0, 0, 1)));
					x[15 + t * 2 + 0] = tp.x;
					x[15 + t * 2 + 1] = tp.z;
					x[21 + t * 2 + 0] = td.x;
					x[21 + t * 2 + 1] = td.z;
				}
			}
		}
	}

	// Normalisation per group: the mean per dimension, the groups' mean standard deviation, divided by its weight.
	feature_offset.resize(FEATURE_COUNT);
	feature_scale.resize(FEATURE_COUNT);
	{
		float *w = features.ptrw();
		for (int g = 0; g < GROUP_MAX; g++) {
			float std_sum = 0.0f;
			for (int d = group_offset[g]; d < group_offset[g] + group_size[g]; d++) {
				double mean = 0.0;
				for (int f = 0; f < frame_count; f++) {
					mean += w[f * FEATURE_COUNT + d];
				}
				mean /= frame_count;
				double var = 0.0;
				for (int f = 0; f < frame_count; f++) {
					const double e = w[f * FEATURE_COUNT + d] - mean;
					var += e * e;
				}
				feature_offset.set(d, float(mean));
				std_sum += float(Math::sqrt(var / frame_count));
			}
			const float std_group = MAX(std_sum / group_size[g], 1e-5f);
			for (int d = group_offset[g]; d < group_offset[g] + group_size[g]; d++) {
				feature_scale.set(d, weights[g] > 0.0f ? std_group / weights[g] : FLT_MAX);
			}
		}
		for (int f = 0; f < frame_count; f++) {
			for (int d = 0; d < FEATURE_COUNT; d++) {
				w[f * FEATURE_COUNT + d] = (w[f * FEATURE_COUNT + d] - feature_offset[d]) / feature_scale[d];
			}
		}
	}

	_rebuild_acceleration();
	emit_changed();
	return OK;
}

void FellowMotionDatabase::normalize_query(float *r_query) const {
	for (int d = 0; d < FEATURE_COUNT; d++) {
		r_query[d] = (r_query[d] - feature_offset[d]) / feature_scale[d];
	}
}

void FellowMotionDatabase::_rebuild_acceleration() {
	searchable.resize(frame_count);
	for (int f = 0; f < frame_count; f++) {
		searchable[f] = 0;
	}
	for (int r = 0; r < range_starts.size(); r++) {
		for (int f = range_starts[r]; f < range_stops[r] - ignore_range_end; f++) {
			searchable[f] = 1;
		}
	}
	const int nsmall = (frame_count + 15) / 16;
	const int nlarge = (frame_count + 63) / 64;
	bound_small_min.resize(nsmall * FEATURE_COUNT);
	bound_small_max.resize(nsmall * FEATURE_COUNT);
	bound_large_min.resize(nlarge * FEATURE_COUNT);
	bound_large_max.resize(nlarge * FEATURE_COUNT);
	for (int i = 0; i < nsmall * FEATURE_COUNT; i++) {
		bound_small_min[i] = FLT_MAX;
		bound_small_max[i] = -FLT_MAX;
	}
	for (int i = 0; i < nlarge * FEATURE_COUNT; i++) {
		bound_large_min[i] = FLT_MAX;
		bound_large_max[i] = -FLT_MAX;
	}
	const float *x = features.ptr();
	for (int f = 0; f < frame_count; f++) {
		if (!searchable[f]) {
			continue; // A box only bounds the frames that can be found.
		}
		const int s = f / 16;
		const int l = f / 64;
		for (int d = 0; d < FEATURE_COUNT; d++) {
			const float v = x[f * FEATURE_COUNT + d];
			bound_small_min[s * FEATURE_COUNT + d] = MIN(bound_small_min[s * FEATURE_COUNT + d], v);
			bound_small_max[s * FEATURE_COUNT + d] = MAX(bound_small_max[s * FEATURE_COUNT + d], v);
			bound_large_min[l * FEATURE_COUNT + d] = MIN(bound_large_min[l * FEATURE_COUNT + d], v);
			bound_large_max[l * FEATURE_COUNT + d] = MAX(bound_large_max[l * FEATURE_COUNT + d], v);
		}
	}
}

static inline float box_distance(const float *p_query, const float *p_min, const float *p_max) {
	float cost = 0.0f;
	for (int d = 0; d < FellowMotionDatabase::FEATURE_COUNT; d++) {
		const float q = p_query[d];
		const float c = CLAMP(q, p_min[d], p_max[d]) - q;
		cost += c * c;
	}
	return cost; // A box with no searchable frame has min > max: its clamp still bounds nothing below (FLT_MAX away).
}

FellowMotionDatabase::SearchResult FellowMotionDatabase::search(const float *p_query, int p_current_frame, bool p_include_current) const {
	SearchResult result;
	result.cost = FLT_MAX;
	if (!is_valid() || searchable.size() != uint32_t(frame_count)) {
		return result;
	}
	const float *x = features.ptr();
	if (p_include_current && p_current_frame >= 0 && p_current_frame < frame_count) {
		float cost = 0.0f;
		for (int d = 0; d < FEATURE_COUNT; d++) {
			const float e = p_query[d] - x[p_current_frame * FEATURE_COUNT + d];
			cost += e * e;
		}
		result.frame = p_current_frame;
		result.cost = cost;
	}
	int i = 0;
	while (i < frame_count) {
		const int l = i / 64;
		const int large_end = MIN((l + 1) * 64, frame_count);
		if (box_distance(p_query, bound_large_min.ptr() + l * FEATURE_COUNT, bound_large_max.ptr() + l * FEATURE_COUNT) >= result.cost) {
			i = large_end;
			continue;
		}
		while (i < large_end) {
			const int s = i / 16;
			const int small_end = MIN((s + 1) * 16, frame_count);
			if (box_distance(p_query, bound_small_min.ptr() + s * FEATURE_COUNT, bound_small_max.ptr() + s * FEATURE_COUNT) >= result.cost) {
				i = small_end;
				continue;
			}
			for (; i < small_end; i++) {
				if (!searchable[i]) {
					continue;
				}
				result.frames_checked++;
				const float *fx = x + i * FEATURE_COUNT;
				float cost = 0.0f;
				for (int d = 0; d < FEATURE_COUNT; d++) {
					const float e = p_query[d] - fx[d];
					cost += e * e;
					if (cost >= result.cost) {
						break;
					}
				}
				if (cost < result.cost) {
					result.cost = cost;
					result.frame = i;
				}
			}
		}
	}
	return result;
}

int FellowMotionDatabase::search_trajectory(const float *p_query) const {
	int best = -1;
	float best_cost = FLT_MAX;
	const float *x = features.ptr();
	for (int f = 0; f < frame_count; f++) {
		if (searchable.size() == uint32_t(frame_count) && !searchable[f]) {
			continue;
		}
		float cost = 0.0f;
		for (int d = group_offset[GROUP_TRAJECTORY_POSITION]; d < FEATURE_COUNT; d++) {
			const float e = p_query[d] - x[f * FEATURE_COUNT + d];
			cost += e * e;
		}
		if (cost < best_cost) {
			best_cost = cost;
			best = f;
		}
	}
	return best;
}

Dictionary FellowMotionDatabase::get_info() const {
	Dictionary info;
	info["frames"] = frame_count;
	info["bones"] = bone_names.size();
	info["clips"] = range_starts.size();
	info["sample_rate"] = sample_rate;
	info["seconds"] = frame_count / sample_rate;
	int64_t bytes = int64_t(positions.size()) * sizeof(Vector3) + int64_t(rotations.size()) + int64_t(features.size()) * 4 + int64_t(root_positions.size()) * sizeof(Vector3) + int64_t(root_rotations.size()) * 4 + contacts.size();
	info["bytes"] = bytes;
	int contact_frames[2] = { 0, 0 };
	for (int f = 0; f < frame_count; f++) {
		contact_frames[0] += contacts[f * 2];
		contact_frames[1] += contacts[f * 2 + 1];
	}
	info["contact_fraction_left"] = frame_count ? float(contact_frames[0]) / frame_count : 0.0f;
	info["contact_fraction_right"] = frame_count ? float(contact_frames[1]) / frame_count : 0.0f;
	Dictionary roles;
	for (int r = 0; r < ROLE_MAX; r++) {
		roles[role_option[r]] = bone_roles[r] >= 0 ? Variant(bone_names[bone_roles[r]]) : Variant();
	}
	info["roles"] = roles;
	return info;
}

Dictionary FellowMotionDatabase::get_data() const {
	Dictionary d;
	d["bone_names"] = bone_names;
	d["bone_parents"] = bone_parents;
	d["bone_rest_positions"] = bone_rest_positions;
	d["bone_rest_rotations"] = bone_rest_rotations;
	PackedInt32Array roles;
	for (int r = 0; r < ROLE_MAX; r++) {
		roles.push_back(bone_roles[r]);
	}
	d["bone_roles"] = roles;
	d["sample_rate"] = sample_rate;
	PackedInt32Array traj;
	for (int i = 0; i < TRAJECTORY_POINTS; i++) {
		traj.push_back(trajectory_frames[i]);
	}
	d["trajectory_frames"] = traj;
	d["ignore_range_end"] = ignore_range_end;
	d["frame_count"] = frame_count;
	d["position_slots"] = position_slots;
	d["rotation_slots"] = rotation_slots;
	d["positions"] = positions;
	d["rotations"] = rotations;
	d["root_positions"] = root_positions;
	d["root_rotations"] = root_rotations;
	d["contacts"] = contacts;
	d["range_starts"] = range_starts;
	d["range_stops"] = range_stops;
	d["range_names"] = range_names;
	d["features"] = features;
	d["feature_offset"] = feature_offset;
	d["feature_scale"] = feature_scale;
	d["weights"] = weights;
	return d;
}

void FellowMotionDatabase::set_data(const Dictionary &p_data) {
	if (p_data.is_empty()) {
		return;
	}
	bone_names = p_data.get("bone_names", PackedStringArray());
	bone_parents = p_data.get("bone_parents", PackedInt32Array());
	bone_rest_positions = p_data.get("bone_rest_positions", PackedVector3Array());
	bone_rest_rotations = p_data.get("bone_rest_rotations", PackedFloat32Array());
	const PackedInt32Array roles = p_data.get("bone_roles", PackedInt32Array());
	for (int r = 0; r < ROLE_MAX; r++) {
		bone_roles[r] = r < roles.size() ? roles[r] : -1;
	}
	sample_rate = p_data.get("sample_rate", 60.0);
	const PackedInt32Array traj = p_data.get("trajectory_frames", PackedInt32Array());
	for (int i = 0; i < TRAJECTORY_POINTS && i < traj.size(); i++) {
		trajectory_frames[i] = traj[i];
	}
	ignore_range_end = p_data.get("ignore_range_end", 20);
	frame_count = p_data.get("frame_count", 0);
	position_slots = p_data.get("position_slots", PackedInt32Array());
	rotation_slots = p_data.get("rotation_slots", PackedInt32Array());
	position_slot_count = 0;
	rotation_slot_count = 0;
	for (int b = 0; b < position_slots.size(); b++) {
		position_slot_count = MAX(position_slot_count, position_slots[b] + 1);
	}
	for (int b = 0; b < rotation_slots.size(); b++) {
		rotation_slot_count = MAX(rotation_slot_count, rotation_slots[b] + 1);
	}
	positions = p_data.get("positions", PackedVector3Array());
	rotations = p_data.get("rotations", PackedByteArray());
	root_positions = p_data.get("root_positions", PackedVector3Array());
	root_rotations = p_data.get("root_rotations", PackedFloat32Array());
	contacts = p_data.get("contacts", PackedByteArray());
	range_starts = p_data.get("range_starts", PackedInt32Array());
	range_stops = p_data.get("range_stops", PackedInt32Array());
	range_names = p_data.get("range_names", PackedStringArray());
	features = p_data.get("features", PackedFloat32Array());
	feature_offset = p_data.get("feature_offset", PackedFloat32Array());
	feature_scale = p_data.get("feature_scale", PackedFloat32Array());
	weights = p_data.get("weights", PackedFloat32Array());
	const int bones = bone_names.size();
	ERR_FAIL_COND_MSG(position_slots.size() != bones || rotation_slots.size() != bones || positions.size() != frame_count * position_slot_count || rotations.size() != frame_count * rotation_slot_count * 8 || features.size() != frame_count * FEATURE_COUNT || contacts.size() != frame_count * 2, "FellowMotionDatabase: inconsistent data.");
	_rebuild_acceleration();
	emit_changed();
}

// BVH (Biovision hierarchy): the joints with their offsets and channels, and the frames.
Dictionary FellowMotionDatabase::import_bvh(const String &p_path, float p_scale, bool p_skip_first_frame) {
	Dictionary result;
	Ref<FileAccess> f = FileAccess::open(p_path, FileAccess::READ);
	ERR_FAIL_COND_V_MSG(f.is_null(), result, "FellowMotionDatabase.import_bvh(): cannot open " + p_path);
	const String text = f->get_as_utf8_string();
	const Vector<String> tokens = text.split_spaces();
	int pos = 0;
	auto next = [&]() -> String {
		return pos < tokens.size() ? tokens[pos++] : String();
	};

	struct Joint {
		String name;
		int parent = -1;
		Vector3 offset;
		LocalVector<int> channels; // 0-2 position x/y/z, 3-5 rotation x/y/z
		int channel_start = 0;
	};
	LocalVector<Joint> joints;
	LocalVector<int> stack;
	int channel_total = 0;
	ERR_FAIL_COND_V_MSG(next() != "HIERARCHY", result, "FellowMotionDatabase.import_bvh(): not a BVH file: " + p_path);
	while (pos < tokens.size()) {
		const String t = next();
		if (t == "ROOT" || t == "JOINT" || t == "End") {
			Joint j;
			const String joint_name = next();
			j.name = t == "End" ? (stack.is_empty() ? String("End") : joints[stack[stack.size() - 1]].name + "_end") : joint_name;
			j.parent = stack.is_empty() ? -1 : stack[stack.size() - 1];
			ERR_FAIL_COND_V(next() != "{", result);
			joints.push_back(j);
			stack.push_back(joints.size() - 1);
		} else if (t == "OFFSET") {
			ERR_FAIL_COND_V(stack.is_empty(), result);
			Joint &j = joints[stack[stack.size() - 1]];
			j.offset.x = next().to_float() * p_scale;
			j.offset.y = next().to_float() * p_scale;
			j.offset.z = next().to_float() * p_scale;
		} else if (t == "CHANNELS") {
			ERR_FAIL_COND_V(stack.is_empty(), result);
			Joint &j = joints[stack[stack.size() - 1]];
			const int n = next().to_int();
			j.channel_start = channel_total;
			for (int c = 0; c < n; c++) {
				const String ch = next().to_lower();
				const int axis = ch[0] - 'x';
				j.channels.push_back(ch.contains("position") ? axis : 3 + axis);
			}
			channel_total += n;
		} else if (t == "}") {
			ERR_FAIL_COND_V(stack.is_empty(), result);
			stack.resize(stack.size() - 1);
		} else if (t == "MOTION") {
			break;
		}
	}
	ERR_FAIL_COND_V(next() != "Frames:", result);
	const int frames = next().to_int();
	ERR_FAIL_COND_V(next() != "Frame", result);
	ERR_FAIL_COND_V(next() != "Time:", result);
	const double frame_time = next().to_float();
	ERR_FAIL_COND_V(frames <= 0 || frame_time <= 0.0, result);
	const int first = p_skip_first_frame ? 1 : 0;
	ERR_FAIL_COND_V(frames - first < 2, result);
	ERR_FAIL_COND_V_MSG(tokens.size() - pos < frames * channel_total, result, "FellowMotionDatabase.import_bvh(): truncated motion in " + p_path);

	Ref<Animation> anim;
	anim.instantiate();
	anim->set_name(p_path.get_file().get_basename());
	anim->set_length((frames - first - 1) * frame_time);
	PackedStringArray names;
	PackedInt32Array parents;
	PackedVector3Array offsets;
	for (uint32_t i = 0; i < joints.size(); i++) {
		names.push_back(joints[i].name);
		parents.push_back(joints[i].parent);
		offsets.push_back(joints[i].offset);
	}
	LocalVector<int> rotation_tracks;
	LocalVector<int> position_tracks;
	rotation_tracks.resize(joints.size());
	position_tracks.resize(joints.size());
	for (uint32_t i = 0; i < joints.size(); i++) {
		rotation_tracks[i] = -1;
		position_tracks[i] = -1;
		bool has_rot = false;
		bool has_pos = false;
		for (int c : joints[i].channels) {
			has_rot = has_rot || c >= 3;
			has_pos = has_pos || c < 3;
		}
		const NodePath path(String("Skeleton3D:") + joints[i].name);
		if (has_pos) {
			position_tracks[i] = anim->add_track(Animation::TYPE_POSITION_3D);
			anim->track_set_path(position_tracks[i], path);
		}
		if (has_rot) {
			rotation_tracks[i] = anim->add_track(Animation::TYPE_ROTATION_3D);
			anim->track_set_path(rotation_tracks[i], path);
		}
	}
	const Vector3 axes[3] = { Vector3(1, 0, 0), Vector3(0, 1, 0), Vector3(0, 0, 1) };
	for (int fr = 0; fr < frames; fr++) {
		const int base = pos + fr * channel_total;
		if (fr < first) {
			continue;
		}
		const double time = (fr - first) * frame_time;
		for (uint32_t i = 0; i < joints.size(); i++) {
			const Joint &j = joints[i];
			if (j.channels.is_empty()) {
				continue;
			}
			Vector3 p = j.offset;
			Quaternion q;
			for (uint32_t c = 0; c < j.channels.size(); c++) {
				const float v = tokens[base + j.channel_start + c].to_float();
				const int ch = j.channels[c];
				if (ch < 3) {
					p[ch] = v * p_scale;
				} else {
					q = q * Quaternion(axes[ch - 3], Math::deg_to_rad(v));
				}
			}
			if (position_tracks[i] >= 0) {
				anim->position_track_insert_key(position_tracks[i], time, p);
			}
			if (rotation_tracks[i] >= 0) {
				anim->rotation_track_insert_key(rotation_tracks[i], time, q.normalized());
			}
		}
	}
	result["animation"] = anim;
	result["bone_names"] = names;
	result["bone_parents"] = parents;
	result["bone_offsets"] = offsets;
	result["frame_time"] = frame_time;
	result["frame_count"] = frames - first;
	return result;
}

Node *FellowMotionDatabase::create_skeleton(const Dictionary &p_bvh) {
	const PackedStringArray names = p_bvh.get("bone_names", PackedStringArray());
	const PackedInt32Array parents = p_bvh.get("bone_parents", PackedInt32Array());
	const PackedVector3Array offsets = p_bvh.get("bone_offsets", PackedVector3Array());
	ERR_FAIL_COND_V(names.size() == 0 || parents.size() != names.size() || offsets.size() != names.size(), nullptr);
	Skeleton3D *skeleton = memnew(Skeleton3D);
	skeleton->set_name("Skeleton3D");
	for (int i = 0; i < names.size(); i++) {
		skeleton->add_bone(names[i]);
	}
	for (int i = 0; i < names.size(); i++) {
		skeleton->set_bone_parent(i, parents[i]);
		skeleton->set_bone_rest(i, Transform3D(Basis(), offsets[i]));
		skeleton->set_bone_pose_position(i, offsets[i]);
	}
	return skeleton;
}

void FellowMotionDatabase::_bind_methods() {
	ClassDB::bind_method(D_METHOD("build", "skeleton", "animations", "options"), &FellowMotionDatabase::build, DEFVAL(Dictionary()));
	ClassDB::bind_static_method("FellowMotionDatabase", D_METHOD("import_bvh", "path", "scale", "skip_first_frame"), &FellowMotionDatabase::import_bvh, DEFVAL(1.0), DEFVAL(false));
	ClassDB::bind_static_method("FellowMotionDatabase", D_METHOD("create_skeleton", "bvh"), &FellowMotionDatabase::create_skeleton);
	ClassDB::bind_method(D_METHOD("get_info"), &FellowMotionDatabase::get_info);
	ClassDB::bind_method(D_METHOD("get_frame_count"), &FellowMotionDatabase::get_frame_count);
	ClassDB::bind_method(D_METHOD("set_data", "data"), &FellowMotionDatabase::set_data);
	ClassDB::bind_method(D_METHOD("get_data"), &FellowMotionDatabase::get_data);
	ADD_PROPERTY(PropertyInfo(Variant::DICTIONARY, "data", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_STORAGE), "set_data", "get_data");

	BIND_ENUM_CONSTANT(ROLE_HIPS);
	BIND_ENUM_CONSTANT(ROLE_LEFT_UPPER_LEG);
	BIND_ENUM_CONSTANT(ROLE_LEFT_LOWER_LEG);
	BIND_ENUM_CONSTANT(ROLE_LEFT_FOOT);
	BIND_ENUM_CONSTANT(ROLE_LEFT_TOE);
	BIND_ENUM_CONSTANT(ROLE_RIGHT_UPPER_LEG);
	BIND_ENUM_CONSTANT(ROLE_RIGHT_LOWER_LEG);
	BIND_ENUM_CONSTANT(ROLE_RIGHT_FOOT);
	BIND_ENUM_CONSTANT(ROLE_RIGHT_TOE);
	BIND_ENUM_CONSTANT(ROLE_MAX);
}
