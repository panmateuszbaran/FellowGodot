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

// FellowEngine (milestone 166, decision 0307): physical animation. The skeleton's bones as rigid bodies (built from
// its rest pose, on the stock PhysicalBoneSimulator3D), each driven every physics step towards where the animation
// has its bone — velocity feed-forward plus a share of the error — so that the body moves as the animation does until
// something pushes it: a hit knocks the bodies, weakens the struck bone and its neighbours for a moment, and they
// come back; at strength 0 it is a ragdoll. Placed after the modifiers that animate the skeleton (motion matching):
// it reads their pose, the simulator it builds writes the bodies' back.
class FellowPhysicalAnimation3D : public SkeletonModifier3D {
	GDCLASS(FellowPhysicalAnimation3D, SkeletonModifier3D);

public:
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
	void _auto_build();
	void _rebuild_joints();
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

	FellowPhysicalAnimation3D();
};
