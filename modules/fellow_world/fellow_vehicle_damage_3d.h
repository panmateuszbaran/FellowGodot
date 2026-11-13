/**************************************************************************/
/*  fellow_vehicle_damage_3d.h                                            */
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
#include "scene/3d/node_3d.h"
#include "scene/resources/mesh.h"

class FellowVehicle3D;
class MeshInstance3D;

// FellowEngine (milestone 105, decision 0213): a vehicle's body dented where it hits. A child of a FellowVehicle3D; it
// reads the body's contacts each physics step, and an impact whose impulse exceeds impulse_threshold pushes the
// vertices of the vehicle's meshes (its wheels' left out) near the contact into the body — deeper the harder the
// impact, falling off over radius, each vertex at most max_depth from where it was. The dented meshes are copies made at
// the first impact; repair() puts the originals back.
class FellowVehicleDamage3D : public Node3D {
	GDCLASS(FellowVehicleDamage3D, Node3D);

	float impulse_threshold = 2500.0f; // N·s in a physics step
	float softness = 2.0e-5f; // m of dent per N·s beyond the threshold
	float max_depth = 0.3f; // m
	float radius = 0.7f; // m
	TypedArray<NodePath> mesh_paths; // empty: every MeshInstance3D of the vehicle but its wheels'

	struct Target {
		ObjectID instance;
		Ref<Mesh> original;
		Ref<ArrayMesh> dented;
		LocalVector<Array> surfaces; // the original's arrays
		LocalVector<Ref<Material>> materials;
		LocalVector<Mesh::PrimitiveType> primitives;
		LocalVector<LocalVector<Vector3>> offsets; // per surface, per vertex: the dent (the mesh's space)
	};
	LocalVector<Target> targets;
	bool targets_ready = false;
	FellowVehicle3D *vehicle = nullptr;
	float deepest = 0.0f; // m: the largest dent of any vertex
	uint64_t impacts = 0;
	uint64_t deform_usec = 0;
	uint64_t vertices = 0;

	void _gather_targets();
	bool _prepare(Target &r_target);
	void _impact(const Vector3 &p_position, const Vector3 &p_direction, float p_depth);
	void _rebuild(Target &r_target);

protected:
	void _notification(int p_what);
	static void _bind_methods();

public:
	void set_impulse_threshold(float p_impulse) { impulse_threshold = MAX(0.0f, p_impulse); }
	float get_impulse_threshold() const { return impulse_threshold; }
	void set_softness(float p_softness) { softness = MAX(0.0f, p_softness); }
	float get_softness() const { return softness; }
	void set_max_depth(float p_depth) { max_depth = MAX(0.0f, p_depth); }
	float get_max_depth() const { return max_depth; }
	void set_radius(float p_radius) { radius = MAX(0.01f, p_radius); }
	float get_radius() const { return radius; }
	void set_mesh_paths(const TypedArray<NodePath> &p_paths);
	TypedArray<NodePath> get_mesh_paths() const { return mesh_paths; }

	void apply_impact(const Vector3 &p_position, const Vector3 &p_direction, float p_impulse);
	void repair();
	float get_damage() const { return max_depth > 0.0f ? MIN(deepest / max_depth, 1.0f) : 0.0f; }
	float get_deepest_dent() const { return deepest; }
	Vector3 get_vertex_offset(const NodePath &p_mesh, int p_surface, int p_vertex) const;
	Dictionary get_stats() const;

	PackedStringArray get_configuration_warnings() const override;
};
