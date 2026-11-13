/**************************************************************************/
/*  fellow_vehicle_damage_3d.cpp                                          */
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

#include "fellow_vehicle_damage_3d.h"

#include "fellow_vehicle_3d.h"

#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "core/os/os.h"
#include "scene/3d/gpu_particles_3d.h"
#include "scene/3d/mesh_instance_3d.h"
#include "scene/3d/physics/collision_shape_3d.h"
#include "scene/3d/physics/joints/hinge_joint_3d.h"
#include "scene/3d/physics/joints/pin_joint_3d.h"
#include "scene/3d/physics/rigid_body_3d.h"
#include "scene/resources/3d/box_shape_3d.h"
#include "scene/resources/3d/primitive_meshes.h"
#include "scene/resources/curve_texture.h"
#include "scene/resources/gradient_texture.h"
#include "scene/resources/material.h"
#include "scene/resources/particle_process_material.h"
#include "servers/physics_3d/physics_server_3d.h"

void FellowVehicleDamage3D::set_mesh_paths(const TypedArray<NodePath> &p_paths) {
	mesh_paths = p_paths;
	repair();
	targets.clear();
	targets_ready = false;
}

void FellowVehicleDamage3D::_gather_targets() {
	targets.clear();
	targets_ready = true;
	if (!vehicle) {
		return;
	}
	LocalVector<MeshInstance3D *> found;
	if (!mesh_paths.is_empty()) {
		for (int i = 0; i < mesh_paths.size(); i++) {
			if (MeshInstance3D *mi = Object::cast_to<MeshInstance3D>(get_node_or_null(mesh_paths[i]))) {
				found.push_back(mi);
			}
		}
	} else {
		// Every mesh of the vehicle but its wheels' (they turn; a dented tyre is not this node's).
		LocalVector<Node *> stack;
		stack.push_back(vehicle);
		while (!stack.is_empty()) {
			Node *n = stack[stack.size() - 1];
			stack.remove_at(stack.size() - 1);
			for (int i = 0; i < n->get_child_count(); i++) {
				Node *c = n->get_child(i);
				if (Object::cast_to<FellowWheel3D>(c)) {
					continue;
				}
				if (MeshInstance3D *mi = Object::cast_to<MeshInstance3D>(c)) {
					found.push_back(mi);
				}
				stack.push_back(c);
			}
		}
	}
	for (MeshInstance3D *mi : found) {
		Target t;
		t.instance = mi->get_instance_id();
		targets.push_back(t);
	}
}

bool FellowVehicleDamage3D::_prepare(Target &r_target) {
	if (r_target.dented.is_valid()) {
		return true;
	}
	MeshInstance3D *mi = ObjectDB::get_instance<MeshInstance3D>(r_target.instance);
	if (!mi || mi->get_mesh().is_null()) {
		return false;
	}
	const Ref<Mesh> mesh = mi->get_mesh();
	if (mesh->get_blend_shape_count() > 0) {
		return false; // its blend shapes' arrays would have to follow
	}
	r_target.original = mesh;
	r_target.surfaces.clear();
	r_target.materials.clear();
	r_target.primitives.clear();
	r_target.offsets.clear();
	for (int s = 0; s < mesh->get_surface_count(); s++) {
		const Array arrays = mesh->surface_get_arrays(s);
		const PackedVector3Array v = arrays[Mesh::ARRAY_VERTEX];
		r_target.surfaces.push_back(arrays);
		r_target.materials.push_back(mesh->surface_get_material(s));
		r_target.primitives.push_back(mesh->surface_get_primitive_type(s));
		LocalVector<Vector3> off;
		off.resize(v.size());
		for (Vector3 &o : off) {
			o = Vector3();
		}
		r_target.offsets.push_back(std::move(off));
		vertices += v.size();
	}
	r_target.dented.instantiate();
	return true;
}

// Each vertex's smooth normal from the triangles around it (area-weighted); where the mesh splits its vertices (hard
// edges), each side keeps its own.
static PackedVector3Array _fellow_damage_normals(const PackedVector3Array &p_vertices, const PackedInt32Array &p_indices) {
	PackedVector3Array n;
	n.resize(p_vertices.size());
	Vector3 *nw = n.ptrw();
	for (int i = 0; i < n.size(); i++) {
		nw[i] = Vector3();
	}
	const Vector3 *v = p_vertices.ptr();
	const int count = p_indices.is_empty() ? p_vertices.size() : p_indices.size();
	for (int i = 0; i + 2 < count; i += 3) {
		const int a = p_indices.is_empty() ? i : p_indices[i];
		const int b = p_indices.is_empty() ? i + 1 : p_indices[i + 1];
		const int c = p_indices.is_empty() ? i + 2 : p_indices[i + 2];
		// Godot's front faces are clockwise: the normal is (c - a) x (b - a).
		const Vector3 f = (v[c] - v[a]).cross(v[b] - v[a]);
		nw[a] += f;
		nw[b] += f;
		nw[c] += f;
	}
	for (int i = 0; i < n.size(); i++) {
		nw[i] = nw[i].length_squared() > 0.0f ? nw[i].normalized() : Vector3(0, 1, 0);
	}
	return n;
}

void FellowVehicleDamage3D::_rebuild(Target &r_target) {
	MeshInstance3D *mi = ObjectDB::get_instance<MeshInstance3D>(r_target.instance);
	if (!mi) {
		return;
	}
	r_target.dented->clear_surfaces();
	for (uint32_t s = 0; s < r_target.surfaces.size(); s++) {
		Array arrays = r_target.surfaces[s].duplicate();
		PackedVector3Array v = arrays[Mesh::ARRAY_VERTEX];
		Vector3 *vw = v.ptrw();
		const LocalVector<Vector3> &off = r_target.offsets[s];
		for (int i = 0; i < v.size() && i < int(off.size()); i++) {
			vw[i] += off[i];
		}
		arrays[Mesh::ARRAY_VERTEX] = v;
		if (r_target.primitives[s] == Mesh::PRIMITIVE_TRIANGLES && arrays[Mesh::ARRAY_NORMAL].get_type() == Variant::PACKED_VECTOR3_ARRAY) {
			const PackedInt32Array indices = arrays[Mesh::ARRAY_INDEX].get_type() == Variant::PACKED_INT32_ARRAY ? PackedInt32Array(arrays[Mesh::ARRAY_INDEX]) : PackedInt32Array();
			arrays[Mesh::ARRAY_NORMAL] = _fellow_damage_normals(v, indices);
		}
		r_target.dented->add_surface_from_arrays(r_target.primitives[s], arrays);
		r_target.dented->surface_set_material(s, r_target.materials[s]);
	}
	if (mi->get_mesh() != r_target.dented) {
		mi->set_mesh(r_target.dented);
	}
}

void FellowVehicleDamage3D::_impact(const Vector3 &p_position, const Vector3 &p_direction, float p_depth) {
	if (!targets_ready) {
		_gather_targets();
	}
	const uint64_t t0 = OS::get_singleton()->get_ticks_usec();
	for (Target &t : targets) {
		MeshInstance3D *mi = ObjectDB::get_instance<MeshInstance3D>(t.instance);
		if (!mi || !mi->is_inside_tree()) {
			continue;
		}
		if (vehicle && !vehicle->is_ancestor_of(mi)) {
			continue; // milestone 108: a part that came off
		}
		const Transform3D xf = mi->get_global_transform();
		const float scale = MAX(float(xf.basis.get_scale().length()) / float(Math::SQRT3), 1e-4f);
		const Transform3D inv = xf.affine_inverse();
		const Vector3 p = inv.xform(p_position);
		const Vector3 d = inv.basis.xform(p_direction).normalized();
		const float r = radius / scale;
		const float depth = p_depth / scale;
		const float limit = max_depth / scale;
		// Only a mesh within reach of the impact.
		if (!mi->get_aabb().grow(r).has_point(p)) {
			continue;
		}
		if (!_prepare(t)) {
			continue;
		}
		bool moved = false;
		for (uint32_t s = 0; s < t.surfaces.size(); s++) {
			const PackedVector3Array v = t.surfaces[s][Mesh::ARRAY_VERTEX];
			const Vector3 *vr = v.ptr();
			LocalVector<Vector3> &off = t.offsets[s];
			for (int i = 0; i < v.size(); i++) {
				const float dist = (vr[i] + off[i]).distance_to(p);
				if (dist >= r) {
					continue;
				}
				const float q = 1.0f - (dist / r) * (dist / r);
				Vector3 o = off[i] + d * (depth * q * q);
				const float len = o.length();
				if (len > limit) {
					o *= limit / len;
				}
				off[i] = o;
				deepest = MAX(deepest, MIN(len, limit) * scale);
				moved = true;
			}
		}
		if (moved) {
			_rebuild(t);
		}
	}
	deform_usec += OS::get_singleton()->get_ticks_usec() - t0;
}

// Milestone 107 (decision 0215): how much of a dent at p_position, pushed along p_direction, reaches a part at p_part
// (global): the vertices' falloff over radius, the distance along the push counted half (an impact reaches into the
// body twice as far as it spreads over it).
float FellowVehicleDamage3D::_reach(const Vector3 &p_part, const Vector3 &p_position, const Vector3 &p_direction) const {
	const Vector3 v = p_part - p_position;
	const float along = v.dot(p_direction);
	const Vector3 across = v - p_direction * along;
	const float dist = Math::sqrt(across.length_squared() + 0.25f * along * along);
	if (dist >= radius) {
		return 0.0f;
	}
	const float q = 1.0f - (dist / radius) * (dist / radius);
	return q * q;
}

// Milestone 107: a dent reaching a wheel bends its toe — the way the push turns it about its upright (an impact
// ahead of the wheel's centre and outside it turns its front out, inside it in), up to wheel_bend in all; one reaching
// the engine takes engine_power_loss of its torque at max_depth.
void FellowVehicleDamage3D::_running_gear(const Vector3 &p_position, const Vector3 &p_direction, float p_depth) {
	if (!vehicle || max_depth <= 0.0f) {
		return;
	}
	if (wheel_bend > 0.0f || suspension_damage > 0.0f) {
		const Basis to_vehicle = vehicle->get_global_basis().inverse();
		for (FellowWheel3D *w : vehicle->get_wheels()) {
			if (!w->is_inside_tree()) {
				continue;
			}
			const Vector3 c = w->get_global_position();
			const float reach = _reach(c, p_position, p_direction);
			if (reach <= 0.0f) {
				continue;
			}
			// The push's turning about the wheel's upright (the vehicle's +Y): + turns its front (-Z) towards -X.
			const Vector3 arm = to_vehicle.xform(p_position - c);
			const Vector3 push = to_vehicle.xform(p_direction);
			const float turn = CLAMP(arm.cross(push).y / MAX(w->get_radius(), 0.05f), -1.0f, 1.0f);
			const bool bends = wheel_bend > 0.0f && Math::abs(turn) >= 1e-4f;
			if (!bends && suspension_damage <= 0.0f) {
				continue;
			}
			Bent *b = nullptr;
			for (Bent &e : bent) {
				if (e.wheel == w->get_instance_id()) {
					b = &e;
				}
			}
			if (!b) {
				Bent e;
				e.wheel = w->get_instance_id();
				e.toe_base = w->get_toe();
				e.toe_set = e.toe_base;
				bent.push_back(e);
				b = &bent[bent.size() - 1];
			}
			if (bends) {
				if (w->get_toe() != b->toe_set) {
					b->toe_base = w->get_toe(); // the game set it since
				}
				// A turn of the front towards -X is toe-out on a left wheel (x < 0), toe-in on a right one.
				const float side = w->get_position().x < 0.0f ? -1.0f : 1.0f;
				b->bend = CLAMP(b->bend + side * turn * wheel_bend * reach * p_depth / max_depth, -wheel_bend, wheel_bend);
				b->toe_set = b->toe_base + b->bend;
				w->set_toe(b->toe_set);
			}
			// Milestone 110: the suspension's travel lost (a bent strut: that corner sits lower), up to
			// suspension_damage.
			if (suspension_damage > 0.0f) {
				if (b->length_base < 0.0f || w->get_suspension_length() != b->length_set) {
					b->length_base = w->get_suspension_length(); // the first sag, or the game set it since
				}
				b->sag = MIN(b->sag + suspension_damage * reach * p_depth / max_depth, suspension_damage);
				b->length_set = MAX(b->length_base - b->sag, 0.0f);
				w->set_suspension_length(b->length_set);
			}
		}
	}
	if (engine_power_loss > 0.0f) {
		const float reach = _reach(vehicle->get_global_transform().xform(engine_position), p_position, p_direction);
		if (reach > 0.0f) {
			engine_dent = MIN(engine_dent + p_depth * reach, max_depth);
			if (torque_base < 0.0f || vehicle->get_max_torque() != torque_set) {
				torque_base = vehicle->get_max_torque(); // the first dent, or the game set it since
			}
			torque_set = torque_base * (1.0f - engine_power_loss * engine_dent / max_depth);
			vehicle->set_max_torque(torque_set);
		}
	}
}

// Milestone 108 (decision 0216): the parts that can come off, where they are now.
void FellowVehicleDamage3D::set_detachable_paths(const TypedArray<NodePath> &p_paths) {
	repair();
	detachable_paths = p_paths;
	parts.clear();
	parts_ready = false;
}

void FellowVehicleDamage3D::_gather_parts() {
	parts.clear();
	parts_ready = true;
	for (int i = 0; i < detachable_paths.size(); i++) {
		Node3D *n = Object::cast_to<Node3D>(get_node_or_null(detachable_paths[i]));
		if (!n || !n->get_parent()) {
			continue;
		}
		Part part;
		part.path = detachable_paths[i];
		part.node = n->get_instance_id();
		part.parent = n->get_parent()->get_instance_id();
		part.local = n->get_transform();
		part.index = n->get_index();
		parts.push_back(part);
	}
}

// The dent that reaches each part — at the point of its box nearest the impact (a bumper's end is reached by a hit at
// its corner) — added up; at detach_depth it comes off (after the physics step: bodies are not added during it).
void FellowVehicleDamage3D::_parts_hit(const Vector3 &p_position, const Vector3 &p_direction, float p_depth) {
	if (detachable_paths.is_empty()) {
		return;
	}
	if (!parts_ready) {
		_gather_parts();
	}
	for (Part &part : parts) {
		Node3D *n = ObjectDB::get_instance<Node3D>(part.node);
		const bool off = part.body.is_valid() && !part.joint.is_valid();
		if (!n || off || part.pending || !n->is_inside_tree()) {
			continue;
		}
		const VisualInstance3D *vi = Object::cast_to<VisualInstance3D>(n);
		const AABB box = vi ? vi->get_aabb() : AABB(Vector3(-0.05, -0.05, -0.05), Vector3(0.1, 0.1, 0.1));
		const Transform3D xf = n->get_global_transform();
		const Vector3 local = xf.affine_inverse().xform(p_position);
		const Vector3 nearest = xf.xform(local.clamp(box.position, box.position + box.size));
		const float reach = _reach(nearest, p_position, p_direction);
		if (reach <= 0.0f) {
			continue;
		}
		part.dent += p_depth * reach;
		part.last_hit = local;
		bool queue = false;
		if (part.dent >= detach_depth) {
			part.pending = true;
			queue = true;
		} else if (hang_depth > 0.0f && part.dent >= hang_depth && !part.body.is_valid() && !part.hang_pending) {
			part.hang_pending = true; // milestone 109
			queue = true;
		}
		if (queue && !detach_queued) {
			detach_queued = true;
			callable_mp(this, &FellowVehicleDamage3D::_detach_pending).call_deferred();
		}
	}
}

// A part falling away: a rigid body of detach_mass where it is (a box of its bounds, the vehicle's layers, not colliding
// with the vehicle it came from), moving as the vehicle's body moved at that point; the part moved under it.
RigidBody3D *FellowVehicleDamage3D::_make_body(Part &r_part) {
	Node3D *n = ObjectDB::get_instance<Node3D>(r_part.node);
	const VisualInstance3D *vi = Object::cast_to<VisualInstance3D>(n);
	const AABB box = vi ? vi->get_aabb() : AABB(Vector3(-0.05, -0.05, -0.05), Vector3(0.1, 0.1, 0.1));
	const Transform3D xf = n->get_global_transform();
	const Vector3 scale = xf.basis.get_scale().abs();
	RigidBody3D *body = memnew(RigidBody3D);
	body->set_name(String(n->get_name()) + "Detached");
	body->set_mass(detach_mass);
	body->set_collision_layer(vehicle->get_collision_layer());
	body->set_collision_mask(vehicle->get_collision_mask());
	CollisionShape3D *cs = memnew(CollisionShape3D);
	Ref<BoxShape3D> shape;
	shape.instantiate();
	shape->set_size((box.size * scale).max(Vector3(0.02, 0.02, 0.02)));
	cs->set_shape(shape);
	cs->set_position(box.get_center() * scale);
	body->add_child(cs);
	vehicle->get_parent()->add_child(body);
	body->set_global_transform(Transform3D(xf.basis.orthonormalized(), xf.origin));
	body->add_collision_exception_with(vehicle);
	const Vector3 at = xf.xform(box.get_center());
	body->set_linear_velocity(vehicle->get_linear_velocity() + vehicle->get_angular_velocity().cross(at - vehicle->get_global_position()));
	body->set_angular_velocity(vehicle->get_angular_velocity());
	n->reparent(body, true);
	r_part.body = body->get_instance_id();
	return body;
}

// The parts' changes after the physics step: hanging (milestone 109: its body pinned to the vehicle at the end of its
// longest side farthest from its last hit) or off (the pin, if it hung, let go).
void FellowVehicleDamage3D::_detach_pending() {
	detach_queued = false;
	if (!vehicle || !vehicle->get_parent()) {
		return;
	}
	for (Part &part : parts) {
		if (!part.pending && !part.hang_pending) {
			continue;
		}
		const bool hang = part.hang_pending && !part.pending;
		part.pending = false;
		part.hang_pending = false;
		Node3D *n = ObjectDB::get_instance<Node3D>(part.node);
		if (!n || !n->is_inside_tree()) {
			continue;
		}
		RigidBody3D *body = ObjectDB::get_instance<RigidBody3D>(part.body);
		if (!body) {
			body = _make_body(part);
		}
		if (hang) {
			const VisualInstance3D *vi = Object::cast_to<VisualInstance3D>(n);
			const AABB box = vi ? vi->get_aabb() : AABB(Vector3(-0.05, -0.05, -0.05), Vector3(0.1, 0.1, 0.1));
			const int axis = box.get_longest_axis_index();
			Vector3 end_a = box.get_center();
			Vector3 end_b = end_a;
			end_a[axis] = box.position[axis];
			end_b[axis] = box.position[axis] + box.size[axis];
			const Vector3 pin = n->get_global_transform().xform(part.last_hit.distance_squared_to(end_a) > part.last_hit.distance_squared_to(end_b) ? end_a : end_b);
			// Milestone 110: a part with the metadata fellow_hinge_axis (a direction in its space: a door's vertical,
			// a bonnet's across) swings on a hinge along it through that point instead of every way about a pin.
			const Variant hinge_axis = n->has_meta(SNAME("fellow_hinge_axis")) ? n->get_meta(SNAME("fellow_hinge_axis")) : Variant();
			Joint3D *joint = nullptr;
			if (hinge_axis.get_type() == Variant::VECTOR3 && !Vector3(hinge_axis).is_zero_approx()) {
				const Vector3 axis = n->get_global_transform().basis.xform(Vector3(hinge_axis)).normalized();
				const Vector3 up = Math::abs(axis.y) < 0.9f ? Vector3(0, 1, 0) : Vector3(1, 0, 0);
				joint = memnew(HingeJoint3D); // it turns about its own Z
				joint->set_name(String(n->get_name()) + "Hinge");
				vehicle->get_parent()->add_child(joint);
				joint->set_global_transform(Transform3D(Basis::looking_at(-axis, up), pin));
			} else {
				joint = memnew(PinJoint3D);
				joint->set_name(String(n->get_name()) + "Pin");
				vehicle->get_parent()->add_child(joint);
				joint->set_global_position(pin);
			}
			joint->set_node_a(joint->get_path_to(vehicle));
			joint->set_node_b(joint->get_path_to(body));
			part.joint = joint->get_instance_id();
			emit_signal(SNAME("part_hanging"), n, body);
		} else {
			if (Joint3D *joint = ObjectDB::get_instance<Joint3D>(part.joint)) {
				joint->set_node_b(NodePath());
				joint->queue_free();
			}
			part.joint = ObjectID();
			detached++;
			emit_signal(SNAME("part_detached"), n, body);
		}
	}
}

bool FellowVehicleDamage3D::is_part_detached(const NodePath &p_part) const {
	for (const Part &part : parts) {
		if (part.path == p_part) {
			return part.body.is_valid() && !part.joint.is_valid();
		}
	}
	return false;
}

bool FellowVehicleDamage3D::is_part_hanging(const NodePath &p_part) const {
	for (const Part &part : parts) {
		if (part.path == p_part) {
			return part.joint.is_valid();
		}
	}
	return false;
}

// Milestone 110: a hurt engine smokes — dark smoke rising from engine_position, as much as the dent there is deep (of
// max_depth; from a tenth of it: a glancing hit's edge does not). Made when the node enters the tree with engine_power_loss set (its materials' shaders are shared by key,
// none compiled for each car), not drawn until it smokes.
void FellowVehicleDamage3D::_make_smoke() {
	if (smoke_fx || !engine_smoke || Engine::get_singleton()->is_editor_hint()) {
		return;
	}
	Ref<ParticleProcessMaterial> pm;
	pm.instantiate();
	pm->set_direction(Vector3(0, 1, 0));
	pm->set_spread(25.0f);
	pm->set_param_min(ParticleProcessMaterial::PARAM_INITIAL_LINEAR_VELOCITY, 0.6f);
	pm->set_param_max(ParticleProcessMaterial::PARAM_INITIAL_LINEAR_VELOCITY, 1.2f);
	pm->set_gravity(Vector3(0, 0.4f, 0));
	pm->set_param_min(ParticleProcessMaterial::PARAM_DAMPING, 0.5f);
	pm->set_param_max(ParticleProcessMaterial::PARAM_DAMPING, 1.0f);
	pm->set_param_min(ParticleProcessMaterial::PARAM_SCALE, 0.4f);
	pm->set_param_max(ParticleProcessMaterial::PARAM_SCALE, 0.7f);
	pm->set_param_min(ParticleProcessMaterial::PARAM_ANGLE, -180.0f);
	pm->set_param_max(ParticleProcessMaterial::PARAM_ANGLE, 180.0f);
	pm->set_emission_shape(ParticleProcessMaterial::EMISSION_SHAPE_BOX);
	pm->set_emission_box_extents(Vector3(0.4f, 0.05f, 0.3f));
	Ref<Curve> grow;
	grow.instantiate();
	grow->set_max_value(4.0f);
	grow->add_point(Vector2(0, 0.5f));
	grow->add_point(Vector2(1, 3.0f));
	Ref<CurveTexture> grow_tex;
	grow_tex.instantiate();
	grow_tex->set_curve(grow);
	pm->set_param_texture(ParticleProcessMaterial::PARAM_SCALE, grow_tex);
	Ref<Gradient> fade;
	fade.instantiate();
	fade->set_color(0, Color(0.22f, 0.22f, 0.23f, 0.7f));
	fade->set_color(1, Color(0.45f, 0.45f, 0.46f, 0.0f));
	Ref<GradientTexture1D> fade_tex;
	fade_tex.instantiate();
	fade_tex->set_gradient(fade);
	pm->set_color_ramp(fade_tex);
	Ref<StandardMaterial3D> dm;
	dm.instantiate();
	dm->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA);
	dm->set_shading_mode(BaseMaterial3D::SHADING_MODE_UNSHADED);
	dm->set_billboard_mode(BaseMaterial3D::BILLBOARD_PARTICLES);
	dm->set_flag(BaseMaterial3D::FLAG_ALBEDO_FROM_VERTEX_COLOR, true);
	dm->set_cull_mode(BaseMaterial3D::CULL_DISABLED);
	// A soft round puff (a quad's square edges show where the smoke trails past the camera).
	Ref<Gradient> puff;
	puff.instantiate();
	puff->set_color(0, Color(1, 1, 1, 1));
	puff->set_color(1, Color(1, 1, 1, 0));
	Ref<GradientTexture2D> puff_tex;
	puff_tex.instantiate();
	puff_tex->set_gradient(puff);
	puff_tex->set_width(64);
	puff_tex->set_height(64);
	puff_tex->set_fill(GradientTexture2D::FILL_RADIAL);
	puff_tex->set_fill_from(Vector2(0.5f, 0.5f));
	puff_tex->set_fill_to(Vector2(0.5f, 0.0f));
	dm->set_texture(BaseMaterial3D::TEXTURE_ALBEDO, puff_tex);
	Ref<QuadMesh> quad;
	quad.instantiate();
	quad->set_size(Vector2(0.9f, 0.9f));
	quad->set_material(dm);
	smoke_fx = memnew(GPUParticles3D);
	smoke_fx->set_name("EngineSmoke");
	smoke_fx->set_amount(48);
	smoke_fx->set_lifetime(2.5f);
	smoke_fx->set_process_material(pm);
	smoke_fx->set_draw_pass_mesh(0, quad);
	smoke_fx->set_use_local_coordinates(false);
	smoke_fx->set_emitting(false);
	smoke_fx->set_cast_shadows_setting(GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);
	smoke_fx->set_visibility_aabb(AABB(Vector3(-8, -2, -8), Vector3(16, 12, 16)));
	smoke_fx->set_as_top_level(true);
	add_child(smoke_fx, false, INTERNAL_MODE_FRONT);
}

void FellowVehicleDamage3D::_update_smoke() {
	if (!vehicle || max_depth <= 0.0f) {
		return;
	}
	const float share = engine_power_loss > 0.0f ? engine_dent / max_depth : 0.0f;
	const bool on = engine_smoke && share > 0.1f;
	if (on && !smoke_fx) {
		_make_smoke(); // engine_power_loss set after the node entered the tree
	}
	if (!smoke_fx) {
		return;
	}
	if (on) {
		smoke_fx->set_global_position(vehicle->get_global_transform().xform(engine_position + Vector3(0, 0.35f, 0)));
		smoke_fx->set_amount_ratio(MIN(share, 1.0f));
	}
	if (smoke_fx->is_emitting() != on) {
		smoke_fx->set_emitting(on);
	}
}

float FellowVehicleDamage3D::get_wheel_sag(const NodePath &p_wheel) const {
	const Node *n = get_node_or_null(p_wheel);
	for (const Bent &b : bent) {
		if (n && b.wheel == n->get_instance_id()) {
			return b.sag;
		}
	}
	return 0.0f;
}

bool FellowVehicleDamage3D::is_engine_smoking() const {
	return smoke_fx && smoke_fx->is_emitting();
}

float FellowVehicleDamage3D::get_engine_power() const {
	return max_depth > 0.0f ? 1.0f - engine_power_loss * engine_dent / max_depth : 1.0f;
}

float FellowVehicleDamage3D::get_wheel_toe_change(const NodePath &p_wheel) const {
	const Node *n = get_node_or_null(p_wheel);
	for (const Bent &b : bent) {
		if (n && b.wheel == n->get_instance_id()) {
			return b.bend;
		}
	}
	return 0.0f;
}

void FellowVehicleDamage3D::apply_impact(const Vector3 &p_position, const Vector3 &p_direction, float p_impulse) {
	const float excess = p_impulse - impulse_threshold;
	if (excess <= 0.0f || p_direction.is_zero_approx()) {
		return;
	}
	const float depth = MIN(softness * excess, max_depth);
	impacts++;
	_impact(p_position, p_direction.normalized(), depth);
	_running_gear(p_position, p_direction.normalized(), depth);
	_parts_hit(p_position, p_direction.normalized(), depth);
	emit_signal(SNAME("impacted"), p_position, p_impulse, depth);
}

void FellowVehicleDamage3D::repair() {
	for (Target &t : targets) {
		MeshInstance3D *mi = ObjectDB::get_instance<MeshInstance3D>(t.instance);
		if (mi && t.dented.is_valid() && mi->get_mesh() == t.dented) {
			mi->set_mesh(t.original);
		}
		t.dented.unref();
		t.surfaces.clear();
		t.offsets.clear();
	}
	deepest = 0.0f;
	// Milestone 107: the wheels' toe and the engine's torque back (unless the game has set them since).
	for (const Bent &b : bent) {
		FellowWheel3D *w = ObjectDB::get_instance<FellowWheel3D>(b.wheel);
		if (w && w->get_toe() == b.toe_set) {
			w->set_toe(b.toe_base);
		}
		if (w && b.length_base >= 0.0f && w->get_suspension_length() == b.length_set) {
			w->set_suspension_length(b.length_base); // milestone 110
		}
	}
	bent.clear();
	if (vehicle && torque_base >= 0.0f && vehicle->get_max_torque() == torque_set) {
		vehicle->set_max_torque(torque_base);
	}
	torque_base = -1.0f;
	torque_set = -1.0f;
	engine_dent = 0.0f;
	// Milestone 108: the parts back where they were, their bodies freed.
	for (Part &part : parts) {
		Node3D *n = ObjectDB::get_instance<Node3D>(part.node);
		RigidBody3D *body = ObjectDB::get_instance<RigidBody3D>(part.body);
		Node *parent = ObjectDB::get_instance<Node>(part.parent);
		if (n && body && parent && n->get_parent() == body) {
			n->reparent(parent, false);
			n->set_transform(part.local);
			parent->move_child(n, MIN(part.index, parent->get_child_count() - 1));
		}
		if (Joint3D *joint = ObjectDB::get_instance<Joint3D>(part.joint)) {
			joint->set_node_b(NodePath());
			joint->queue_free();
		}
		part.joint = ObjectID();
		part.hang_pending = false;
		if (body) {
			body->queue_free();
		}
		part.body = ObjectID();
		part.dent = 0.0f;
		part.pending = false;
	}
}

Vector3 FellowVehicleDamage3D::get_vertex_offset(const NodePath &p_mesh, int p_surface, int p_vertex) const {
	const Node *n = get_node_or_null(p_mesh);
	for (const Target &t : targets) {
		if (n && t.instance == n->get_instance_id() && p_surface >= 0 && p_surface < int(t.offsets.size()) && p_vertex >= 0 && p_vertex < int(t.offsets[p_surface].size())) {
			return t.offsets[p_surface][p_vertex];
		}
	}
	return Vector3();
}

Dictionary FellowVehicleDamage3D::get_stats() const {
	Dictionary d;
	d["impacts"] = impacts;
	d["deepest"] = deepest;
	d["damage"] = get_damage();
	d["deform_usec"] = deform_usec;
	d["meshes"] = targets.size();
	int dented = 0;
	for (const Target &t : targets) {
		dented += t.dented.is_valid() ? 1 : 0;
	}
	d["dented_meshes"] = dented;
	d["vertices"] = vertices;
	d["engine_power"] = get_engine_power();
	Dictionary toe;
	for (const Bent &b : bent) {
		if (const Node *w = ObjectDB::get_instance<Node>(b.wheel)) {
			toe[w->get_name()] = b.bend;
		}
	}
	d["wheel_toe_change"] = toe;
	d["detached"] = detached;
	d["engine_smoking"] = is_engine_smoking();
	return d;
}

PackedStringArray FellowVehicleDamage3D::get_configuration_warnings() const {
	PackedStringArray warnings = Node3D::get_configuration_warnings();
	if (!Object::cast_to<FellowVehicle3D>(get_parent())) {
		warnings.push_back(RTR("FellowVehicleDamage3D dents a vehicle's body: make it a child of a FellowVehicle3D."));
	}
	return warnings;
}

void FellowVehicleDamage3D::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_ENTER_TREE: {
			vehicle = Object::cast_to<FellowVehicle3D>(get_parent());
			targets_ready = false;
			if (vehicle && !Engine::get_singleton()->is_editor_hint()) {
				// The body's contacts, to read the impacts' impulses.
				vehicle->set_contact_monitor(true);
				vehicle->set_max_contacts_reported(MAX(vehicle->get_max_contacts_reported(), 8));
				set_physics_process_internal(true);
				if (engine_power_loss > 0.0f) {
					_make_smoke(); // milestone 110
				}
			}
		} break;
		case NOTIFICATION_EXIT_TREE: {
			set_physics_process_internal(false);
			vehicle = nullptr;
		} break;
		case NOTIFICATION_INTERNAL_PHYSICS_PROCESS: {
			if (!vehicle) {
				break;
			}
			_update_smoke(); // milestone 110
			PhysicsDirectBodyState3D *state = PhysicsServer3D::get_singleton()->body_get_direct_state(vehicle->get_rid());
			if (!state) {
				break;
			}
			const int count = state->get_contact_count();
			float total = 0.0f;
			for (int i = 0; i < count; i++) {
				total += state->get_contact_impulse(i).length();
			}
			if (total <= impulse_threshold) {
				break;
			}
			// The step's impulse beyond the threshold, shared among its contacts: each dents where it is, into the body
			// (along its normal, the side towards the body's centre of mass).
			const Vector3 com = state->get_transform().origin + state->get_center_of_mass();
			for (int i = 0; i < count; i++) {
				const float j = state->get_contact_impulse(i).length();
				if (j <= 0.0f) {
					continue;
				}
				const Vector3 p = state->get_contact_local_position(i);
				Vector3 n = state->get_contact_local_normal(i);
				const Vector3 to_com = com - p;
				if (n.is_zero_approx()) {
					n = to_com;
				} else if (n.dot(to_com) < 0.0f) {
					n = -n;
				}
				apply_impact(p, n, impulse_threshold + (total - impulse_threshold) * (j / total));
			}
		} break;
	}
}

void FellowVehicleDamage3D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_impulse_threshold", "impulse"), &FellowVehicleDamage3D::set_impulse_threshold);
	ClassDB::bind_method(D_METHOD("get_impulse_threshold"), &FellowVehicleDamage3D::get_impulse_threshold);
	ClassDB::bind_method(D_METHOD("set_softness", "softness"), &FellowVehicleDamage3D::set_softness);
	ClassDB::bind_method(D_METHOD("get_softness"), &FellowVehicleDamage3D::get_softness);
	ClassDB::bind_method(D_METHOD("set_max_depth", "depth"), &FellowVehicleDamage3D::set_max_depth);
	ClassDB::bind_method(D_METHOD("get_max_depth"), &FellowVehicleDamage3D::get_max_depth);
	ClassDB::bind_method(D_METHOD("set_radius", "radius"), &FellowVehicleDamage3D::set_radius);
	ClassDB::bind_method(D_METHOD("get_radius"), &FellowVehicleDamage3D::get_radius);
	ClassDB::bind_method(D_METHOD("set_mesh_paths", "paths"), &FellowVehicleDamage3D::set_mesh_paths);
	ClassDB::bind_method(D_METHOD("get_mesh_paths"), &FellowVehicleDamage3D::get_mesh_paths);
	ClassDB::bind_method(D_METHOD("apply_impact", "position", "direction", "impulse"), &FellowVehicleDamage3D::apply_impact);
	ClassDB::bind_method(D_METHOD("repair"), &FellowVehicleDamage3D::repair);
	ClassDB::bind_method(D_METHOD("get_damage"), &FellowVehicleDamage3D::get_damage);
	ClassDB::bind_method(D_METHOD("get_deepest_dent"), &FellowVehicleDamage3D::get_deepest_dent);
	ClassDB::bind_method(D_METHOD("set_wheel_bend", "degrees"), &FellowVehicleDamage3D::set_wheel_bend);
	ClassDB::bind_method(D_METHOD("get_wheel_bend"), &FellowVehicleDamage3D::get_wheel_bend);
	ClassDB::bind_method(D_METHOD("set_engine_power_loss", "share"), &FellowVehicleDamage3D::set_engine_power_loss);
	ClassDB::bind_method(D_METHOD("get_engine_power_loss"), &FellowVehicleDamage3D::get_engine_power_loss);
	ClassDB::bind_method(D_METHOD("set_engine_position", "position"), &FellowVehicleDamage3D::set_engine_position);
	ClassDB::bind_method(D_METHOD("get_engine_position"), &FellowVehicleDamage3D::get_engine_position);
	ClassDB::bind_method(D_METHOD("get_engine_power"), &FellowVehicleDamage3D::get_engine_power);
	ClassDB::bind_method(D_METHOD("get_wheel_toe_change", "wheel"), &FellowVehicleDamage3D::get_wheel_toe_change);
	ClassDB::bind_method(D_METHOD("set_detachable_paths", "paths"), &FellowVehicleDamage3D::set_detachable_paths);
	ClassDB::bind_method(D_METHOD("get_detachable_paths"), &FellowVehicleDamage3D::get_detachable_paths);
	ClassDB::bind_method(D_METHOD("set_detach_depth", "depth"), &FellowVehicleDamage3D::set_detach_depth);
	ClassDB::bind_method(D_METHOD("get_detach_depth"), &FellowVehicleDamage3D::get_detach_depth);
	ClassDB::bind_method(D_METHOD("set_detach_mass", "mass"), &FellowVehicleDamage3D::set_detach_mass);
	ClassDB::bind_method(D_METHOD("get_detach_mass"), &FellowVehicleDamage3D::get_detach_mass);
	ClassDB::bind_method(D_METHOD("is_part_detached", "part"), &FellowVehicleDamage3D::is_part_detached);
	ClassDB::bind_method(D_METHOD("set_hang_depth", "depth"), &FellowVehicleDamage3D::set_hang_depth);
	ClassDB::bind_method(D_METHOD("get_hang_depth"), &FellowVehicleDamage3D::get_hang_depth);
	ClassDB::bind_method(D_METHOD("is_part_hanging", "part"), &FellowVehicleDamage3D::is_part_hanging);
	ClassDB::bind_method(D_METHOD("set_suspension_damage", "metres"), &FellowVehicleDamage3D::set_suspension_damage);
	ClassDB::bind_method(D_METHOD("get_suspension_damage"), &FellowVehicleDamage3D::get_suspension_damage);
	ClassDB::bind_method(D_METHOD("get_wheel_sag", "wheel"), &FellowVehicleDamage3D::get_wheel_sag);
	ClassDB::bind_method(D_METHOD("set_engine_smoke", "enabled"), &FellowVehicleDamage3D::set_engine_smoke);
	ClassDB::bind_method(D_METHOD("has_engine_smoke"), &FellowVehicleDamage3D::has_engine_smoke);
	ClassDB::bind_method(D_METHOD("is_engine_smoking"), &FellowVehicleDamage3D::is_engine_smoking);
	ClassDB::bind_method(D_METHOD("get_vertex_offset", "mesh", "surface", "vertex"), &FellowVehicleDamage3D::get_vertex_offset);
	ClassDB::bind_method(D_METHOD("get_stats"), &FellowVehicleDamage3D::get_stats);

	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "impulse_threshold", PROPERTY_HINT_RANGE, "0,100000,10,or_greater,suffix:N·s"), "set_impulse_threshold", "get_impulse_threshold");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "softness", PROPERTY_HINT_RANGE, "0,0.001,0.000001,or_greater,suffix:m/(N·s)"), "set_softness", "get_softness");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "max_depth", PROPERTY_HINT_RANGE, "0,2,0.01,or_greater,suffix:m"), "set_max_depth", "get_max_depth");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "radius", PROPERTY_HINT_RANGE, "0.01,5,0.01,or_greater,suffix:m"), "set_radius", "get_radius");
	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "mesh_paths", PROPERTY_HINT_ARRAY_TYPE, vformat("%s/%s:%s", Variant::NODE_PATH, PROPERTY_HINT_NODE_PATH_VALID_TYPES, "MeshInstance3D")), "set_mesh_paths", "get_mesh_paths");

	ADD_GROUP("Running Gear", "");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "wheel_bend", PROPERTY_HINT_RANGE, "0,15,0.1,or_greater,suffix:°"), "set_wheel_bend", "get_wheel_bend");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "suspension_damage", PROPERTY_HINT_RANGE, "0,0.3,0.001,or_greater,suffix:m"), "set_suspension_damage", "get_suspension_damage");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "engine_power_loss", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_engine_power_loss", "get_engine_power_loss");
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR3, "engine_position", PROPERTY_HINT_NONE, "suffix:m"), "set_engine_position", "get_engine_position");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "engine_smoke"), "set_engine_smoke", "has_engine_smoke");

	ADD_GROUP("Detachable Parts", "");
	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "detachable_paths", PROPERTY_HINT_ARRAY_TYPE, vformat("%s/%s:%s", Variant::NODE_PATH, PROPERTY_HINT_NODE_PATH_VALID_TYPES, "Node3D")), "set_detachable_paths", "get_detachable_paths");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "detach_depth", PROPERTY_HINT_RANGE, "0,1,0.01,or_greater,suffix:m"), "set_detach_depth", "get_detach_depth");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "detach_mass", PROPERTY_HINT_RANGE, "0.1,200,0.1,or_greater,suffix:kg"), "set_detach_mass", "get_detach_mass");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "hang_depth", PROPERTY_HINT_RANGE, "0,1,0.01,or_greater,suffix:m"), "set_hang_depth", "get_hang_depth");

	ADD_SIGNAL(MethodInfo("part_hanging", PropertyInfo(Variant::OBJECT, "part", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_DEFAULT, "Node3D"), PropertyInfo(Variant::OBJECT, "body", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_DEFAULT, "RigidBody3D")));
	ADD_SIGNAL(MethodInfo("part_detached", PropertyInfo(Variant::OBJECT, "part", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_DEFAULT, "Node3D"), PropertyInfo(Variant::OBJECT, "body", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_DEFAULT, "RigidBody3D")));
	ADD_SIGNAL(MethodInfo("impacted", PropertyInfo(Variant::VECTOR3, "position"), PropertyInfo(Variant::FLOAT, "impulse"), PropertyInfo(Variant::FLOAT, "depth")));
}
