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

#include "core/object/class_db.h"
#include "core/os/os.h"
#include "scene/3d/mesh_instance_3d.h"
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

void FellowVehicleDamage3D::apply_impact(const Vector3 &p_position, const Vector3 &p_direction, float p_impulse) {
	const float excess = p_impulse - impulse_threshold;
	if (excess <= 0.0f || p_direction.is_zero_approx()) {
		return;
	}
	const float depth = MIN(softness * excess, max_depth);
	impacts++;
	_impact(p_position, p_direction.normalized(), depth);
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
	ClassDB::bind_method(D_METHOD("get_vertex_offset", "mesh", "surface", "vertex"), &FellowVehicleDamage3D::get_vertex_offset);
	ClassDB::bind_method(D_METHOD("get_stats"), &FellowVehicleDamage3D::get_stats);

	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "impulse_threshold", PROPERTY_HINT_RANGE, "0,100000,10,or_greater,suffix:N·s"), "set_impulse_threshold", "get_impulse_threshold");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "softness", PROPERTY_HINT_RANGE, "0,0.001,0.000001,or_greater,suffix:m/(N·s)"), "set_softness", "get_softness");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "max_depth", PROPERTY_HINT_RANGE, "0,2,0.01,or_greater,suffix:m"), "set_max_depth", "get_max_depth");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "radius", PROPERTY_HINT_RANGE, "0.01,5,0.01,or_greater,suffix:m"), "set_radius", "get_radius");
	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "mesh_paths", PROPERTY_HINT_ARRAY_TYPE, vformat("%s/%s:%s", Variant::NODE_PATH, PROPERTY_HINT_NODE_PATH_VALID_TYPES, "MeshInstance3D")), "set_mesh_paths", "get_mesh_paths");

	ADD_SIGNAL(MethodInfo("impacted", PropertyInfo(Variant::VECTOR3, "position"), PropertyInfo(Variant::FLOAT, "impulse"), PropertyInfo(Variant::FLOAT, "depth")));
}
