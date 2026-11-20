/**************************************************************************/
/*  fellow_motion_matcher_3d.h                                            */
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

#include "fellow_motion_database.h"

#include "core/templates/safe_refcount.h"
#include "scene/3d/skeleton_modifier_3d.h"

// FellowEngine (milestone 48, decision 0149): motion matching on a Skeleton3D. A simulated character follows the
// desired velocity and facing (critically damped springs); its predicted trajectory and the current pose's features
// query the database every search interval, and the best frame is blended in by inertialization. The character root
// moves with the animation's root motion, pulled towards the simulation; feet in contact are locked and reached with
// two-bone IK. Farther from the camera it searches less often, skips the IK, then updates at a quarter of the rate.
class FellowMotionMatcher3D : public SkeletonModifier3D {
	GDCLASS(FellowMotionMatcher3D, SkeletonModifier3D);

	struct Foot {
		bool locked = false;
		Vector3 lock_position; // World.
		Vector3 offset; // World: the target minus the animated foot.
		Vector3 offset_velocity;
		Vector3 last_animated;
		Vector3 last_output;
		Vector3 last_toe; // World, where the toe was shown (the ground under the foot: its ankle's and its toe's).
		bool last_contact = false;
		bool has_last = false;
	};

	// Updated from the worker threads of advance_group().
	struct GlobalStats {
		SafeNumeric<uint64_t> updates;
		SafeNumeric<uint64_t> update_usec;
		SafeNumeric<uint64_t> update_usec_max;
		SafeNumeric<uint64_t> searches;
		SafeNumeric<uint64_t> search_usec;
		SafeNumeric<uint64_t> search_usec_max;
		SafeNumeric<uint64_t> frames_checked;
		SafeNumeric<uint64_t> transitions;
		SafeNumeric<uint64_t> lod_updates[3];
		SafeNumeric<uint32_t> search_histogram[1000]; // 5 µs bins.
		SafeNumeric<uint32_t> update_histogram[1000];
	};
	static GlobalStats global_stats;

	Ref<FellowMotionDatabase> database;
	NodePath root_path;
	Vector3 desired_velocity;
	Vector3 desired_facing;
	float search_interval = 0.1f;
	float inertialization_halflife = 0.1f;
	float velocity_halflife = 0.27f;
	float rotation_halflife = 0.27f;
	float adjustment_halflife = 0.1f;
	float adjustment_velocity_ratio = 0.5f;
	float clamp_distance = 0.15f;
	float clamp_angle = Math::PI * 0.5f;
	float unlock_radius = 0.2f;
	float force_search_velocity_change = 0.1f; // m/s of desired velocity that triggers a search.
	bool inertialization = true;
	bool synchronization = true;
	bool foot_locking = true;
	bool foot_ik = true;
	bool auto_update = true;
	PackedFloat32Array lod_distances;
	// Milestone 164 (decision 0305): the AAA layer over the matched animation.
	// Transitions: the playing pose continues unless a frame is cheaper by `transition_cost_bias`; frames of the
	// playing clip within `transition_jump_threshold` of it are not candidates (a jump there is a stutter, not a
	// transition).
	float transition_cost_bias = 2.0f;
	float transition_jump_threshold = 0.25f; // Seconds.
	// Stride warping: each foot's place along the way the character goes scaled about the hips by its speed over the
	// animation's (within the limits), the hips lowered where a leg must reach farther.
	bool stride_warping = true;
	Vector2 stride_warping_limits = Vector2(0.75f, 1.35f);
	// Lean: the body turned about the ground under the hips into the acceleration the character has and its
	// animation has not (a turn sharper or a start faster than the clip's), as far as balance asks (atan(a / g)).
	float lean_amount = 1.0f;
	// Orientation warping (milestone 165): the hips turned about the vertical towards the way the character goes from
	// the way its clip goes (a strafe between the database's, a walk at an angle to the facing), the spine turned
	// back over its bones so that the chest keeps the facing; within the largest angle.
	bool orientation_warping = true;
	float orientation_warping_max_angle = Math::deg_to_rad(90.0f);
	LocalVector<int> spine_chain;
	const void *spine_chain_of = nullptr;
	void _find_spine_chain();
	double orientation_sum = 0.0;
	double orientation_time = 0.0;
	float lean_max_angle = Math::deg_to_rad(20.0f);
	struct Warp {
		bool has_prev = false;
		Vector3 prev_char_position;
		Vector3 char_velocity; // Smoothed, world.
		Vector3 anim_velocity; // The animation's root motion, smoothed, world (turned by orientation warping).
		Vector3 clip_velocity; // The clip's own root motion, smoothed, world.
		Vector3 char_acceleration;
		Vector3 anim_acceleration;
		Vector3 travel; // Character space, unit.
		float ratio = 1.0f;
		float ratio_velocity = 0.0f;
		float hips_drop = 0.0f;
		float hips_drop_velocity = 0.0f;
		float lean[2] = {}; // Radians: about the character's forward axis (to its +X side) and its side axis (forwards).
		float lean_velocity[2] = {};
		float orientation = 0.0f; // Radians about the vertical: from the clip's way to the character's.
		float orientation_velocity = 0.0f;
	};
	Warp warp;
	void _update_warp(float p_dt, const Vector3 &p_anim_velocity, const Vector3 &p_clip_velocity);
	// The body's lean beside balance's (statistics): both smoothed over a stride.
	float stat_body_lean = 0.0f;
	float stat_physics_lean = 0.0f;
	bool stat_lean_init = false;
	double lean_samples = 0.0;
	double lean_error_sq = 0.0;
	double lean_sum_xy = 0.0;
	double lean_sum_xx = 0.0;
	double lean_sum_x = 0.0;
	double lean_sum_y = 0.0;
	double stride_ratio_sum = 0.0;
	double stride_ratio_samples = 0.0;

	// Tags (milestone 56): the clips searched — every required tag, none of the excluded ones.
	PackedStringArray tags_required;
	PackedStringArray tags_excluded;
	uint32_t tags_required_mask = 0;
	uint32_t tags_excluded_mask = 0;
	bool tags_dirty = true;
	void _update_tag_masks();
	// Ground adaptation (milestone 56): rays down against the physics world under the root and each foot, cast on the
	// main thread before an update (the update itself touches nothing outside the matcher).
	bool ground_adaptation = false;
	uint32_t ground_collision_mask = 1;
	float ground_probe_height = 0.6f; // Metres above and below the character's ground.
	float ground_halflife = 0.05f;
	bool ground_alignment = true;
	bool ground_foot_placement = true; // false: only the root follows the ground
	struct GroundProbe {
		bool hit = false;
		float y = 0.0f;
		Vector3 normal = Vector3(0, 1, 0);
	};
	GroundProbe ground_root;
	GroundProbe ground_feet[2];
	bool ground_root_init = false;
	float ground_root_y = 0.0f;
	float ground_root_velocity = 0.0f;
	float ground_foot_offset[2] = {};
	float ground_foot_offset_velocity[2] = {};
	float ground_align_weight[2] = {};
	float ground_align_weight_velocity[2] = {};
	float hips_drop = 0.0f;
	float hips_drop_velocity = 0.0f;
	float hips_drop_min = 0.0f;
	void _probe_ground(); // Main thread.

	// State.
	bool started = false;
	double frame_time = 0.0; // In database frames.
	int range = 0;
	float search_timer = 0.0f;
	bool force_search = true;
	Vector3 searched_velocity;
	int lod = 0;
	int lod_skip = 0;
	float lod_accumulated = 0.0f;
	Vector3 sim_position;
	Vector3 sim_velocity;
	Vector3 sim_acceleration;
	Quaternion sim_rotation;
	Vector3 sim_angular_velocity;
	Vector3 char_position;
	Quaternion char_rotation;
	Vector3 char_velocity; // Root motion, world.
	float char_yaw_rate = 0.0f;

	LocalVector<Vector3> anim_positions;
	LocalVector<Quaternion> anim_rotations;
	LocalVector<Vector3> anim_velocities;
	LocalVector<Vector3> anim_angular_velocities;
	LocalVector<Vector3> offset_positions;
	LocalVector<Vector3> offset_velocities;
	LocalVector<Quaternion> offset_rotations;
	LocalVector<Vector3> offset_angular_velocities;
	Vector3 offset_root_velocity; // Character space.
	Vector3 offset_root_velocity_rate;
	float offset_yaw_rate = 0.0f;
	float offset_yaw_rate_rate = 0.0f;
	LocalVector<Vector3> out_positions;
	LocalVector<Quaternion> out_rotations;
	LocalVector<int> skeleton_bones; // Database bone -> skeleton bone.
	LocalVector<Transform3D> parent_rest_globals; // The skeleton-space rest of each database bone's parent chain.
	bool pose_ready = false;
	Foot feet[2];

	// The last search (for tools and debugging).
	float last_query[FellowMotionDatabase::FEATURE_COUNT] = {};
	int last_result_frame = -1;
	float last_result_cost = 0.0f;
	int last_current_frame = -1;

	// Statistics.
	uint64_t searches = 0;
	uint64_t transitions = 0;
	uint64_t search_usec = 0;
	uint64_t search_usec_max = 0;
	double foot_slide_distance = 0.0;
	double foot_slide_locked = 0.0; // Of which while locked (the IK not reaching).
	double foot_contact_time = 0.0;
	double trajectory_error_sum = 0.0;
	// Popping: per frame, the largest second difference of the hips' and the legs' joint positions in the character's
	// space (an acceleration times dt², in millimetres), as a histogram of 0.5 mm bins.
	LocalVector<Vector3> joint_history[2];
	int joint_history_count = 0;
	uint32_t pop_histogram[400] = {};
	uint64_t pop_frames = 0;
	float pop_max = 0.0f;
	int pop_max_bone = -1;
	bool pop_max_after_transition = false;
	bool transitioned_this_frame = false;
	void _measure_pop();
	double trajectory_time = 0.0;

	void _start();
	void _update(float p_dt, int p_lod); // Touches nothing outside the matcher: safe on a worker thread.
	void _apply_root();
	void _group_update(uint32_t p_index, const LocalVector<FellowMotionMatcher3D *> *p_matchers);
	float group_dt = 0.0f;
	void _map_bones();
	void _sample(double p_frame_time, LocalVector<Vector3> &r_positions, LocalVector<Quaternion> &r_rotations, LocalVector<Vector3> *r_velocities, LocalVector<Vector3> *r_angular_velocities, Vector3 *r_root_velocity, float *r_yaw_rate) const;
	void _transition(int p_frame);
	void _build_query(float *r_query) const;
	void _solve_feet(float p_dt);
	void _forward_kinematics(LocalVector<Vector3> &r_gp, LocalVector<Quaternion> &r_gq) const;
	Node3D *_get_root() const;
	int _compute_lod() const;

protected:
	void _notification(int p_what);
	static void _bind_methods();
	virtual void _process_modification(double p_delta) override;
	virtual void _skeleton_changed(Skeleton3D *p_old, Skeleton3D *p_new) override;

public:
	virtual bool has_process() const override { return true; }
	PackedStringArray get_configuration_warnings() const override;

	void advance(double p_delta);
	// Several matchers' updates on the WorkerThreadPool (their roots and skeletons are then set on this thread).
	static void advance_group(const TypedArray<FellowMotionMatcher3D> &p_matchers, double p_delta);
	void reset();
	void teleport(const Vector3 &p_position, const Vector3 &p_facing);

	void set_database(const Ref<FellowMotionDatabase> &p_database);
	Ref<FellowMotionDatabase> get_database() const { return database; }
	void set_root_path(const NodePath &p_path) {
		root_path = p_path;
		update_configuration_warnings();
	}
	NodePath get_root_path() const { return root_path; }
	void set_desired_velocity(const Vector3 &p_velocity) { desired_velocity = Vector3(p_velocity.x, 0.0f, p_velocity.z); }
	Vector3 get_desired_velocity() const { return desired_velocity; }
	void set_desired_facing(const Vector3 &p_facing) { desired_facing = Vector3(p_facing.x, 0.0f, p_facing.z); }
	Vector3 get_desired_facing() const { return desired_facing; }
	void set_search_interval(float p_seconds) { search_interval = MAX(p_seconds, 0.0f); }
	float get_search_interval() const { return search_interval; }
	void set_inertialization_halflife(float p_seconds) { inertialization_halflife = MAX(p_seconds, 0.0f); }
	float get_inertialization_halflife() const { return inertialization_halflife; }
	void set_velocity_halflife(float p_seconds) { velocity_halflife = MAX(p_seconds, 0.001f); }
	float get_velocity_halflife() const { return velocity_halflife; }
	void set_rotation_halflife(float p_seconds) { rotation_halflife = MAX(p_seconds, 0.001f); }
	float get_rotation_halflife() const { return rotation_halflife; }
	void set_adjustment_halflife(float p_seconds) { adjustment_halflife = MAX(p_seconds, 0.001f); }
	float get_adjustment_halflife() const { return adjustment_halflife; }
	void set_clamp_distance(float p_distance) { clamp_distance = MAX(p_distance, 0.0f); }
	float get_clamp_distance() const { return clamp_distance; }
	void set_unlock_radius(float p_radius) { unlock_radius = MAX(p_radius, 0.0f); }
	float get_unlock_radius() const { return unlock_radius; }
	void set_inertialization(bool p_enable) { inertialization = p_enable; }
	bool is_inertialization() const { return inertialization; }
	void set_synchronization(bool p_enable) { synchronization = p_enable; }
	bool is_synchronization() const { return synchronization; }
	void set_foot_locking(bool p_enable) { foot_locking = p_enable; }
	bool is_foot_locking() const { return foot_locking; }
	void set_foot_ik(bool p_enable) { foot_ik = p_enable; }
	bool is_foot_ik() const { return foot_ik; }
	void set_auto_update(bool p_enable);
	bool is_auto_update() const { return auto_update; }
	void set_tags_required(const PackedStringArray &p_tags) {
		tags_required = p_tags;
		tags_dirty = true;
	}
	PackedStringArray get_tags_required() const { return tags_required; }
	void set_tags_excluded(const PackedStringArray &p_tags) {
		tags_excluded = p_tags;
		tags_dirty = true;
	}
	PackedStringArray get_tags_excluded() const { return tags_excluded; }
	void set_ground_adaptation(bool p_enable);
	bool is_ground_adaptation() const { return ground_adaptation; }
	void set_ground_collision_mask(uint32_t p_mask) { ground_collision_mask = p_mask; }
	uint32_t get_ground_collision_mask() const { return ground_collision_mask; }
	void set_ground_probe_height(float p_height) { ground_probe_height = MAX(p_height, 0.01f); }
	float get_ground_probe_height() const { return ground_probe_height; }
	void set_ground_halflife(float p_seconds) { ground_halflife = MAX(p_seconds, 0.001f); }
	float get_ground_halflife() const { return ground_halflife; }
	void set_ground_alignment(bool p_enable) { ground_alignment = p_enable; }
	bool is_ground_alignment() const { return ground_alignment; }
	void set_ground_foot_placement(bool p_enable) { ground_foot_placement = p_enable; }
	bool is_ground_foot_placement() const { return ground_foot_placement; }
	void set_transition_cost_bias(float p_bias) { transition_cost_bias = MAX(p_bias, 0.0f); }
	float get_transition_cost_bias() const { return transition_cost_bias; }
	void set_transition_jump_threshold(float p_seconds) { transition_jump_threshold = MAX(p_seconds, 0.0f); }
	float get_transition_jump_threshold() const { return transition_jump_threshold; }
	void set_stride_warping(bool p_enable) { stride_warping = p_enable; }
	bool is_stride_warping() const { return stride_warping; }
	void set_stride_warping_limits(const Vector2 &p_limits) { stride_warping_limits = Vector2(CLAMP(p_limits.x, 0.1f, 1.0f), MAX(p_limits.y, 1.0f)); }
	Vector2 get_stride_warping_limits() const { return stride_warping_limits; }
	void set_lean_amount(float p_amount) { lean_amount = CLAMP(p_amount, 0.0f, 2.0f); }
	float get_lean_amount() const { return lean_amount; }
	void set_lean_max_angle(float p_radians) { lean_max_angle = CLAMP(p_radians, 0.0f, Math::deg_to_rad(60.0f)); }
	float get_lean_max_angle() const { return lean_max_angle; }
	void set_orientation_warping(bool p_enable) { orientation_warping = p_enable; }
	bool is_orientation_warping() const { return orientation_warping; }
	void set_orientation_warping_max_angle(float p_radians) { orientation_warping_max_angle = CLAMP(p_radians, 0.0f, Math::deg_to_rad(150.0f)); }
	float get_orientation_warping_max_angle() const { return orientation_warping_max_angle; }
	void set_lod_distances(const PackedFloat32Array &p_distances) { lod_distances = p_distances; }
	PackedFloat32Array get_lod_distances() const { return lod_distances; }

	Vector3 get_simulation_position() const { return sim_position; }
	Vector3 get_simulation_velocity() const { return sim_velocity; }
	Vector3 get_character_position() const { return char_position; }
	int get_current_frame() const;
	String get_current_clip() const;
	int get_lod() const { return lod; }
	PackedVector3Array get_foot_positions() const;
	Dictionary get_stats() const;
	Dictionary get_last_search() const;
	void reset_stats();
	static Dictionary get_global_stats();
	static void reset_global_stats();
};
