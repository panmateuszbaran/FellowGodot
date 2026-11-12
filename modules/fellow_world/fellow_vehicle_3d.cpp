/**************************************************************************/
/*  fellow_vehicle_3d.cpp                                                 */
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

#include "fellow_vehicle_3d.h"

#include "core/config/engine.h"
#include "core/object/class_db.h"
#include "core/os/os.h"
#include "servers/physics_3d/physics_server_3d.h"

namespace {

const float AIR_DENSITY = 1.225f;
const float ASSIST_SLIP = 0.15f; // the slip ratio the traction control and the anti-lock brakes keep under
const float REVERSE_DELAY = 0.5f; // seconds stopped with the brake held before the gearbox takes reverse
const float ANCHOR_SPEED = 0.3f; // m/s: under this at its contact (along and across) a tyre holds where it stands
const float ANCHOR_SLACK = 0.03f; // m: the anchor's spring gives the tyre's whole grip this far from it
const float BUMP_STOP = 10.0f; // the bump stop's stiffness over the spring's
const float SLIP_SPEED_MIN = 1.5f; // m/s: slips over less are taken as over this (no division by a standing wheel's speed)

// The tyre's force over its combined slip (a magic-formula curve): rising steeply, peaking at 0.13, 0.8 of the peak at
// 0.5, 0.7 sliding at 1.
const float TYRE_B = 13.0f;
const float TYRE_C = 1.6f;
const float TYRE_E = 0.3f;

float tyre_curve(float p_slip) {
	const float bs = TYRE_B * p_slip;
	return Math::sin(TYRE_C * Math::atan(bs - TYRE_E * (bs - Math::atan(bs))));
}

// The curve and its slope over the slip.
float tyre_curve_slope(float p_slip, float *r_slope) {
	const float bs = TYRE_B * p_slip;
	const float phi = bs - TYRE_E * (bs - Math::atan(bs));
	const float dphi = TYRE_B * (1.0f - TYRE_E + TYRE_E / (1.0f + bs * bs));
	const float psi = TYRE_C * Math::atan(phi);
	*r_slope = Math::cos(psi) * TYRE_C * dphi / (1.0f + phi * phi);
	return Math::sin(psi);
}

// The tyre's longitudinal pull (N) at a wheel's spin, the lateral slip held, and its slope over the spin.
struct TyreAt {
	float r, v_long, denom, sy, peak;
	float operator()(float p_omega, float *r_slope) const {
		const float sx = (p_omega * r - v_long) / denom;
		const float sl2 = sx * sx + sy * sy;
		if (sl2 < 1e-12f) {
			*r_slope = peak * TYRE_B * TYRE_C * r / denom;
			return peak * TYRE_B * TYRE_C * sx;
		}
		const float sl = Math::sqrt(sl2);
		float dg;
		const float g = tyre_curve_slope(sl, &dg);
		*r_slope = peak * (dg * sx * sx / sl2 + g / sl * sy * sy / sl2) * r / denom;
		return peak * g * sx / sl;
	}
};

// A wheel's spin after a step, implicitly: the w' with w' = w + h/I (drive - r pull(w') - hold sign(w')), the brake
// holding a stopped wheel when it can. The left side less the right grows with w' (the tyre's curve falls slower
// past its peak than the wheel's inertia allows over a step), so the root is bracketed and found by Newton's steps
// (over the curve's own slope) kept inside the bracket.
float solve_spin(float p_omega, float p_drive, float p_hold, float p_inertia, float p_h, const TyreAt &p_tyre) {
	const float a = p_h / p_inertia;
	float slope;
	auto f = [&](float p_w, float p_sign) {
		return p_w - p_omega - a * (p_drive - p_tyre.r * p_tyre(p_w, &slope) - p_hold * p_sign);
	};
	const float span = a * (Math::abs(p_drive) + p_hold + p_tyre.r * p_tyre.peak) + 1e-3f;
	float lo = p_omega - span;
	float hi = p_omega + span;
	float sign = 0.0f;
	if (p_hold > 0.0f) {
		const float up = f(0.0f, 1.0f);
		const float down = up - 2.0f * a * p_hold; // f(0, -1)
		if (down <= 0.0f && up >= 0.0f) {
			return 0.0f; // held
		}
		if (up < 0.0f) {
			lo = 0.0f;
			hi = MAX(p_omega, 0.0f) + span;
			sign = 1.0f;
		} else {
			lo = MIN(p_omega, 0.0f) - span;
			hi = 0.0f;
			sign = -1.0f;
		}
	}
	float x = CLAMP(p_omega, lo, hi);
	for (int it = 0; it < 24; it++) {
		const float fx = f(x, sign);
		if (fx > 0.0f) {
			hi = x;
		} else {
			lo = x;
		}
		const float d = 1.0f + a * p_tyre.r * slope;
		float next = d > 1e-6f ? x - fx / d : 0.5f * (lo + hi);
		if (!(next > lo && next < hi)) {
			next = 0.5f * (lo + hi);
		}
		if (Math::abs(next - x) < 1e-3f) {
			return next;
		}
		x = next;
	}
	return x;
}

} // namespace

FellowVehicle3D::GlobalStats FellowVehicle3D::global_stats;

// ---------------------------------------------------------------------------------------------------------------- wheel

Node3D *FellowWheel3D::get_ground() const {
	return ObjectDB::get_instance<Node3D>(ground);
}

void FellowWheel3D::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_ENTER_TREE: {
			if (FellowVehicle3D *v = Object::cast_to<FellowVehicle3D>(get_parent())) {
				if (!mounted) {
					// The node marks the wheel's centre with the suspension fully extended: the mount is that far above.
					mount = get_transform();
					mount.origin += mount.basis.get_column(1).normalized() * suspension_length;
					mounted = true;
				}
				v->wheel_entered(this);
			}
		} break;
		case NOTIFICATION_EXIT_TREE: {
			if (FellowVehicle3D *v = Object::cast_to<FellowVehicle3D>(get_parent())) {
				v->wheel_exited(this);
			}
		} break;
	}
}

PackedStringArray FellowWheel3D::get_configuration_warnings() const {
	PackedStringArray warnings = Node3D::get_configuration_warnings();
	if (!Object::cast_to<FellowVehicle3D>(get_parent())) {
		warnings.push_back(RTR("A FellowWheel3D is a wheel of its parent, which must be a FellowVehicle3D."));
	}
	return warnings;
}

void FellowWheel3D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_radius", "radius"), &FellowWheel3D::set_radius);
	ClassDB::bind_method(D_METHOD("get_radius"), &FellowWheel3D::get_radius);
	ClassDB::bind_method(D_METHOD("set_suspension_length", "length"), &FellowWheel3D::set_suspension_length);
	ClassDB::bind_method(D_METHOD("get_suspension_length"), &FellowWheel3D::get_suspension_length);
	ClassDB::bind_method(D_METHOD("set_spring_rate", "rate"), &FellowWheel3D::set_spring_rate);
	ClassDB::bind_method(D_METHOD("get_spring_rate"), &FellowWheel3D::get_spring_rate);
	ClassDB::bind_method(D_METHOD("set_damping_compression", "damping"), &FellowWheel3D::set_damping_compression);
	ClassDB::bind_method(D_METHOD("get_damping_compression"), &FellowWheel3D::get_damping_compression);
	ClassDB::bind_method(D_METHOD("set_damping_rebound", "damping"), &FellowWheel3D::set_damping_rebound);
	ClassDB::bind_method(D_METHOD("get_damping_rebound"), &FellowWheel3D::get_damping_rebound);
	ClassDB::bind_method(D_METHOD("set_grip", "grip"), &FellowWheel3D::set_grip);
	ClassDB::bind_method(D_METHOD("get_grip"), &FellowWheel3D::get_grip);
	ClassDB::bind_method(D_METHOD("set_rolling_resistance", "coefficient"), &FellowWheel3D::set_rolling_resistance);
	ClassDB::bind_method(D_METHOD("get_rolling_resistance"), &FellowWheel3D::get_rolling_resistance);
	ClassDB::bind_method(D_METHOD("set_inertia", "inertia"), &FellowWheel3D::set_inertia);
	ClassDB::bind_method(D_METHOD("get_inertia"), &FellowWheel3D::get_inertia);
	ClassDB::bind_method(D_METHOD("set_steering", "enabled"), &FellowWheel3D::set_steering);
	ClassDB::bind_method(D_METHOD("is_steering"), &FellowWheel3D::is_steering);
	ClassDB::bind_method(D_METHOD("set_driven", "enabled"), &FellowWheel3D::set_driven);
	ClassDB::bind_method(D_METHOD("is_driven"), &FellowWheel3D::is_driven);
	ClassDB::bind_method(D_METHOD("set_braked", "enabled"), &FellowWheel3D::set_braked);
	ClassDB::bind_method(D_METHOD("is_braked"), &FellowWheel3D::is_braked);
	ClassDB::bind_method(D_METHOD("set_handbrake", "enabled"), &FellowWheel3D::set_handbrake);
	ClassDB::bind_method(D_METHOD("is_handbrake"), &FellowWheel3D::is_handbrake);
	ClassDB::bind_method(D_METHOD("is_in_contact"), &FellowWheel3D::is_in_contact);
	ClassDB::bind_method(D_METHOD("get_compression"), &FellowWheel3D::get_compression);
	ClassDB::bind_method(D_METHOD("get_load"), &FellowWheel3D::get_load);
	ClassDB::bind_method(D_METHOD("get_rpm"), &FellowWheel3D::get_rpm);
	ClassDB::bind_method(D_METHOD("get_slip_ratio"), &FellowWheel3D::get_slip_ratio);
	ClassDB::bind_method(D_METHOD("get_slip_angle"), &FellowWheel3D::get_slip_angle);
	ClassDB::bind_method(D_METHOD("get_contact_point"), &FellowWheel3D::get_contact_point);
	ClassDB::bind_method(D_METHOD("get_ground"), &FellowWheel3D::get_ground);

	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "radius", PROPERTY_HINT_RANGE, "0.05,2,0.001,or_greater,suffix:m"), "set_radius", "get_radius");
	ADD_GROUP("Suspension", "");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "suspension_length", PROPERTY_HINT_RANGE, "0,2,0.001,or_greater,suffix:m"), "set_suspension_length", "get_suspension_length");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "spring_rate", PROPERTY_HINT_RANGE, "0,500000,1,or_greater,suffix:N/m"), "set_spring_rate", "get_spring_rate");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "damping_compression", PROPERTY_HINT_RANGE, "0,50000,1,or_greater,suffix:N·s/m"), "set_damping_compression", "get_damping_compression");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "damping_rebound", PROPERTY_HINT_RANGE, "0,50000,1,or_greater,suffix:N·s/m"), "set_damping_rebound", "get_damping_rebound");
	ADD_GROUP("Tyre", "");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "grip", PROPERTY_HINT_RANGE, "0,3,0.01,or_greater"), "set_grip", "get_grip");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "rolling_resistance", PROPERTY_HINT_RANGE, "0,0.2,0.001"), "set_rolling_resistance", "get_rolling_resistance");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "inertia", PROPERTY_HINT_RANGE, "0.01,20,0.01,or_greater,suffix:kg·m²"), "set_inertia", "get_inertia");
	ADD_GROUP("Role", "");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "steering"), "set_steering", "is_steering");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "driven"), "set_driven", "is_driven");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "braked"), "set_braked", "is_braked");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "handbrake"), "set_handbrake", "is_handbrake");
}

// -------------------------------------------------------------------------------------------------------------- vehicle

FellowVehicle3D::FellowVehicle3D() {
	set_mass(1400.0f);
	// The node's own drag stands for the air; the project's default damping (0.1 a second) would be a second drag of
	// 2 m/s² at 70 km/h.
	set_linear_damp_mode(DAMP_MODE_REPLACE);
	set_angular_damp_mode(DAMP_MODE_REPLACE);
	set_linear_damp(0.0f);
	set_angular_damp(0.0f);
}

void FellowVehicle3D::wheel_entered(FellowWheel3D *p_wheel) {
	if (!wheels.has(p_wheel)) {
		wheels.push_back(p_wheel);
	}
	update_configuration_warnings();
}

void FellowVehicle3D::wheel_exited(FellowWheel3D *p_wheel) {
	wheels.erase(p_wheel);
	update_configuration_warnings();
}

// The engine's torque at an rpm: the curve's share of max_torque, or (no curve) a smooth rise from 55 % at idle to the
// peak and down to 80 % at max_rpm; none past max_rpm (the limiter).
float FellowVehicle3D::_engine_torque(float p_rpm) const {
	if (p_rpm >= max_rpm) {
		return 0.0f;
	}
	if (torque_curve.is_valid()) {
		return max_torque * torque_curve->sample_baked(CLAMP(p_rpm / max_rpm, 0.0f, 1.0f));
	}
	float share;
	if (p_rpm <= peak_rpm) {
		const float u = CLAMP((p_rpm - idle_rpm) / MAX(peak_rpm - idle_rpm, 1.0f), 0.0f, 1.0f);
		share = 0.55f + 0.45f * (1.0f - (1.0f - u) * (1.0f - u));
	} else {
		const float u = CLAMP((p_rpm - peak_rpm) / MAX(max_rpm - peak_rpm, 1.0f), 0.0f, 1.0f);
		share = 1.0f - 0.2f * u * u;
	}
	return max_torque * share;
}

float FellowVehicle3D::_gear_ratio() const {
	if (gear < 0) {
		return -reverse_ratio;
	}
	if (gear == 0 || gear_ratios.is_empty()) {
		return 0.0f;
	}
	return gear_ratios[MIN(gear, gear_ratios.size()) - 1];
}

void FellowVehicle3D::set_gear(int p_gear) {
	gear = CLAMP(p_gear, -1, int(gear_ratios.size()));
	shift_timer = shift_time;
}

void FellowVehicle3D::set_gear_ratios(const PackedFloat32Array &p_ratios) {
	gear_ratios = p_ratios;
	gear = CLAMP(gear, -1, int(gear_ratios.size()));
}

// The wheels' nodes where the wheels are: down their suspension, steered, turned by their spin.
void FellowVehicle3D::_update_wheel_nodes() {
	for (FellowWheel3D *w : wheels) {
		const Vector3 up = w->mount.basis.get_column(1).normalized();
		const float drop = w->suspension_length - w->compression;
		Basis b = w->mount.basis;
		b = Basis(up, w->steer) * b;
		b = b * Basis(Vector3(1, 0, 0), -w->spin); // rolling forward (-Z): the top towards -Z
		w->set_transform(Transform3D(b, w->mount.origin - up * drop));
	}
}

void FellowVehicle3D::_body_state_changed(PhysicsDirectBodyState3D *p_state) {
	RigidBody3D::_body_state_changed(p_state);
	if (wheels.is_empty() || Engine::get_singleton()->is_editor_hint()) {
		return;
	}
	const uint64_t t0 = OS::get_singleton()->get_ticks_usec();
	const float dt = p_state->get_step();
	if (dt <= 0.0f) {
		return;
	}
	const Transform3D xf = p_state->get_transform();
	const Vector3 body_up = xf.basis.get_column(1).normalized();
	const Vector3 body_fwd = -xf.basis.get_column(2).normalized();
	PhysicsDirectSpaceState3D *space = p_state->get_space_state();
	HashSet<RID> exclude;
	exclude.insert(get_rid());

	// The ground under each wheel: a ray down its suspension from its mount (once a step).
	const uint32_t nw = wheels.size();
	for (FellowWheel3D *w : wheels) {
		const Transform3D mx = xf * w->mount;
		const Vector3 up = mx.basis.get_column(1).normalized();
		PhysicsDirectSpaceState3D::RayParameters rq;
		rq.from = mx.origin;
		rq.to = mx.origin - up * (w->suspension_length + w->radius);
		rq.exclude = exclude;
		rq.collision_mask = get_collision_mask();
		PhysicsDirectSpaceState3D::RayResult rr;
		w->contact = space && space->intersect_ray(rq, rr);
		if (w->contact) {
			const float dist = rq.from.distance_to(rr.position);
			const float pushed = w->suspension_length + w->radius - dist;
			w->compression = CLAMP(pushed, 0.0f, w->suspension_length);
			w->bottomed = MAX(0.0f, pushed - w->suspension_length);
			w->contact_point = rr.position;
			w->contact_normal = rr.normal.normalized();
			w->ground = rr.collider_id;
			// A body's velocity is taken into the slips (a moving platform, a conveyor); a rigid one is pushed back.
			w->ground_body = Object::cast_to<PhysicsBody3D>(rr.collider) ? rr.rid : RID();
			// The surface's grip: the ground's friction (its physics material's; 1 without one) times the tyre's.
			w->ground_friction = w->ground_body.is_valid() ? float(PhysicsServer3D::get_singleton()->body_get_param(w->ground_body, PhysicsServer3D::BODY_PARAM_FRICTION)) : 1.0f;
		} else {
			w->compression = 0.0f;
			w->bottomed = 0.0f;
			w->anchored = false;
			w->load = 0.0f;
			w->ground = ObjectID();
			w->ground_body = RID();
		}
	}

	// The steering, eased, less at speed.
	speed = p_state->get_linear_velocity().dot(body_fwd);
	const float steer_scale = Math::lerp(1.0f, steer_at_speed, CLAMP(Math::abs(speed) / 27.8f, 0.0f, 1.0f));
	const float steer_target = steering_input * Math::deg_to_rad(max_steer) * steer_scale;
	steer_now = Math::move_toward(steer_now, steer_target, steer_speed * dt);

	// The gearbox (automatic): up past shift_up_rpm, down under shift_down_rpm; reverse when stopped with the brake held
	// (REVERSE_DELAY, not with the handbrake on) and no throttle (the brake then drives it backwards), forward again with
	// the throttle.
	float drive_input = throttle;
	float brake_input = brake;
	float wheel_radius = 0.0f;
	int driven_count = 0;
	for (const FellowWheel3D *w : wheels) {
		if (w->driven) {
			wheel_radius += w->radius;
			driven_count++;
		}
	}
	wheel_radius = driven_count > 0 ? wheel_radius / float(driven_count) : 0.35f;
	gear_time += dt;
	const bool stopped_braking = Math::abs(speed) < 1.0f && brake > 0.1f && throttle < 0.05f;
	held_time = stopped_braking ? held_time + dt : 0.0f;
	if (automatic) {
		shift_timer = MAX(0.0f, shift_timer - dt);
		if (gear < 0) {
			if (throttle > 0.1f && speed > -1.0f) {
				gear = 1;
				shift_timer = shift_time;
			} else {
				drive_input = brake;
				brake_input = throttle;
			}
		} else if (stopped_braking && !handbrake_input && held_time > REVERSE_DELAY) {
			gear = -1;
			shift_timer = shift_time;
			drive_input = brake;
			brake_input = 0.0f;
		} else if (gear == 0) {
			gear = 1;
		} else if (shift_timer <= 0.0f && gear_time > 4.0f * shift_time + 0.5f) {
			// From the speed over the ground, not the driven wheels' (a spinning wheel would shift up and down). The
			// shift up comes earlier with less throttle (from 1.5 times the shift down at none to shift_up_rpm at
			// full), never so early that the next gear would be under the shift down; with the throttle floored the
			// gearbox kicks down when the lower gear has room.
			const int count = int(gear_ratios.size());
			const float to_rpm = 60.0f / float(Math::TAU) / MAX(wheel_radius, 0.05f) * final_drive;
			const float road_rpm = Math::abs(speed) * _gear_ratio() * to_rpm;
			float up_rpm = shift_up_rpm;
			if (gear < count) {
				const float step = gear_ratios[gear - 1] / MAX(gear_ratios[gear], 0.01f);
				up_rpm = MIN(shift_up_rpm, MAX(Math::lerp(shift_down_rpm * 1.5f, shift_up_rpm, CLAMP(throttle, 0.0f, 1.0f)), shift_down_rpm * step * 1.15f));
			}
			const float lower_rpm = gear > 1 ? Math::abs(speed) * gear_ratios[gear - 2] * to_rpm : 0.0f;
			// Up also when the engine is there with its wheels spinning, if the next gear would not be under the
			// shift down at once.
			const bool spinning_up = engine_rpm > shift_up_rpm && gear < count && road_rpm * gear_ratios[MIN(gear, count - 1)] / MAX(_gear_ratio(), 0.01f) > shift_down_rpm * 1.2f;
			const bool kickdown = throttle > 0.9f && gear > 1 && lower_rpm < shift_up_rpm * 0.85f;
			if ((road_rpm > up_rpm || spinning_up) && gear < count) {
				gear++;
				shift_timer = shift_time;
				gear_time = 0.0f;
				emit_signal(SNAME("gear_changed"), gear);
			} else if ((road_rpm < shift_down_rpm || kickdown) && gear > 1 && lower_rpm < MAX(up_rpm, shift_down_rpm * 1.5f) * 0.9f) {
				// Down only if the lower gear would not be past its shift up at once.
				gear--;
				shift_timer = shift_time;
				gear_time = 0.0f;
				emit_signal(SNAME("gear_changed"), gear);
			}
		}
	}
	const float ratio = _gear_ratio() * final_drive;
	const bool clutch = shift_timer <= 0.0f || !automatic; // no drive while shifting

	int driven = 0;
	for (FellowWheel3D *w : wheels) {
		driven += w->driven ? 1 : 0;
		w->steer = w->steering ? steer_now : 0.0f;
	}

	// Anti-roll: the wheels paired by axle (the same distance along the body, opposite sides).
	LocalVector<int> pair;
	pair.resize(nw);
	for (uint32_t i = 0; i < nw; i++) {
		pair[i] = -1;
		for (uint32_t j = 0; j < nw; j++) {
			if (i != j && Math::abs(wheels[i]->mount.origin.z - wheels[j]->mount.origin.z) < 0.2f && wheels[i]->mount.origin.x * wheels[j]->mount.origin.x < 0.0f) {
				pair[i] = j;
				break;
			}
		}
	}

	// The substeps. Each reads the body's velocities once, works out every wheel's suspension and tyre forces from them
	// and applies them together (no wheel sees another's impulse first: the same forces whichever wheel comes first). A
	// tyre's impulse is limited to what would stop its own contact's slip, shared by the wheels on the ground (stiff
	// tyres at walking pace would otherwise overshoot); a wheel's spin is integrated implicitly over the tyre's slope.
	const int n = MAX(1, substeps);
	const float h = dt / float(n);
	for (FellowWheel3D *w : wheels) {
		w->force = Vector3();
	}
	int contacts = 0;
	for (const FellowWheel3D *w : wheels) {
		contacts += w->contact ? 1 : 0;
	}
	const float share = 1.0f / float(MAX(1, contacts));
	const float inv_mass = p_state->get_inverse_mass();
	const Basis inv_inertia = p_state->get_inverse_inertia_tensor();
	const Vector3 com = p_state->get_center_of_mass();
	auto effective_mass = [&](const Vector3 &p_rel, const Vector3 &p_dir) {
		const Vector3 rxd = (p_rel - com).cross(p_dir);
		const float k = inv_mass + rxd.dot(inv_inertia.xform(rxd));
		return k > 1e-9f ? 1.0f / k : 0.0f;
	};
	// Each wheel's frame on the ground for the whole step (the body's place does not change over the substeps, only
	// its velocities): the contact, the suspension's axis, the tyre's axes, the masses felt along them, the ground's
	// body.
	struct WheelFrame {
		Vector3 rel, up, fwd, side, ground_rel;
		float m_fwd = 0.0f, m_side = 0.0f;
		PhysicsDirectBodyState3D *ground = nullptr;
		bool ground_rigid = false;
		bool tyre = false;
	};
	LocalVector<WheelFrame> frames;
	LocalVector<Vector3> impulse;
	frames.resize(nw);
	impulse.resize(nw);
	for (uint32_t i = 0; i < nw; i++) {
		FellowWheel3D *w = wheels[i];
		WheelFrame &fr = frames[i];
		if (!w->contact) {
			continue;
		}
		const Basis mb = xf.basis * w->mount.basis;
		fr.rel = w->contact_point - xf.origin;
		fr.up = mb.get_column(1).normalized();
		const Vector3 n_ = w->contact_normal;
		Vector3 fwd = Basis(fr.up, w->steer).xform(mb.xform(Vector3(0, 0, -1)));
		fwd -= n_ * fwd.dot(n_);
		fr.tyre = fwd.length_squared() > 1e-8f;
		if (fr.tyre) {
			fr.fwd = fwd.normalized();
			fr.side = n_.cross(fr.fwd).normalized(); // the wheel's left
			fr.m_fwd = effective_mass(fr.rel, fr.fwd) * share;
			fr.m_side = effective_mass(fr.rel, fr.side) * share;
		}
		if (w->ground_body.is_valid()) {
			PhysicsServer3D *ps = PhysicsServer3D::get_singleton();
			fr.ground = ps->body_get_direct_state(w->ground_body);
			if (fr.ground) {
				fr.ground_rel = w->contact_point - fr.ground->get_transform().origin;
				const PhysicsServer3D::BodyMode mode = ps->body_get_mode(w->ground_body);
				fr.ground_rigid = mode == PhysicsServer3D::BODY_MODE_RIGID || mode == PhysicsServer3D::BODY_MODE_RIGID_LINEAR;
			}
		}
	}
	for (int s = 0; s < n; s++) {
		// The engine: its rpm from the driven wheels (an idle clutch below idle), its torque to them; none past the
		// reverse speed's limit.
		float wheel_omega = 0.0f;
		for (FellowWheel3D *w : wheels) {
			wheel_omega += w->driven ? w->omega : 0.0f;
		}
		wheel_omega = driven > 0 ? wheel_omega / float(driven) : 0.0f;
		engine_rpm = MAX(idle_rpm, Math::abs(wheel_omega * ratio) * 60.0f / Math::TAU);
		float engine = clutch && ratio != 0.0f ? drive_input * _engine_torque(engine_rpm) : 0.0f;
		if (clutch && ratio != 0.0f && engine_rpm > idle_rpm * 1.1f) {
			// The engine's own friction when the throttle lifts (engine braking), against its turning.
			engine -= (1.0f - drive_input) * engine_braking * engine_rpm / max_rpm * SIGN(wheel_omega * ratio);
		}
		if (gear < 0) {
			engine *= CLAMP((max_reverse_speed + speed) / MAX(max_reverse_speed * 0.2f, 0.1f), 0.0f, 1.0f);
		}
		const float drive_each = driven > 0 ? engine * ratio * drivetrain_efficiency / float(driven) : 0.0f;

		for (uint32_t i = 0; i < nw; i++) {
			FellowWheel3D *w = wheels[i];
			const WheelFrame &fr = frames[i];
			impulse[i] = Vector3();
			float drive = w->driven ? drive_each : 0.0f;
			const float foot = w->braked ? brake_torque * brake_input : 0.0f;
			// The engine turns with the driven wheels: its inertia through the gears, shared.
			const float spin_inertia = w->inertia + (w->driven && clutch && ratio != 0.0f ? engine_inertia * ratio * ratio / float(driven) : 0.0f);
			// The brakes, the handbrake and the tyre's rolling resistance (its share of the load, at the radius).
			const float brakes = foot + (w->handbrake && handbrake_input ? handbrake_torque : 0.0f);
			const float hold = brakes + w->rolling_resistance * w->load * w->radius;
			if (!w->contact) {
				w->load = 0.0f;
				w->slip_ratio = 0.0f;
				w->slip_angle = 0.0f;
				w->omega += drive / spin_inertia * h;
				if (hold > 0.0f) {
					const float dw = hold / spin_inertia * h;
					w->omega = Math::abs(w->omega) <= dw ? 0.0f : w->omega - SIGN(w->omega) * dw;
				}
				continue;
			}
			const Vector3 n_ = w->contact_normal;
			const Vector3 &rel = fr.rel;
			Vector3 v = p_state->get_velocity_at_local_position(rel);
			if (fr.ground) {
				v -= fr.ground->get_velocity_at_local_position(fr.ground_rel);
			}
			// The suspension: a spring over the compression, a damper over its rate (the contact point's speed along
			// the suspension), the other wheel of the axle's difference through the anti-roll bar.
			const float rate = -v.dot(fr.up);
			float f = w->spring_rate * w->compression + (rate > 0.0f ? w->damping_compression : w->damping_rebound) * rate;
			if (pair[i] >= 0 && wheels[pair[i]]->contact) {
				f += anti_roll * (w->compression - wheels[pair[i]]->compression);
			}
			if (w->bottomed > 0.0f) {
				// Past its travel: the bump stop, ten times the spring, damped (a hard landing does not drop the body
				// onto the ground at once).
				f += BUMP_STOP * w->spring_rate * w->bottomed + (rate > 0.0f ? w->damping_compression : 0.0f) * rate;
			}
			f = MAX(0.0f, f);
			w->load = f;
			Vector3 imp = n_ * (f * h);
			// The tyre: its slips, the force from the curve and the load.
			if (fr.tyre) {
				const Vector3 &fwd = fr.fwd;
				const Vector3 &side = fr.side;
				const float v_long = v.dot(fwd);
				const float v_lat = v.dot(side);
				const float denom = MAX(Math::abs(v_long), SLIP_SPEED_MIN);
				const float sy = v_lat / denom;
				const float peak = w->grip * w->ground_friction * f;
				// The spin over the step (implicitly), then the assists: the traction control keeps the wheel from
				// spinning past ASSIST_SLIP (the drive cut, the wheel braked), the anti-lock brakes from locking past it
				// (the brake eased; not with the handbrake on).
				const TyreAt tyre{ w->radius, v_long, denom, sy, peak };
				w->omega = solve_spin(w->omega, drive, hold, spin_inertia, h, tyre);
				if (traction_control && drive != 0.0f) {
					const float cap = (v_long + SIGN(drive) * ASSIST_SLIP * denom) / w->radius;
					w->omega = drive > 0.0f ? MIN(w->omega, MAX(cap, 0.0f)) : MAX(w->omega, MIN(cap, 0.0f));
				}
				if (anti_lock_brakes && foot > 0.0f && !(w->handbrake && handbrake_input) && Math::abs(v_long) > ASSIST_SLIP * denom) {
					const float least = (v_long - SIGN(v_long) * ASSIST_SLIP * denom) / w->radius;
					w->omega = v_long > 0.0f ? MAX(w->omega, least) : MIN(w->omega, least);
				}
				const float sx = (w->omega * w->radius - v_long) / denom;
				const float sl = Math::sqrt(sx * sx + sy * sy);
				const float pull = sl > 1e-6f ? peak * tyre_curve(sl) / sl : 0.0f;
				float fx = pull * sx;
				float fy = -pull * sy;
				// Nearly still on the ground, the tyre holds where it stands: a stiff spring to an anchor on the ground
				// (on the ground's body, carried with it) across the wheel and, when the brakes hold it, along it too
				// (a free wheel's anchor moves along with it as it rolls), up to the tyre's grip, where it lets go.
				// The slips' forces would need a creeping slip to hold a steady load (a slope, a turning platform).
				bool held = false;
				if (Math::abs(v_long) < ANCHOR_SPEED && Math::abs(v_lat) < ANCHOR_SPEED && peak > 0.0f) {
					const Transform3D gxf = fr.ground ? fr.ground->get_transform() : Transform3D();
					if (!w->anchored || w->anchor_body != w->ground_body) {
						w->anchor = gxf.affine_inverse().xform(w->contact_point);
						w->anchor_body = w->ground_body;
						w->anchored = true;
					}
					Vector3 d = w->contact_point - gxf.xform(w->anchor);
					d -= n_ * d.dot(n_);
					const float k = peak / ANCHOR_SLACK;
					const float ay = -k * d.dot(side) - 1.4f * Math::sqrt(k * fr.m_side) * v_lat;
					// Along the wheel only while its brakes hold it against the pull (else it rolls: the anchor with it).
					const float dx = d.dot(fwd);
					float ax = fx;
					bool locked = brakes > 0.0f && w->omega == 0.0f;
					if (locked) {
						ax = -k * dx - 1.4f * Math::sqrt(k * fr.m_fwd) * v_long;
						if (Math::abs(ax) * w->radius > hold) {
							ax = fx;
							locked = false;
						}
					}
					if (!locked) {
						w->anchor = gxf.affine_inverse().xform(gxf.xform(w->anchor) + fwd * dx);
					}
					if (ax * ax + ay * ay <= peak * peak) {
						fx = ax;
						fy = ay;
						held = true;
					} else {
						w->anchored = false;
					}
				} else {
					w->anchored = false;
				}
				if (!held) {
					// No more than stops the slip at this contact, shared.
					const float lim_y = fr.m_side * Math::abs(v_lat) / h;
					fy = CLAMP(fy, -lim_y, lim_y);
					const float lim_x = fr.m_fwd * Math::abs(w->omega * w->radius - v_long) / h;
					fx = CLAMP(fx, -lim_x, lim_x);
				}
				w->slip_ratio = sx;
				w->slip_angle = Math::atan2(v_lat, MAX(Math::abs(v_long), 0.1f));
				const Vector3 force = fwd * fx + side * fy;
				w->force += force / float(n);
				imp += force * h;
			}
			impulse[i] = imp;
		}
		for (uint32_t i = 0; i < nw; i++) {
			if (wheels[i]->contact) {
				p_state->apply_impulse(impulse[i], frames[i].rel);
				if (frames[i].ground_rigid) {
					frames[i].ground->apply_impulse(-impulse[i], frames[i].ground_rel);
				}
			}
		}
		if (differential == DIFFERENTIAL_LOCKED) {
			// The driven wheels of each axle turn together.
			for (uint32_t i = 0; i < nw; i++) {
				const int j = pair[i];
				if (j > int(i) && wheels[i]->driven && wheels[j]->driven) {
					const float m = (wheels[i]->omega * wheels[i]->inertia + wheels[j]->omega * wheels[j]->inertia) / (wheels[i]->inertia + wheels[j]->inertia);
					wheels[i]->omega = m;
					wheels[j]->omega = m;
				}
			}
		}
		// The air: drag against the velocity, downforce with the speed squared.
		const Vector3 vb = p_state->get_linear_velocity();
		const float vlen = vb.length();
		if (vlen > 0.01f) {
			const Vector3 air = -vb * (0.5f * AIR_DENSITY * drag_area * vlen) - body_up * (0.5f * AIR_DENSITY * downforce_area * vb.dot(body_fwd) * vb.dot(body_fwd));
			p_state->apply_central_impulse(air * h);
		}
	}
	for (FellowWheel3D *w : wheels) {
		w->spin = Math::fposmod(w->spin + w->omega * dt, float(Math::TAU));
	}
	_update_wheel_nodes();
	last_step_usec = OS::get_singleton()->get_ticks_usec() - t0;
	global_stats.steps.increment();
	global_stats.step_usec.add(last_step_usec);
	// Asleep only when nothing is asked of it.
	if (throttle > 0.0f || Math::abs(steering_input) > 0.0f) {
		p_state->set_sleep_state(false);
	}
}

void FellowVehicle3D::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_READY: {
			if (!Engine::get_singleton()->is_editor_hint()) {
				_update_wheel_nodes();
			}
		} break;
	}
}

Dictionary FellowVehicle3D::get_stats() const {
	Dictionary d;
	d["speed_kmh"] = speed * 3.6f;
	d["rpm"] = engine_rpm;
	d["gear"] = gear;
	d["steer_deg"] = Math::rad_to_deg(steer_now);
	d["step_usec"] = int64_t(last_step_usec);
	Array ws;
	for (const FellowWheel3D *w : wheels) {
		Dictionary e;
		e["name"] = w->get_name();
		e["contact"] = w->contact;
		e["compression"] = w->compression;
		e["load"] = w->load;
		e["rpm"] = w->get_rpm();
		e["slip_ratio"] = w->slip_ratio;
		e["slip_angle_deg"] = Math::rad_to_deg(w->slip_angle);
		e["force"] = w->force;
		e["holding"] = w->anchored;
		e["bottomed"] = w->bottomed > 0.0f;
		e["surface_friction"] = w->ground_friction;
		ws.push_back(e);
	}
	d["wheels"] = ws;
	return d;
}

Dictionary FellowVehicle3D::get_global_stats() {
	Dictionary d;
	d["steps"] = int64_t(global_stats.steps.get());
	d["step_usec"] = int64_t(global_stats.step_usec.get());
	return d;
}

void FellowVehicle3D::reset_global_stats() {
	global_stats.steps.set(0);
	global_stats.step_usec.set(0);
}

PackedStringArray FellowVehicle3D::get_configuration_warnings() const {
	PackedStringArray warnings = RigidBody3D::get_configuration_warnings();
	if (wheels.size() < 3) {
		warnings.push_back(RTR("A FellowVehicle3D stands on FellowWheel3D children: add at least three."));
	}
	bool any_driven = false;
	for (const FellowWheel3D *w : wheels) {
		any_driven = any_driven || w->driven;
	}
	if (!wheels.is_empty() && !any_driven) {
		warnings.push_back(RTR("No FellowWheel3D is driven: the engine moves nothing."));
	}
	return warnings;
}

void FellowVehicle3D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_throttle", "value"), &FellowVehicle3D::set_throttle);
	ClassDB::bind_method(D_METHOD("get_throttle"), &FellowVehicle3D::get_throttle);
	ClassDB::bind_method(D_METHOD("set_brake", "value"), &FellowVehicle3D::set_brake);
	ClassDB::bind_method(D_METHOD("get_brake"), &FellowVehicle3D::get_brake);
	ClassDB::bind_method(D_METHOD("set_steering", "value"), &FellowVehicle3D::set_steering);
	ClassDB::bind_method(D_METHOD("get_steering"), &FellowVehicle3D::get_steering);
	ClassDB::bind_method(D_METHOD("set_handbrake", "enabled"), &FellowVehicle3D::set_handbrake);
	ClassDB::bind_method(D_METHOD("is_handbrake"), &FellowVehicle3D::is_handbrake);
	ClassDB::bind_method(D_METHOD("get_speed"), &FellowVehicle3D::get_speed);
	ClassDB::bind_method(D_METHOD("get_speed_kmh"), &FellowVehicle3D::get_speed_kmh);
	ClassDB::bind_method(D_METHOD("get_engine_rpm"), &FellowVehicle3D::get_engine_rpm);
	ClassDB::bind_method(D_METHOD("get_gear"), &FellowVehicle3D::get_gear);
	ClassDB::bind_method(D_METHOD("set_gear", "gear"), &FellowVehicle3D::set_gear);
	ClassDB::bind_method(D_METHOD("get_stats"), &FellowVehicle3D::get_stats);
	ClassDB::bind_static_method("FellowVehicle3D", D_METHOD("get_global_stats"), &FellowVehicle3D::get_global_stats);
	ClassDB::bind_static_method("FellowVehicle3D", D_METHOD("reset_global_stats"), &FellowVehicle3D::reset_global_stats);

	ClassDB::bind_method(D_METHOD("set_max_torque", "torque"), &FellowVehicle3D::set_max_torque);
	ClassDB::bind_method(D_METHOD("get_max_torque"), &FellowVehicle3D::get_max_torque);
	ClassDB::bind_method(D_METHOD("set_idle_rpm", "rpm"), &FellowVehicle3D::set_idle_rpm);
	ClassDB::bind_method(D_METHOD("get_idle_rpm"), &FellowVehicle3D::get_idle_rpm);
	ClassDB::bind_method(D_METHOD("set_peak_rpm", "rpm"), &FellowVehicle3D::set_peak_rpm);
	ClassDB::bind_method(D_METHOD("get_peak_rpm"), &FellowVehicle3D::get_peak_rpm);
	ClassDB::bind_method(D_METHOD("set_max_rpm", "rpm"), &FellowVehicle3D::set_max_rpm);
	ClassDB::bind_method(D_METHOD("get_max_rpm"), &FellowVehicle3D::get_max_rpm);
	ClassDB::bind_method(D_METHOD("set_torque_curve", "curve"), &FellowVehicle3D::set_torque_curve);
	ClassDB::bind_method(D_METHOD("get_torque_curve"), &FellowVehicle3D::get_torque_curve);
	ClassDB::bind_method(D_METHOD("set_gear_ratios", "ratios"), &FellowVehicle3D::set_gear_ratios);
	ClassDB::bind_method(D_METHOD("get_gear_ratios"), &FellowVehicle3D::get_gear_ratios);
	ClassDB::bind_method(D_METHOD("set_reverse_ratio", "ratio"), &FellowVehicle3D::set_reverse_ratio);
	ClassDB::bind_method(D_METHOD("get_reverse_ratio"), &FellowVehicle3D::get_reverse_ratio);
	ClassDB::bind_method(D_METHOD("set_engine_inertia", "inertia"), &FellowVehicle3D::set_engine_inertia);
	ClassDB::bind_method(D_METHOD("get_engine_inertia"), &FellowVehicle3D::get_engine_inertia);
	ClassDB::bind_method(D_METHOD("set_engine_braking", "torque"), &FellowVehicle3D::set_engine_braking);
	ClassDB::bind_method(D_METHOD("get_engine_braking"), &FellowVehicle3D::get_engine_braking);
	ClassDB::bind_method(D_METHOD("set_traction_control", "enabled"), &FellowVehicle3D::set_traction_control);
	ClassDB::bind_method(D_METHOD("has_traction_control"), &FellowVehicle3D::has_traction_control);
	ClassDB::bind_method(D_METHOD("set_anti_lock_brakes", "enabled"), &FellowVehicle3D::set_anti_lock_brakes);
	ClassDB::bind_method(D_METHOD("has_anti_lock_brakes"), &FellowVehicle3D::has_anti_lock_brakes);
	ClassDB::bind_method(D_METHOD("set_max_reverse_speed", "speed"), &FellowVehicle3D::set_max_reverse_speed);
	ClassDB::bind_method(D_METHOD("get_max_reverse_speed"), &FellowVehicle3D::get_max_reverse_speed);
	ClassDB::bind_method(D_METHOD("set_final_drive", "ratio"), &FellowVehicle3D::set_final_drive);
	ClassDB::bind_method(D_METHOD("get_final_drive"), &FellowVehicle3D::get_final_drive);
	ClassDB::bind_method(D_METHOD("set_drivetrain_efficiency", "efficiency"), &FellowVehicle3D::set_drivetrain_efficiency);
	ClassDB::bind_method(D_METHOD("get_drivetrain_efficiency"), &FellowVehicle3D::get_drivetrain_efficiency);
	ClassDB::bind_method(D_METHOD("set_automatic", "enabled"), &FellowVehicle3D::set_automatic);
	ClassDB::bind_method(D_METHOD("is_automatic"), &FellowVehicle3D::is_automatic);
	ClassDB::bind_method(D_METHOD("set_shift_up_rpm", "rpm"), &FellowVehicle3D::set_shift_up_rpm);
	ClassDB::bind_method(D_METHOD("get_shift_up_rpm"), &FellowVehicle3D::get_shift_up_rpm);
	ClassDB::bind_method(D_METHOD("set_shift_down_rpm", "rpm"), &FellowVehicle3D::set_shift_down_rpm);
	ClassDB::bind_method(D_METHOD("get_shift_down_rpm"), &FellowVehicle3D::get_shift_down_rpm);
	ClassDB::bind_method(D_METHOD("set_shift_time", "seconds"), &FellowVehicle3D::set_shift_time);
	ClassDB::bind_method(D_METHOD("get_shift_time"), &FellowVehicle3D::get_shift_time);
	ClassDB::bind_method(D_METHOD("set_differential", "differential"), &FellowVehicle3D::set_differential);
	ClassDB::bind_method(D_METHOD("get_differential"), &FellowVehicle3D::get_differential);
	ClassDB::bind_method(D_METHOD("set_brake_torque", "torque"), &FellowVehicle3D::set_brake_torque);
	ClassDB::bind_method(D_METHOD("get_brake_torque"), &FellowVehicle3D::get_brake_torque);
	ClassDB::bind_method(D_METHOD("set_handbrake_torque", "torque"), &FellowVehicle3D::set_handbrake_torque);
	ClassDB::bind_method(D_METHOD("get_handbrake_torque"), &FellowVehicle3D::get_handbrake_torque);
	ClassDB::bind_method(D_METHOD("set_max_steer", "degrees"), &FellowVehicle3D::set_max_steer);
	ClassDB::bind_method(D_METHOD("get_max_steer"), &FellowVehicle3D::get_max_steer);
	ClassDB::bind_method(D_METHOD("set_steer_at_speed", "share"), &FellowVehicle3D::set_steer_at_speed);
	ClassDB::bind_method(D_METHOD("get_steer_at_speed"), &FellowVehicle3D::get_steer_at_speed);
	ClassDB::bind_method(D_METHOD("set_steer_speed", "speed"), &FellowVehicle3D::set_steer_speed);
	ClassDB::bind_method(D_METHOD("get_steer_speed"), &FellowVehicle3D::get_steer_speed);
	ClassDB::bind_method(D_METHOD("set_anti_roll", "rate"), &FellowVehicle3D::set_anti_roll);
	ClassDB::bind_method(D_METHOD("get_anti_roll"), &FellowVehicle3D::get_anti_roll);
	ClassDB::bind_method(D_METHOD("set_drag_area", "area"), &FellowVehicle3D::set_drag_area);
	ClassDB::bind_method(D_METHOD("get_drag_area"), &FellowVehicle3D::get_drag_area);
	ClassDB::bind_method(D_METHOD("set_downforce_area", "area"), &FellowVehicle3D::set_downforce_area);
	ClassDB::bind_method(D_METHOD("get_downforce_area"), &FellowVehicle3D::get_downforce_area);
	ClassDB::bind_method(D_METHOD("set_substeps", "substeps"), &FellowVehicle3D::set_substeps);
	ClassDB::bind_method(D_METHOD("get_substeps"), &FellowVehicle3D::get_substeps);

	ADD_GROUP("Input", "");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "throttle", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_throttle", "get_throttle");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "brake", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_brake", "get_brake");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "steering", PROPERTY_HINT_RANGE, "-1,1,0.01"), "set_steering", "get_steering");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "handbrake"), "set_handbrake", "is_handbrake");
	ADD_GROUP("Engine", "");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "max_torque", PROPERTY_HINT_RANGE, "0,3000,1,or_greater,suffix:N·m"), "set_max_torque", "get_max_torque");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "engine_braking", PROPERTY_HINT_RANGE, "0,1000,1,or_greater,suffix:N·m"), "set_engine_braking", "get_engine_braking");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "engine_inertia", PROPERTY_HINT_RANGE, "0,5,0.01,or_greater,suffix:kg·m²"), "set_engine_inertia", "get_engine_inertia");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "idle_rpm", PROPERTY_HINT_RANGE, "100,3000,1,or_greater"), "set_idle_rpm", "get_idle_rpm");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "peak_rpm", PROPERTY_HINT_RANGE, "500,20000,1,or_greater"), "set_peak_rpm", "get_peak_rpm");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "max_rpm", PROPERTY_HINT_RANGE, "1000,20000,1,or_greater"), "set_max_rpm", "get_max_rpm");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "torque_curve", PROPERTY_HINT_RESOURCE_TYPE, "Curve"), "set_torque_curve", "get_torque_curve");
	ADD_GROUP("Drivetrain", "");
	ADD_PROPERTY(PropertyInfo(Variant::PACKED_FLOAT32_ARRAY, "gear_ratios"), "set_gear_ratios", "get_gear_ratios");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "reverse_ratio", PROPERTY_HINT_RANGE, "0.1,10,0.01"), "set_reverse_ratio", "get_reverse_ratio");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "max_reverse_speed", PROPERTY_HINT_RANGE, "0,30,0.1,or_greater,suffix:m/s"), "set_max_reverse_speed", "get_max_reverse_speed");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "final_drive", PROPERTY_HINT_RANGE, "0.1,10,0.01"), "set_final_drive", "get_final_drive");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "drivetrain_efficiency", PROPERTY_HINT_RANGE, "0.1,1,0.01"), "set_drivetrain_efficiency", "get_drivetrain_efficiency");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "automatic"), "set_automatic", "is_automatic");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "shift_up_rpm", PROPERTY_HINT_RANGE, "500,20000,1"), "set_shift_up_rpm", "get_shift_up_rpm");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "shift_down_rpm", PROPERTY_HINT_RANGE, "300,20000,1"), "set_shift_down_rpm", "get_shift_down_rpm");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "shift_time", PROPERTY_HINT_RANGE, "0,2,0.01,suffix:s"), "set_shift_time", "get_shift_time");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "differential", PROPERTY_HINT_ENUM, "Open,Locked"), "set_differential", "get_differential");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "traction_control"), "set_traction_control", "has_traction_control");
	ADD_GROUP("Brakes and Steering", "");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "brake_torque", PROPERTY_HINT_RANGE, "0,20000,1,or_greater,suffix:N·m"), "set_brake_torque", "get_brake_torque");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "handbrake_torque", PROPERTY_HINT_RANGE, "0,20000,1,or_greater,suffix:N·m"), "set_handbrake_torque", "get_handbrake_torque");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "anti_lock_brakes"), "set_anti_lock_brakes", "has_anti_lock_brakes");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "max_steer", PROPERTY_HINT_RANGE, "0,80,0.1,suffix:°"), "set_max_steer", "get_max_steer");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "steer_at_speed", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_steer_at_speed", "get_steer_at_speed");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "steer_speed", PROPERTY_HINT_RANGE, "0.1,20,0.1,suffix:rad/s"), "set_steer_speed", "get_steer_speed");
	ADD_GROUP("Body", "");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "anti_roll", PROPERTY_HINT_RANGE, "0,100000,1,or_greater,suffix:N/m"), "set_anti_roll", "get_anti_roll");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "drag_area", PROPERTY_HINT_RANGE, "0,5,0.01,or_greater,suffix:m²"), "set_drag_area", "get_drag_area");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "downforce_area", PROPERTY_HINT_RANGE, "0,10,0.01,or_greater,suffix:m²"), "set_downforce_area", "get_downforce_area");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "substeps", PROPERTY_HINT_RANGE, "1,16,1"), "set_substeps", "get_substeps");

	ADD_SIGNAL(MethodInfo("gear_changed", PropertyInfo(Variant::INT, "gear")));

	BIND_ENUM_CONSTANT(DIFFERENTIAL_OPEN);
	BIND_ENUM_CONSTANT(DIFFERENTIAL_LOCKED);
}
