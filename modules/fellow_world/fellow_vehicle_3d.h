/**************************************************************************/
/*  fellow_vehicle_3d.h                                                   */
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
#include "scene/3d/physics/rigid_body_3d.h"
#include "scene/resources/curve.h"

class FellowVehicle3D;

// FellowEngine (milestone 77, decision 0185): a wheel of a FellowVehicle3D — its mount, suspension and tyre. A child of
// the vehicle placed where the suspension is mounted (the wheel hangs below it, its axle along the node's X); the
// vehicle moves the node to where the wheel is each step (down the suspension, steered, spinning), so a wheel mesh under
// it follows.
class FellowWheel3D : public Node3D {
	GDCLASS(FellowWheel3D, Node3D);
	friend class FellowVehicle3D;

public:
	// Milestone 101 (decision 0209): how the ground is found — a ray down the suspension (milestone 77's), or the tyre's
	// shape (a sphere of its radius, a cylinder of its radius and width) swept down it: a curb is met by the tyre's
	// front, a gap narrower than the tyre bridged.
	enum ContactShape {
		CONTACT_RAY,
		CONTACT_SPHERE,
		CONTACT_CYLINDER,
	};

private:
	float radius = 0.34f;
	float width = 0.22f; // milestone 101: the tyre's (a cylinder's contact)
	ContactShape contact_shape = CONTACT_RAY;
	RID shape_rid; // the swept shape (made on the main thread when the contact or the size changes)
	float suspension_length = 0.3f; // from the mount to the wheel's centre, fully extended
	float spring_rate = 40000.0f; // N/m
	float damping_compression = 3000.0f; // N·s/m
	float damping_rebound = 4000.0f;
	float grip = 1.0f; // the tyre's peak friction coefficient
	float rolling_resistance = 0.012f; // the share of the load the tyre's rolling costs
	float inertia = 1.2f; // kg·m²
	bool steering = false;
	bool driven = false;
	bool braked = true;
	bool handbrake = false;
	float toe = 0.0f; // milestone 107 (decision 0215): degrees, + the wheel's front turned towards the centre line

	// State (the vehicle's step).
	Transform3D mount; // in the vehicle's space, at entering the tree
	bool mounted = false;
	bool contact = false;
	float compression = 0.0f; // metres the suspension is pushed in
	float bottomed = 0.0f; // metres past its travel (on the bump stop)
	float load = 0.0f; // N
	float omega = 0.0f; // rad/s
	float spin = 0.0f; // radians
	float steer = 0.0f; // radians
	float slip_ratio = 0.0f;
	float slip_angle = 0.0f; // radians
	float slip_speed = 0.0f; // m/s: how fast the tyre slides over the ground
	Vector3 contact_point;
	Vector3 contact_normal;
	Vector3 force; // the tyre's last force (global)
	ObjectID ground;
	RID ground_body; // the ground's body, when it is a physics body
	float ground_friction = 1.0f;
	bool anchored = false; // holding where it stands (nearly still)
	Vector3 anchor; // where, in the ground body's space (or the world's)
	RID anchor_body;

	void _update_shape();

protected:
	void _notification(int p_what);
	static void _bind_methods();

public:
	PackedStringArray get_configuration_warnings() const override;

	void set_radius(float p_radius) {
		radius = MAX(0.05f, p_radius);
		_update_shape();
	}
	float get_radius() const { return radius; }
	void set_width(float p_width) {
		width = MAX(0.02f, p_width);
		_update_shape();
	}
	float get_width() const { return width; }
	void set_contact_shape(ContactShape p_shape) {
		contact_shape = p_shape;
		_update_shape();
	}
	ContactShape get_contact_shape() const { return contact_shape; }
	void set_suspension_length(float p_length) { suspension_length = MAX(0.0f, p_length); }
	float get_suspension_length() const { return suspension_length; }
	void set_spring_rate(float p_rate) { spring_rate = MAX(0.0f, p_rate); }
	float get_spring_rate() const { return spring_rate; }
	void set_damping_compression(float p_damping) { damping_compression = MAX(0.0f, p_damping); }
	float get_damping_compression() const { return damping_compression; }
	void set_damping_rebound(float p_damping) { damping_rebound = MAX(0.0f, p_damping); }
	float get_damping_rebound() const { return damping_rebound; }
	void set_grip(float p_grip) { grip = MAX(0.0f, p_grip); }
	float get_grip() const { return grip; }
	void set_rolling_resistance(float p_coefficient) { rolling_resistance = MAX(0.0f, p_coefficient); }
	float get_rolling_resistance() const { return rolling_resistance; }
	void set_inertia(float p_inertia) { inertia = MAX(0.01f, p_inertia); }
	float get_inertia() const { return inertia; }
	void set_steering(bool p_enabled) { steering = p_enabled; }
	bool is_steering() const { return steering; }
	void set_driven(bool p_enabled) { driven = p_enabled; }
	bool is_driven() const { return driven; }
	void set_braked(bool p_enabled) { braked = p_enabled; }
	bool is_braked() const { return braked; }
	void set_handbrake(bool p_enabled) { handbrake = p_enabled; }
	bool is_handbrake() const { return handbrake; }
	void set_toe(float p_degrees) { toe = CLAMP(p_degrees, -45.0f, 45.0f); }
	float get_toe() const { return toe; }

	bool is_in_contact() const { return contact; }
	float get_compression() const { return compression; }
	float get_load() const { return load; }
	float get_rpm() const { return omega * 60.0f / Math::TAU; }
	float get_slip_ratio() const { return slip_ratio; }
	float get_slip_angle() const { return slip_angle; }
	float get_slip_speed() const { return slip_speed; }
	Vector3 get_contact_normal() const { return contact_normal; }
	Vector3 get_contact_point() const { return contact_point; }
	ObjectID get_ground_id() const { return ground; } // FellowEngine milestone 88: what the tyre stands on
	Node3D *get_ground() const;

	~FellowWheel3D();
};

VARIANT_ENUM_CAST(FellowWheel3D::ContactShape);

// FellowEngine (milestone 77, decision 0185): a car — a RigidBody3D on FellowWheel3D children: each wheel's suspension
// a spring and damper from a ray down from its mount, its tyre's force from its slip (ratio and angle, combined, on a
// magic-formula curve) and its load, an engine with a torque curve driving the driven wheels through a gearbox (shifting
// itself) and a differential, brakes, a handbrake, anti-roll bars, steering that lessens with speed, drag and downforce;
// stepped in substeps of the physics step. The game sets throttle, brake, steering and handbrake.
class FellowVehicle3D : public RigidBody3D {
	GDCLASS(FellowVehicle3D, RigidBody3D);

public:
	enum Differential {
		DIFFERENTIAL_OPEN, // equal torque to each driven wheel
		DIFFERENTIAL_LOCKED, // the driven wheels of an axle turn together
	};

	struct GlobalStats {
		SafeNumeric<uint64_t> steps;
		SafeNumeric<uint64_t> step_usec;
	};
	static GlobalStats global_stats;

private:
	// Inputs.
	float throttle = 0.0f;
	float brake = 0.0f;
	float steering_input = 0.0f;
	bool handbrake_input = false;
	// Engine and drivetrain.
	float max_torque = 400.0f; // N·m
	float engine_inertia = 0.25f; // kg·m²: the engine, the flywheel and the clutch
	float engine_braking = 70.0f; // N·m against the engine at max_rpm with the throttle closed
	float idle_rpm = 900.0f;
	float peak_rpm = 4500.0f;
	float max_rpm = 7000.0f;
	Ref<Curve> torque_curve; // over rpm / max_rpm: the share of max_torque (empty: a curve peaking at peak_rpm)
	PackedFloat32Array gear_ratios = { 3.6f, 2.2f, 1.5f, 1.15f, 0.92f, 0.76f };
	float reverse_ratio = 3.4f;
	float max_reverse_speed = 8.0f; // m/s
	bool traction_control = true;
	bool anti_lock_brakes = true;
	float final_drive = 3.7f;
	float drivetrain_efficiency = 0.9f;
	bool automatic = true;
	float shift_up_rpm = 6300.0f;
	float shift_down_rpm = 2600.0f;
	float shift_time = 0.25f;
	Differential differential = DIFFERENTIAL_OPEN;
	// Brakes and steering.
	float brake_torque = 2500.0f; // N·m a wheel at full brake
	float handbrake_torque = 3500.0f;
	float max_steer = 35.0f; // degrees
	float steer_at_speed = 0.35f; // the share of max_steer at 100 km/h
	float steer_speed = 4.0f; // radians a second the wheels turn
	// Body.
	float anti_roll = 8000.0f; // N/m between the wheels of an axle
	float drag_area = 0.7f; // Cd·A, m²
	float downforce_area = 0.3f; // Cl·A, m²
	int substeps = 4;

	// State.
	LocalVector<FellowWheel3D *> wheels;
	int gear = 1; // -1 reverse, 0 neutral, 1..n
	float engine_rpm = 900.0f;
	float shift_timer = 0.0f;
	float gear_time = 0.0f; // seconds in this gear
	float held_time = 0.0f; // seconds stopped with the brake held
	float steer_now = 0.0f;
	float speed = 0.0f; // m/s along the body's forward
	uint64_t last_step_usec = 0;

	void _gather_wheels();
	float _engine_torque(float p_rpm) const;
	float _gear_ratio() const;
	void _update_wheel_nodes();
	void _shape_contact(FellowWheel3D *w, const Transform3D &p_mount, const Vector3 &p_up, PhysicsDirectSpaceState3D *p_space, const HashSet<RID> &p_exclude);

protected:
	void _notification(int p_what);
	static void _bind_methods();
	void _body_state_changed(PhysicsDirectBodyState3D *p_state) override;

public:
	PackedStringArray get_configuration_warnings() const override;

	void wheel_entered(FellowWheel3D *p_wheel);
	void wheel_exited(FellowWheel3D *p_wheel);

	void set_throttle(float p_value) { throttle = CLAMP(p_value, 0.0f, 1.0f); }
	float get_throttle() const { return throttle; }
	void set_brake(float p_value) { brake = CLAMP(p_value, 0.0f, 1.0f); }
	float get_brake() const { return brake; }
	void set_steering(float p_value) { steering_input = CLAMP(p_value, -1.0f, 1.0f); }
	float get_steering() const { return steering_input; }
	void set_handbrake(bool p_enabled) { handbrake_input = p_enabled; }
	bool is_handbrake() const { return handbrake_input; }

	float get_speed() const { return speed; }
	float get_speed_kmh() const { return speed * 3.6f; }
	float get_engine_rpm() const { return engine_rpm; }
	int get_gear() const { return gear; }
	void set_gear(int p_gear);
	Dictionary get_stats() const;
	static Dictionary get_global_stats();
	const LocalVector<FellowWheel3D *> &get_wheels() const { return wheels; } // milestone 107: FellowVehicleDamage3D's
	static void reset_global_stats();

	void set_max_torque(float p_torque) { max_torque = MAX(0.0f, p_torque); }
	float get_max_torque() const { return max_torque; }
	void set_engine_braking(float p_torque) { engine_braking = MAX(0.0f, p_torque); }
	float get_engine_braking() const { return engine_braking; }
	void set_engine_inertia(float p_inertia) { engine_inertia = MAX(0.0f, p_inertia); }
	float get_engine_inertia() const { return engine_inertia; }
	void set_idle_rpm(float p_rpm) { idle_rpm = MAX(100.0f, p_rpm); }
	float get_idle_rpm() const { return idle_rpm; }
	void set_peak_rpm(float p_rpm) { peak_rpm = MAX(500.0f, p_rpm); }
	float get_peak_rpm() const { return peak_rpm; }
	void set_max_rpm(float p_rpm) { max_rpm = MAX(1000.0f, p_rpm); }
	float get_max_rpm() const { return max_rpm; }
	void set_torque_curve(const Ref<Curve> &p_curve) { torque_curve = p_curve; }
	Ref<Curve> get_torque_curve() const { return torque_curve; }
	void set_gear_ratios(const PackedFloat32Array &p_ratios);
	PackedFloat32Array get_gear_ratios() const { return gear_ratios; }
	void set_reverse_ratio(float p_ratio) { reverse_ratio = MAX(0.1f, p_ratio); }
	float get_reverse_ratio() const { return reverse_ratio; }
	void set_traction_control(bool p_enabled) { traction_control = p_enabled; }
	bool has_traction_control() const { return traction_control; }
	void set_anti_lock_brakes(bool p_enabled) { anti_lock_brakes = p_enabled; }
	bool has_anti_lock_brakes() const { return anti_lock_brakes; }
	void set_max_reverse_speed(float p_speed) { max_reverse_speed = MAX(0.0f, p_speed); }
	float get_max_reverse_speed() const { return max_reverse_speed; }
	void set_final_drive(float p_ratio) { final_drive = MAX(0.1f, p_ratio); }
	float get_final_drive() const { return final_drive; }
	void set_drivetrain_efficiency(float p_efficiency) { drivetrain_efficiency = CLAMP(p_efficiency, 0.1f, 1.0f); }
	float get_drivetrain_efficiency() const { return drivetrain_efficiency; }
	void set_automatic(bool p_enabled) { automatic = p_enabled; }
	bool is_automatic() const { return automatic; }
	void set_shift_up_rpm(float p_rpm) { shift_up_rpm = MAX(500.0f, p_rpm); }
	float get_shift_up_rpm() const { return shift_up_rpm; }
	void set_shift_down_rpm(float p_rpm) { shift_down_rpm = MAX(300.0f, p_rpm); }
	float get_shift_down_rpm() const { return shift_down_rpm; }
	void set_shift_time(float p_seconds) { shift_time = MAX(0.0f, p_seconds); }
	float get_shift_time() const { return shift_time; }
	void set_differential(Differential p_differential) { differential = p_differential; }
	Differential get_differential() const { return differential; }
	void set_brake_torque(float p_torque) { brake_torque = MAX(0.0f, p_torque); }
	float get_brake_torque() const { return brake_torque; }
	void set_handbrake_torque(float p_torque) { handbrake_torque = MAX(0.0f, p_torque); }
	float get_handbrake_torque() const { return handbrake_torque; }
	void set_max_steer(float p_degrees) { max_steer = CLAMP(p_degrees, 0.0f, 80.0f); }
	float get_max_steer() const { return max_steer; }
	void set_steer_at_speed(float p_share) { steer_at_speed = CLAMP(p_share, 0.0f, 1.0f); }
	float get_steer_at_speed() const { return steer_at_speed; }
	void set_steer_speed(float p_speed) { steer_speed = MAX(0.1f, p_speed); }
	float get_steer_speed() const { return steer_speed; }
	void set_anti_roll(float p_rate) { anti_roll = MAX(0.0f, p_rate); }
	float get_anti_roll() const { return anti_roll; }
	void set_drag_area(float p_area) { drag_area = MAX(0.0f, p_area); }
	float get_drag_area() const { return drag_area; }
	void set_downforce_area(float p_area) { downforce_area = MAX(0.0f, p_area); }
	float get_downforce_area() const { return downforce_area; }
	void set_substeps(int p_substeps) { substeps = CLAMP(p_substeps, 1, 16); }
	int get_substeps() const { return substeps; }

	FellowVehicle3D();
};

VARIANT_ENUM_CAST(FellowVehicle3D::Differential);
