/**************************************************************************/
/*  character_body_3d.cpp                                                 */
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

#include "character_body_3d.h"

#include "core/config/engine.h"
#include "core/object/class_db.h"

#ifndef DISABLE_DEPRECATED
#include "scene/resources/3d/world_3d.h" // FellowEngine (decision 0311)
#include "servers/physics_3d/physics_server_3d_extension.h"
#endif

//so, if you pass 45 as limit, avoid numerical precision errors when angle is 45.
#define FLOOR_ANGLE_THRESHOLD 0.01

bool CharacterBody3D::move_and_slide() {
	const FellowMotionCacheScope fellow_scope(this); // FellowEngine (decision 0093)
	// Hack in order to work with calling from _process as well as from _physics_process; calling from thread is risky
	double delta = Engine::get_singleton()->is_in_physics_frame() ? get_physics_process_delta_time() : get_process_delta_time();

	for (int i = 0; i < 3; i++) {
		if (locked_axis & (1 << i)) {
			velocity[i] = 0.0;
		}
	}

	Transform3D gt = get_global_transform();
	previous_position = gt.origin;
	const Vector3 fellow_wanted = velocity; // FellowEngine (decision 0312): the velocity asked for, before the slides

	Vector3 current_platform_velocity = platform_velocity;

	if ((collision_state.floor || collision_state.wall) && platform_rid.is_valid()) {
		bool excluded = false;
		if (collision_state.floor) {
			excluded = (platform_floor_layers & platform_layer) == 0;
		} else if (collision_state.wall) {
			excluded = (platform_wall_layers & platform_layer) == 0;
		}
		if (!excluded) {
			PhysicsDirectBodyState3D *bs = nullptr;

			// We need to check the platform_rid object still exists before accessing.
			// A valid RID is no guarantee that the object has not been deleted.

			// We can only perform the ObjectDB lifetime check on Object derived objects.
			// Note that physics also creates RIDs for non-Object derived objects, these cannot
			// be lifetime checked through ObjectDB, and therefore there is a still a vulnerability
			// to dangling RIDs (access after free) in this scenario.
			if (platform_object_id.is_null() || ObjectDB::get_instance(platform_object_id)) {
				// This approach makes sure there is less delay between the actual body velocity and the one we saved.
				bs = PhysicsServer3D::get_singleton()->body_get_direct_state(platform_rid);
			}

			if (bs) {
				Vector3 local_position = gt.origin - bs->get_transform().origin;
				current_platform_velocity = bs->get_velocity_at_local_position(local_position);
			} else {
				// Body is removed or destroyed, invalidate floor.
				current_platform_velocity = Vector3();
				platform_rid = RID();
			}
		} else {
			current_platform_velocity = Vector3();
		}
	}

	motion_results.clear();

	bool was_on_floor = collision_state.floor;
	collision_state.state = 0;
	fellow_last_step = 0.0; // FellowEngine (decision 0311)
	fellow_floor_valid = fellow_floor_valid && was_on_floor;

	last_motion = Vector3();

	if (!current_platform_velocity.is_zero_approx()) {
		PhysicsServer3D::MotionParameters parameters(get_global_transform(), current_platform_velocity * delta, margin);
		parameters.recovery_as_collision = true; // Also report collisions generated only from recovery.

		parameters.exclude_bodies.insert(platform_rid);
		if (platform_object_id.is_valid()) {
			parameters.exclude_objects.insert(platform_object_id);
		}

		PhysicsServer3D::MotionResult floor_result;
		if (move_and_collide(parameters, floor_result, false, false)) {
			motion_results.push_back(floor_result);

			CollisionState result_state;
			_set_collision_direction(floor_result, result_state);
		}
	}

	if (motion_mode == MOTION_MODE_GROUNDED) {
		_move_and_slide_grounded(delta, was_on_floor);
	} else {
		_move_and_slide_floating(delta);
	}

	// FellowEngine (decision 0312): the rigid bodies the moves were stopped by pushed, the one stood on weighed down.
	if (rigid_push_force > 0.0 || rigid_standing_mass > 0.0) {
		_fellow_push_rigid_bodies(delta, fellow_wanted);
	}

	// Compute real velocity.
	real_velocity = get_position_delta() / delta;

	if (platform_on_leave != PLATFORM_ON_LEAVE_DO_NOTHING) {
		// Add last platform velocity when just left a moving platform.
		if (!collision_state.floor && !collision_state.wall) {
			if (platform_on_leave == PLATFORM_ON_LEAVE_ADD_UPWARD_VELOCITY && current_platform_velocity.dot(up_direction) < 0) {
				current_platform_velocity = current_platform_velocity.slide(up_direction);
			}
			velocity += current_platform_velocity;
		}
	}

	return motion_results.size() > 0;
}

void CharacterBody3D::_move_and_slide_grounded(double p_delta, bool p_was_on_floor) {
	Vector3 motion = velocity * p_delta;
	Vector3 motion_slide_up = motion.slide(up_direction);
	Vector3 prev_floor_normal = floor_normal;

	platform_rid = RID();
	platform_object_id = ObjectID();
	platform_velocity = Vector3();
	platform_angular_velocity = Vector3();
	platform_ceiling_velocity = Vector3();
	floor_normal = Vector3();
	wall_normal = Vector3();
	ceiling_normal = Vector3();

	// No sliding on first attempt to keep floor motion stable when possible,
	// When stop on slope is enabled or when there is no up direction.
	bool sliding_enabled = !floor_stop_on_slope;
	// Constant speed can be applied only the first time sliding is enabled.
	bool can_apply_constant_speed = sliding_enabled;
	// If the platform's ceiling push down the body.
	bool apply_ceiling_velocity = false;
	bool first_slide = true;
	bool vel_dir_facing_up = velocity.dot(up_direction) > 0;
	Vector3 total_travel;

	for (int iteration = 0; iteration < max_slides; ++iteration) {
		PhysicsServer3D::MotionParameters parameters(get_global_transform(), motion, margin);
		parameters.max_collisions = 6; // There can be 4 collisions between 2 walls + 2 more for the floor.
		parameters.recovery_as_collision = true; // Also report collisions generated only from recovery.

		PhysicsServer3D::MotionResult result;
		bool collided = move_and_collide(parameters, result, false, !sliding_enabled);

		last_motion = result.travel;

		if (collided) {
			motion_results.push_back(result);

			CollisionState previous_state = collision_state;

			CollisionState result_state;
			_set_collision_direction(result, result_state);

			// If we hit a ceiling platform, we set the vertical velocity to at least the platform one.
			if (collision_state.ceiling && platform_ceiling_velocity != Vector3() && platform_ceiling_velocity.dot(up_direction) < 0) {
				// If ceiling sliding is on, only apply when the ceiling is flat or when the motion is upward.
				if (!slide_on_ceiling || motion.dot(up_direction) < 0 || (ceiling_normal + up_direction).length() < 0.01) {
					apply_ceiling_velocity = true;
					Vector3 ceiling_vertical_velocity = up_direction * up_direction.dot(platform_ceiling_velocity);
					Vector3 motion_vertical_velocity = up_direction * up_direction.dot(velocity);
					if (motion_vertical_velocity.dot(up_direction) > 0 || ceiling_vertical_velocity.length_squared() > motion_vertical_velocity.length_squared()) {
						velocity = ceiling_vertical_velocity + velocity.slide(up_direction);
					}
				}
			}

			// FellowEngine (decision 0311): stopped by a riser or a step's edge while on the floor: up onto the step.
			if (floor_step_height > 0.0 && p_was_on_floor && !vel_dir_facing_up && up_direction != Vector3() && _fellow_step_ahead(result, result.remainder.slide(up_direction))) {
				Vector3 motion_left;
				if (_fellow_step_up(result.remainder.slide(up_direction), motion_left)) {
					total_travel += result.travel;
					motion = motion_left;
					if (motion.is_zero_approx()) {
						break;
					}
					sliding_enabled = true;
					first_slide = false;
					continue;
				}
			}

			if (collision_state.floor && floor_stop_on_slope && (velocity.normalized() + up_direction).length() < 0.01) {
				Transform3D gt = get_global_transform();
				if (result.travel.length() <= margin + CMP_EPSILON) {
					gt.origin -= result.travel;
				}
				set_global_transform(gt);
				velocity = Vector3();
				motion = Vector3();
				last_motion = Vector3();
				break;
			}

			if (result.remainder.is_zero_approx()) {
				motion = Vector3();
				break;
			}

			// Apply regular sliding by default.
			bool apply_default_sliding = true;

			// Wall collision checks.
			if (result_state.wall && (motion_slide_up.dot(wall_normal) <= 0)) {
				// Move on floor only checks.
				if (floor_block_on_wall) {
					// Needs horizontal motion from current motion instead of motion_slide_up
					// to properly test the angle and avoid standing on slopes
					Vector3 horizontal_motion = motion.slide(up_direction);
					Vector3 horizontal_normal = wall_normal.slide(up_direction).normalized();
					real_t motion_angle = Math::abs(Math::acos(-horizontal_normal.dot(horizontal_motion.normalized())));

					// Avoid to move forward on a wall if floor_block_on_wall is true.
					// Applies only when the motion angle is under 90 degrees,
					// in order to avoid blocking lateral motion along a wall.
					if (motion_angle < .5 * Math::PI) {
						apply_default_sliding = false;
						if (p_was_on_floor && !vel_dir_facing_up) {
							// Cancel the motion.
							Transform3D gt = get_global_transform();
							real_t travel_total = result.travel.length();
							real_t cancel_dist_max = MIN(0.1, margin * 20);
							if (travel_total <= margin + CMP_EPSILON) {
								gt.origin -= result.travel;
								result.travel = Vector3(); // Cancel for constant speed computation.
							} else if (travel_total < cancel_dist_max) { // If the movement is large the body can be prevented from reaching the walls.
								gt.origin -= result.travel.slide(up_direction);
								// Keep remaining motion in sync with amount canceled.
								motion = motion.slide(up_direction);
								result.travel = Vector3();
							} else {
								// Travel is too high to be safely canceled, we take it into account.
								result.travel = result.travel.slide(up_direction);
								motion = result.remainder;
							}
							set_global_transform(gt);
							// Determines if you are on the ground, and limits the possibility of climbing on the walls because of the approximations.
							_snap_on_floor(true, false);
						} else {
							// If the movement is not canceled we only keep the remaining.
							motion = result.remainder;
						}

						// Apply slide on forward in order to allow only lateral motion on next step.
						Vector3 forward = wall_normal.slide(up_direction).normalized();
						motion = motion.slide(forward);

						// Scales the horizontal velocity according to the wall slope.
						if (vel_dir_facing_up) {
							Vector3 slide_motion = velocity.slide(result.collisions[0].normal);
							// Keeps the vertical motion from velocity and add the horizontal motion of the projection.
							velocity = up_direction * up_direction.dot(velocity) + slide_motion.slide(up_direction);
						} else {
							velocity = velocity.slide(forward);
						}

						// Allow only lateral motion along previous floor when already on floor.
						// Fixes slowing down when moving in diagonal against an inclined wall.
						if (p_was_on_floor && !vel_dir_facing_up && (motion.dot(up_direction) > 0.0)) {
							// Slide along the corner between the wall and previous floor.
							Vector3 floor_side = prev_floor_normal.cross(wall_normal);
							if (floor_side != Vector3()) {
								motion = floor_side * motion.dot(floor_side);
							}
						}

						// Stop all motion when a second wall is hit (unless sliding down or jumping),
						// in order to avoid jittering in corner cases.
						bool stop_all_motion = previous_state.wall && !vel_dir_facing_up;

						// Allow sliding when the body falls.
						if (!collision_state.floor && motion.dot(up_direction) < 0) {
							Vector3 slide_motion = motion.slide(wall_normal);
							// Test again to allow sliding only if the result goes downwards.
							// Fixes jittering issues at the bottom of inclined walls.
							if (slide_motion.dot(up_direction) < 0) {
								stop_all_motion = false;
								motion = slide_motion;
							}
						}

						if (stop_all_motion) {
							motion = Vector3();
							velocity = Vector3();
						}
					}
				}

				// Stop horizontal motion when under wall slide threshold.
				if (p_was_on_floor && (wall_min_slide_angle > 0.0) && result_state.wall) {
					Vector3 horizontal_normal = wall_normal.slide(up_direction).normalized();
					real_t motion_angle = Math::abs(Math::acos(-horizontal_normal.dot(motion_slide_up.normalized())));
					if (motion_angle < wall_min_slide_angle) {
						motion = up_direction * motion.dot(up_direction);
						velocity = up_direction * velocity.dot(up_direction);

						apply_default_sliding = false;
					}
				}
			}

			if (apply_default_sliding) {
				// Regular sliding, the last part of the test handle the case when you don't want to slide on the ceiling.
				if ((sliding_enabled || !collision_state.floor) && (!collision_state.ceiling || slide_on_ceiling || !vel_dir_facing_up) && !apply_ceiling_velocity) {
					const PhysicsServer3D::MotionCollision &collision = result.collisions[0];

					Vector3 slide_motion = result.remainder.slide(collision.normal);
					if (collision_state.floor && !collision_state.wall && !motion_slide_up.is_zero_approx()) {
						// Slide using the intersection between the motion plane and the floor plane,
						// in order to keep the direction intact.
						real_t motion_length = slide_motion.length();
						slide_motion = up_direction.cross(result.remainder).cross(floor_normal);

						// Keep the length from default slide to change speed in slopes by default,
						// when constant speed is not enabled.
						slide_motion.normalize();
						slide_motion *= motion_length;
					}

					if (slide_motion.dot(velocity) > 0.0) {
						motion = slide_motion;
					} else {
						motion = Vector3();
					}

					if (slide_on_ceiling && result_state.ceiling) {
						// Apply slide only in the direction of the input motion, otherwise just stop to avoid jittering when moving against a wall.
						if (vel_dir_facing_up) {
							velocity = velocity.slide(collision.normal);
						} else {
							// Avoid acceleration in slope when falling.
							velocity = up_direction * up_direction.dot(velocity);
						}
					}
				}
				// No sliding on first attempt to keep floor motion stable when possible.
				else {
					motion = result.remainder;
					if (result_state.ceiling && !slide_on_ceiling && vel_dir_facing_up) {
						velocity = velocity.slide(up_direction);
						motion = motion.slide(up_direction);
					}
				}
			}

			total_travel += result.travel;

			// Apply Constant Speed.
			if (p_was_on_floor && floor_constant_speed && can_apply_constant_speed && collision_state.floor && !motion.is_zero_approx()) {
				Vector3 travel_slide_up = total_travel.slide(up_direction);
				motion = motion.normalized() * MAX(0, (motion_slide_up.length() - travel_slide_up.length()));
			}
		}
		// When you move forward in a downward slope you don’t collide because you will be in the air.
		// This test ensures that constant speed is applied, only if the player is still on the ground after the snap is applied.
		else if (floor_constant_speed && first_slide && _on_floor_if_snapped(p_was_on_floor, vel_dir_facing_up)) {
			can_apply_constant_speed = false;
			sliding_enabled = true;
			Transform3D gt = get_global_transform();
			gt.origin = gt.origin - result.travel;
			set_global_transform(gt);

			// Slide using the intersection between the motion plane and the floor plane,
			// in order to keep the direction intact.
			Vector3 motion_slide_norm = up_direction.cross(motion).cross(prev_floor_normal);
			motion_slide_norm.normalize();

			motion = motion_slide_norm * (motion_slide_up.length());
			collided = true;
		}

		if (!collided || motion.is_zero_approx()) {
			break;
		}

		can_apply_constant_speed = !can_apply_constant_speed && !sliding_enabled;
		sliding_enabled = true;
		first_slide = false;
	}

	_snap_on_floor(p_was_on_floor, vel_dir_facing_up);

	// Reset the gravity accumulation when touching the ground.
	if (collision_state.floor && !vel_dir_facing_up) {
		velocity = velocity.slide(up_direction);
	}
}

void CharacterBody3D::_move_and_slide_floating(double p_delta) {
	Vector3 motion = velocity * p_delta;

	platform_rid = RID();
	platform_object_id = ObjectID();
	floor_normal = Vector3();
	platform_velocity = Vector3();
	platform_angular_velocity = Vector3();

	bool first_slide = true;
	for (int iteration = 0; iteration < max_slides; ++iteration) {
		PhysicsServer3D::MotionParameters parameters(get_global_transform(), motion, margin);
		parameters.recovery_as_collision = true; // Also report collisions generated only from recovery.

		PhysicsServer3D::MotionResult result;
		bool collided = move_and_collide(parameters, result, false, false);

		last_motion = result.travel;

		if (collided) {
			motion_results.push_back(result);

			CollisionState result_state;
			_set_collision_direction(result, result_state);

			if (result.remainder.is_zero_approx()) {
				motion = Vector3();
				break;
			}

			if (wall_min_slide_angle != 0 && Math::acos(wall_normal.dot(-velocity.normalized())) < wall_min_slide_angle + FLOOR_ANGLE_THRESHOLD) {
				motion = Vector3();
				if (result.travel.length() < margin + CMP_EPSILON) {
					Transform3D gt = get_global_transform();
					gt.origin -= result.travel;
					set_global_transform(gt);
				}
			} else if (first_slide) {
				Vector3 motion_slide_norm = result.remainder.slide(wall_normal).normalized();
				motion = motion_slide_norm * (motion.length() - result.travel.length());
			} else {
				motion = result.remainder.slide(wall_normal);
			}

			if (motion.dot(velocity) <= 0.0) {
				motion = Vector3();
			}
		}

		if (!collided || motion.is_zero_approx()) {
			break;
		}

		first_slide = false;
	}
}

void CharacterBody3D::apply_floor_snap() {
	if (collision_state.floor) {
		return;
	}

	// Snap by at least collision margin to keep floor state consistent.
	real_t length = _fellow_snap_length(); // FellowEngine (decision 0311): down a step too

	PhysicsServer3D::MotionParameters parameters(get_global_transform(), -up_direction * length, margin);
	parameters.max_collisions = 4;
	parameters.recovery_as_collision = true; // Also report collisions generated only from recovery.
	parameters.collide_separation_ray = true;

	PhysicsServer3D::MotionResult result;
	if (move_and_collide(parameters, result, true, false)) {
		CollisionState result_state;
		// Apply direction for floor only.
		_set_collision_direction(result, result_state, CollisionState(true, false, false));

		if (result_state.floor) {
			// Ensure that we only move the body along the up axis, because
			// move_and_collide may stray the object a bit when getting it unstuck.
			// Canceling this motion should not affect move_and_slide, as previous
			// calls to move_and_collide already took care of freeing the body.
			if (result.travel.length() > margin) {
				result.travel = up_direction * up_direction.dot(result.travel);
			} else {
				result.travel = Vector3();
			}

			parameters.from.origin += result.travel;
			set_global_transform(parameters.from);
			// FellowEngine (decision 0311): a step down (further than stock's snap would have reached).
			const real_t drop = -result.travel.dot(up_direction);
			if (drop > floor_snap_length) {
				fellow_last_step -= drop;
			}
		}
	}
}

void CharacterBody3D::_snap_on_floor(bool p_was_on_floor, bool p_vel_dir_facing_up) {
	if (collision_state.floor || !p_was_on_floor || p_vel_dir_facing_up) {
		return;
	}

	apply_floor_snap();
}

bool CharacterBody3D::_on_floor_if_snapped(bool p_was_on_floor, bool p_vel_dir_facing_up) {
	if (up_direction == Vector3() || collision_state.floor || !p_was_on_floor || p_vel_dir_facing_up) {
		return false;
	}

	// Snap by at least collision margin to keep floor state consistent.
	real_t length = _fellow_snap_length(); // FellowEngine (decision 0311)

	PhysicsServer3D::MotionParameters parameters(get_global_transform(), -up_direction * length, margin);
	parameters.max_collisions = 4;
	parameters.recovery_as_collision = true; // Also report collisions generated only from recovery.
	parameters.collide_separation_ray = true;

	PhysicsServer3D::MotionResult result;
	if (move_and_collide(parameters, result, true, false)) {
		CollisionState result_state;
		// Don't apply direction for any type.
		_set_collision_direction(result, result_state, CollisionState());

		return result_state.floor;
	}

	return false;
}

void CharacterBody3D::_set_collision_direction(const PhysicsServer3D::MotionResult &p_result, CollisionState &r_state, CollisionState p_apply_state) {
	r_state.state = 0;

	real_t wall_depth = -1.0;
	real_t floor_depth = -1.0;

	bool was_on_wall = collision_state.wall;
	Vector3 prev_wall_normal = wall_normal;
	int wall_collision_count = 0;
	Vector3 combined_wall_normal;
	Vector3 tmp_wall_col; // Avoid duplicate on average calculation.

	for (int i = p_result.collision_count - 1; i >= 0; i--) {
		const PhysicsServer3D::MotionCollision &collision = p_result.collisions[i];

		if (motion_mode == MOTION_MODE_GROUNDED) {
			// Check if any collision is floor.
			real_t floor_angle = collision.get_angle(up_direction);
			// FellowEngine (decision 0311): with steps, a steep contact under the body on a walkable top's edge too.
			if (floor_angle <= floor_max_angle + FLOOR_ANGLE_THRESHOLD || (floor_step_height > 0.0 && _fellow_edge_is_floor(collision))) {
				r_state.floor = true;
				if (p_apply_state.floor && collision.depth > floor_depth) {
					collision_state.floor = true;
					floor_normal = collision.normal;
					floor_depth = collision.depth;
					_set_platform_data(collision);
					fellow_floor_height = collision.position.dot(up_direction); // FellowEngine (decision 0311)
					fellow_floor_valid = true;
					fellow_floor_point = collision.position;
					fellow_floor_body = collision.collider;
				}
				continue;
			}

			// Check if any collision is ceiling.
			real_t ceiling_angle = collision.get_angle(-up_direction);
			if (ceiling_angle <= floor_max_angle + FLOOR_ANGLE_THRESHOLD) {
				r_state.ceiling = true;
				if (p_apply_state.ceiling) {
					platform_ceiling_velocity = collision.collider_velocity;
					ceiling_normal = collision.normal;
					collision_state.ceiling = true;
				}
				continue;
			}
		}

		// Collision is wall by default.
		r_state.wall = true;

		if (p_apply_state.wall && collision.depth > wall_depth) {
			collision_state.wall = true;
			wall_depth = collision.depth;
			wall_normal = collision.normal;

			// Don't apply wall velocity when the collider is a CharacterBody3D.
			if (ObjectDB::get_instance<CharacterBody3D>(collision.collider_id) == nullptr) {
				_set_platform_data(collision);
			}
		}

		// Collect normal for calculating average.
		if (!collision.normal.is_equal_approx(tmp_wall_col)) {
			tmp_wall_col = collision.normal;
			combined_wall_normal += collision.normal;
			wall_collision_count++;
		}
	}

	if (r_state.wall) {
		if (wall_collision_count > 1 && !r_state.floor) {
			// Check if wall normals cancel out to floor support.
			if (!r_state.floor && motion_mode == MOTION_MODE_GROUNDED) {
				combined_wall_normal.normalize();
				real_t floor_angle = Math::acos(combined_wall_normal.dot(up_direction));
				if (floor_angle <= floor_max_angle + FLOOR_ANGLE_THRESHOLD) {
					r_state.floor = true;
					r_state.wall = false;
					if (p_apply_state.floor) {
						collision_state.floor = true;
						floor_normal = combined_wall_normal;
					}
					if (p_apply_state.wall) {
						collision_state.wall = was_on_wall;
						wall_normal = prev_wall_normal;
					}
					return;
				}
			}
		}
	}
}

void CharacterBody3D::_set_platform_data(const PhysicsServer3D::MotionCollision &p_collision) {
	PhysicsDirectBodyState3D *bs = PhysicsServer3D::get_singleton()->body_get_direct_state(p_collision.collider);
	if (bs == nullptr) {
		return;
	}

	platform_rid = p_collision.collider;
	platform_object_id = p_collision.collider_id;
	platform_velocity = p_collision.collider_velocity;
	platform_angular_velocity = p_collision.collider_angular_velocity;

#ifndef DISABLE_DEPRECATED
	// Try to accommodate for any physics extensions that have yet to implement `PhysicsDirectBodyState3D::get_collision_layer`.
	PhysicsDirectBodyState3DExtension *bs_ext = Object::cast_to<PhysicsDirectBodyState3DExtension>(bs);
	if (bs_ext != nullptr && !GDVIRTUAL_IS_OVERRIDDEN_PTR(bs_ext, _get_collision_layer)) {
		platform_layer = PhysicsServer3D::get_singleton()->body_get_collision_layer(p_collision.collider);
	} else
#endif
	{
		platform_layer = bs->get_collision_layer();
	}
}

// FellowEngine (decision 0311): steps.

real_t CharacterBody3D::_fellow_snap_length() const {
	// Down a step as well as stock's snap: the body walks down stairs instead of falling down each step.
	return MAX(MAX(floor_snap_length, floor_step_height), margin);
}

// A steep contact under the body that is the edge of a walkable top (a step's nosing, a kerb, a box's rim): a ray down
// just past the contact, on the edge's side, finds a walkable face at about the contact's height, and one on the body's
// side a floor within a step below. The contact's own normal
// stays the floor's: sliding along it rolls the body over the edge (or off it) instead of stopping against it.
bool CharacterBody3D::_fellow_edge_is_floor(const PhysicsServer3D::MotionCollision &p_collision) const {
	if (p_collision.normal.dot(up_direction) <= 0.01 || !is_inside_tree()) {
		return false; // not under the body
	}
	if (rigid_push_force > 0.0 && _fellow_is_rigid(p_collision.collider)) {
		return false; // a rigid body's edge is pushed, not stood on (decision 0312)
	}
	if (fellow_floor_valid && p_collision.position.dot(up_direction) - fellow_floor_height > floor_step_height + margin) {
		return false; // higher above the floor the body is on than a step
	}
	Vector3 outward = -p_collision.normal.slide(up_direction);
	if (outward.length_squared() < CMP_EPSILON2) {
		return false;
	}
	outward.normalize();
	PhysicsDirectSpaceState3D *space = get_world_3d()->get_direct_space_state();
	if (space == nullptr) {
		return false;
	}
	PhysicsDirectSpaceState3D::RayParameters ray;
	// 5 cm in from the contact and up to 6 cm above it: Jolt rounds boxes' edges by their margin (4 cm by default), its
	// contacts on them below the top's plane.
	ray.from = p_collision.position + outward * 0.05 + up_direction * 0.1;
	ray.to = ray.from - up_direction * 0.2;
	ray.exclude.insert(get_rid());
	ray.collision_mask = get_collision_mask();
	PhysicsDirectSpaceState3D::RayResult hit;
	if (!space->intersect_ray(ray, hit)) {
		return false;
	}
	if (Math::acos(CLAMP(hit.normal.dot(up_direction), (real_t)-1.0, (real_t)1.0)) > floor_max_angle + FLOOR_ANGLE_THRESHOLD) {
		return false; // a steep face beyond the edge: no walkable top
	}
	if (Math::abs((hit.position - p_collision.position).dot(up_direction)) > 0.06) {
		return false;
	}
	// And a floor no more than a step below it on the body's side: a step's edge, not a ledge's (walking off a ledge
	// higher than a step the body falls, its rounded bottom not held on the edge).
	ray.from = p_collision.position - outward * 0.05 + up_direction * 0.02;
	ray.to = ray.from - up_direction * (floor_step_height + 0.08);
	return space->intersect_ray(ray, hit);
}

bool CharacterBody3D::_fellow_is_floor(const PhysicsServer3D::MotionCollision &p_collision) const {
	return p_collision.get_angle(up_direction) <= floor_max_angle + FLOOR_ANGLE_THRESHOLD || _fellow_edge_is_floor(p_collision);
}

// A move stopped by what may be a step: a contact steeper than floor (a riser, or a step's edge under the body's rounded
// bottom), not a ceiling, facing the way the body goes on, and not touching it higher above the floor than a step (a
// wall: no casts for it).
bool CharacterBody3D::_fellow_step_ahead(const PhysicsServer3D::MotionResult &p_result, const Vector3 &p_along) const {
	if (p_along.length_squared() < CMP_EPSILON2) {
		return false;
	}
	for (int i = 0; i < p_result.collision_count; i++) {
		const PhysicsServer3D::MotionCollision &c = p_result.collisions[i];
		if (c.get_angle(up_direction) > floor_max_angle + FLOOR_ANGLE_THRESHOLD && c.normal.dot(up_direction) > -0.01 && c.normal.slide(up_direction).dot(p_along) < 0.0) {
			if (fellow_floor_valid && c.position.dot(up_direction) - fellow_floor_height > floor_step_height + margin) {
				continue;
			}
			return true;
		}
	}
	return false;
}

// From where the move stopped against a step: up by floor_step_height (less under a ceiling), along the motion left,
// and down onto what is there. Kept if it lands on floor (or a walkable edge) higher than it started and no more than
// floor_step_height above the floor it stood on: the body moved there, on the floor, at once (get_last_step() says by
// how much); r_motion_left is what the move along did not make (another riser further on).
bool CharacterBody3D::_fellow_step_up(const Vector3 &p_along, Vector3 &r_motion_left) {
	const Vector3 along = p_along;
	const Transform3D start = get_global_transform();
	const real_t length = along.length();
	const Vector3 dir = along / length;
	if (start.origin.distance_squared_to(fellow_step_failed_at) < margin * margin && dir.dot(fellow_step_failed_dir) > 0.9998 && length <= fellow_step_failed_length + margin) {
		return false; // failed from here that way and that far already (a body pushing against a wall: no three casts every tick)
	}
	fellow_step_failed_at = start.origin; // until it succeeds below
	fellow_step_failed_dir = dir;
	fellow_step_failed_length = length;
	PhysicsServer3D::MotionParameters parameters(start, up_direction * floor_step_height, margin);
	PhysicsServer3D::MotionResult up;
	move_and_collide(parameters, up, true, false);
	const real_t raised = up.travel.dot(up_direction);
	if (raised <= margin) {
		return false; // no room above
	}
	parameters.from.origin = start.origin + up_direction * raised;
	parameters.motion = along;
	PhysicsServer3D::MotionResult forward;
	move_and_collide(parameters, forward, true, false);
	const Vector3 forward_travel = forward.travel.slide(up_direction);
	if (forward_travel.length_squared() < CMP_EPSILON2) {
		return false; // the wall goes on above the step's height
	}
	parameters.from.origin += forward_travel;
	parameters.motion = -up_direction * (raised + margin);
	parameters.max_collisions = 4;
	parameters.recovery_as_collision = true;
	parameters.collide_separation_ray = true;
	PhysicsServer3D::MotionResult down;
	if (!move_and_collide(parameters, down, true, false)) {
		return false; // nothing under it at the height it started from
	}
	const Vector3 landed = parameters.from.origin + up_direction * up_direction.dot(down.travel);
	const real_t rise = (landed - start.origin).dot(up_direction);
	if (rise <= margin) {
		return false; // no higher than it was: not a step
	}
	real_t top = -INFINITY;
	for (int i = 0; i < down.collision_count; i++) {
		if (rigid_push_force > 0.0 && _fellow_is_rigid(down.collisions[i].collider)) {
			return false; // a rigid body in the way is pushed, not stepped onto (decision 0312)
		}
		if (_fellow_is_floor(down.collisions[i])) {
			top = MAX(top, down.collisions[i].position.dot(up_direction));
		}
	}
	if (top == -INFINITY) {
		return false; // a steep slope, not a step
	}
	if (fellow_floor_valid && top - fellow_floor_height > floor_step_height + margin) {
		return false; // higher than a step above the floor it stood on (the rounded bottom reaching over the edge)
	}
	CollisionState landing;
	set_global_transform(Transform3D(start.basis, landed));
	collision_state.wall = false;
	wall_normal = Vector3();
	_set_collision_direction(down, landing, CollisionState(true, false, false));
	fellow_last_step += rise;
	fellow_step_failed_at = Vector3(Math::INF, Math::INF, Math::INF);
	r_motion_left = forward.remainder.slide(up_direction);
	return true;
}

// FellowEngine (decision 0312): rigid bodies pushed and stood on.

bool CharacterBody3D::_fellow_is_rigid(RID p_body) const {
	if (!p_body.is_valid()) {
		return false;
	}
	const PhysicsServer3D::BodyMode mode = PhysicsServer3D::get_singleton()->body_get_mode(p_body);
	return mode == PhysicsServer3D::BODY_MODE_RIGID || mode == PhysicsServer3D::BODY_MODE_RIGID_LINEAR;
}

// After the move: each rigid body a slide was stopped by (a contact steeper than floor, the velocity asked for going
// into it) gets the impulse that brings the contact point, at its centre of mass's height, to that velocity along the
// contact's normal (its mass and inertia at that point taken into account), at most rigid_push_force for the tick; the
// strongest of its contacts. The rigid body stood on gets the body's weight (rigid_standing_mass under its gravity) at
// the floor's contact.
void CharacterBody3D::_fellow_push_rigid_bodies(double p_delta, const Vector3 &p_wanted) {
	PhysicsServer3D *ps = PhysicsServer3D::get_singleton();
	if (rigid_push_force > 0.0) {
		struct Push {
			RID body;
			Vector3 impulse;
			Vector3 at;
		};
		LocalVector<Push> pushes;
		const real_t most = rigid_push_force * p_delta;
		for (const PhysicsServer3D::MotionResult &result : motion_results) {
			for (int i = 0; i < result.collision_count; i++) {
				const PhysicsServer3D::MotionCollision &c = result.collisions[i];
				if (c.get_angle(up_direction) <= floor_max_angle + FLOOR_ANGLE_THRESHOLD || !_fellow_is_rigid(c.collider)) {
					continue;
				}
				// Along the ground: a push down into a light body pressed it into the floor under it (on GodotPhysics3D a ball
				// shot back off it at 28 m/s).
				Vector3 into = -c.normal.slide(up_direction);
				if (into.length_squared() < CMP_EPSILON2) {
					continue;
				}
				into.normalize();
				const real_t wanted = p_wanted.dot(into);
				if (wanted <= 0.0) {
					continue;
				}
				PhysicsDirectBodyState3D *bs = ps->body_get_direct_state(c.collider);
				if (bs == nullptr) {
					continue;
				}
				// At the contact, lowered or raised to the centre of mass's height: a crate slides rather than tips over
				// (where along the body's height a capsule touches it says little of where a person would push it).
				Vector3 at = c.position - bs->get_transform().origin;
				at -= up_direction * (at - bs->get_center_of_mass()).dot(up_direction);
				const real_t has = bs->get_velocity_at_local_position(at).dot(into);
				if (has >= wanted) {
					continue;
				}
				// The mass the push meets at that point: 1 / (1 / m + (r x n) . I^-1 (r x n)), r from the centre of mass.
				const Vector3 r = at - bs->get_center_of_mass();
				const Vector3 rn = r.cross(into);
				const real_t inverse = bs->get_inverse_mass() + rn.dot(bs->get_inverse_inertia_tensor().xform(rn));
				if (inverse <= CMP_EPSILON) {
					continue;
				}
				const real_t amount = MIN((wanted - has) / inverse, most);
				bool found = false;
				for (Push &p : pushes) {
					if (p.body == c.collider) {
						found = true;
						if (amount > p.impulse.length()) {
							p.impulse = into * amount;
							p.at = at;
						}
						break;
					}
				}
				if (!found) {
					pushes.push_back({ c.collider, into * amount, at });
				}
			}
		}
		for (const Push &p : pushes) {
			ps->body_apply_impulse(p.body, p.impulse, p.at);
		}
	}
	// The floor's own body (the platform may be a wall's: a ball pushed beside the floor took the weight and flew off).
	if (rigid_standing_mass > 0.0 && collision_state.floor && _fellow_is_rigid(fellow_floor_body)) {
		PhysicsDirectBodyState3D *bs = ps->body_get_direct_state(fellow_floor_body);
		if (bs != nullptr) {
			ps->body_apply_impulse(fellow_floor_body, get_gravity() * (rigid_standing_mass * p_delta), fellow_floor_point - bs->get_transform().origin);
		}
	}
}

real_t CharacterBody3D::get_rigid_push_force() const {
	return rigid_push_force;
}

void CharacterBody3D::set_rigid_push_force(real_t p_force) {
	ERR_FAIL_COND(p_force < 0);
	rigid_push_force = p_force;
}

real_t CharacterBody3D::get_rigid_standing_mass() const {
	return rigid_standing_mass;
}

void CharacterBody3D::set_rigid_standing_mass(real_t p_mass) {
	ERR_FAIL_COND(p_mass < 0);
	rigid_standing_mass = p_mass;
}

real_t CharacterBody3D::get_floor_step_height() const {
	return floor_step_height;
}

void CharacterBody3D::set_floor_step_height(real_t p_height) {
	ERR_FAIL_COND(p_height < 0);
	floor_step_height = p_height;
}

real_t CharacterBody3D::get_last_step() const {
	return fellow_last_step;
}

void CharacterBody3D::set_safe_margin(real_t p_margin) {
	margin = p_margin;
}

real_t CharacterBody3D::get_safe_margin() const {
	return margin;
}

const Vector3 &CharacterBody3D::get_velocity() const {
	return velocity;
}

void CharacterBody3D::set_velocity(const Vector3 &p_velocity) {
	velocity = p_velocity;
}

bool CharacterBody3D::is_on_floor() const {
	return collision_state.floor;
}

bool CharacterBody3D::is_on_floor_only() const {
	return collision_state.floor && !collision_state.wall && !collision_state.ceiling;
}

bool CharacterBody3D::is_on_wall() const {
	return collision_state.wall;
}

bool CharacterBody3D::is_on_wall_only() const {
	return collision_state.wall && !collision_state.floor && !collision_state.ceiling;
}

bool CharacterBody3D::is_on_ceiling() const {
	return collision_state.ceiling;
}

bool CharacterBody3D::is_on_ceiling_only() const {
	return collision_state.ceiling && !collision_state.floor && !collision_state.wall;
}

const Vector3 &CharacterBody3D::get_floor_normal() const {
	return floor_normal;
}

const Vector3 &CharacterBody3D::get_wall_normal() const {
	return wall_normal;
}

const Vector3 &CharacterBody3D::get_last_motion() const {
	return last_motion;
}

Vector3 CharacterBody3D::get_position_delta() const {
	return get_global_transform().origin - previous_position;
}

const Vector3 &CharacterBody3D::get_real_velocity() const {
	return real_velocity;
}

real_t CharacterBody3D::get_floor_angle(const Vector3 &p_up_direction) const {
	ERR_FAIL_COND_V(p_up_direction == Vector3(), 0);
	return Math::acos(floor_normal.dot(p_up_direction));
}

const Vector3 &CharacterBody3D::get_platform_velocity() const {
	return platform_velocity;
}

const Vector3 &CharacterBody3D::get_platform_angular_velocity() const {
	return platform_angular_velocity;
}

Vector3 CharacterBody3D::get_linear_velocity() const {
	return get_real_velocity();
}

int CharacterBody3D::get_slide_collision_count() const {
	return motion_results.size();
}

PhysicsServer3D::MotionResult CharacterBody3D::get_slide_collision(int p_bounce) const {
	ERR_FAIL_INDEX_V(p_bounce, motion_results.size(), PhysicsServer3D::MotionResult());
	return motion_results[p_bounce];
}

Ref<KinematicCollision3D> CharacterBody3D::_get_slide_collision(int p_bounce) {
	ERR_FAIL_INDEX_V(p_bounce, motion_results.size(), Ref<KinematicCollision3D>());
	if (p_bounce >= slide_colliders.size()) {
		slide_colliders.resize(p_bounce + 1);
	}

	// Create a new instance when the cached reference is invalid or still in use in script.
	if (slide_colliders[p_bounce].is_null() || slide_colliders[p_bounce]->get_reference_count() > 1) {
		slide_colliders.write[p_bounce].instantiate();
		slide_colliders.write[p_bounce]->owner_id = get_instance_id();
	}

	slide_colliders.write[p_bounce]->result = motion_results[p_bounce];
	return slide_colliders[p_bounce];
}

Ref<KinematicCollision3D> CharacterBody3D::_get_last_slide_collision() {
	if (motion_results.is_empty()) {
		return Ref<KinematicCollision3D>();
	}
	return _get_slide_collision(motion_results.size() - 1);
}

bool CharacterBody3D::is_floor_stop_on_slope_enabled() const {
	return floor_stop_on_slope;
}

void CharacterBody3D::set_floor_stop_on_slope_enabled(bool p_enabled) {
	floor_stop_on_slope = p_enabled;
}

bool CharacterBody3D::is_floor_constant_speed_enabled() const {
	return floor_constant_speed;
}

void CharacterBody3D::set_floor_constant_speed_enabled(bool p_enabled) {
	floor_constant_speed = p_enabled;
}

bool CharacterBody3D::is_floor_block_on_wall_enabled() const {
	return floor_block_on_wall;
}

void CharacterBody3D::set_floor_block_on_wall_enabled(bool p_enabled) {
	floor_block_on_wall = p_enabled;
}

bool CharacterBody3D::is_slide_on_ceiling_enabled() const {
	return slide_on_ceiling;
}

void CharacterBody3D::set_slide_on_ceiling_enabled(bool p_enabled) {
	slide_on_ceiling = p_enabled;
}

uint32_t CharacterBody3D::get_platform_floor_layers() const {
	return platform_floor_layers;
}

void CharacterBody3D::set_platform_floor_layers(uint32_t p_exclude_layers) {
	platform_floor_layers = p_exclude_layers;
}

uint32_t CharacterBody3D::get_platform_wall_layers() const {
	return platform_wall_layers;
}

void CharacterBody3D::set_platform_wall_layers(uint32_t p_exclude_layers) {
	platform_wall_layers = p_exclude_layers;
}

void CharacterBody3D::set_motion_mode(MotionMode p_mode) {
	motion_mode = p_mode;
}

CharacterBody3D::MotionMode CharacterBody3D::get_motion_mode() const {
	return motion_mode;
}

void CharacterBody3D::set_platform_on_leave(PlatformOnLeave p_on_leave_apply_velocity) {
	platform_on_leave = p_on_leave_apply_velocity;
}

CharacterBody3D::PlatformOnLeave CharacterBody3D::get_platform_on_leave() const {
	return platform_on_leave;
}

int CharacterBody3D::get_max_slides() const {
	return max_slides;
}

void CharacterBody3D::set_max_slides(int p_max_slides) {
	ERR_FAIL_COND(p_max_slides < 1);
	max_slides = p_max_slides;
}

real_t CharacterBody3D::get_floor_max_angle() const {
	return floor_max_angle;
}

void CharacterBody3D::set_floor_max_angle(real_t p_radians) {
	floor_max_angle = p_radians;
}

real_t CharacterBody3D::get_floor_snap_length() {
	return floor_snap_length;
}

void CharacterBody3D::set_floor_snap_length(real_t p_floor_snap_length) {
	ERR_FAIL_COND(p_floor_snap_length < 0);
	floor_snap_length = p_floor_snap_length;
}

real_t CharacterBody3D::get_wall_min_slide_angle() const {
	return wall_min_slide_angle;
}

void CharacterBody3D::set_wall_min_slide_angle(real_t p_radians) {
	wall_min_slide_angle = p_radians;
}

const Vector3 &CharacterBody3D::get_up_direction() const {
	return up_direction;
}

void CharacterBody3D::set_up_direction(const Vector3 &p_up_direction) {
	ERR_FAIL_COND_MSG(p_up_direction == Vector3(), "up_direction can't be equal to Vector3.ZERO, consider using Floating motion mode instead.");
	up_direction = p_up_direction.normalized();
}

void CharacterBody3D::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_ENTER_TREE: {
			// Reset move_and_slide() data.
			collision_state.state = 0;
			platform_rid = RID();
			platform_object_id = ObjectID();
			motion_results.clear();
			platform_velocity = Vector3();
			platform_angular_velocity = Vector3();
		} break;
	}
}

void CharacterBody3D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("move_and_slide"), &CharacterBody3D::move_and_slide);
	ClassDB::bind_method(D_METHOD("apply_floor_snap"), &CharacterBody3D::apply_floor_snap);

	ClassDB::bind_method(D_METHOD("set_velocity", "velocity"), &CharacterBody3D::set_velocity);
	ClassDB::bind_method(D_METHOD("get_velocity"), &CharacterBody3D::get_velocity);

	ClassDB::bind_method(D_METHOD("set_safe_margin", "margin"), &CharacterBody3D::set_safe_margin);
	ClassDB::bind_method(D_METHOD("get_safe_margin"), &CharacterBody3D::get_safe_margin);
	ClassDB::bind_method(D_METHOD("is_floor_stop_on_slope_enabled"), &CharacterBody3D::is_floor_stop_on_slope_enabled);
	ClassDB::bind_method(D_METHOD("set_floor_stop_on_slope_enabled", "enabled"), &CharacterBody3D::set_floor_stop_on_slope_enabled);
	ClassDB::bind_method(D_METHOD("set_floor_constant_speed_enabled", "enabled"), &CharacterBody3D::set_floor_constant_speed_enabled);
	ClassDB::bind_method(D_METHOD("is_floor_constant_speed_enabled"), &CharacterBody3D::is_floor_constant_speed_enabled);
	ClassDB::bind_method(D_METHOD("set_floor_block_on_wall_enabled", "enabled"), &CharacterBody3D::set_floor_block_on_wall_enabled);
	ClassDB::bind_method(D_METHOD("is_floor_block_on_wall_enabled"), &CharacterBody3D::is_floor_block_on_wall_enabled);
	ClassDB::bind_method(D_METHOD("set_slide_on_ceiling_enabled", "enabled"), &CharacterBody3D::set_slide_on_ceiling_enabled);
	ClassDB::bind_method(D_METHOD("is_slide_on_ceiling_enabled"), &CharacterBody3D::is_slide_on_ceiling_enabled);

	ClassDB::bind_method(D_METHOD("set_platform_floor_layers", "exclude_layer"), &CharacterBody3D::set_platform_floor_layers);
	ClassDB::bind_method(D_METHOD("get_platform_floor_layers"), &CharacterBody3D::get_platform_floor_layers);
	ClassDB::bind_method(D_METHOD("set_platform_wall_layers", "exclude_layer"), &CharacterBody3D::set_platform_wall_layers);
	ClassDB::bind_method(D_METHOD("get_platform_wall_layers"), &CharacterBody3D::get_platform_wall_layers);

	ClassDB::bind_method(D_METHOD("get_max_slides"), &CharacterBody3D::get_max_slides);
	ClassDB::bind_method(D_METHOD("set_max_slides", "max_slides"), &CharacterBody3D::set_max_slides);
	ClassDB::bind_method(D_METHOD("get_floor_max_angle"), &CharacterBody3D::get_floor_max_angle);
	ClassDB::bind_method(D_METHOD("set_floor_max_angle", "radians"), &CharacterBody3D::set_floor_max_angle);
	ClassDB::bind_method(D_METHOD("get_floor_snap_length"), &CharacterBody3D::get_floor_snap_length);
	ClassDB::bind_method(D_METHOD("set_floor_snap_length", "floor_snap_length"), &CharacterBody3D::set_floor_snap_length);
	ClassDB::bind_method(D_METHOD("get_floor_step_height"), &CharacterBody3D::get_floor_step_height); // FellowEngine (decision 0311)
	ClassDB::bind_method(D_METHOD("set_floor_step_height", "height"), &CharacterBody3D::set_floor_step_height);
	ClassDB::bind_method(D_METHOD("get_last_step"), &CharacterBody3D::get_last_step);
	ClassDB::bind_method(D_METHOD("get_rigid_push_force"), &CharacterBody3D::get_rigid_push_force); // FellowEngine (decision 0312)
	ClassDB::bind_method(D_METHOD("set_rigid_push_force", "force"), &CharacterBody3D::set_rigid_push_force);
	ClassDB::bind_method(D_METHOD("get_rigid_standing_mass"), &CharacterBody3D::get_rigid_standing_mass);
	ClassDB::bind_method(D_METHOD("set_rigid_standing_mass", "mass"), &CharacterBody3D::set_rigid_standing_mass);
	ClassDB::bind_method(D_METHOD("get_wall_min_slide_angle"), &CharacterBody3D::get_wall_min_slide_angle);
	ClassDB::bind_method(D_METHOD("set_wall_min_slide_angle", "radians"), &CharacterBody3D::set_wall_min_slide_angle);
	ClassDB::bind_method(D_METHOD("get_up_direction"), &CharacterBody3D::get_up_direction);
	ClassDB::bind_method(D_METHOD("set_up_direction", "up_direction"), &CharacterBody3D::set_up_direction);
	ClassDB::bind_method(D_METHOD("set_motion_mode", "mode"), &CharacterBody3D::set_motion_mode);
	ClassDB::bind_method(D_METHOD("get_motion_mode"), &CharacterBody3D::get_motion_mode);
	ClassDB::bind_method(D_METHOD("set_platform_on_leave", "on_leave_apply_velocity"), &CharacterBody3D::set_platform_on_leave);
	ClassDB::bind_method(D_METHOD("get_platform_on_leave"), &CharacterBody3D::get_platform_on_leave);

	ClassDB::bind_method(D_METHOD("is_on_floor"), &CharacterBody3D::is_on_floor);
	ClassDB::bind_method(D_METHOD("is_on_floor_only"), &CharacterBody3D::is_on_floor_only);
	ClassDB::bind_method(D_METHOD("is_on_ceiling"), &CharacterBody3D::is_on_ceiling);
	ClassDB::bind_method(D_METHOD("is_on_ceiling_only"), &CharacterBody3D::is_on_ceiling_only);
	ClassDB::bind_method(D_METHOD("is_on_wall"), &CharacterBody3D::is_on_wall);
	ClassDB::bind_method(D_METHOD("is_on_wall_only"), &CharacterBody3D::is_on_wall_only);
	ClassDB::bind_method(D_METHOD("get_floor_normal"), &CharacterBody3D::get_floor_normal);
	ClassDB::bind_method(D_METHOD("get_wall_normal"), &CharacterBody3D::get_wall_normal);
	ClassDB::bind_method(D_METHOD("get_last_motion"), &CharacterBody3D::get_last_motion);
	ClassDB::bind_method(D_METHOD("get_position_delta"), &CharacterBody3D::get_position_delta);
	ClassDB::bind_method(D_METHOD("get_real_velocity"), &CharacterBody3D::get_real_velocity);
	ClassDB::bind_method(D_METHOD("get_floor_angle", "up_direction"), &CharacterBody3D::get_floor_angle, DEFVAL(Vector3(0.0, 1.0, 0.0)));
	ClassDB::bind_method(D_METHOD("get_platform_velocity"), &CharacterBody3D::get_platform_velocity);
	ClassDB::bind_method(D_METHOD("get_platform_angular_velocity"), &CharacterBody3D::get_platform_angular_velocity);
	ClassDB::bind_method(D_METHOD("get_slide_collision_count"), &CharacterBody3D::get_slide_collision_count);
	ClassDB::bind_method(D_METHOD("get_slide_collision", "slide_idx"), &CharacterBody3D::_get_slide_collision);
	ClassDB::bind_method(D_METHOD("get_last_slide_collision"), &CharacterBody3D::_get_last_slide_collision);

	ADD_PROPERTY(PropertyInfo(Variant::INT, "motion_mode", PROPERTY_HINT_ENUM, "Grounded,Floating", PROPERTY_USAGE_DEFAULT | PROPERTY_USAGE_UPDATE_ALL_IF_MODIFIED), "set_motion_mode", "get_motion_mode");
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR3, "up_direction"), "set_up_direction", "get_up_direction");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "slide_on_ceiling"), "set_slide_on_ceiling_enabled", "is_slide_on_ceiling_enabled");
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR3, "velocity", PROPERTY_HINT_NONE, "suffix:m/s", PROPERTY_USAGE_NO_EDITOR), "set_velocity", "get_velocity");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "max_slides", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NO_EDITOR), "set_max_slides", "get_max_slides");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "wall_min_slide_angle", PROPERTY_HINT_RANGE, "0,180,0.1,radians_as_degrees", PROPERTY_USAGE_DEFAULT), "set_wall_min_slide_angle", "get_wall_min_slide_angle");

	ADD_GROUP("Floor", "floor_");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "floor_stop_on_slope"), "set_floor_stop_on_slope_enabled", "is_floor_stop_on_slope_enabled");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "floor_constant_speed"), "set_floor_constant_speed_enabled", "is_floor_constant_speed_enabled");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "floor_block_on_wall"), "set_floor_block_on_wall_enabled", "is_floor_block_on_wall_enabled");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "floor_max_angle", PROPERTY_HINT_RANGE, "0,180,0.1,radians_as_degrees"), "set_floor_max_angle", "get_floor_max_angle");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "floor_snap_length", PROPERTY_HINT_RANGE, "0,1,0.01,or_greater,suffix:m"), "set_floor_snap_length", "get_floor_snap_length");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "floor_step_height", PROPERTY_HINT_RANGE, "0,1,0.01,or_greater,suffix:m"), "set_floor_step_height", "get_floor_step_height"); // FellowEngine (decision 0311)

	ADD_GROUP("Rigid Bodies", "rigid_"); // FellowEngine (decision 0312)
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "rigid_push_force", PROPERTY_HINT_RANGE, "0,2000,1,or_greater,suffix:N"), "set_rigid_push_force", "get_rigid_push_force");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "rigid_standing_mass", PROPERTY_HINT_RANGE, "0,500,0.1,or_greater,suffix:kg"), "set_rigid_standing_mass", "get_rigid_standing_mass");
	ADD_GROUP("Moving Platform", "platform_");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "platform_on_leave", PROPERTY_HINT_ENUM, "Add Velocity,Add Upward Velocity,Do Nothing", PROPERTY_USAGE_DEFAULT), "set_platform_on_leave", "get_platform_on_leave");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "platform_floor_layers", PROPERTY_HINT_LAYERS_3D_PHYSICS), "set_platform_floor_layers", "get_platform_floor_layers");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "platform_wall_layers", PROPERTY_HINT_LAYERS_3D_PHYSICS), "set_platform_wall_layers", "get_platform_wall_layers");

	ADD_GROUP("Collision", "");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "safe_margin", PROPERTY_HINT_RANGE, "0.001,256,0.001,suffix:m"), "set_safe_margin", "get_safe_margin");

	BIND_ENUM_CONSTANT(MOTION_MODE_GROUNDED);
	BIND_ENUM_CONSTANT(MOTION_MODE_FLOATING);

	BIND_ENUM_CONSTANT(PLATFORM_ON_LEAVE_ADD_VELOCITY);
	BIND_ENUM_CONSTANT(PLATFORM_ON_LEAVE_ADD_UPWARD_VELOCITY);
	BIND_ENUM_CONSTANT(PLATFORM_ON_LEAVE_DO_NOTHING);
}

void CharacterBody3D::_validate_property(PropertyInfo &p_property) const {
	if (!Engine::get_singleton()->is_editor_hint()) {
		return;
	}
	if (motion_mode == MOTION_MODE_FLOATING) {
		if (p_property.name.begins_with("floor_") || p_property.name == "up_direction" || p_property.name == "slide_on_ceiling") {
			p_property.usage = PROPERTY_USAGE_NO_EDITOR;
		}
	}
}

CharacterBody3D::CharacterBody3D() :
		PhysicsBody3D(PhysicsServer3D::BODY_MODE_KINEMATIC) {
}
