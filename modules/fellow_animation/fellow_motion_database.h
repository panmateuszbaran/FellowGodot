/**************************************************************************/
/*  fellow_motion_database.h                                              */
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

#pragma once

#include "core/io/resource.h"
#include "core/templates/local_vector.h"
#include "core/variant/typed_array.h"
#include "scene/resources/animation.h"

class Skeleton3D;

// FellowEngine (milestone 48, decision 0149): a motion-matching database — every frame of a set of animation clips
// sampled at a fixed rate, the pose of each bone, the "simulation" root on the ground under the hips with the
// character's facing, the foot contacts, and per frame a normalised feature vector (feet positions and velocities,
// hips velocity, the root's trajectory 1/3, 2/3 and 1 s ahead) searched by FellowMotionMatcher3D.
class FellowMotionDatabase : public Resource {
	GDCLASS(FellowMotionDatabase, Resource);

public:
	enum {
		FEATURE_COUNT = 27,
		TRAJECTORY_POINTS = 3,
		MAX_IK_LEGS = 2,
	};

	enum BoneRole {
		ROLE_HIPS,
		ROLE_LEFT_UPPER_LEG,
		ROLE_LEFT_LOWER_LEG,
		ROLE_LEFT_FOOT,
		ROLE_LEFT_TOE,
		ROLE_RIGHT_UPPER_LEG,
		ROLE_RIGHT_LOWER_LEG,
		ROLE_RIGHT_FOOT,
		ROLE_RIGHT_TOE,
		ROLE_MAX,
	};

	struct SearchResult {
		int frame = -1;
		float cost = 0.0f;
		uint32_t frames_checked = 0;
	};

private:
	// Bones (the skeleton's order at build time).
	PackedStringArray bone_names;
	PackedInt32Array bone_parents;
	PackedVector3Array bone_rest_positions;
	PackedFloat32Array bone_rest_rotations; // xyzw
	int bone_roles[ROLE_MAX];
	float sample_rate = 60.0f;
	int trajectory_frames[TRAJECTORY_POINTS] = { 20, 40, 60 };

	// Frames: the bones' local poses, the hips relative to the root. Only bones that move are stored (a position
	// slot for a bone whose position changes, a rotation slot for one whose rotation does); rotations in 16 bits.
	int frame_count = 0;
	PackedInt32Array position_slots; // Per bone: its slot, or -1 (the rest).
	PackedInt32Array rotation_slots;
	int position_slot_count = 0;
	int rotation_slot_count = 0;
	PackedVector3Array positions; // frame_count * position_slot_count
	PackedByteArray rotations; // frame_count * rotation_slot_count * 4 int16
	PackedVector3Array root_positions; // in the clip's space
	PackedFloat32Array root_rotations; // yaw, xyzw
	PackedByteArray contacts; // frame_count * 2 (left, right)

	// Clips: [start, stop) frame ranges.
	PackedInt32Array range_starts;
	PackedInt32Array range_stops;
	PackedStringArray range_names;

	// Features, normalised and weighted.
	PackedFloat32Array features; // frame_count * FEATURE_COUNT
	PackedFloat32Array feature_offset; // FEATURE_COUNT
	PackedFloat32Array feature_scale; // FEATURE_COUNT
	PackedFloat32Array weights; // 5 groups: foot position, foot velocity, hips velocity, trajectory position, direction
	int ignore_range_end = 20;

	// Search acceleration (built, not stored): boxes over 16 and 64 frames.
	LocalVector<float> bound_small_min;
	LocalVector<float> bound_small_max;
	LocalVector<float> bound_large_min;
	LocalVector<float> bound_large_max;
	LocalVector<uint8_t> searchable;

	void _rebuild_acceleration();
	static void _bind_methods();

public:
	Error build(Object *p_skeleton, const TypedArray<Animation> &p_animations, const Dictionary &p_options);
	static Dictionary import_bvh(const String &p_path, float p_scale, bool p_skip_first_frame);
	static Node *create_skeleton(const Dictionary &p_bvh);

	// Search: the frame whose features are nearest the query (normalised with normalize_query), at most p_best_cost.
	SearchResult search(const float *p_query, int p_current_frame, bool p_include_current) const;
	// Brute force over the trajectory dimensions only (a start without a pose).
	int search_trajectory(const float *p_query) const;
	void normalize_query(float *r_query) const;
	const float *get_features(int p_frame) const { return features.ptr() + p_frame * FEATURE_COUNT; }
	float get_feature_scale(int p_dim) const { return feature_scale[p_dim]; }
	float get_feature_offset(int p_dim) const { return feature_offset[p_dim]; }

	int get_frame_count() const { return frame_count; }
	int get_bone_count() const { return bone_names.size(); }
	float get_sample_rate() const { return sample_rate; }
	int get_trajectory_frame(int p_point) const { return trajectory_frames[p_point]; }
	int get_bone_role(BoneRole p_role) const { return bone_roles[p_role]; }
	int get_bone_parent(int p_bone) const { return bone_parents[p_bone]; }
	String get_bone_name(int p_bone) const { return bone_names[p_bone]; }
	Vector3 get_bone_rest_position(int p_bone) const { return bone_rest_positions[p_bone]; }
	Quaternion get_bone_rest_rotation(int p_bone) const;
	_FORCE_INLINE_ Vector3 get_bone_position(int p_frame, int p_bone) const {
		const int slot = position_slots[p_bone];
		return slot < 0 ? bone_rest_positions[p_bone] : positions[p_frame * position_slot_count + slot];
	}
	_FORCE_INLINE_ Quaternion get_bone_rotation(int p_frame, int p_bone) const {
		const int slot = rotation_slots[p_bone];
		if (slot < 0) {
			return get_bone_rest_rotation(p_bone);
		}
		const int16_t *q = reinterpret_cast<const int16_t *>(rotations.ptr()) + (p_frame * rotation_slot_count + slot) * 4;
		return Quaternion(q[0], q[1], q[2], q[3]).normalized();
	}
	Vector3 get_root_position(int p_frame) const { return root_positions[p_frame]; }
	Quaternion get_root_rotation(int p_frame) const;
	bool get_contact(int p_frame, int p_side) const { return contacts[p_frame * 2 + p_side] != 0; }
	int get_range_of(int p_frame) const;
	int get_range_start(int p_range) const { return range_starts[p_range]; }
	int get_range_stop(int p_range) const { return range_stops[p_range]; }
	String get_range_name(int p_range) const { return range_names[p_range]; }
	int get_range_count() const { return range_starts.size(); }
	bool is_valid() const { return frame_count > 0 && features.size() == frame_count * FEATURE_COUNT; }
	Dictionary get_info() const;

	// Storage.
	void set_data(const Dictionary &p_data);
	Dictionary get_data() const;

	FellowMotionDatabase();
};

VARIANT_ENUM_CAST(FellowMotionDatabase::BoneRole);
