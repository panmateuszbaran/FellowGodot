/**************************************************************************/
/*  fellow_destructible_3d.h                                              */
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

#include "fellow_fracture.h"

#include "core/object/worker_thread_pool.h"
#include "core/templates/hash_set.h"
#include "core/templates/safe_refcount.h"
#include "scene/3d/node_3d.h"
#include "scene/resources/3d/convex_polygon_shape_3d.h"
#include "scene/resources/material.h"
#include "scene/resources/mesh.h"

class CollisionObject3D;
class MeshInstance3D;

// A fracture's pieces, ready to spawn: their meshes and collision shapes, shared by every destructible broken with the
// same pattern (the same mesh, seed and count, prefractured).
class CollisionShape3D;
class MeshInstance3D;
class RigidBody3D;

class FellowFractureResult : public RefCounted {
	GDCLASS(FellowFractureResult, RefCounted);

public:
	LocalVector<FellowFracture::Piece> pieces;
	Vector<Vector<Array>> surface_arrays; // prepared, until built
	Vector<Ref<ArrayMesh>> meshes;
	Vector<Vector<int>> mesh_surfaces; // each mesh surface's source surface (the interior: the source's count)
	Vector<Vector<Ref<ConvexPolygonShape3D>>> shapes; // each piece's convex parts
	int source_surfaces = 0;
	bool prepared = false;
	bool built = false;
	// Milestone 80 (decision 0188): the pieces that touch (share three or more of their surface's points: a cell's
	// face), found when prepared.
	Vector<Vector<int>> adjacency;

	void prepare(); // the surfaces' arrays (any thread)
	void build(); // the meshes and shapes (main thread)
};

// FellowEngine (milestone 74, decision 0182): an object that breaks — a crate, a pillar, a wall, a pane of glass. A
// child of its body (a StaticBody3D, RigidBody3D or AnimatableBody3D), its mesh the body's MeshInstance3D: when hit
// hard enough (the body's contacts, or a rigid body striking it), damaged to zero health, or told to, the mesh is
// broken along a Voronoi diagram — more pieces near the impact — each piece a RigidBody3D with the cut faces in an
// interior material and a convex collision shape, thrown by the impact; the body is removed. The pieces are debris:
// asleep past their lifetime they shrink away, and a budget of debris for the whole game removes the oldest first.
class FellowDestructible3D : public Node3D {
	GDCLASS(FellowDestructible3D, Node3D);

public:
	struct GlobalStats {
		SafeNumeric<uint64_t> breaks;
		SafeNumeric<uint64_t> fractures; // computed (a shared pattern counts once)
		SafeNumeric<uint64_t> fracture_usec; // worker threads (or the main thread when prefractured at ready)
		SafeNumeric<uint64_t> build_usec; // the pieces' meshes and shapes (main thread)
		SafeNumeric<uint64_t> spawn_usec; // the pieces' nodes (main thread)
		SafeNumeric<uint64_t> pieces;
		SafeNumeric<uint64_t> faded;
	};
	static GlobalStats global_stats;

private:
	struct Debris {
		ObjectID body;
		double spawned = 0.0; // the debris clock
		float lifetime = 10.0f;
		bool fading = false;
		float fade = 0.0f;
		Vector3 scale = Vector3(1, 1, 1);
	};
	static LocalVector<Debris> debris;
	static int max_debris;
	static double debris_clock; // s of game time
	static bool debris_connected;
	static HashMap<String, Ref<FellowFractureResult>> cache;
	static int max_pieces_per_frame;
	static uint64_t budget_frame;
	static int budget_used;

	NodePath mesh_path;
	NodePath body_path;
	int piece_count = 12;
	int seed = 0;
	float impact_focus = 0.5f; // the share of the pieces' sites around the impact
	Ref<Material> interior_material;
	float interior_uv_scale = 1.0f;
	float health = 100.0f;
	bool break_on_impact = true;
	float break_impulse = 40.0f; // N·s
	float density = 600.0f; // kg/m³
	float burst_speed = 1.5f; // m/s, the pieces thrown out from the impact
	float debris_lifetime = 10.0f; // s (0: forever)
	bool prefracture = false;
	bool free_after_break = true;
	// Milestone 80 (decision 0188): a static object broken where it is struck; the rest stands while it is supported.
	bool structural = false;
	float break_radius = 0.6f; // m: the pieces within it of a blow break off
	float anchor_height = 0.1f; // m: the pieces reaching this near the object's lowest point stand on the ground

	struct StructPiece {
		MeshInstance3D *mesh = nullptr;
		LocalVector<CollisionShape3D *> shapes;
		bool alive = false;
		bool anchored = false;
	};
	LocalVector<StructPiece> struct_pieces;
	Ref<FellowFractureResult> struct_result;
	bool structure_built = false;
	Transform3D struct_mxf; // the mesh's global transform when the structure was made
	HashSet<RID> own_bodies; // the pieces and chunks it made: they do not strike it
	int last_broken = 0;
	int last_fallen = 0;
	int last_chunks = 0;
	struct Exception {
		ObjectID body; // a piece or chunk let through the standing object's shapes until (the debris clock)
		double until = 0.0;
	};
	LocalVector<Exception> exceptions;
	void _expire_exceptions();
	ObjectID last_striker; // the body that struck last, and when (the debris clock): it strikes again only later
	double last_strike_time = -1.0;

	bool broken = false;
	bool pending = false;
	Vector3 pending_position;
	Vector3 pending_impulse;
	WorkerThreadPool::TaskID task = WorkerThreadPool::INVALID_TASK_ID;
	Ref<FellowFractureResult> result;
	Ref<FellowFractureResult> prefractured;
	Ref<FellowFractureResult> spawn_result; // ready, waiting for the frame's budget
	// The task's inputs and output (owned here; the task is waited for before the node goes).
	LocalVector<FellowFracture::Triangle> source;
	FellowFracture::Params params;
	LocalVector<Vector3> sites;
	Vector3 task_impact; // mesh space
	int task_count = 12;
	uint64_t task_seed = 0;
	float task_focus = 0.5f;
	uint64_t task_usec = 0;
	float current_health = 100.0f;
	uint64_t last_spawn_usec = 0;
	int last_pieces = 0;
	Vector3 last_velocity; // a rigid body's, the last physics frame
	bool has_last_velocity = false;
	float last_impact = 0.0f; // N·s, the last blow the body felt

	MeshInstance3D *_get_mesh() const;
	CollisionObject3D *_get_body() const;
	bool _prepare_source();
	void _fracture_task(int p_unused);
	void _spawn(const Ref<FellowFractureResult> &p_result, const Vector3 &p_position, const Vector3 &p_impulse);
	void _build_structure(const Ref<FellowFractureResult> &p_result);
	void _strike(const Vector3 &p_position, const Vector3 &p_impulse);
	RigidBody3D *_make_body(const LocalVector<int> &p_pieces, const Transform3D &p_mxf);
	void _check_impacts();
	static bool _spawn_allowed(int p_pieces);
	String _cache_key() const;
	void _prefracture();
	static void _debris_tick();
	static void _add_debris(Node3D *p_body, float p_lifetime);

protected:
	void _notification(int p_what);
	static void _bind_methods();

public:
	PackedStringArray get_configuration_warnings() const override;

	void break_apart(const Vector3 &p_position = Vector3(), const Vector3 &p_impulse = Vector3());
	void damage(float p_amount, const Vector3 &p_position = Vector3(), const Vector3 &p_impulse = Vector3());
	bool is_broken() const { return broken || pending; }
	float get_current_health() const { return current_health; }
	Dictionary get_stats() const;
	static Dictionary get_global_stats();
	static void reset_global_stats();
	static void set_max_debris(int p_count) { max_debris = MAX(0, p_count); }
	static int get_max_debris() { return max_debris; }
	static int get_debris_count();
	static void clear_cache() { cache.clear(); }
	static void set_max_pieces_per_frame(int p_count) { max_pieces_per_frame = MAX(0, p_count); }
	static int get_max_pieces_per_frame() { return max_pieces_per_frame; }

	void set_mesh_path(const NodePath &p_path);
	NodePath get_mesh_path() const { return mesh_path; }
	void set_body_path(const NodePath &p_path);
	NodePath get_body_path() const { return body_path; }
	void set_piece_count(int p_count) { piece_count = CLAMP(p_count, 2, 256); }
	int get_piece_count() const { return piece_count; }
	void set_seed(int p_seed) { seed = p_seed; }
	int get_seed() const { return seed; }
	void set_impact_focus(float p_focus) { impact_focus = CLAMP(p_focus, 0.0f, 1.0f); }
	float get_impact_focus() const { return impact_focus; }
	void set_interior_material(const Ref<Material> &p_material) { interior_material = p_material; }
	Ref<Material> get_interior_material() const { return interior_material; }
	void set_interior_uv_scale(float p_scale) { interior_uv_scale = p_scale; }
	float get_interior_uv_scale() const { return interior_uv_scale; }
	void set_health(float p_health);
	float get_health() const { return health; }
	void set_break_on_impact(bool p_enabled) { break_on_impact = p_enabled; }
	bool is_break_on_impact() const { return break_on_impact; }
	void set_break_impulse(float p_impulse) { break_impulse = MAX(0.0f, p_impulse); }
	float get_break_impulse() const { return break_impulse; }
	void set_density(float p_density) { density = MAX(1.0f, p_density); }
	float get_density() const { return density; }
	void set_burst_speed(float p_speed) { burst_speed = MAX(0.0f, p_speed); }
	float get_burst_speed() const { return burst_speed; }
	void set_debris_lifetime(float p_seconds) { debris_lifetime = MAX(0.0f, p_seconds); }
	float get_debris_lifetime() const { return debris_lifetime; }
	void set_prefracture(bool p_enabled);
	bool is_prefracture() const { return prefracture; }
	void set_free_after_break(bool p_enabled) { free_after_break = p_enabled; }
	bool is_free_after_break() const { return free_after_break; }
	void set_structural(bool p_enabled) { structural = p_enabled; }
	bool is_structural() const { return structural; }
	void set_break_radius(float p_radius) { break_radius = MAX(0.0f, p_radius); }
	float get_break_radius() const { return break_radius; }
	void set_anchor_height(float p_height) { anchor_height = MAX(0.0f, p_height); }
	float get_anchor_height() const { return anchor_height; }
	int get_standing_pieces() const;
	Array get_structure() const;

	FellowDestructible3D();
	~FellowDestructible3D();
};
