/**************************************************************************/
/*  fellow_physical_animation_3d.h                                        */
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

#include "core/templates/local_vector.h"
#include "core/variant/typed_array.h"
#include "scene/3d/skeleton_modifier_3d.h"

class PhysicalBone3D;
class PhysicalBoneSimulator3D;
class FellowMotionMatcher3D;
class FellowMotionDatabase;

// FellowEngine (milestone 166, decision 0307): physical animation. The skeleton's bones as rigid bodies (built from
// its rest pose, on the stock PhysicalBoneSimulator3D), each driven every physics step towards where the animation
// has its bone — velocity feed-forward plus a share of the error — so that the body moves as the animation does until
// something pushes it: a hit knocks the bodies, weakens the struck bone and its neighbours for a moment, and they
// come back; at strength 0 it is a ragdoll. Placed after the modifiers that animate the skeleton (motion matching):
// it reads their pose, the simulator it builds writes the bodies' back.
class FellowPhysicalAnimation3D : public SkeletonModifier3D {
	GDCLASS(FellowPhysicalAnimation3D, SkeletonModifier3D);

public:
	// Milestone 183 (decision 0325): balance, falls and getting up.
	enum State {
		STATE_STANDING,
		STATE_STAGGERING,
		STATE_FALLING,
		STATE_DOWN,
		STATE_GETTING_UP,
	};
	enum Face {
		FACE_UP, // on its back
		FACE_DOWN,
		FACE_SIDE,
	};

	struct Body {
		int bone = -1;
		PhysicalBone3D *node = nullptr;
		Transform3D offset; // The body in its bone's space.
		Transform3D target; // World, from the animation.
		Transform3D previous_target;
		bool has_target = false;
		bool has_previous = false;
		float weakness = 0.0f; // 0..1: a hit's.
		int parent = -1; // The nearest body up the bones.
		float radius = 0.0f;
		float length = 0.0f;
		// Milestone 185 (decision 0327): the joint to its parent as a ragdoll's, from where it sits in the body (with
		// anatomical_limits): a ball (swing about two axes, twist about the bone) or a hinge (a knee, an elbow: its
		// flexion about `hinge_axis`, between flex_min and flex_max from straight).
		int8_t joint_kind = 0; // JOINT_BALL, JOINT_HINGE
		float swing = 0.0f; // radians
		float twist = 0.0f;
		float flex_min = 0.0f;
		float flex_max = 0.0f;
		Vector3 hinge_axis; // the hinge's axis in its parent's bone's space (flexion positive about it)
		Vector3 neutral; // a hip's: the thigh's way at rest, in its parent's bone's space (flexion from it)
		float friction = 0.0f; // N·m: the limp joint's friction (a motor held at rest, its torque limited)
		int8_t part = 0; // JOINT_PART_*: what it is (statistics)
	};
	enum {
		JOINT_BALL = 0,
		JOINT_HINGE = 1,
		JOINT_HIP = 2, // a ball whose flexion is limited from the thigh's way at rest, not about the pose it went limp in
	};
	enum {
		JOINT_PART_OTHER = 0,
		JOINT_PART_SPINE,
		JOINT_PART_NECK,
		JOINT_PART_GIRDLE, // a hip's or a shoulder's own bone (the pelvis's, a collarbone)
		JOINT_PART_HIP,
		JOINT_PART_KNEE,
		JOINT_PART_ANKLE,
		JOINT_PART_SHOULDER,
		JOINT_PART_ELBOW,
		JOINT_PART_WRIST,
	};

private:
	bool simulate = true;
	float strength = 1.0f;
	float position_gain = 0.5f;
	float rotation_gain = 0.5f;
	float recovery_halflife = 0.3f;
	float hit_weakening = 0.85f;
	float total_mass = 70.0f;
	float min_bone_length = 0.08f;
	uint32_t collision_layer = 1u << 15;
	uint32_t collision_mask = 1u;
	float swing_limit = Math::deg_to_rad(50.0f);
	float twist_limit = Math::deg_to_rad(30.0f);
	bool anatomical_limits = true; // milestone 185 (decision 0327): each joint limp as its part of the body bends

	// Milestone 183 (decision 0325): balance — a hit's capture point (where the centre of mass must be stood over to
	// stop: its offset plus its velocity over sqrt(g / h)) beyond `step_threshold` staggers the character (its motion
	// matcher pushed there), beyond `fall_threshold` makes it fall (milestone 184, decision 0326: its legs give way, its
	// knees bending over its feet, as it leans about them and its arms reach towards the ground) until it goes limp with
	// its hips near the ground; lying still, it gets up with the motion matcher's database's clip (tagged `get_up_tag`)
	// whose first pose fits the body's best, blended from the ragdoll's pose, and walks on.
	bool balance = false;
	NodePath motion_matcher_path;
	float step_threshold = 0.12f; // m
	float fall_threshold = 0.7f; // m
	float topple_angle = Math::deg_to_rad(60.0f); // the fall's lean where the body goes limp (if its hips are still up)
	StringName get_up_tag = "get_up";
	bool auto_get_up = true;
	float get_up_delay = 1.0f; // s lying still
	float get_up_blend = 0.5f; // s
	float handover_blend = 0.4f; // s: from the clip's last pose to the motion matcher's

	State state = STATE_STANDING;
	float state_time = 0.0f;
	float state_strength = 1.0f; // the state's share of the strength
	float still_time = 0.0f;
	float stagger_end = 0.0f; // s: the stagger's push
	int landed_face = FACE_UP;
	// The fall: the pose when it began. Milestone 184 (decision 0326): the legs give way — the hips sink (their speed
	// rising to `FALL_SINK_SPEED` after a reaction's time) and lean about the feet as an inverted pendulum, the upper
	// body with them; the knees bent by IK over the feet, which stay where they stand; the arms reaching towards the
	// ground it falls to. Without the legs' roles (no motion matcher's database), milestone 183's stiff fall: the pose
	// turned about the feet.
	LocalVector<Transform3D> fall_targets;
	LocalVector<int8_t> fall_part; // per body: FALL_UPPER, FALL_LEG + side, FALL_FOOT + side, FALL_ARM + arm
	int fall_leg[2][3] = { { -1, -1, -1 }, { -1, -1, -1 } }; // per side: the thigh's, the shin's and the foot's body
	int fall_arm[2] = { -1, -1 }; // per arm: its upper arm's body (it and the bodies below reach)
	Vector3 fall_hand[2]; // per arm: its far end when the fall began (world)
	Vector3 fall_hips; // the hips' bone when it began (world)
	Vector3 fall_forward; // the way the character faced
	bool fall_buckles = false; // the legs found: the legs give way (else the stiff fall)
	bool fall_kneeling = false; // the knees down to the ground (falling forward): limp
	Vector3 fall_pivot;
	Vector3 fall_axis;
	Vector3 fall_direction;
	float fall_angle = 0.0f;
	float fall_angle0 = 0.0f;
	float fall_rate = 0.0f;
	float fall_height = 1.0f; // buckling: the hips' bone's distance from the feet; stiff: the centre of mass's height
	float fall_ground = 0.0f;
	float fall_sink = 0.0f; // m the hips have sunk
	float fall_sink_speed = 0.0f;
	float stat_fall_limp_time = 0.0f; // s from the fall's start to going limp
	float stat_fall_limp_height = 0.0f; // the hips' target over the ground then
	float stat_fall_limp_angle = 0.0f; // the lean then (radians)
	bool stat_fall_kneeling = false;
	enum {
		FALL_UPPER = -1,
		FALL_LEG = 0,
		FALL_FOOT = 2,
		FALL_ARM = 4,
	};
	static constexpr float FALL_REACTION = 0.12f; // s before the legs give way
	static constexpr float FALL_SINK_SPEED = 2.0f; // m/s: the hips' sinking at most
	static constexpr float FALL_SINK_RISE = 0.22f; // s: the sinking speed's rise (its time constant)
	static constexpr float FALL_LIMP_HEIGHT = 0.38f; // m: the hips' bone over the ground where it goes limp
	static constexpr float FALL_REACH_DELAY = 0.1f; // s before the arms reach out
	static constexpr float FALL_REACH_TIME = 0.35f; // s they take
	// The get-up clips found in the database: their range, the frames played (from just before the body rises to
	// standing), the face they start on.
	struct GetUpClip {
		int range = -1;
		int start = 0;
		int stop = 0;
		int face = FACE_UP;
		String name;
	};
	LocalVector<GetUpClip> get_up_clips;
	ObjectID scanned_database;
	LocalVector<int> db_bone_of; // per skeleton bone: the database's bone, or -1
	LocalVector<Transform3D> clip_globals; // scratch: a clip frame's bones (skeleton space)
	int gu_clip = -1;
	double gu_frame = 0.0;
	Transform3D gu_anchor; // the clip's space in the world
	LocalVector<Transform3D> gu_snapshot; // the bodies when it began (world)
	LocalVector<Transform3D> handover_from; // the clip's last targets (world)
	bool handing_over = false;
	float handover_time = 0.0f;
	int fall_hips_body = -1;
	// Statistics (get_balance_stats()).
	int64_t stat_hits = 0;
	int64_t stat_staggers = 0;
	int64_t stat_falls = 0;
	int64_t stat_get_ups = 0;
	float stat_capture_offset = 0.0f;
	float stat_match_error = 0.0f;
	float stat_blend_speed_max = 0.0f;
	String stat_get_up_clip;
	float stat_get_up_seconds = 0.0f;
	float stat_fall_seconds = 0.0f;

	LocalVector<Body> bodies;
	PhysicalBoneSimulator3D *simulator = nullptr;
	ObjectID simulator_id;
	bool built = false;
	bool targets_fresh = false;
	bool limp = false;
	bool pending_start = false;
	float rescue_distance = 0.75f;
	int64_t rescues = 0;

	// Statistics since reset_stats(): the bodies' distance from the animation's bones (with full strength only).
	double error_sum = 0.0;
	double error_angle_sum = 0.0;
	double error_samples = 0.0;
	float error_max = 0.0f;
	float speed_max = 0.0f;
	uint64_t drive_usec = 0;
	uint64_t drive_steps = 0;

	void _drive(double p_delta);
	FellowMotionMatcher3D *_get_matcher() const;
	void _set_state(State p_state);
	void _centre_of_mass(Vector3 &r_position, Vector3 &r_velocity, Vector3 &r_target, float &r_feet_y) const;
	void _fall(const Vector3 &p_direction, float p_speed);
	void _fall_parts();
	void _fall_pose();
	Vector3 _fall_hips_at(float p_sink, float p_angle) const;
	bool _fall_leg(int p_side, const Vector3 &p_hips, const Basis &p_lean, Transform3D &r_thigh, Transform3D &r_shin, Vector3 &r_foot) const;
	void _update_balance(float p_dt);
	bool _scan_get_up_clips();
	void _clip_pose(const FellowMotionDatabase *p_db, int p_frame);
	int _face_of(const Vector3 &p_hips, const Vector3 &p_top, const Vector3 &p_left, const Vector3 &p_right) const;
	int _top_bone() const;
	void _auto_build();
	void _rebuild_joints();
	void _classify_joints();
	void _anatomical_masses(LocalVector<float> &r_masses) const;
	void _sole_shapes();
	void _limp_joint(Body &p_body);
	float _hinge_flexion(const Body &p_body, Vector3 *r_axis = nullptr) const;
	float _hip_flexion(const Body &p_body, Vector3 *r_axis = nullptr) const;
	void _apply_simulate();
	PhysicalBoneSimulator3D *_get_simulator() const;

protected:
	void _notification(int p_what);
	static void _bind_methods();
	virtual void _process_modification(double p_delta) override;

public:
	PackedStringArray get_configuration_warnings() const override;

	void build();
	void clear();
	bool is_built() const { return built; }
	int get_body_count() const { return bodies.size(); }
	PackedStringArray get_body_bones() const;
	void reset_bodies();
	PackedFloat32Array get_body_errors() const;
	TypedArray<Transform3D> get_body_targets() const;
	PackedFloat32Array get_body_weakness() const;
	void apply_hit(const Vector3 &p_position, const Vector3 &p_impulse);
	void apply_hit_to_bone(const StringName &p_bone, const Vector3 &p_impulse);
	Dictionary get_stats() const;
	void reset_stats();

	void set_simulate(bool p_simulate);
	bool is_simulating() const { return simulate; }
	void set_strength(float p_strength) { strength = CLAMP(p_strength, 0.0f, 1.0f); }
	float get_strength() const { return strength; }
	void set_position_gain(float p_gain) { position_gain = CLAMP(p_gain, 0.0f, 1.0f); }
	float get_position_gain() const { return position_gain; }
	void set_rotation_gain(float p_gain) { rotation_gain = CLAMP(p_gain, 0.0f, 1.0f); }
	float get_rotation_gain() const { return rotation_gain; }
	void set_recovery_halflife(float p_seconds) { recovery_halflife = MAX(p_seconds, 0.01f); }
	float get_recovery_halflife() const { return recovery_halflife; }
	void set_hit_weakening(float p_weakening) { hit_weakening = CLAMP(p_weakening, 0.0f, 1.0f); }
	float get_hit_weakening() const { return hit_weakening; }
	void set_total_mass(float p_mass) { total_mass = MAX(p_mass, 0.1f); }
	float get_total_mass() const { return total_mass; }
	void set_min_bone_length(float p_length) { min_bone_length = MAX(p_length, 0.0f); }
	float get_min_bone_length() const { return min_bone_length; }
	void set_collision_layer(uint32_t p_layer) { collision_layer = p_layer; }
	uint32_t get_collision_layer() const { return collision_layer; }
	void set_collision_mask(uint32_t p_mask) { collision_mask = p_mask; }
	uint32_t get_collision_mask() const { return collision_mask; }
	void set_swing_limit(float p_radians) { swing_limit = CLAMP(p_radians, 0.0f, Math::PI); }
	float get_swing_limit() const { return swing_limit; }
	void set_twist_limit(float p_radians) { twist_limit = CLAMP(p_radians, 0.0f, Math::PI); }
	float get_twist_limit() const { return twist_limit; }
	void set_anatomical_limits(bool p_enable);
	bool is_anatomical_limits() const { return anatomical_limits; }
	PackedFloat32Array get_joint_angles() const;

	// Milestone 183 (decision 0325).
	void set_balance(bool p_balance) { balance = p_balance; }
	bool is_balance() const { return balance; }
	void set_motion_matcher_path(const NodePath &p_path) { motion_matcher_path = p_path; }
	NodePath get_motion_matcher_path() const { return motion_matcher_path; }
	void set_step_threshold(float p_metres) { step_threshold = MAX(p_metres, 0.0f); }
	float get_step_threshold() const { return step_threshold; }
	void set_fall_threshold(float p_metres) { fall_threshold = MAX(p_metres, 0.0f); }
	float get_fall_threshold() const { return fall_threshold; }
	void set_topple_angle(float p_radians) { topple_angle = CLAMP(p_radians, 0.0f, Math::PI * 0.5f); }
	float get_topple_angle() const { return topple_angle; }
	void set_get_up_tag(const StringName &p_tag) {
		get_up_tag = p_tag;
		scanned_database = ObjectID();
	}
	StringName get_get_up_tag() const { return get_up_tag; }
	void set_auto_get_up(bool p_auto) { auto_get_up = p_auto; }
	bool is_auto_get_up() const { return auto_get_up; }
	void set_get_up_delay(float p_seconds) { get_up_delay = MAX(p_seconds, 0.0f); }
	float get_get_up_delay() const { return get_up_delay; }
	void set_get_up_blend(float p_seconds) { get_up_blend = MAX(p_seconds, 0.01f); }
	float get_get_up_blend() const { return get_up_blend; }
	State get_state() const { return state; }
	int get_landed_face() const { return landed_face; }
	void knock_down(const Vector3 &p_velocity);
	bool get_up();
	Dictionary get_balance_stats() const;

	FellowPhysicalAnimation3D();
};

VARIANT_ENUM_CAST(FellowPhysicalAnimation3D::State);
VARIANT_ENUM_CAST(FellowPhysicalAnimation3D::Face);
