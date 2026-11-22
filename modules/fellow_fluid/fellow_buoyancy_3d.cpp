/**************************************************************************/
/*  fellow_buoyancy_3d.cpp                                                */
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

#include "fellow_buoyancy_3d.h"

#include "fellow_water_3d.h"

#include "core/config/project_settings.h"
#include "core/math/convex_hull.h"
#include "core/object/class_db.h"
#include "core/os/os.h"
#include "scene/3d/physics/collision_shape_3d.h"
#include "scene/3d/physics/rigid_body_3d.h"
#include "scene/resources/3d/box_shape_3d.h"
#include "scene/resources/3d/capsule_shape_3d.h"
#include "scene/resources/3d/convex_polygon_shape_3d.h"
#include "scene/resources/3d/cylinder_shape_3d.h"
#include "scene/resources/3d/sphere_shape_3d.h"
#include "servers/physics_3d/physics_server_3d.h"

RigidBody3D *FellowBuoyancy3D::_body() const {
	return Object::cast_to<RigidBody3D>(get_parent());
}

FellowWater3D *FellowBuoyancy3D::_water(const Vector3 &p_at) const {
	if (!water_path.is_empty()) {
		return Object::cast_to<FellowWater3D>(get_node_or_null(water_path));
	}
	return FellowWater3D::find_water(p_at);
}

void FellowBuoyancy3D::set_point_spacing(float p_spacing) {
	point_spacing = MAX(0.02f, p_spacing);
	points_dirty = true;
}

void FellowBuoyancy3D::set_max_points(int p_points) {
	max_points = CLAMP(p_points, 1, 1024);
	points_dirty = true;
}

// The body's collision shapes filled with points on a grid of point_spacing (coarser until there are no more than
// max_points), each the share of its shape's volume; a shape too small for the grid is one point at its centre.
void FellowBuoyancy3D::_rebuild_points() {
	points_dirty = false;
	points.clear();
	total_volume = 0.0f;
	approximate_shapes = false;
	RigidBody3D *body = _body();
	if (!body) {
		return;
	}
	struct ShapeInfo {
		Transform3D xform;
		Ref<Shape3D> shape;
	};
	LocalVector<ShapeInfo> shapes;
	for (int i = 0; i < body->get_child_count(); i++) {
		CollisionShape3D *cs = Object::cast_to<CollisionShape3D>(body->get_child(i));
		if (cs && !cs->is_disabled() && cs->get_shape().is_valid()) {
			shapes.push_back({ cs->get_transform(), cs->get_shape() });
		}
	}
	float spacing = point_spacing;
	for (int attempt = 0; attempt < 32; attempt++) {
		points.clear();
		total_volume = 0.0f;
		for (const ShapeInfo &si : shapes) {
			const Ref<Shape3D> &shape = si.shape;
			AABB box;
			float volume = 0.0f;
			LocalVector<Plane> planes;
			int kind = 0; // 0 box, 1 sphere, 2 capsule, 3 cylinder, 4 convex
			float radius = 0.0f;
			float height = 0.0f;
			if (Ref<BoxShape3D> b = shape; b.is_valid()) {
				box = AABB(-b->get_size() * 0.5f, b->get_size());
				volume = b->get_size().x * b->get_size().y * b->get_size().z;
			} else if (Ref<SphereShape3D> sp = shape; sp.is_valid()) {
				kind = 1;
				radius = sp->get_radius();
				box = AABB(Vector3(-radius, -radius, -radius), Vector3(radius, radius, radius) * 2.0f);
				volume = 4.0f / 3.0f * Math::PI * radius * radius * radius;
			} else if (Ref<CapsuleShape3D> cp = shape; cp.is_valid()) {
				kind = 2;
				radius = cp->get_radius();
				height = MAX(cp->get_height(), radius * 2.0f);
				box = AABB(Vector3(-radius, -height * 0.5f, -radius), Vector3(radius * 2.0f, height, radius * 2.0f));
				volume = Math::PI * radius * radius * (height - 2.0f * radius) + 4.0f / 3.0f * Math::PI * radius * radius * radius;
			} else if (Ref<CylinderShape3D> cy = shape; cy.is_valid()) {
				kind = 3;
				radius = cy->get_radius();
				height = cy->get_height();
				box = AABB(Vector3(-radius, -height * 0.5f, -radius), Vector3(radius * 2.0f, height, radius * 2.0f));
				volume = Math::PI * radius * radius * height;
			} else if (Ref<ConvexPolygonShape3D> cv = shape; cv.is_valid() && cv->get_points().size() >= 4) {
				kind = 4;
				Geometry3D::MeshData md;
				if (ConvexHullComputer::convex_hull(cv->get_points(), md) == OK && md.faces.size() >= 4) {
					for (const Geometry3D::MeshData::Face &f : md.faces) {
						planes.push_back(f.plane);
						// The hull's volume: tetrahedra from the origin over each face's fan.
						for (uint32_t t = 1; t + 1 < f.indices.size(); t++) {
							const Vector3 &a = md.vertices[f.indices[0]];
							const Vector3 &bb = md.vertices[f.indices[t]];
							const Vector3 &cc = md.vertices[f.indices[t + 1]];
							volume += a.dot(bb.cross(cc)) / 6.0f;
						}
					}
					volume = Math::abs(volume);
					box = AABB(md.vertices[0], Vector3());
					for (const Vector3 &v : md.vertices) {
						box.expand_to(v);
					}
				} else {
					kind = 0;
					box = shape->get_debug_mesh().is_valid() ? shape->get_debug_mesh()->get_aabb() : AABB();
					volume = box.get_volume();
					approximate_shapes = true;
				}
			} else {
				// Another shape: its debug mesh's box (a trimesh, a height map: hollow or open — floated as a box).
				box = shape->get_debug_mesh().is_valid() ? shape->get_debug_mesh()->get_aabb() : AABB();
				volume = box.get_volume();
				approximate_shapes = true;
			}
			if (volume <= 0.0f) {
				continue;
			}
			int counts[3];
			for (int a = 0; a < 3; a++) {
				counts[a] = MAX(1, int(Math::round(box.size[a] / spacing)));
			}
			const Vector3 cell(box.size.x / counts[0], box.size.y / counts[1], box.size.z / counts[2]);
			const uint32_t first = points.size();
			for (int x = 0; x < counts[0]; x++) {
				for (int y = 0; y < counts[1]; y++) {
					for (int z = 0; z < counts[2]; z++) {
						const Vector3 p = box.position + Vector3((x + 0.5f) * cell.x, (y + 0.5f) * cell.y, (z + 0.5f) * cell.z);
						bool inside = true;
						switch (kind) {
							case 1:
								inside = p.length() <= radius;
								break;
							case 2: {
								const float seg = MAX(0.0f, height * 0.5f - radius);
								inside = Vector3(p.x, p.y - CLAMP(p.y, -seg, seg), p.z).length() <= radius;
							} break;
							case 3:
								inside = Math::abs(p.y) <= height * 0.5f && p.x * p.x + p.z * p.z <= radius * radius;
								break;
							case 4:
								for (const Plane &pl : planes) {
									if (pl.distance_to(p) > 0.0f) {
										inside = false;
										break;
									}
								}
								break;
						}
						if (inside) {
							Point pt;
							pt.local = si.xform.xform(p);
							pt.half_height = cell.y * 0.5f;
							pt.half_width = MAX(cell.x, cell.z) * 0.5f;
							points.push_back(pt);
						}
					}
				}
			}
			if (points.size() == first) {
				Point pt;
				pt.local = si.xform.xform(box.get_center());
				pt.half_height = box.size.y * 0.5f;
				pt.half_width = MAX(box.size.x, box.size.z) * 0.5f;
				points.push_back(pt);
			}
			const float share = volume / float(points.size() - first);
			for (uint32_t i = first; i < points.size(); i++) {
				points[i].volume = share;
			}
			total_volume += volume;
		}
		if ((int)points.size() <= max_points) {
			break;
		}
		spacing *= 1.25f;
	}
	bound_radius = 0.0f;
	for (const Point &pt : points) {
		bound_radius = MAX(bound_radius, pt.local.length() + Vector3(pt.half_width, pt.half_height, pt.half_width).length());
	}
	// Not known yet: as if under, so that a body found under the surface (made or set there) does not splash; one that
	// comes from above it is seen out of the water first.
	under.resize(points.size());
	for (uint8_t &u : under) {
		u = 1;
	}
	under_cleared = false;
}

// Foam (decision 0318): a point at the waterline moving through the water faster than FOAM_SPEED leaves foam, more the
// faster; a point striking the water faster than SPLASH_SPEED splashes it round.
static const float FOAM_SPEED = 0.4f; // m/s
static const float FOAM_RATE = 1.0f; // coverage a second per m/s over FOAM_SPEED
static const float SPLASH_SPEED = 1.0f; // m/s
static const float SPLASH_GAIN = 0.2f; // coverage per m/s
static const float LAP_SPEED = 0.15f; // m/s: the water closing over a point faster laps (a sound)

float FellowBuoyancy3D::_splash_random() {
	splash_seed = splash_seed * 1664525u + 1013904223u;
	return float(splash_seed >> 8) / float(1u << 24);
}

void FellowBuoyancy3D::_physics_tick() {
	RigidBody3D *body = _body();
	if (!body || !body->is_inside_tree() || body->is_freeze_enabled()) {
		submerged_share = 0.0f;
		return;
	}
	if (points_dirty) {
		_rebuild_points();
	}
	if (points.is_empty() || total_volume <= 0.0f) {
		return;
	}
	PhysicsDirectBodyState3D *state = PhysicsServer3D::get_singleton()->body_get_direct_state(body->get_rid());
	if (!state) {
		return;
	}
	const uint64_t t0 = OS::get_singleton()->get_ticks_usec();
	const Transform3D xf = body->get_global_transform();
	FellowWater3D *water = _water(xf.origin);
	// Out of the water: none under it, or the body above where its waves reach (decision 0318: no waves asked for).
	if (!water || xf.origin.y - bound_radius * xf.basis.get_scale().length() * 0.57735f > water->get_surface_ceiling()) {
		submerged_share = 0.0f;
		if (!under_cleared) {
			for (uint8_t &u : under) {
				u = 0;
			}
			under_cleared = true;
		}
		if (in_water) {
			in_water = false;
			emit_signal(SNAME("water_exited"));
		}
		ticks++;
		skipped_ticks++;
		usec += OS::get_singleton()->get_ticks_usec() - t0;
		return;
	}
	const Vector3 com = xf.origin + state->get_center_of_mass();
	const Vector3 v = state->get_linear_velocity();
	const Vector3 w = state->get_angular_velocity();
	const float mass = body->get_mass();
	const float g = float(GLOBAL_GET("physics/3d/default_gravity"));
	const float dt = float(get_physics_process_delta_time());
	Vector3 force;
	Vector3 torque;
	float submerged = 0.0f;
	float entry_speed = 0.0f;
	const float share_before = submerged_share;
	// The sound (decision 0319): the points striking the water, those at the waterline and how fast the water moves past
	// them.
	int splash_points = 0;
	Vector3 splash_at;
	float splash_speed = 0.0f;
	float splash_width = 0.0f;
	int line_points = 0;
	Vector3 line_at;
	float line_motion = 0.0f;
	int lap_points = 0;
	Vector3 lap_at;
	float lap_speed = 0.0f;
	float lap_width = 0.0f;
	for (uint32_t i = 0; i < points.size(); i++) {
		const Point &pt = points[i];
		const Vector3 wp = xf.xform(pt.local);
		float h;
		Vector3 u;
		if (!water->fellow_sample_surface(wp, h, u)) {
			under[i] = 0;
			continue;
		}
		const float hh = MAX(pt.half_height, 1e-3f);
		const float f = CLAMP((h - wp.y + hh) / (2.0f * hh), 0.0f, 1.0f);
		if (f <= 0.0f) {
			under[i] = 0;
			continue;
		}
		// Archimedes: the weight of the water the point displaces, up; and a drag towards the water's motion, as the
		// point's share of the body's mass.
		const Vector3 r = wp - com;
		const Vector3 vp = v + w.cross(r);
		const Vector3 rel = vp - (follow_waves ? u : Vector3());
		const Vector3 fp = Vector3(0.0f, water_density * g * pt.volume * f, 0.0f) - rel * (linear_drag * mass * (pt.volume / total_volume) * f);
		force += fp;
		torque += r.cross(fp);
		submerged += pt.volume * f;
		const Vector3 against = vp - u; // through the water
		if (!under[i]) {
			entry_speed = MAX(entry_speed, against.length());
			if (-against.y > SPLASH_SPEED && share_before < 0.05f) {
				splash_points++;
				splash_at += Vector3(wp.x, h, wp.z);
				splash_speed = MAX(splash_speed, -against.y);
				splash_width += pt.half_width * 2.0f;
			} else if (Math::abs(against.y) > LAP_SPEED) {
				// The water closing over a point of a body already in it (a wave, its bobbing, it going deeper): a lap.
				lap_points++;
				lap_at += Vector3(wp.x, h, wp.z);
				lap_speed = MAX(lap_speed, Math::abs(against.y));
				lap_width += pt.half_width * 2.0f;
			}
		}
		if (f < 1.0f) {
			line_points++;
			line_at += Vector3(wp.x, h, wp.z);
			line_motion += MAX(0.0f, Vector2(against.x, against.z).length() - 0.15f) * pt.half_width * 2.0f;
		}
		if (foam > 0.0f) {
			const Vector3 at(wp.x, h, wp.z);
			if (!under[i] && -against.y > SPLASH_SPEED) {
				// Each point's splash a little off and of its own size: an irregular patch, not a disc.
				const float s = -against.y;
				const float r = pt.half_width * 2.0f + 0.06f * s;
				const float j[3] = { _splash_random(), _splash_random(), _splash_random() };
				water->add_foam(at + Vector3(j[0] - 0.5f, 0.0f, j[1] - 0.5f) * r, r * (0.6f + 0.8f * j[2]), foam * MIN(1.0f, SPLASH_GAIN * s));
			} else if (f < 1.0f) {
				const float s = Vector2(against.x, against.z).length() - FOAM_SPEED;
				if (s > 0.0f) {
					water->add_foam(at, pt.half_width * 2.0f + 0.15f * s, foam * FOAM_RATE * s * dt); // spreading with the speed
				}
			}
		}
		under[i] = 1;
	}
	under_cleared = false;
	submerged_share = submerged / total_volume;
	if (sound > 0.0f) {
		// A splash when a body out of the water strikes it (its footprint: the striking points' widths over the side of
		// their square), else a lap where the water closes over points of a body in it; the waterline's motion through
		// the water (summed over its points, over the side of their square: its length) as the wake's level.
		if (splash_points > 0) {
			water->fellow_splash(splash_at / float(splash_points), splash_speed, splash_width / Math::sqrt(float(splash_points)), sound);
		} else if (lap_points > 0) {
			water->fellow_splash(lap_at / float(lap_points), lap_speed, lap_width / Math::sqrt(float(lap_points)), sound * 0.7f);
		}
		if (line_points > 0) {
			const float level = line_motion / Math::sqrt(float(line_points));
			if (level > 0.02f) {
				water->add_wake_sound(line_at / float(line_points), level * sound);
			}
		}
	}
	if (submerged_share > 0.0f) {
		// The turning slowed as much as the water around the body.
		const Basis inertia = state->get_inverse_inertia_tensor().inverse();
		torque -= inertia.xform(w) * (angular_drag * submerged_share);
		body->apply_central_force(force);
		body->apply_torque(torque);
		if (body->is_sleeping() && (water->get_wave_scale() > 0.0f || submerged_share < 0.999f)) {
			body->set_sleeping(false); // the waves move it
		}
	}
	const bool now_in = submerged_share > 0.0f;
	ticks++;
	usec += OS::get_singleton()->get_ticks_usec() - t0;
	if (now_in != in_water) {
		in_water = now_in;
		if (now_in) {
			emit_signal(SNAME("water_entered"), entry_speed);
		} else {
			emit_signal(SNAME("water_exited"));
		}
	}
}

void FellowBuoyancy3D::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_ENTER_TREE: {
			points_dirty = true;
			splash_seed = uint32_t(uint64_t(get_instance_id()));
			set_physics_process_internal(true);
		} break;
		case NOTIFICATION_EXIT_TREE: {
			set_physics_process_internal(false);
		} break;
		case NOTIFICATION_INTERNAL_PHYSICS_PROCESS: {
			if (!Engine::get_singleton()->is_editor_hint()) {
				_physics_tick();
			}
		} break;
	}
}

int FellowBuoyancy3D::get_point_count() {
	if (points_dirty) {
		_rebuild_points();
	}
	return points.size();
}

float FellowBuoyancy3D::get_volume() {
	if (points_dirty) {
		_rebuild_points();
	}
	return total_volume;
}

Dictionary FellowBuoyancy3D::get_stats() const {
	Dictionary d;
	d["points"] = (int64_t)points.size();
	d["volume"] = total_volume;
	d["ticks"] = (int64_t)ticks;
	d["usec_per_tick"] = ticks > 0 ? double(usec) / double(ticks) : 0.0;
	d["submerged_share"] = submerged_share;
	d["skipped_ticks"] = (int64_t)skipped_ticks;
	d["bound_radius"] = bound_radius;
	return d;
}

PackedStringArray FellowBuoyancy3D::get_configuration_warnings() const {
	PackedStringArray warnings = Node3D::get_configuration_warnings();
	RigidBody3D *body = _body();
	if (!body) {
		warnings.push_back(RTR("FellowBuoyancy3D floats its parent: make it a child of a RigidBody3D."));
	} else {
		bool shapes = false;
		for (int i = 0; i < body->get_child_count(); i++) {
			CollisionShape3D *cs = Object::cast_to<CollisionShape3D>(body->get_child(i));
			shapes = shapes || (cs && cs->get_shape().is_valid());
		}
		if (!shapes) {
			warnings.push_back(RTR("The body has no CollisionShape3D children: nothing to float."));
		}
		if (approximate_shapes) {
			warnings.push_back(RTR("Some of the body's shapes are neither boxes, spheres, capsules, cylinders nor convex: floated as their bounding boxes."));
		}
	}
	if (!water_path.is_empty() && !Object::cast_to<FellowWater3D>(get_node_or_null(water_path))) {
		warnings.push_back(RTR("water_path is not a FellowWater3D."));
	}
	return warnings;
}

void FellowBuoyancy3D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_water_path", "path"), &FellowBuoyancy3D::set_water_path);
	ClassDB::bind_method(D_METHOD("get_water_path"), &FellowBuoyancy3D::get_water_path);
	ClassDB::bind_method(D_METHOD("set_water_density", "density"), &FellowBuoyancy3D::set_water_density);
	ClassDB::bind_method(D_METHOD("get_water_density"), &FellowBuoyancy3D::get_water_density);
	ClassDB::bind_method(D_METHOD("set_point_spacing", "spacing"), &FellowBuoyancy3D::set_point_spacing);
	ClassDB::bind_method(D_METHOD("get_point_spacing"), &FellowBuoyancy3D::get_point_spacing);
	ClassDB::bind_method(D_METHOD("set_max_points", "points"), &FellowBuoyancy3D::set_max_points);
	ClassDB::bind_method(D_METHOD("get_max_points"), &FellowBuoyancy3D::get_max_points);
	ClassDB::bind_method(D_METHOD("set_linear_drag", "drag"), &FellowBuoyancy3D::set_linear_drag);
	ClassDB::bind_method(D_METHOD("get_linear_drag"), &FellowBuoyancy3D::get_linear_drag);
	ClassDB::bind_method(D_METHOD("set_angular_drag", "drag"), &FellowBuoyancy3D::set_angular_drag);
	ClassDB::bind_method(D_METHOD("get_angular_drag"), &FellowBuoyancy3D::get_angular_drag);
	ClassDB::bind_method(D_METHOD("set_follow_waves", "follow"), &FellowBuoyancy3D::set_follow_waves);
	ClassDB::bind_method(D_METHOD("is_follow_waves"), &FellowBuoyancy3D::is_follow_waves);
	ClassDB::bind_method(D_METHOD("set_foam", "foam"), &FellowBuoyancy3D::set_foam);
	ClassDB::bind_method(D_METHOD("get_foam"), &FellowBuoyancy3D::get_foam);
	ClassDB::bind_method(D_METHOD("is_in_water"), &FellowBuoyancy3D::is_in_water);
	ClassDB::bind_method(D_METHOD("set_sound", "sound"), &FellowBuoyancy3D::set_sound);
	ClassDB::bind_method(D_METHOD("get_sound"), &FellowBuoyancy3D::get_sound);
	ClassDB::bind_method(D_METHOD("rebuild_points"), &FellowBuoyancy3D::rebuild_points);
	ClassDB::bind_method(D_METHOD("get_point_count"), &FellowBuoyancy3D::get_point_count);
	ClassDB::bind_method(D_METHOD("get_volume"), &FellowBuoyancy3D::get_volume);
	ClassDB::bind_method(D_METHOD("get_submerged_share"), &FellowBuoyancy3D::get_submerged_share);
	ClassDB::bind_method(D_METHOD("get_stats"), &FellowBuoyancy3D::get_stats);

	ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "water_path", PROPERTY_HINT_NODE_PATH_VALID_TYPES, "FellowWater3D"), "set_water_path", "get_water_path");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "water_density", PROPERTY_HINT_RANGE, "1,2000,1,suffix:kg/m\u00b3"), "set_water_density", "get_water_density");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "point_spacing", PROPERTY_HINT_RANGE, "0.02,2,0.01,suffix:m"), "set_point_spacing", "get_point_spacing");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "max_points", PROPERTY_HINT_RANGE, "1,1024,1"), "set_max_points", "get_max_points");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "linear_drag", PROPERTY_HINT_RANGE, "0,10,0.01,suffix:1/s"), "set_linear_drag", "get_linear_drag");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "angular_drag", PROPERTY_HINT_RANGE, "0,10,0.01,suffix:1/s"), "set_angular_drag", "get_angular_drag");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "follow_waves"), "set_follow_waves", "is_follow_waves");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "foam", PROPERTY_HINT_RANGE, "0,4,0.01"), "set_foam", "get_foam");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "sound", PROPERTY_HINT_RANGE, "0,4,0.01"), "set_sound", "get_sound");

	ADD_SIGNAL(MethodInfo("water_entered", PropertyInfo(Variant::FLOAT, "speed")));
	ADD_SIGNAL(MethodInfo("water_exited"));
}
