/**************************************************************************/
/*  fellow_traversal_3d.h                                                 */
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

#include "core/templates/safe_refcount.h"
#include "scene/3d/node_3d.h"
#include "scene/resources/animation.h"

class CollisionObject3D;
class FellowLadder3D;
class ImmediateMesh;
class MeshInstance3D;
class StandardMaterial3D;

// FellowEngine (milestone 72, decision 0180): mantling and vaulting for a character's body — a ledge in front of it
// found with the physics server's queries (a wall within reach, a top it can stand on, room for its capsule there and
// on the way up), then the body moved onto it (or over a thin obstacle) along a curve that keeps its capsule clear of
// the wall; a FellowTraversalPose3D on its skeleton poses the body procedurally, or plays a clip set for the kind.
class FellowTraversal3D : public Node3D {
	GDCLASS(FellowTraversal3D, Node3D);

public:
	enum Kind {
		KIND_NONE, // nothing to climb in reach
		KIND_STEP, // lower than min_height: the character's own movement steps or jumps up
		KIND_LOW, // a mantle onto a top up to low_max_height
		KIND_HIGH, // a mantle onto a top up to max_height (from a hang)
		KIND_VAULT, // over a thin obstacle up to vault_max_height
		KIND_TOO_HIGH, // a wall without a top below max_height
		KIND_BLOCKED, // a top, but no room on it or on the way (the reason says which)
		KIND_HANG, // milestone 75: hanging from a ledge, moving along it
		KIND_LADDER, // milestone 75: on a FellowLadder3D
		KIND_CLIMB, // milestone 148: climbing a wall's face (free_climb)
		KIND_MAX,
	};

	// Milestone 81: how a hanging body went to another ledge.
	enum Transfer {
		TRANSFER_OUTER_CORNER, // round a corner where the wall turns away
		TRANSFER_INNER_CORNER, // onto the wall that meets it
		TRANSFER_JUMP_ALONG, // across a gap along the edge
		TRANSFER_JUMP_UP, // up to a ledge above
		TRANSFER_DROP, // milestone 84: from the floor over its edge into a hang
		TRANSFER_JUMP_DOWN, // milestone 86: down to a ledge below
		TRANSFER_JUMP_DIAGONAL, // milestone 93: along and up or down at once
		TRANSFER_JUMP_BACK, // milestone 95: to a ledge on the wall behind
		TRANSFER_JUMP_CORNER, // milestone 149: round the corner past the edge's end, to a ledge on the side
		TRANSFER_REACH_UP, // milestone 151: up to a ledge within hang_reach_up, hand over hand
		TRANSFER_REACH_SIDE, // milestone 152: across to a ledge within hang_reach_side to the side (or up and to the side), hand over hand
		TRANSFER_STEP_UP, // milestone 203: up onto the hold the hands are on (pulled up, pressed, a foot on it, stood up), reaching from it to one up to hang_step_up_height above
	};
	// Milestone 198 (decision 0340): the leaps a clip's timing can be given for (set_leap_timing()).
	enum LeapKind {
		LEAP_UP, // TRANSFER_JUMP_UP
		LEAP_ALONG, // TRANSFER_JUMP_ALONG, TRANSFER_JUMP_DIAGONAL
	};

	// A check's result (global space).
	struct Ledge {
		Kind kind = KIND_NONE;
		String reason;
		Vector3 edge; // the top's edge in front of the body: on the wall's face at the top's height
		Vector3 inward; // horizontal, into the wall (the way the body climbs)
		Vector3 top_normal;
		float height = 0.0f; // the top over the body's feet
		Vector3 target; // the feet on the top, or where a vault lands
		float depth = 0.0f; // a vaulted obstacle's depth
		bool open_below = false; // the body could go on under the ledge (the automatic mantle leaves it)
		float face_lean = 0.0f; // milestone 148: metres the face under the edge goes back from the body per metre up (a slab)
		float out = 0.0f; // milestone 151: metres the body hangs further out from the edge (past holds below that stand out more)
		float spread = 0.24f; // milestone 152: each hand this far from the edge's point, along it (closer on a narrow hold)
		float drop = 0.0f; // milestone 152 (decision 0289): over a parapet, the floor beyond it this far under its top
		ObjectID collider;
	};

	// What a FellowTraversalPose3D poses the body to this physics frame (global space; weights 0 to 1).
	struct PoseHints {
		bool active = false;
		Kind kind = KIND_NONE;
		float progress = 0.0f;
		Vector3 inward; // into the wall
		Vector3 left; // along the edge, the body's left facing the wall
		Vector3 hand[2]; // left, right
		float hand_weight[2] = { 0.0f, 0.0f };
		Vector3 foot[2];
		float foot_weight[2] = { 0.0f, 0.0f };
		float foot_follow[2] = { 1.0f, 1.0f }; // how far a foot's target moves with the hips' drop (on the wall: 1)
		float lean = 0.0f; // radians, the torso tipped towards the wall
		float hips_drop = 0.0f; // meters the hips may hang below the capsule's place for the hands to reach the edge
		Vector3 body_offset; // milestone 153 (decision 0290), world: the body shown off the capsule's place (a leap's wind-up)
		float hand_grip[2] = { 0.0f, 0.0f }; // milestone 153 (decision 0291): a hand on its hold (1), or moving to the next (0)
		Vector3 look; // milestone 154 (decision 0293), world: where the climber looks (the next hold, the way it shimmies, over the top)
		float look_weight = 0.0f;
		Ref<Animation> clip; // set for the kind: played at clip_time instead of the procedural pose
		double clip_time = 0.0;
		StringName clip_root_bone;
		Vector3 clip_root_offset; // the root bone's displacement since the clip's start (taken out of its pose)
		// Milestone 196 (decision 0338), hanging: the stone a braced foot is on (its outermost point within the shin's
		// reach above the foot; its face square to inward), where found (1): the knee and the toes kept off it.
		Vector3 wall[2];
		float wall_weight[2] = { 0.0f, 0.0f };
		// Milestone 196 (decision 0338): hanging from one ledge (1; not between holds), the hands on its edge — the
		// pose may slide them along it as the animation's go (FellowTraversalPose3D's hand_slide).
		float slide = 0.0f;
		// Milestone 198 (decision 0340): a hand between holds, how far along its way it is (0: at the hold it let go of,
		// 1: at the next; 1 on a hold), and the top's normal at its hold (the hand's target is HOLD_IN over the edge onto
		// that top, HOLD_UP over it) — the pose closes the hand over the edge as it arrives.
		float hand_reach[2] = { 1.0f, 1.0f };
		Vector3 hold[2]; // the hold a hand is on, or (from half way) going to: as hand[] there
		Vector3 hold_normal[2] = { Vector3(0, 1, 0), Vector3(0, 1, 0) };
		// Milestone 198 (decision 0340): the stone just under each hand's hold — how far out of the edge it stands (x, m)
		// within 12 cm under it, and how far under the top its own top is (y, m); (0, 0): a plain edge.
		Vector2 hold_lip[2];
		// Milestone 201 (decision 0343): the hands reaching for a hold from the air (a jump aimed at it,
		// aim_jump_at_hold): the body is the controller's, not the traversal's (the pose's sway spring off).
		bool air_reach = false;
		// Milestone 201 (decision 0343): the clip's feet kept out of the stone at wall[] (reaching for a hold from the air,
		// settling from its catch: Mixamo's step up a wall put a foot into it).
		bool foot_clear = false;
		// Milestone 203 (decision 0345): the way an elbow or a knee bends (world, from the shoulder or the hip), where the move
		// knows it better than the pose's own (pressing up on a top the elbows point up and back; a foot stepped onto a
		// top at the hip's height lifts its knee up and out): weight 0, the pose's own.
		Vector3 hand_pole[2];
		float hand_pole_weight[2] = { 0.0f, 0.0f };
		Vector3 foot_pole[2];
		float foot_pole_weight[2] = { 0.0f, 0.0f };
		// Milestone 203 (decision 0345): a top a foot stands on (a point on it, world): the ball of the foot kept on it, not
		// in the stone (the foot pitched about the ankle; weight 0: none).
		Vector3 sole[2];
		float sole_weight[2] = { 0.0f, 0.0f };
		// Milestone 204 (decision 0346): the hips kept at the height off the body they had when this rose from 0 (a leap's
		// push and flight: the body's arc the traversal's alone).
		float hips_hold_weight = 0.0f;
	};

	// Milestone 198 (decision 0340): a hand's target on a ledge: this far in over the edge, this far over the top.
	static constexpr float HOLD_IN = 0.06f;
	static constexpr float HOLD_UP = 0.02f;

	// Every traversal's work since the last reset (get_global_stats()).
	struct GlobalStats {
		SafeNumeric<uint64_t> checks;
		SafeNumeric<uint64_t> check_usec;
		SafeNumeric<uint64_t> steps;
		SafeNumeric<uint64_t> step_usec;
		SafeNumeric<uint64_t> mantles[3]; // low, high, vault
		SafeNumeric<uint64_t> hangs; // milestone 75
		SafeNumeric<uint64_t> transfers; // milestone 81: corners wrapped, jumps between ledges
		SafeNumeric<uint64_t> ladders;
		SafeNumeric<uint64_t> climbs; // milestone 148
		SafeNumeric<uint64_t> poses;
		SafeNumeric<uint64_t> pose_usec;
	};
	static GlobalStats global_stats;

private:
	NodePath body_path;
	bool enabled = true;
	bool auto_mantle = true;
	float auto_min_speed = 0.5f;
	bool grab_in_air = true;
	bool vault = true;
	bool align_to_wall = true;
	float min_height = 0.5f;
	float low_max_height = 1.3f;
	float max_height = 2.3f;
	float vault_max_height = 1.3f;
	float vault_max_depth = 0.6f;
	float vault_min_speed = 2.5f;
	float reach_distance = 0.6f;
	float max_wall_angle = 45.0f; // degrees between the way the body goes and into the wall
	float max_ledge_slope = 40.0f; // degrees
	uint32_t collision_mask = 1;
	int check_interval = 2; // physics frames between automatic checks while nothing is in reach
	float low_duration = 0.6f;
	float high_duration = 1.1f;
	float vault_duration = 0.6f;
	float exit_speed = 1.5f;
	Ref<Animation> clips[3]; // low, high, vault
	Ref<Animation> hang_clip; // milestone 153 (decision 0290): a mantle from a hang (climbing up onto the ledge held)
	bool mantle_from_hang = false; // the mantle under way started from a hang
	StringName clip_root_bone;
	bool debug_draw = false;
	// Milestone 75: hanging and ladders.
	bool hang = true;
	bool hang_first = false;
	float hang_reach = 2.05f; // the hands over the feet, hanging
	float shimmy_speed = 1.0f;
	// Milestone 81 (decision 0189): round corners and across to other ledges.
	bool wrap_corners = true;
	float hang_jump_distance = 1.6f; // m along the edge past its end
	float hang_jump_height = 1.3f; // m up
	float hang_jump_depth = 2.6f; // m down (milestone 86)
	// Milestone 151 (decision 0282): climbing from hold to hold.
	float hang_reach_up = 0.0f; // m: a ledge up to this far above is climbed to hand over hand (0: jumped to)
	float hang_brace_height = 0.0f; // m: the feet braced on the wall found under the ledge, this high over the body's feet (0: off)
	float hang_reach_side = 0.0f; // milestone 152 (decision 0285), m: a gap to a ledge to the side up to this wide is crossed hand over hand (0: jumped)
	float hold_min_width = 0.0f; // milestone 152 (decision 0286), m: a hold narrower than both hands' 0.48 m, down to this wide, is held with the hands closer (0: off)
	float hang_leap_windup = 0.0f; // milestone 153 (decision 0290), s: a leap from a hold winds up this long, springs, flies hands first and is caught with its speed (0: as before)
	float leap_gravity = 0.0f; // milestone 200 (decision 0342), m/s²: a timed leap up flies under this gravity (0: its clip's rise scaled, as milestone 198)
	// Milestone 203 (decision 0345): a person's strength. leap_push_force: the support force at a leap's peak, in body
	// weights (the push's force rising and falling as a half sine; a leap up reaches no higher than it carries the body,
	// and one further is stepped up to or not made; 0: no limit). climb_pull_speed: the body pulled up between holds no
	// faster than this (m/s; a reach takes as long as that needs; 0: their fixed times). hang_step_up_height: a hold
	// beyond a leap's reach, up to this far above, is climbed to by standing up on the one the hands are on (its top at
	// least STEP_MIN_DEPTH deep; 0: off).
	float leap_push_force = 0.0f;
	float climb_pull_speed = 0.0f;
	float hang_step_up_height = 0.0f;
	float hang_side_dead_zone = 0.0f; // milestone 207
	// Milestone 84 (decision 0192): walking over an edge, hanging from it.
	bool drop_to_hang = false;
	float drop_max_speed = 2.0f; // m/s: faster, the body goes over
	double drop_duration = 0.8;
	bool ladders = true;
	float ladder_speed = 1.2f;
	float ladder_reach = 0.6f;
	// Milestone 148 (decision 0276): climbing any wall's face.
	bool free_climb = false;
	uint32_t climb_mask = 1; // the physics layers whose walls are climbable
	float climb_speed = 1.0f;
	float climb_max_overhang = 20.0f; // degrees past vertical the face may lean over the body
	float climb_jump_distance = 1.2f; // m: a lunge over the face (hang_jump() while climbing)

	// The body's capsule (body space), from its first CollisionShape3D child.
	float radius = 0.35f;
	float height = 1.8f;
	Vector3 shape_center = Vector3(0, 0.9f, 0);
	RID probe_shape; // the physics server's shapes the queries cast
	RID body_shape;
	// Milestone 148: the size body_shape was given last (a shape's data set again rebuilds it: Jolt's, every time).
	mutable float body_shape_radius = -1.0f;
	mutable float body_shape_height = -1.0f;

	// The mantle in progress.
	bool active = false;
	Kind action = KIND_NONE;
	Ledge ledge;
	double time = 0.0;
	double duration = 1.0;
	Transform3D start_xf;
	Vector3 start_feet;
	Basis end_basis;
	Transform3D collider_start; // the ledge's collider when it was found (a moving platform carries the ledge)
	float entry_speed = 0.0f;
	double cooldown = 0.0;
	int skip_checks = 0;
	Vector3 last_body_origin;
	bool has_last_origin = false;
	Vector3 intent; // the way the player wants to go (set by the controller), over the body's velocity
	// A clip's root motion (sampled), for the body's path warped onto the ledge.
	LocalVector<Vector3> clip_root;
	double clip_root_step = 1.0 / 60.0;
	PoseHints hints;
	// Milestone 204 (decision 0346): a foot's way in and out from the wall, eased (FOOT_DEPTH_SPEED).
	float foot_depth[2] = { 0.0f, 0.0f };
	bool foot_depth_valid[2] = { false, false };
	Vector3 foot_depth_in;
	Ledge last_check;
	// Hanging (action KIND_HANG) and climbing a ladder (KIND_LADDER).
	Vector3 hang_feet; // where the body hangs (global)
	float shimmy_phase = 0.0f; // metres moved along the edge (the hands and feet step with it)
	float shimmy_velocity = 0.0f;
	bool climb_requested = false;
	bool release_requested = false;
	bool push_armed = false; // hanging: the push at the wall let go of since it caught the ledge (a new push climbs)
	bool jump_requested = false;
	// A transfer from one ledge to another (round a corner, a jump): the feet along a curve, the facing turning.
	struct TransferState {
		bool active = false;
		int how = 0;
		double t = 0.0;
		double duration = 0.5;
		Vector3 from_feet, control, to_feet;
		Vector3 from_inward;
		Vector3 from_normal = Vector3(0, 1, 0); // milestone 198: the top's normal at the hold let go of
		Vector2 from_lip[2], to_lip[2]; // milestone 198: the stone under each hand's hold, at either end
		Vector3 from_hands[2];
		Ledge to;
		int lead = 0; // milestone 151: the hand that goes first (a reach up)
		double windup = 0.0; // milestone 153: a leap's wind-up before it springs (s, within duration)
		int timing = -1; // milestone 198: the leap timing it follows (LEAP_UP, LEAP_ALONG), or none
		// Milestone 200 (decision 0342): a leap up in flight under leap_gravity — its dip, its push (from rest to the
		// speed of the flight, the hands letting go), its flight (to the dead point, where the hands catch) and its settle
		// (down into the hang as the arms take the weight): their times (s); the dip's depth, the push's rise, the speed
		// it flies off with, the settle's drop (m, m/s); the clip's let-go of the last hand and its catch (shares of its
		// time) and the move's (shares of the move's).
		bool ballistic = false;
		float b_dip_time = 0.0f, b_push_time = 0.0f, b_flight_time = 0.0f, b_settle_time = 0.0f;
		float b_dip = 0.0f, b_push = 0.0f, b_speed = 0.0f, b_settle = 0.0f;
		float b_clip_release = 0.0f, b_clip_catch = 1.0f, b_release = 0.0f, b_catch = 1.0f;
		// Milestone 203 (decision 0345): a step up — the body's feet (over the hold the hands are on) at the start and the
		// ends of its phases (pulled up, pressed up with a foot on the top, stood up, reached, hung from the next hold) and
		// the phases' ends (shares of the move); where the body stands on the hold; the hold's edge and the wall's face over
		// it (at the edge's height); the hands on it at the start.
		float s_feet[6] = {};
		float s_end[5] = {};
		Vector3 stand_feet;
		Vector3 from_edge;
		Vector3 face;
		Vector3 from_hold_hands[2];
	} transfer;
	// Milestone 198 (decision 0340): a leap timed as a clip's — its hips' way along the move (0 at the start, 1 at the end),
	// their lift (m: over the straight line along; over the end's height up), sampled evenly over its time; its hands'
	// let-go and catch (the leading hand's, the other's: shares of the time).
	struct LeapTiming {
		float time = 0.0f;
		float reach = 1.0f; // m: the clip's own way (up: its rise; along: across)
		LocalVector<float> progress;
		LocalVector<float> lift;
		Vector4 hands;
	};
	LeapTiming leap_timing[2];
	static float _sample(const LocalVector<float> &p_values, float p_u);
	static float _leap_rise(float p_z, float p_reach, float p_rise);
	static constexpr float LEAP_EASE_IN = 0.15f; // m of a clip's rise up (milestone 198)
	static constexpr float LEAP_EASE_OUT = 0.3f;
	// Milestone 200 (decision 0342): a leap up under gravity — a dip of LEAP_DIP over LEAP_DIP_TIME before the push; the
	// push LEAP_PUSH_SHARE of the rise to the dead point (at least the clip's, at most LEAP_PUSH_MAX, at most
	// LEAP_PUSH_MOST of it).
	static constexpr float LEAP_DIP = 0.08f; // m
	static constexpr float LEAP_DIP_TIME = 0.2f; // s
	static constexpr float LEAP_PUSH_SHARE = 0.25f;
	static constexpr float LEAP_PUSH_MAX = 0.65f; // m
	static constexpr float LEAP_PUSH_MOST = 0.6f;
	static constexpr float LEAP_OUT = 0.06f; // m: the body out from the wall in mid-flight (the feet's push)
	static constexpr float LEAP_HANDS_LEAD = 2.5f; // the hands' way from let-go to catch: 1 - (1 - t)^this (the body's: 2)
	// Milestone 203 (decision 0345): the step up. Its top at least STEP_MIN_DEPTH deep; the hips (over the hold) pulled up to
	// STEP_PULL (the chest at the edge), pressed up to STEP_PRESS (a foot on the top), stood up to STEP_STAND over it (the
	// hips' height over the feet); standing, the hips STEP_OFF off the wall's face (over the top: the body's weight over
	// its feet — the clips' hips are STEP_HIPS_BACK behind the body's middle) and the torso tipped STEP_LEAN to the wall;
	// the press and the stand no faster than STEP_PRESS_SPEED, STEP_STAND_SPEED (m/s: a press on the hands, a one-legged
	// squat).
	static constexpr float STEP_MIN_DEPTH = 0.12f;
	static constexpr float STEP_PULL = -0.40f;
	static constexpr float STEP_PRESS = 0.0f; // (the arms straight on the hold, the hips out by STEP_PRESS_GAP: higher, the body's fit to the arms' reach held them under it)
	static constexpr float STEP_STAND = 0.92f;
	static constexpr float STEP_OFF = 0.12f;
	static constexpr float STEP_HIPS_BACK = 0.13f;
	static constexpr float STEP_LEAN = 0.3f; // radians
	static constexpr float STEP_ANKLE_OFF = 0.19f; // m: standing on the top, the ankles out from the face (the balls of the feet on it)
	static constexpr float STEP_PULL_GAP = 0.33f; // m: the hips out from the face, pulled up
	static constexpr float STEP_PRESS_GAP = 0.28f; // m: and pressed up (the hold stands out 0.1-0.2 m)
	static constexpr float STEP_PRESS_SPEED = 0.8f;
	static constexpr float STEP_STAND_SPEED = 0.9f;
	static constexpr float STEP_REACH_TIME = 0.6f;
	static constexpr float FOOT_DEPTH_SPEED = 1.5f; // m/s
	static constexpr float FOOT_HIGH_STEP = 0.25f; // m: a foot stepping up the wall at most this over its hanging place
	static constexpr float LIP_ROUND_OUT = 0.2f; // m: a lip this far out over the hold left is leapt to round its front
	static constexpr float LIP_ROUND_CONTROL = 0.5f; // m: the hands' curve's control that far out from the lip's hold
	static constexpr float STEP_HANG_TIME = 0.6f;
	float _leap_reach_max() const; // milestone 203: how far up a leap carries the body with leap_push_force (m; a large number without)
	bool _step_stand(const Ledge &p_ledge, Vector3 &r_feet, Vector3 *r_face = nullptr) const; // milestone 203: where the body stands on a ledge's top (false: too narrow, no room)
	void _step_up_step(double p_delta); // milestone 203: the step up under way
	float _leap_clip_to_move(float p_f) const; // a clip's share of its time as the ballistic leap's share of the move
	// Milestone 201 (decision 0343): a jump at a wall aimed at a hold (aim_jump_at_hold) — the body flies under the
	// controller's gravity to the top, where the hands are on the hold (the body AIM_CATCH_LIFT over its hang, the arms a
	// little bent); the hands reach for it as it rises (their IK in over the first AIM_REACH_IN of the rise; from in
	// front of the chest up the wall's face to AIM_OUT out of the edge and AIM_UP over it by AIM_OVER of the way to the
	// catch, then in over the top by AIM_ON), and it is caught at the top. Looked for up to AIM_REACH further than reach_distance (the body runs at the wall as it rises).
	struct AimState {
		bool active = false;
		Ledge ledge;
		double time = 0.0;
		double rise_time = 0.0; // s: the takeoff to the top
		double catch_time = 0.0; // s: the takeoff to the catch (rising no faster than AIM_CATCH_SPEED)
		Vector2 lip[2];
		Vector3 wall[2]; // the stone the feet would brace on, under the hold
		float wall_found[2] = { 0.0f, 0.0f };
	} aim;
	static constexpr float AIM_CATCH_LIFT = 0.08f; // m
	static constexpr float AIM_MIN_RISE = 0.12f; // m
	static constexpr float AIM_REACH = 1.0f; // m
	static constexpr float AIM_REACH_IN = 0.5f;
	static constexpr float AIM_OUT = 0.18f; // m
	static constexpr float AIM_UP = 0.08f; // m
	static constexpr float AIM_OVER = 0.5f;
	static constexpr float AIM_ON = 0.85f; // on the hold this far to the catch (the limbs' smoothing behind it)
	static constexpr float AIM_CATCH_SPEED = 0.2f; // m/s: rising no faster than this, at the top
	static constexpr float AIM_LATE = 0.25f; // s past the top without a catch: given up
	static constexpr float AIM_FEET_SETTLE = 0.4f; // s: after an aimed jump's catch, the clip's feet kept out of the stone
	float reach_extra = 0.0f; // m added to reach_distance by a check (the aim's)
	bool hang_hands_on = false; // a hang begun with the hands on the hold already (an aimed jump's catch)
	void _step_aim(double p_delta);
	void _cancel_aim();
	// After a corner the wish held as it was goes on along the new edge (the stick held left stays "left").
	bool remap = false;
	Vector3 remap_from, remap_to;
	double corner_wait = 0.0; // after a corner looked for in vain, the next look (s)
	double reach_wait = 0.0; // milestone 151: after a ledge above looked for in vain, the next look (s)
	int reach_lead = 1; // milestone 151: the hand that led the last reach up (0: left, 1: right); the next leads with the other
	double reach_side_wait = 0.0; // milestone 152: after a ledge to the side looked for in vain at an edge's end, the next look (s)
	float climb_input = 0.0f; // the controller's up (1) or down (-1) on a ladder; 0: from the intent
	ObjectID ladder_id;
	float ladder_h = 0.0f; // the feet's height along the ladder from its foot
	bool ladder_from_top = false; // taken from the floor at its top (over the edge, then down)
	double hold_time = 0.0; // how long the body has hung or climbed (the blend in)
	// Climbing a wall (KIND_CLIMB): the point on the face at the chest's height, the face's outward normal there.
	Vector3 climb_point;
	Vector3 climb_normal;
	Vector3 climb_facing; // the body's facing, turned towards the face's (round a corner, over a few frames)
	float climb_travel = 0.0f; // metres moved on the face (the hands and feet step with it)
	// Milestone 150: the capsule pushed out from the face where it would not fit at its gap (a string course standing
	// out of the wall above or below it, a setback), and the feet as placed (their distance from the face eased).
	float climb_out = 0.0f;
	float climb_out_goal = 0.0f;
	Vector3 climb_feet_now;
	Vector3 climb_feet_normal; // the face's (flat) normal where the feet were last placed
	// The wish held turns with the face (round a tower, a corner: the stick held left keeps going left along it).
	bool climb_hold = false;
	Vector3 climb_hold_intent;
	Vector3 climb_hold_normal;
	// A lunge over the face (hang_jump() while climbing): the chest's point from one place on it to another.
	struct ClimbLunge {
		bool active = false;
		double t = 0.0;
		double duration = 0.35;
		Vector3 from_point, from_normal, to_point, to_normal;
	} lunge;

	// The debug drawing (debug_draw).
	MeshInstance3D *debug_instance = nullptr;
	Ref<ImmediateMesh> debug_mesh;
	Ref<StandardMaterial3D> debug_material;

	CollisionObject3D *_get_body() const;
	void _read_body_shape(CollisionObject3D *p_body);
	Vector3 _feet_of(const Transform3D &p_body_xf) const;
	Transform3D _body_at(const Vector3 &p_feet, const Basis &p_basis) const;
	Vector3 _body_velocity(CollisionObject3D *p_body, double p_delta);
	bool _capsule_free(const Vector3 &p_feet, float p_radius, float p_height, const HashSet<RID> &p_exclude) const;
	Ledge _check(const Vector3 &p_direction, bool p_in_air, float p_speed, float p_min_height = -1.0f);
	Ledge _check_ledge(const Vector3 &p_direction, bool p_in_air, float p_speed, float p_min_height);
	bool _start(const Ledge &p_ledge, float p_speed, bool p_from_hang = false);
	const Ref<Animation> &_mantle_clip() const;
	void _finish(bool p_cancelled);
	void _step(double p_delta);
	void _step_body(double p_delta);
	void _update_hints(const Vector3 &p_feet, const Vector3 &p_edge, const Vector3 &p_inward);
	Vector3 _path(double p_t, const Vector3 &p_edge, const Vector3 &p_inward, const Vector3 &p_target) const;
	void _prepare_clip();
	void _draw_debug();
	Vector3 _hang_feet_at(const Vector3 &p_edge, const Vector3 &p_inward) const;
	Vector3 _hang_feet_of(const Ledge &p_ledge) const { return _hang_feet_at(p_ledge.edge, p_ledge.inward) - p_ledge.inward * p_ledge.out; }
	Vector3 _hang_foot(int p_side, const Vector3 &p_feet, const Vector3 &p_inward, float p_y, const Vector3 &p_fallback, Vector3 *r_wall = nullptr, float *r_found = nullptr) const;
	Vector2 _probe_lip(const Vector3 &p_edge, const Vector3 &p_inward) const;
	Vector3 lip_point[2]; // milestone 198: where each hand's hold was last probed for the stone under it (hanging)
	Vector2 lip_value[2];
	bool _probe_edge(const Vector3 &p_edge, const Vector3 &p_inward, float p_along, Ledge &r_ledge) const;
	bool _start_hang(const Ledge &p_ledge, bool p_step = true);
	void _step_hang(double p_delta);
	void _end_hang(bool p_climbed);
	float _edge_end(float p_sign) const;
	bool _fit_reach(Ledge &r_to, const Vector3 &p_from_feet, const Vector3 &p_bias, const HashSet<RID> &p_exclude, Vector3 &r_control) const;
	bool _fit_hold(Ledge &r_ledge) const;
	Vector3 _path_mantle(double p_t, const Vector3 &p_edge, const Vector3 &p_inward, const Vector3 &p_target) const;
	bool _hands_room(const Ledge &p_ledge, float p_height) const;
	bool _find_corner(float p_sign, Ledge &r_ledge, Vector3 &r_control, int &r_how) const;
	bool _find_jump(const Vector3 &p_wish, Ledge &r_ledge, Vector3 &r_control, int &r_how) const;
	bool _find_drop(const Vector3 &p_direction, Ledge &r_ledge, Vector3 &r_mid) const;
	bool _start_drop(const Ledge &p_ledge, const Vector3 &p_mid);
	void _start_transfer(const Ledge &p_to, const Vector3 &p_control, int p_how, double p_duration);
	void _step_transfer(double p_delta);
	FellowLadder3D *_find_ladder(const Vector3 &p_feet, const Vector3 &p_direction, bool p_in_air, float &r_h, bool &r_from_top) const;
	bool _start_ladder(FellowLadder3D *p_ladder, float p_h, bool p_from_top);
	void _step_ladder(double p_delta);
	void _end_ladder(bool p_at_top, const Vector3 &p_velocity);
	void _place(CollisionObject3D *p_body, const Vector3 &p_feet, const Vector3 &p_facing, double p_delta);
	bool _probe_wall(const Vector3 &p_from, const Vector3 &p_dir, float p_length, Vector3 &r_point, Vector3 &r_normal, ObjectID &r_collider) const;
	bool _climbable(const Vector3 &p_normal, ObjectID p_collider) const;
	Vector3 _climb_feet(const Vector3 &p_point, const Vector3 &p_normal) const;
	bool _find_climb(const Vector3 &p_direction, Vector3 &r_point, Vector3 &r_normal) const;
	bool _climb_top(Ledge &r_ledge) const;
	float _climb_clearance(const Vector3 &p_point, const Vector3 &p_normal, const HashSet<RID> &p_exclude) const;
	bool _start_climb(const Vector3 &p_point, const Vector3 &p_normal);
	void _step_climb(double p_delta);
	void _end_climb(bool p_at_top, const Vector3 &p_velocity);
	static Dictionary _ledge_dict(const Ledge &p_ledge);

protected:
	void _notification(int p_what);
	static void _bind_methods();

public:
	PackedStringArray get_configuration_warnings() const override;

	// Gameplay.
	Dictionary find_ledge(const Vector3 &p_direction = Vector3());
	bool try_mantle(const Vector3 &p_direction = Vector3());
	void cancel();
	bool is_active() const { return active; }
	Kind get_action() const { return active ? action : KIND_NONE; }
	float get_progress() const { return active ? float(time / duration) : 0.0f; }
	Dictionary get_last_check() const { return _ledge_dict(last_check); }
	const PoseHints &get_pose_hints() const { return hints; }
	Dictionary get_pose_targets() const;
	static Dictionary get_global_stats();
	static void reset_global_stats();
	void set_intent(const Vector3 &p_direction) { intent = p_direction; }
	Vector3 get_intent() const { return intent; }
	// Milestone 75.
	bool try_hang(const Vector3 &p_direction = Vector3());
	bool try_drop(const Vector3 &p_direction = Vector3());
	void hang_jump() { jump_requested = true; }
	// Milestone 201 (decision 0343).
	float aim_jump_at_hold(const Vector3 &p_direction, float p_gravity, float p_max_speed = 0.0f);
	bool is_aiming() const { return aim.active; }
	Dictionary get_aim_state() const; // milestone 203: the aim under way (its time, its catch's, the body's place hanging from its hold)
	bool is_transferring() const { return active && action == KIND_HANG && transfer.active; }
	bool try_ladder();
	void climb_up() { climb_requested = true; }
	void release() { release_requested = true; }
	bool is_hanging() const { return active && action == KIND_HANG; }
	bool is_on_ladder() const { return active && action == KIND_LADDER; }
	Node3D *get_ladder() const;
	// Milestone 148.
	bool try_climb(const Vector3 &p_direction = Vector3());
	bool is_climbing() const { return active && action == KIND_CLIMB; }
	void set_climb_input(float p_input) { climb_input = CLAMP(p_input, -1.0f, 1.0f); }
	float get_climb_input() const { return climb_input; }

	void set_body_path(const NodePath &p_path);
	NodePath get_body_path() const { return body_path; }
	void set_enabled(bool p_enabled);
	bool is_enabled() const { return enabled; }
	void set_auto_mantle(bool p_enabled) { auto_mantle = p_enabled; }
	bool is_auto_mantle() const { return auto_mantle; }
	void set_auto_min_speed(float p_speed) { auto_min_speed = MAX(0.0f, p_speed); }
	float get_auto_min_speed() const { return auto_min_speed; }
	void set_grab_in_air(bool p_enabled) { grab_in_air = p_enabled; }
	bool is_grab_in_air() const { return grab_in_air; }
	void set_vault(bool p_enabled) { vault = p_enabled; }
	bool is_vault() const { return vault; }
	void set_align_to_wall(bool p_enabled) { align_to_wall = p_enabled; }
	bool is_align_to_wall() const { return align_to_wall; }
	void set_min_height(float p_height);
	float get_min_height() const { return min_height; }
	void set_low_max_height(float p_height);
	float get_low_max_height() const { return low_max_height; }
	void set_max_height(float p_height);
	float get_max_height() const { return max_height; }
	void set_vault_max_height(float p_height) { vault_max_height = MAX(0.0f, p_height); }
	float get_vault_max_height() const { return vault_max_height; }
	void set_vault_max_depth(float p_depth) { vault_max_depth = MAX(0.05f, p_depth); }
	float get_vault_max_depth() const { return vault_max_depth; }
	void set_vault_min_speed(float p_speed) { vault_min_speed = MAX(0.0f, p_speed); }
	float get_vault_min_speed() const { return vault_min_speed; }
	void set_reach_distance(float p_distance);
	float get_reach_distance() const { return reach_distance; }
	void set_max_wall_angle(float p_degrees) { max_wall_angle = CLAMP(p_degrees, 0.0f, 89.0f); }
	float get_max_wall_angle() const { return max_wall_angle; }
	void set_max_ledge_slope(float p_degrees) { max_ledge_slope = CLAMP(p_degrees, 0.0f, 89.0f); }
	float get_max_ledge_slope() const { return max_ledge_slope; }
	void set_check_interval(int p_frames) { check_interval = CLAMP(p_frames, 1, 8); }
	int get_check_interval() const { return check_interval; }
	void set_collision_mask(uint32_t p_mask) { collision_mask = p_mask; }
	uint32_t get_collision_mask() const { return collision_mask; }
	void set_low_duration(float p_seconds) { low_duration = MAX(0.05f, p_seconds); }
	float get_low_duration() const { return low_duration; }
	void set_high_duration(float p_seconds) { high_duration = MAX(0.05f, p_seconds); }
	float get_high_duration() const { return high_duration; }
	void set_vault_duration(float p_seconds) { vault_duration = MAX(0.05f, p_seconds); }
	float get_vault_duration() const { return vault_duration; }
	void set_exit_speed(float p_speed) { exit_speed = MAX(0.0f, p_speed); }
	float get_exit_speed() const { return exit_speed; }
	void set_low_clip(const Ref<Animation> &p_clip) { clips[0] = p_clip; }
	Ref<Animation> get_low_clip() const { return clips[0]; }
	void set_high_clip(const Ref<Animation> &p_clip) { clips[1] = p_clip; }
	Ref<Animation> get_high_clip() const { return clips[1]; }
	void set_vault_clip(const Ref<Animation> &p_clip) { clips[2] = p_clip; }
	Ref<Animation> get_vault_clip() const { return clips[2]; }
	void set_hang_clip(const Ref<Animation> &p_clip) { hang_clip = p_clip; }
	Ref<Animation> get_hang_clip() const { return hang_clip; }
	void set_clip_root_bone(const StringName &p_bone) { clip_root_bone = p_bone; }
	StringName get_clip_root_bone() const { return clip_root_bone; }
	void set_debug_draw(bool p_enabled);
	bool is_debug_draw() const { return debug_draw; }
	void set_hang(bool p_enabled) { hang = p_enabled; }
	bool is_hang() const { return hang; }
	void set_hang_first(bool p_enabled) { hang_first = p_enabled; }
	bool is_hang_first() const { return hang_first; }
	void set_hang_reach(float p_reach) { hang_reach = CLAMP(p_reach, 0.5f, 4.0f); }
	float get_hang_reach() const { return hang_reach; }
	void set_shimmy_speed(float p_speed) { shimmy_speed = MAX(0.0f, p_speed); }
	float get_shimmy_speed() const { return shimmy_speed; }
	void set_wrap_corners(bool p_enabled) { wrap_corners = p_enabled; }
	bool is_wrap_corners() const { return wrap_corners; }
	void set_hang_jump_distance(float p_distance) { hang_jump_distance = MAX(0.0f, p_distance); }
	float get_hang_jump_distance() const { return hang_jump_distance; }
	void set_hang_jump_height(float p_height) { hang_jump_height = MAX(0.0f, p_height); }
	float get_hang_jump_height() const { return hang_jump_height; }
	void set_hang_jump_depth(float p_depth) { hang_jump_depth = MAX(0.0f, p_depth); }
	float get_hang_jump_depth() const { return hang_jump_depth; }
	void set_hang_reach_up(float p_height) { hang_reach_up = CLAMP(p_height, 0.0f, 2.5f); }
	float get_hang_reach_up() const { return hang_reach_up; }
	void set_hang_brace_height(float p_height) { hang_brace_height = CLAMP(p_height, 0.0f, 1.5f); }
	float get_hang_brace_height() const { return hang_brace_height; }
	void set_hang_reach_side(float p_gap) { hang_reach_side = CLAMP(p_gap, 0.0f, 1.5f); }
	float get_hang_reach_side() const { return hang_reach_side; }
	void set_hold_min_width(float p_width) { hold_min_width = CLAMP(p_width, 0.0f, 0.48f); }
	float get_hold_min_width() const { return hold_min_width; }
	void set_hang_leap_windup(float p_seconds) { hang_leap_windup = CLAMP(p_seconds, 0.0f, 0.4f); }
	void set_leap_timing(LeapKind p_kind, float p_time, float p_reach, const PackedFloat32Array &p_progress, const PackedFloat32Array &p_lift, const Vector4 &p_hands);
	void clear_leap_timing(LeapKind p_kind);
	bool has_leap_timing(LeapKind p_kind) const;
	float get_hang_leap_windup() const { return hang_leap_windup; }
	void set_leap_gravity(float p_gravity) { leap_gravity = MAX(0.0f, p_gravity); }
	float get_leap_gravity() const { return leap_gravity; }
	void set_leap_push_force(float p_body_weights) { leap_push_force = p_body_weights <= 0.0f ? 0.0f : MAX(1.05f, p_body_weights); }
	float get_leap_push_force() const { return leap_push_force; }
	void set_climb_pull_speed(float p_speed) { climb_pull_speed = MAX(0.0f, p_speed); }
	float get_climb_pull_speed() const { return climb_pull_speed; }
	void set_hang_step_up_height(float p_height) { hang_step_up_height = CLAMP(p_height, 0.0f, 3.0f); }
	float get_hang_step_up_height() const { return hang_step_up_height; }
	void set_hang_side_dead_zone(float p_zone) { hang_side_dead_zone = CLAMP(p_zone, 0.0f, 1.0f); }
	float get_hang_side_dead_zone() const { return hang_side_dead_zone; }
	PackedFloat32Array get_leap_phases() const;
	void set_drop_to_hang(bool p_enabled) { drop_to_hang = p_enabled; }
	bool is_drop_to_hang() const { return drop_to_hang; }
	void set_drop_max_speed(float p_speed) { drop_max_speed = MAX(0.0f, p_speed); }
	float get_drop_max_speed() const { return drop_max_speed; }
	void set_ladders(bool p_enabled) { ladders = p_enabled; }
	bool is_ladders() const { return ladders; }
	void set_ladder_speed(float p_speed) { ladder_speed = MAX(0.0f, p_speed); }
	float get_ladder_speed() const { return ladder_speed; }
	void set_ladder_reach(float p_reach) { ladder_reach = MAX(0.05f, p_reach); }
	float get_ladder_reach() const { return ladder_reach; }
	void set_free_climb(bool p_enabled) { free_climb = p_enabled; }
	bool is_free_climb() const { return free_climb; }
	void set_climb_mask(uint32_t p_mask) { climb_mask = p_mask; }
	uint32_t get_climb_mask() const { return climb_mask; }
	void set_climb_speed(float p_speed) { climb_speed = MAX(0.0f, p_speed); }
	float get_climb_speed() const { return climb_speed; }
	void set_climb_max_overhang(float p_degrees) { climb_max_overhang = CLAMP(p_degrees, 0.0f, 60.0f); }
	float get_climb_max_overhang() const { return climb_max_overhang; }
	void set_climb_jump_distance(float p_distance) { climb_jump_distance = MAX(0.0f, p_distance); }
	float get_climb_jump_distance() const { return climb_jump_distance; }

	FellowTraversal3D();
	~FellowTraversal3D();
};

VARIANT_ENUM_CAST(FellowTraversal3D::Kind);
VARIANT_ENUM_CAST(FellowTraversal3D::Transfer);
VARIANT_ENUM_CAST(FellowTraversal3D::LeapKind);
