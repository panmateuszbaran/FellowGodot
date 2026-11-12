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
		KIND_MAX,
	};

	// Milestone 81: how a hanging body went to another ledge.
	enum Transfer {
		TRANSFER_OUTER_CORNER, // round a corner where the wall turns away
		TRANSFER_INNER_CORNER, // onto the wall that meets it
		TRANSFER_JUMP_ALONG, // across a gap along the edge
		TRANSFER_JUMP_UP, // up to a ledge above
		TRANSFER_DROP, // milestone 84: from the floor over its edge into a hang
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
		Ref<Animation> clip; // set for the kind: played at clip_time instead of the procedural pose
		double clip_time = 0.0;
		StringName clip_root_bone;
		Vector3 clip_root_offset; // the root bone's displacement since the clip's start (taken out of its pose)
	};

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
	// Milestone 84 (decision 0192): walking over an edge, hanging from it.
	bool drop_to_hang = false;
	float drop_max_speed = 2.0f; // m/s: faster, the body goes over
	double drop_duration = 0.8;
	bool ladders = true;
	float ladder_speed = 1.2f;
	float ladder_reach = 0.6f;

	// The body's capsule (body space), from its first CollisionShape3D child.
	float radius = 0.35f;
	float height = 1.8f;
	Vector3 shape_center = Vector3(0, 0.9f, 0);
	RID probe_shape; // the physics server's shapes the queries cast
	RID body_shape;

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
		Vector3 from_hands[2];
		Ledge to;
	} transfer;
	// After a corner the wish held as it was goes on along the new edge (the stick held left stays "left").
	bool remap = false;
	Vector3 remap_from, remap_to;
	double corner_wait = 0.0; // after a corner looked for in vain, the next look (s)
	float climb_input = 0.0f; // the controller's up (1) or down (-1) on a ladder; 0: from the intent
	ObjectID ladder_id;
	float ladder_h = 0.0f; // the feet's height along the ladder from its foot
	bool ladder_from_top = false; // taken from the floor at its top (over the edge, then down)
	double hold_time = 0.0; // how long the body has hung or climbed (the blend in)

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
	Ledge _check(const Vector3 &p_direction, bool p_in_air, float p_speed);
	Ledge _check_ledge(const Vector3 &p_direction, bool p_in_air, float p_speed);
	bool _start(const Ledge &p_ledge, float p_speed);
	void _finish(bool p_cancelled);
	void _step(double p_delta);
	void _step_body(double p_delta);
	void _update_hints(const Vector3 &p_feet, const Vector3 &p_edge, const Vector3 &p_inward);
	Vector3 _path(double p_t, const Vector3 &p_edge, const Vector3 &p_inward, const Vector3 &p_target) const;
	void _prepare_clip();
	void _draw_debug();
	Vector3 _hang_feet_at(const Vector3 &p_edge, const Vector3 &p_inward) const;
	bool _probe_edge(const Vector3 &p_edge, const Vector3 &p_inward, float p_along, Ledge &r_ledge) const;
	bool _start_hang(const Ledge &p_ledge, bool p_step = true);
	void _step_hang(double p_delta);
	void _end_hang(bool p_climbed);
	float _edge_end(float p_sign) const;
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
	bool is_transferring() const { return active && action == KIND_HANG && transfer.active; }
	bool try_ladder();
	void climb_up() { climb_requested = true; }
	void release() { release_requested = true; }
	bool is_hanging() const { return active && action == KIND_HANG; }
	bool is_on_ladder() const { return active && action == KIND_LADDER; }
	Node3D *get_ladder() const;
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

	FellowTraversal3D();
	~FellowTraversal3D();
};

VARIANT_ENUM_CAST(FellowTraversal3D::Kind);
VARIANT_ENUM_CAST(FellowTraversal3D::Transfer);
