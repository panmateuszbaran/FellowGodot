/**************************************************************************/
/*  fellow_cloth_3d.h                                                     */
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
#include "core/templates/safe_refcount.h"
#include "scene/3d/visual_instance_3d.h"
#include "scene/resources/material.h"
#include "scene/resources/mesh.h"

// FellowEngine (milestone 73, decision 0181): cloth — flags, banners, curtains, awnings, sails, capes — simulated on
// the CPU by position-based dynamics in small fixed substeps (stretch and bending constraints, long-range tethers to
// the pinned particles), blown by the scene's FellowWind3D (drag and lift on each triangle, turbulence), colliding with
// spheres, capsules, boxes, cylinders, planes and height maps (shapes it is given, and the physics world's under it),
// every cloth of a frame on the worker threads at once. Drawn as a mesh whose vertices are updated each frame through a
// blend shape's per-instance buffers, so that its motion vectors (TAA, FSR) follow the cloth.
class Skeleton3D;

class FellowCloth3D : public GeometryInstance3D {
	GDCLASS(FellowCloth3D, GeometryInstance3D);

public:
	enum PinMode {
		PIN_NONE,
		PIN_TOP_EDGE, // a curtain, a banner
		PIN_LEFT_EDGE, // a flag on a pole
		PIN_TOP_CORNERS, // a sheet hung by two corners
		PIN_CORNERS, // an awning, a tent's fly
		PIN_VERTEX_COLOR, // the source mesh's vertices whose colour's red is 0.5 or more
	};

	// A collision shape in world space, for the solver.
	struct Collider {
		enum Type {
			SPHERE,
			CAPSULE,
			BOX,
			CYLINDER,
			PLANE,
			HEIGHTMAP,
		};
		Type type = SPHERE;
		Transform3D xf;
		Transform3D inv;
		Vector3 half;
		float radius = 0.0f;
		float height = 0.0f;
		Plane plane;
		Vector<float> heights;
		int map_width = 0;
		int map_depth = 0;
	};

	// Every cloth's work since the last reset.
	struct GlobalStats {
		SafeNumeric<uint64_t> frames;
		SafeNumeric<uint64_t> simulations;
		SafeNumeric<uint64_t> substeps;
		SafeNumeric<uint64_t> particles;
		SafeNumeric<uint64_t> simulate_usec; // on the worker threads, summed
		SafeNumeric<uint64_t> frame_usec; // the main thread's whole group: preparing, waiting, uploading
		SafeNumeric<uint64_t> skipped; // beyond simulation_distance
	};
	static GlobalStats global_stats;

private:
	static LocalVector<FellowCloth3D *> registry;
	static uint64_t group_frame;

	// The shape.
	Ref<Mesh> source_mesh;
	Vector2 size = Vector2(2.0f, 1.2f);
	int resolution = 24;
	PinMode pin_mode = PIN_TOP_EDGE;
	Ref<Material> material;

	// The fabric.
	bool simulating = true;
	float mass = 0.25f; // kg per square meter
	float stretch_stiffness = 1.0f;
	float bend_stiffness = 0.2f;
	float damping = 0.05f; // per second
	float tether_slack = 1.08f; // 0: no tethers
	float gravity_scale = 1.0f;
	int substep_rate = 480; // substeps a second
	int max_substeps = 12; // a frame
	int iterations = 2; // stretch passes a substep
	// The air.
	float wind_scale = 1.0f;
	Vector3 wind; // added to FellowWind3D's, global
	float drag = 1.0f;
	float lift = 0.3f;
	float turbulence = 0.3f;
	// Collisions.
	float thickness = 0.02f;
	float friction = 0.3f;
	// Milestone 112 (decision 0221): the cloth kept off itself.
	bool self_collision = false;
	float self_collision_distance = 0.0f; // 0: three quarters of the mean edge (a quarter with self_collision_triangles)
	bool self_collision_triangles = false; // decision 0228: the particles kept off the other layers' triangles too
	float self_collision_friction = 0.0f; // decision 0229: the layers' sliding on each other taken away, 0-1
	TypedArray<NodePath> colliders;
	bool collide_with_world = true;
	uint32_t collision_mask = 1;
	// Milestone 76: the pinned particles on a skeleton's bones (a cape on the shoulders, a skirt on the hips).
	NodePath skeleton_path;
	PackedStringArray pin_bones; // the bones they may follow (empty: any)
	// Level of detail.
	float simulation_distance = 100.0f;
	float lod_distance = 25.0f;

	// The simulation's mesh: particles (vertices welded by position), constraints, the drawn vertices' particles.
	struct Edge {
		uint32_t a;
		uint32_t b;
		float rest;
		float fa = 0.0f; // each end's share of the correction: its inverse mass over the pair's (set with the pins)
		float fb = 0.0f;
	};
	struct Tether {
		uint32_t p;
		uint32_t pin;
		float rest;
	};
	LocalVector<Vector3> rest_local; // a particle in the node's space at rest
	LocalVector<Vector3> x;
	LocalVector<Vector3> prev;
	LocalVector<Vector3> vel;
	LocalVector<Vector3> acc;
	LocalVector<Vector3> air; // the wind on each triangle this frame
	LocalVector<Vector3> normal; // world space, the last substep's
	LocalVector<float> w; // inverse mass, 0 pinned
	LocalVector<float> particle_mass;
	LocalVector<Edge> stretch; // the solver's: without the edges between two pinned particles
	LocalVector<Edge> bend;
	LocalVector<Edge> stretch_all;
	LocalVector<Edge> bend_all;
	LocalVector<Tether> tethers;
	LocalVector<uint32_t> tris;
	LocalVector<uint32_t> render_particle;
	LocalVector<Vector3> render_rest_normal; // node space
	LocalVector<Vector4> render_rest_tangent;
	LocalVector<Vector3> particle_rest_normal; // node space, from the triangles
	// Milestone 112 (decision 0220): tearing. The cloth's topology as built — every triangle (`tris` holds the live
	// ones), its three edges, each edge's triangles, each bending pair's two — and what is left of it.
	float tear_strain = 0.0f; // 0: off
	LocalVector<uint32_t> tris_all;
	LocalVector<uint32_t> tri_edges; // three a triangle: indices into stretch_all
	LocalVector<uint32_t> edge_tri_start; // an edge's triangles: edge_tri_list[edge_tri_start[e] .. edge_tri_start[e + 1]]
	LocalVector<uint32_t> edge_tri_list;
	LocalVector<uint32_t> bend_tris; // two a bending pair
	LocalVector<uint32_t> tri_index; // a triangle's first index in the surface's index array
	LocalVector<uint8_t> tri_alive;
	LocalVector<uint8_t> edge_alive;
	LocalVector<uint8_t> loose; // particles of no live triangle: left where they are, not drawn
	LocalVector<uint32_t> stretch_source; // the solver's stretch edges' places in stretch_all
	LocalVector<Tether> tethers_rest; // as built (reset())
	Vector<uint8_t> index_bytes; // the surface's indices, a torn triangle's made degenerate
	Vector<uint8_t> index_bytes_rest;
	uint32_t index_size = 2;
	bool index_dirty = false;
	bool any_loose = false;
	int torn_edges = 0; // edges gone since the build or reset()
	int torn_pending = 0; // broken on the worker this frame: the signal on the main thread
	Vector3 torn_pending_sum;
	// Self-collision (decision 0221): the frame's pairs of particles near each other (not neighbours at rest), kept
	// apart in each substep; a spatial hash of the particles' cells.
	struct SelfPair {
		uint32_t a;
		uint32_t b;
		float fa;
		float fb;
	};
	LocalVector<SelfPair> self_pairs;
	LocalVector<uint32_t> self_head;
	LocalVector<uint32_t> self_next;
	LocalVector<Vector3i> self_cell;
	float self_distance_now = 0.0f;
	float mean_edge = 0.0f;
	int self_contacts = 0; // pairs pushed apart in the last frame's last substep
	// Decision 0228: the frame's particles near a triangle of another layer (not near it at rest), and the side of it
	// they were on; the triangles hashed into the cells their bounds (and the frame's reach) cover.
	struct SelfTriPair {
		uint32_t p;
		uint32_t t; // the triangle's first index in tris
		float side; // +1 or -1: the side of the triangle's normal the particle was on when found
	};
	struct SelfTriEntry {
		Vector3i cell;
		uint32_t tri;
		uint32_t next;
	};
	LocalVector<SelfTriPair> self_tri_pairs;
	LocalVector<uint32_t> self_tri_head;
	LocalVector<SelfTriEntry> self_tri_entries;
	LocalVector<Vector3> self_tri_bounds; // two a triangle: its bounds grown by the reach
	int self_tri_contacts = 0;
	// Bone pins (milestone 76): each pinned particle's bone (-1: the node) and its place in the bone's space; where it
	// was at the last frame and is at this one (global, main thread), interpolated over the substeps.
	LocalVector<int> pin_bone;
	LocalVector<Vector3> pin_offset;
	LocalVector<Vector3> pin_from;
	LocalVector<Vector3> pin_to;
	int bone_pins = 0;
	ObjectID bound_skeleton;
	float normal_sign = 1.0f;
	bool has_tangents = false;
	bool built = false;
	bool build_dirty = true;
	String build_problem;

	RID mesh;
	Ref<StandardMaterial3D> default_material; // two-sided, when no material is set
	uint64_t surface_format = 0;
	uint32_t vertex_stride = 0;
	uint32_t normal_stride = 0;
	uint32_t normal_offset = 0;
	Vector<uint8_t> vertex_bytes;

	// The frame's inputs (main thread) and the step's state.
	Transform3D xf_prev;
	Transform3D xf_cur;
	bool have_prev = false;
	bool carry_only = false;
	bool lod_now = false;
	int rate_now = 480;
	Vector3 wind_now;
	Vector3 gravity_now;
	LocalVector<Collider> colliders_now;
	double step_acc = 0.0;
	int steps_now = 0;
	double time = 0.0;
	AABB bounds; // world space
	AABB local_bounds;
	uint64_t last_usec = 0;
	RID query_shape;

	void _build();
	void _clear();
	void _make_grid(PackedVector3Array &r_vertices, PackedVector3Array &r_normals, PackedVector2Array &r_uvs, PackedInt32Array &r_indices, PackedColorArray &r_colors) const;
	void _init_particles();
	void _filter_constraints();
	bool _prepare(double p_delta);
	void _gather_colliders();
	void _add_collider(RID p_shape, const Transform3D &p_xf);
	void _simulate();
	void _substep(float p_h, const Transform3D &p_pin_xf, float p_f);
	Skeleton3D *_get_skeleton() const;
	void _bind_bones();
	void _update_bone_pins();
	void _collide(uint32_t p_i);
	void _write_vertices();
	void _finish();
	void _group_task(uint32_t p_index, const LocalVector<FellowCloth3D *> *p_list);
	static void _group_process(double p_delta);
	void _set_build_dirty();
	void _find_self_pairs();
	void _find_self_tri_pairs(float p_d);
	int _tear(const LocalVector<uint32_t> &p_edges, Vector3 *r_sum);
	void _after_tear();
	void _compute_tethers();
	void _upload_indices();

protected:
	void _notification(int p_what);
	static void _bind_methods();

public:
	PackedStringArray get_configuration_warnings() const override;
	AABB get_aabb() const override;

	void reset();
	void apply_impulse(const Vector3 &p_position, const Vector3 &p_impulse, float p_radius);
	int tear(const Vector3 &p_position, float p_radius);
	int get_torn_edge_count() const { return torn_edges; }
	void set_tear_strain(float p_strain) { tear_strain = MAX(0.0f, p_strain); }
	float get_tear_strain() const { return tear_strain; }
	int get_particle_count() const { return int(x.size()); }
	Vector3 get_particle_position(int p_index) const;
	void set_particle_pinned(int p_index, bool p_pinned);
	bool is_particle_pinned(int p_index) const;
	Dictionary get_stats() const;
	static Dictionary get_global_stats();
	static void reset_global_stats();

	void set_source_mesh(const Ref<Mesh> &p_mesh);
	Ref<Mesh> get_source_mesh() const { return source_mesh; }
	void set_size(const Vector2 &p_size);
	Vector2 get_size() const { return size; }
	void set_resolution(int p_resolution);
	int get_resolution() const { return resolution; }
	void set_skeleton_path(const NodePath &p_path);
	NodePath get_skeleton_path() const { return skeleton_path; }
	void set_pin_bones(const PackedStringArray &p_bones);
	PackedStringArray get_pin_bones() const { return pin_bones; }
	void set_pin_mode(PinMode p_mode);
	PinMode get_pin_mode() const { return pin_mode; }
	void set_material(const Ref<Material> &p_material);
	Ref<Material> get_material() const { return material; }
	void set_simulating(bool p_enabled) { simulating = p_enabled; }
	bool is_simulating() const { return simulating; }
	void set_mass(float p_mass);
	float get_mass() const { return mass; }
	void set_stretch_stiffness(float p_stiffness) { stretch_stiffness = CLAMP(p_stiffness, 0.0f, 1.0f); }
	float get_stretch_stiffness() const { return stretch_stiffness; }
	void set_bend_stiffness(float p_stiffness) { bend_stiffness = CLAMP(p_stiffness, 0.0f, 1.0f); }
	float get_bend_stiffness() const { return bend_stiffness; }
	void set_damping(float p_damping) { damping = MAX(0.0f, p_damping); }
	float get_damping() const { return damping; }
	void set_tether_slack(float p_slack);
	float get_tether_slack() const { return tether_slack; }
	void set_gravity_scale(float p_scale) { gravity_scale = p_scale; }
	float get_gravity_scale() const { return gravity_scale; }
	void set_substep_rate(int p_rate) { substep_rate = CLAMP(p_rate, 60, 4000); }
	int get_substep_rate() const { return substep_rate; }
	void set_max_substeps(int p_steps) { max_substeps = CLAMP(p_steps, 1, 64); }
	void set_iterations(int p_iterations) { iterations = CLAMP(p_iterations, 1, 8); }
	int get_iterations() const { return iterations; }
	int get_max_substeps() const { return max_substeps; }
	void set_wind_scale(float p_scale) { wind_scale = p_scale; }
	float get_wind_scale() const { return wind_scale; }
	void set_wind(const Vector3 &p_wind) { wind = p_wind; }
	Vector3 get_wind() const { return wind; }
	void set_drag(float p_drag) { drag = MAX(0.0f, p_drag); }
	float get_drag() const { return drag; }
	void set_lift(float p_lift) { lift = MAX(0.0f, p_lift); }
	float get_lift() const { return lift; }
	void set_turbulence(float p_turbulence) { turbulence = MAX(0.0f, p_turbulence); }
	float get_turbulence() const { return turbulence; }
	void set_thickness(float p_thickness) { thickness = MAX(0.0f, p_thickness); }
	float get_thickness() const { return thickness; }
	void set_friction(float p_friction) { friction = CLAMP(p_friction, 0.0f, 1.0f); }
	void set_self_collision(bool p_enabled) { self_collision = p_enabled; }
	bool is_self_collision() const { return self_collision; }
	void set_self_collision_distance(float p_distance) { self_collision_distance = MAX(0.0f, p_distance); }
	float get_self_collision_distance() const { return self_collision_distance; }
	void set_self_collision_triangles(bool p_enabled) { self_collision_triangles = p_enabled; }
	bool is_self_collision_triangles() const { return self_collision_triangles; }
	void set_self_collision_friction(float p_friction) { self_collision_friction = CLAMP(p_friction, 0.0f, 1.0f); }
	float get_self_collision_friction() const { return self_collision_friction; }
	float get_friction() const { return friction; }
	void set_colliders(const TypedArray<NodePath> &p_colliders);
	TypedArray<NodePath> get_colliders() const { return colliders; }
	void set_collide_with_world(bool p_enabled) { collide_with_world = p_enabled; }
	bool is_collide_with_world() const { return collide_with_world; }
	void set_collision_mask(uint32_t p_mask) { collision_mask = p_mask; }
	uint32_t get_collision_mask() const { return collision_mask; }
	void set_simulation_distance(float p_distance) { simulation_distance = MAX(0.0f, p_distance); }
	void set_lod_distance(float p_distance) { lod_distance = MAX(0.0f, p_distance); }
	float get_lod_distance() const { return lod_distance; }
	float get_simulation_distance() const { return simulation_distance; }

	FellowCloth3D();
	~FellowCloth3D();
};

VARIANT_ENUM_CAST(FellowCloth3D::PinMode);
