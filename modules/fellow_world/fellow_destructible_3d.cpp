/**************************************************************************/
/*  fellow_destructible_3d.cpp                                            */
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

#include "fellow_destructible_3d.h"

#include "core/config/engine.h"
#include "core/config/project_settings.h"
#include "core/math/random_pcg.h"
#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "core/os/os.h"
#include "scene/3d/mesh_instance_3d.h"
#include "scene/3d/physics/collision_shape_3d.h"
#include "scene/3d/physics/rigid_body_3d.h"
#include "scene/main/scene_tree.h"
#include "scene/main/window.h"
#include "scene/resources/3d/world_3d.h"
#include "scene/resources/surface_tool.h"
#include "scene/scene_string_names.h"
#include "servers/physics_3d/physics_server_3d.h"

#include "modules/fellow_fluid/fellow_buoyancy_3d.h"
#include "modules/fellow_fluid/fellow_water_3d.h"

FellowDestructible3D::GlobalStats FellowDestructible3D::global_stats;
LocalVector<FellowDestructible3D::Debris> FellowDestructible3D::debris;
int FellowDestructible3D::max_debris = 256;
double FellowDestructible3D::debris_clock = 0.0;
bool FellowDestructible3D::debris_connected = false;
HashMap<String, Ref<FellowFractureResult>> FellowDestructible3D::cache;
int FellowDestructible3D::max_pieces_per_frame = 128;
uint64_t FellowDestructible3D::budget_frame = UINT64_MAX;
int FellowDestructible3D::budget_used = 0;

// The pieces' surfaces as arrays (any thread): vertices around each piece's centroid, tangents, indexed.
void FellowFractureResult::prepare() {
	if (prepared) {
		return;
	}
	// Milestone 80: which pieces touch — the points of their surfaces that coincide (within a thousandth of the
	// whole's size), three or more a pair (a shared face's corners; the cut points are computed alike for both cells).
	{
		AABB all;
		for (uint32_t k = 0; k < pieces.size(); k++) {
			all = k == 0 ? pieces[k].aabb : all.merge(pieces[k].aabb);
		}
		const float tol = MAX(1e-5f, all.get_longest_axis_size() * 1e-3f);
		const float cell = tol * 2.0f;
		struct Entry {
			int piece;
			Vector3 p;
		};
		HashMap<Vector3i, LocalVector<Entry>> grid;
		auto key = [&](const Vector3 &p) {
			return Vector3i(int(Math::floor(p.x / cell)), int(Math::floor(p.y / cell)), int(Math::floor(p.z / cell)));
		};
		LocalVector<LocalVector<Vector3>> points;
		points.resize(pieces.size());
		for (uint32_t k = 0; k < pieces.size(); k++) {
			HashSet<Vector3i> seen;
			for (const FellowFracture::Triangle &t : pieces[k].tris) {
				for (int m = 0; m < 3; m++) {
					const Vector3 &p = t.v[m].p;
					const Vector3i fine(int(Math::floor(p.x / tol)), int(Math::floor(p.y / tol)), int(Math::floor(p.z / tol)));
					if (seen.has(fine)) {
						continue;
					}
					seen.insert(fine);
					points[k].push_back(p);
					grid[key(p)].push_back(Entry{ int(k), p });
				}
			}
		}
		const uint32_t n = pieces.size();
		LocalVector<int> shared;
		shared.resize(n * n);
		memset(shared.ptr(), 0, sizeof(int) * n * n);
		LocalVector<int> touched;
		touched.resize(n);
		HashMap<uint64_t, Vector<Vector3>> shared_points; // milestone 122: a's points touching b (a < b)
		for (uint32_t a = 0; a < n; a++) {
			for (const Vector3 &p : points[a]) {
				for (uint32_t b = 0; b < n; b++) {
					touched[b] = 0;
				}
				const Vector3i c = key(p);
				for (int dx = -1; dx <= 1; dx++) {
					for (int dy = -1; dy <= 1; dy++) {
						for (int dz = -1; dz <= 1; dz++) {
							const LocalVector<Entry> *e = grid.getptr(c + Vector3i(dx, dy, dz));
							if (!e) {
								continue;
							}
							for (const Entry &q : *e) {
								if (q.piece != int(a) && !touched[q.piece] && q.p.distance_squared_to(p) <= tol * tol) {
									touched[q.piece] = 1;
									shared[a * n + q.piece]++;
									if (uint32_t(q.piece) > a) {
										shared_points[uint64_t(a) * n + q.piece].push_back(p);
									}
								}
							}
						}
					}
				}
			}
		}
		adjacency.resize(n);
		for (uint32_t a = 0; a < n; a++) {
			Vector<int> list;
			for (uint32_t b = 0; b < n; b++) {
				if (b != a && MIN(shared[a * n + b], shared[b * n + a]) >= 3) {
					list.push_back(int(b));
				}
			}
			adjacency.write[a] = list;
		}
		contacts.clear();
		for (uint32_t a = 0; a < n; a++) {
			for (int b : adjacency[a]) {
				if (uint32_t(b) <= a) {
					continue;
				}
				Contact contact;
				contact.a = int(a);
				contact.b = b;
				const Vector<Vector3> *face = shared_points.getptr(uint64_t(a) * n + b);
				if (face) {
					contact.points = *face;
				}
				contacts.push_back(contact);
			}
		}
	}
	surface_arrays.resize(pieces.size());
	mesh_surfaces.resize(pieces.size());
	for (uint32_t k = 0; k < pieces.size(); k++) {
		FellowFracture::Piece &piece = pieces[k];
		Vector<Array> arrays;
		Vector<int> surfaces;
		// One surface per source surface present in the piece, the interior last (as the surfaces' order).
		for (int s = 0; s <= source_surfaces; s++) {
			bool any = false;
			for (const FellowFracture::Triangle &t : piece.tris) {
				if (t.surface == s) {
					any = true;
					break;
				}
			}
			if (!any) {
				continue;
			}
			Ref<SurfaceTool> st;
			st.instantiate();
			st->begin(Mesh::PRIMITIVE_TRIANGLES);
			for (const FellowFracture::Triangle &t : piece.tris) {
				if (t.surface != s) {
					continue;
				}
				for (int m = 0; m < 3; m++) {
					st->set_normal(t.v[m].n);
					st->set_uv(t.v[m].uv);
					st->add_vertex(t.v[m].p - piece.centroid);
				}
			}
			st->index();
			st->generate_tangents();
			arrays.push_back(st->commit_to_arrays());
			surfaces.push_back(s);
		}
		surface_arrays.write[k] = arrays;
		mesh_surfaces.write[k] = surfaces;
		piece.tris.clear(); // in the arrays now
	}
	prepared = true;
}

// The meshes and shapes (the main thread: they are the servers' resources).
void FellowFractureResult::build() {
	if (built) {
		return;
	}
	prepare();
	meshes.resize(pieces.size());
	shapes.resize(pieces.size());
	for (uint32_t k = 0; k < pieces.size(); k++) {
		Ref<ArrayMesh> mesh;
		mesh.instantiate();
		for (const Array &arrays : surface_arrays[k]) {
			mesh->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, arrays);
		}
		meshes.write[k] = mesh;
		Vector<Ref<ConvexPolygonShape3D>> piece_shapes;
		for (const Vector<Vector3> &hull : pieces[k].hulls) {
			Ref<ConvexPolygonShape3D> shape;
			shape.instantiate();
			// A thin margin: Jolt rounds a hull's corners by it, and a shard's sharp tip would sink that far.
			shape->set_margin(0.005f);
			shape->set_points(hull);
			piece_shapes.push_back(shape);
		}
		shapes.write[k] = piece_shapes;
	}
	surface_arrays.clear();
	built = true;
}

FellowDestructible3D::FellowDestructible3D() {
}

FellowDestructible3D::~FellowDestructible3D() {
	if (task != WorkerThreadPool::INVALID_TASK_ID) {
		WorkerThreadPool::get_singleton()->wait_for_task_completion(task);
		task = WorkerThreadPool::INVALID_TASK_ID;
	}
}

MeshInstance3D *FellowDestructible3D::_get_mesh() const {
	if (!mesh_path.is_empty()) {
		return Object::cast_to<MeshInstance3D>(get_node_or_null(mesh_path));
	}
	Node *p = get_parent();
	if (MeshInstance3D *mi = Object::cast_to<MeshInstance3D>(p)) {
		return mi;
	}
	if (p) {
		for (int i = 0; i < p->get_child_count(); i++) {
			if (MeshInstance3D *mi = Object::cast_to<MeshInstance3D>(p->get_child(i))) {
				return mi;
			}
		}
	}
	return nullptr;
}

CollisionObject3D *FellowDestructible3D::_get_body() const {
	if (!body_path.is_empty()) {
		return Object::cast_to<CollisionObject3D>(get_node_or_null(body_path));
	}
	Node *p = get_parent();
	if (CollisionObject3D *co = Object::cast_to<CollisionObject3D>(p)) {
		return co;
	}
	return p ? Object::cast_to<CollisionObject3D>(p->get_parent()) : nullptr;
}

bool FellowDestructible3D::_prepare_source() {
	MeshInstance3D *mi = _get_mesh();
	if (!mi || mi->get_mesh().is_null()) {
		return false;
	}
	FellowFracture::mesh_triangles(mi->get_mesh(), source, params.winding);
	params.interior_surface = mi->get_mesh()->get_surface_count();
	params.interior_uv_scale = interior_uv_scale;
	return !source.is_empty();
}

String FellowDestructible3D::_cache_key() const {
	const MeshInstance3D *mi = _get_mesh();
	return vformat("%d:%d:%d:%f", mi && mi->get_mesh().is_valid() ? int64_t(mi->get_mesh()->get_instance_id()) : 0, seed, piece_count, interior_uv_scale);
}

void FellowDestructible3D::_fracture_task(int p_unused) {
	const uint64_t t0 = OS::get_singleton()->get_ticks_usec();
	FellowFracture::make_sites(source, task_count, task_seed, task_impact, task_focus, sites);
	FellowFracture::voronoi(source, sites, params, result->pieces);
	result->prepare();
	task_usec = OS::get_singleton()->get_ticks_usec() - t0;
}

void FellowDestructible3D::break_apart(const Vector3 &p_position, const Vector3 &p_impulse) {
	if (broken || pending || !is_inside_tree()) {
		return;
	}
	MeshInstance3D *mi = _get_mesh();
	if (!mi) {
		return;
	}
	const Vector3 position = p_position == Vector3() ? mi->get_global_transform().xform(mi->get_aabb().get_center()) : p_position;
	if (structure_built) {
		_strike(position, p_impulse); // standing in pieces already: these break off
		return;
	}
	if (prefractured.is_valid() && prefractured->built) {
		pending = true;
		pending_position = position;
		pending_impulse = p_impulse;
		spawn_result = prefractured;
		if (_spawn_allowed(int(prefractured->pieces.size()))) {
			_spawn(spawn_result, position, p_impulse);
			spawn_result.unref();
		} else {
			set_process_internal(true); // over this frame's budget of pieces: the next frame
		}
		return;
	}
	if (!_prepare_source()) {
		return;
	}
	// The task's inputs, copied: the properties may change while it runs.
	task_impact = mi->get_global_transform().affine_inverse().xform(position);
	task_count = piece_count;
	task_seed = uint64_t(int64_t(seed));
	task_focus = impact_focus;
	result.instantiate();
	result->source_surfaces = params.interior_surface;
	pending = true;
	pending_position = position;
	pending_impulse = p_impulse;
	// Low priority: the engine's own work on the pool (the physics step's jobs, streaming) goes first.
	task = WorkerThreadPool::get_singleton()->add_template_task(this, &FellowDestructible3D::_fracture_task, 0, false, "FellowDestructible3D");
	set_process_internal(true);
}

void FellowDestructible3D::damage(float p_amount, const Vector3 &p_position, const Vector3 &p_impulse) {
	if (broken || pending) {
		return;
	}
	current_health -= p_amount;
	emit_signal(SNAME("damaged"), p_amount, current_health);
	if (current_health <= 0.0f) {
		break_apart(p_position, p_impulse);
	}
}

// The pieces a frame may spawn (their nodes and first physics step are the cost of a break): a break over the budget
// waits for the next frame, intact. The first of a frame always goes.
bool FellowDestructible3D::_spawn_allowed(int p_pieces) {
	const uint64_t frame = Engine::get_singleton()->get_process_frames();
	if (frame != budget_frame) {
		budget_frame = frame;
		budget_used = 0;
	}
	if (max_pieces_per_frame > 0 && budget_used > 0 && budget_used + p_pieces > max_pieces_per_frame) {
		return false;
	}
	budget_used += p_pieces;
	return true;
}

void FellowDestructible3D::_spawn(const Ref<FellowFractureResult> &p_result, const Vector3 &p_position, const Vector3 &p_impulse) {
	const uint64_t t0 = OS::get_singleton()->get_ticks_usec();
	MeshInstance3D *src = _get_mesh();
	CollisionObject3D *body = _get_body();
	if (structural && body && src && p_result.is_valid() && !Object::cast_to<RigidBody3D>(body)) {
		// Milestone 80: the object stands in its pieces; the blow breaks those near it off.
		_build_structure(p_result);
		pending = false;
		_strike(p_position, p_impulse);
		last_spawn_usec = OS::get_singleton()->get_ticks_usec() - t0;
		global_stats.spawn_usec.add(last_spawn_usec);
		return;
	}
	Node *parent = body ? body->get_parent() : (src ? src->get_parent() : nullptr);
	if (!src || !parent || p_result.is_null()) {
		pending = false;
		return;
	}
	const Transform3D mxf = src->get_global_transform();
	const Node3D *parent_3d = Object::cast_to<Node3D>(parent);
	const Transform3D to_parent = parent_3d ? parent_3d->get_global_transform().affine_inverse() : Transform3D();
	const Vector3 scale = mxf.basis.get_scale();
	const Basis rot = mxf.basis.orthonormalized();
	Vector<Ref<Material>> materials;
	for (int s = 0; s < p_result->source_surfaces; s++) {
		materials.push_back(src->get_active_material(s));
	}
	const Ref<Material> interior = interior_material.is_valid() ? interior_material : (materials.is_empty() ? Ref<Material>() : materials[0]);
	Vector3 base_v;
	Vector3 base_w;
	Vector3 body_centre = mxf.origin;
	uint32_t layer = 1;
	uint32_t mask = 1;
	if (body) {
		layer = body->get_collision_layer();
		mask = body->get_collision_mask();
		body_centre = body->get_global_position();
		if (const RigidBody3D *rb = Object::cast_to<RigidBody3D>(body)) {
			base_v = rb->get_linear_velocity();
			base_w = rb->get_angular_velocity();
		}
	}
	const float size = MAX(src->get_aabb().get_longest_axis_size() * MAX(scale.x, MAX(scale.y, scale.z)), 0.01f);
	float total = 0.0f;
	LocalVector<float> near;
	near.resize(p_result->pieces.size());
	for (uint32_t k = 0; k < p_result->pieces.size(); k++) {
		const float d = mxf.xform(p_result->pieces[k].centroid).distance_to(p_position) / size;
		near[k] = 1.0f / (1.0f + 16.0f * d * d);
		total += near[k] * MAX(0.02f, p_result->pieces[k].volume * Math::abs(scale.x * scale.y * scale.z) * density);
	}
	Array spawned;
	const float scale3 = Math::abs(scale.x * scale.y * scale.z);
	for (uint32_t k = 0; k < p_result->pieces.size(); k++) {
		const FellowFracture::Piece &piece = p_result->pieces[k];
		RigidBody3D *rb = memnew(RigidBody3D);
		rb->set_name(vformat("%sPiece%d", src->get_name(), int(k)));
		rb->set_mass(MAX(0.02f, piece.volume * scale3 * density));
		rb->set_collision_layer(layer);
		rb->set_collision_mask(mask);
		MeshInstance3D *mi = memnew(MeshInstance3D);
		mi->set_mesh(p_result->meshes[k]);
		mi->set_scale(scale);
		mi->set_cast_shadows_setting(src->get_cast_shadows_setting());
		const Vector<int> &surfaces = p_result->mesh_surfaces[k];
		for (int i = 0; i < surfaces.size(); i++) {
			const Ref<Material> m = surfaces[i] < materials.size() ? materials[surfaces[i]] : interior;
			if (m.is_valid()) {
				mi->set_surface_override_material(i, m);
			}
		}
		rb->add_child(mi);
		for (const Ref<ConvexPolygonShape3D> &shape : p_result->shapes[k]) {
			CollisionShape3D *cs = memnew(CollisionShape3D);
			cs->set_shape(shape);
			cs->set_scale(scale);
			rb->add_child(cs);
		}
		const Vector3 centre = mxf.xform(piece.centroid);
		rb->set_transform(to_parent * Transform3D(rot, centre));
		parent->add_child(rb);
		Vector3 out = centre - p_position;
		const float dist = out.length();
		out = dist > 1e-4f ? out / dist : Vector3(0, 1, 0);
		Vector3 v = base_v + base_w.cross(centre - body_centre) + out * (burst_speed * near[k]);
		Vector3 w = base_w;
		if (p_impulse != Vector3() && total > 0.0f) {
			// The impulse's momentum shared by the pieces near the impact: each moved by it alike, nearer more, and
			// turned by it about its centre (a uniform piece's inertia). Velocities, not impulses: a new body's mass
			// reaches some physics servers only at their next step.
			const Vector3 dv = p_impulse * (near[k] / total);
			v += dv;
			const float k2 = MAX(1e-4f, piece.aabb.get_longest_axis_size() * MAX(scale.x, MAX(scale.y, scale.z)));
			w += (p_position - centre).cross(dv) / (k2 * k2 / 6.0f);
		}
		rb->set_linear_velocity(v);
		rb->set_angular_velocity(w);
		_spawn_debris(rb);
		spawned.push_back(rb);
	}
	broken = true;
	pending = false;
	last_pieces = int(p_result->pieces.size());
	last_spawn_usec = OS::get_singleton()->get_ticks_usec() - t0;
	global_stats.breaks.increment();
	global_stats.pieces.add(p_result->pieces.size());
	global_stats.spawn_usec.add(last_spawn_usec);
	emit_signal(SNAME("broken"), spawned);
	// The whole object goes: its body (and this node with it), or its mesh hidden.
	src->set_visible(false);
	if (body) {
		for (int i = 0; i < body->get_child_count(); i++) {
			if (CollisionShape3D *cs = Object::cast_to<CollisionShape3D>(body->get_child(i))) {
				cs->set_disabled(true);
			}
		}
		if (free_after_break) {
			body->queue_free();
		}
	} else if (free_after_break) {
		src->queue_free();
	}
}

// Milestone 80 (decision 0188): the object standing in its pieces — each piece's mesh and convex shapes children of
// the object's own body where they were in it, the object's mesh hidden and its shapes off. A piece stands on the
// ground when it reaches within anchor_height of the object's lowest point.
void FellowDestructible3D::_build_structure(const Ref<FellowFractureResult> &p_result) {
	MeshInstance3D *src = _get_mesh();
	CollisionObject3D *body = _get_body();
	if (!src || !body || p_result.is_null() || !p_result->built) {
		return;
	}
	struct_result = p_result;
	struct_mxf = src->get_global_transform();
	const Transform3D to_body = body->get_global_transform().affine_inverse();
	const Vector3 scale = struct_mxf.basis.get_scale();
	const Basis rot = struct_mxf.basis.orthonormalized();
	Vector<Ref<Material>> materials;
	for (int s = 0; s < p_result->source_surfaces; s++) {
		materials.push_back(src->get_active_material(s));
	}
	const Ref<Material> interior = interior_material.is_valid() ? interior_material : (materials.is_empty() ? Ref<Material>() : materials[0]);
	for (int i = 0; i < body->get_child_count(); i++) {
		if (CollisionShape3D *cs = Object::cast_to<CollisionShape3D>(body->get_child(i))) {
			cs->set_disabled(true);
		}
	}
	src->set_visible(false);
	const uint32_t n = p_result->pieces.size();
	struct_pieces.clear();
	struct_pieces.resize(n);
	float lowest = 1e30f;
	LocalVector<float> bottoms;
	bottoms.resize(n);
	for (uint32_t k = 0; k < n; k++) {
		const AABB box = struct_mxf.xform(p_result->pieces[k].aabb);
		bottoms[k] = box.position.y;
		lowest = MIN(lowest, bottoms[k]);
	}
	for (uint32_t k = 0; k < n; k++) {
		const FellowFracture::Piece &piece = p_result->pieces[k];
		const Transform3D at = to_body * Transform3D(rot * Basis::from_scale(scale), struct_mxf.xform(piece.centroid));
		StructPiece &sp = struct_pieces[k];
		MeshInstance3D *mi = memnew(MeshInstance3D);
		mi->set_name(vformat("%sPiece%d", src->get_name(), int(k)));
		mi->set_mesh(p_result->meshes[k]);
		mi->set_cast_shadows_setting(src->get_cast_shadows_setting());
		const Vector<int> &surfaces = p_result->mesh_surfaces[k];
		for (int i = 0; i < surfaces.size(); i++) {
			const Ref<Material> m = surfaces[i] < materials.size() ? materials[surfaces[i]] : interior;
			if (m.is_valid()) {
				mi->set_surface_override_material(i, m);
			}
		}
		mi->set_transform(at);
		body->add_child(mi);
		sp.mesh = mi;
		for (const Ref<ConvexPolygonShape3D> &shape : p_result->shapes[k]) {
			CollisionShape3D *cs = memnew(CollisionShape3D);
			cs->set_shape(shape);
			cs->set_transform(at);
			body->add_child(cs);
			sp.shapes.push_back(cs);
		}
		sp.alive = true;
		sp.anchored = anchor_mode == ANCHOR_LOWEST_POINT && bottoms[k] <= lowest + anchor_height;
	}
	if (anchor_mode == ANCHOR_STATIC_BODIES && body->is_inside_tree()) {
		// Milestone 122: a piece touching another static body (within 2 cm) stands on it.
		PhysicsDirectSpaceState3D *space = body->get_world_3d().is_valid() ? body->get_world_3d()->get_direct_space_state() : nullptr;
		for (uint32_t k = 0; space && k < n; k++) {
			StructPiece &sp = struct_pieces[k];
			for (CollisionShape3D *cs : sp.shapes) {
				if (sp.anchored || cs->get_shape().is_null()) {
					break;
				}
				PhysicsDirectSpaceState3D::ShapeParameters query;
				query.shape_rid = cs->get_shape()->get_rid();
				query.transform = body->get_global_transform() * cs->get_transform();
				query.margin = 0.02f;
				query.collision_mask = body->get_collision_mask();
				query.collide_with_bodies = true;
				query.collide_with_areas = false;
				query.exclude.insert(body->get_rid());
				PhysicsDirectSpaceState3D::ShapeResult results[8];
				const int count = space->intersect_shape(query, results, 8);
				for (int i = 0; i < count; i++) {
					const PhysicsServer3D::BodyMode mode = PhysicsServer3D::get_singleton()->body_get_mode(results[i].rid);
					if (!own_bodies.has(results[i].rid) && (mode == PhysicsServer3D::BODY_MODE_STATIC || mode == PhysicsServer3D::BODY_MODE_KINEMATIC)) {
						sp.anchored = true;
						break;
					}
				}
			}
		}
	}
	structure_built = true;
	_build_bonds(); // milestone 122
}

// The structure as it stands: each piece's centre and bounds (global), whether it stands and on the ground, the pieces
// it touches.
Array FellowDestructible3D::get_structure() const {
	Array out;
	if (!structure_built || struct_result.is_null()) {
		return out;
	}
	for (uint32_t k = 0; k < struct_pieces.size(); k++) {
		Dictionary d;
		d["centre"] = struct_mxf.xform(struct_result->pieces[k].centroid);
		d["aabb"] = struct_mxf.xform(struct_result->pieces[k].aabb);
		d["standing"] = struct_pieces[k].alive;
		d["anchored"] = struct_pieces[k].anchored;
		d["mass"] = k < piece_mass.size() ? piece_mass[k] : 0.0; // milestone 122
		PackedInt32Array touching;
		if (k < piece_bonds.size()) {
			for (int bi : piece_bonds[k]) { // milestone 122: the bonds that hold
				if (!bonds[bi].broken) {
					touching.push_back(bonds[bi].a == int(k) ? bonds[bi].b : bonds[bi].a);
				}
			}
		} else if (k < uint32_t(struct_result->adjacency.size())) {
			for (int j : struct_result->adjacency[k]) {
				touching.push_back(j);
			}
		}
		d["touching"] = touching;
		out.push_back(d);
	}
	return out;
}

// Milestone 122 (decision 0246): the bonds — each touching pair's shared face (its points' convex outline in their
// plane), its area, centre and second moments about its principal axes, in global space as the structure was made.
void FellowDestructible3D::_build_bonds() {
	bonds.clear();
	piece_bonds.clear();
	piece_centre.clear();
	piece_mass.clear();
	stress_x.clear();
	if (struct_result.is_null()) {
		return;
	}
	const uint32_t n = struct_result->pieces.size();
	const Vector3 scale = struct_mxf.basis.get_scale();
	const double scale3 = Math::abs(double(scale.x) * scale.y * scale.z);
	piece_bonds.resize(n);
	piece_centre.resize(n);
	piece_mass.resize(n);
	stress_x.resize(n * 6);
	for (uint32_t k = 0; k < n; k++) {
		piece_centre[k] = struct_mxf.xform(struct_result->pieces[k].centroid);
		piece_mass[k] = MAX(0.02, double(struct_result->pieces[k].volume) * scale3 * density);
	}
	for (double &x : stress_x) {
		x = 0.0;
	}
	for (const FellowFractureResult::Contact &contact : struct_result->contacts) {
		Bond bond;
		bond.a = contact.a;
		bond.b = contact.b;
		const Vector3 ab = piece_centre[contact.b] - piece_centre[contact.a];
		LocalVector<Vector3> points;
		for (const Vector3 &p : contact.points) {
			points.push_back(struct_mxf.xform(p));
		}
		// The face's plane: its two farthest points and the point farthest from their line.
		Vector3 normal;
		Vector3 e1;
		Vector3 origin = points.is_empty() ? (piece_centre[contact.a] + piece_centre[contact.b]) * 0.5f : points[0];
		if (points.size() >= 3) {
			uint32_t i0 = 0, i1 = 1;
			float far = -1.0f;
			for (uint32_t i = 0; i < points.size(); i++) {
				for (uint32_t j = i + 1; j < points.size(); j++) {
					const float d = points[i].distance_squared_to(points[j]);
					if (d > far) {
						far = d;
						i0 = i;
						i1 = j;
					}
				}
			}
			const Vector3 line = (points[i1] - points[i0]).normalized();
			float off = -1.0f;
			uint32_t i2 = 0;
			for (uint32_t i = 0; i < points.size(); i++) {
				const Vector3 v = points[i] - points[i0];
				const float d = (v - line * v.dot(line)).length_squared();
				if (d > off) {
					off = d;
					i2 = i;
				}
			}
			normal = line.cross(points[i2] - points[i0]);
			origin = points[i0];
			e1 = line;
		}
		if (normal.length_squared() < 1e-12f) {
			normal = ab; // a degenerate face: across the centres
			e1 = normal.cross(Math::abs(normal.y) < 0.9f ? Vector3(0, 1, 0) : Vector3(1, 0, 0));
		}
		normal.normalize();
		if (normal.dot(ab) < 0.0f) {
			normal = -normal;
		}
		e1 = (e1 - normal * e1.dot(normal)).normalized();
		const Vector3 e2 = normal.cross(e1);
		// The outline: the points' convex hull in the plane (Andrew's monotone chain), counter-clockwise.
		LocalVector<Vector2> flat;
		for (const Vector3 &p : points) {
			flat.push_back(Vector2((p - origin).dot(e1), (p - origin).dot(e2)));
		}
		flat.sort(); // by x, then y
		LocalVector<Vector2> hull;
		if (flat.size() >= 3) {
			hull.resize(flat.size() * 2);
			int h = 0;
			for (uint32_t i = 0; i < flat.size(); i++) {
				while (h >= 2 && (hull[h - 1] - hull[h - 2]).cross(flat[i] - hull[h - 2]) <= 0.0f) {
					h--;
				}
				hull[h++] = flat[i];
			}
			for (int i = int(flat.size()) - 2, lower = h + 1; i >= 0; i--) {
				while (h >= lower && (hull[h - 1] - hull[h - 2]).cross(flat[i] - hull[h - 2]) <= 0.0f) {
					h--;
				}
				hull[h++] = flat[i];
			}
			hull.resize(MAX(0, h - 1));
		}
		// Area, centre and second moments of the outline (about the plane's origin, then its centre).
		double area = 0.0, cx = 0.0, cy = 0.0, sxx = 0.0, syy = 0.0, sxy = 0.0;
		for (uint32_t i = 0; i < hull.size(); i++) {
			const Vector2 p = hull[i];
			const Vector2 q = hull[(i + 1) % hull.size()];
			const double cross = double(p.x) * q.y - double(q.x) * p.y;
			area += cross;
			cx += (p.x + q.x) * cross;
			cy += (p.y + q.y) * cross;
			sxx += cross * (double(p.x) * p.x + double(p.x) * q.x + double(q.x) * q.x);
			syy += cross * (double(p.y) * p.y + double(p.y) * q.y + double(q.y) * q.y);
			sxy += cross * (double(p.x) * q.y + 2.0 * p.x * p.y + 2.0 * q.x * q.y + double(q.x) * p.y);
		}
		area *= 0.5;
		const double min_area = MAX(1e-8, 1e-6 * ab.length_squared());
		if (area > min_area) {
			cx /= 6.0 * area;
			cy /= 6.0 * area;
			sxx = sxx / 12.0 - area * cx * cx;
			syy = syy / 12.0 - area * cy * cy;
			sxy = sxy / 24.0 - area * cx * cy;
		} else {
			// A face of a point or a line: a small square bond across the centres.
			area = min_area;
			cx = cy = 0.0;
			sxx = syy = area * area / 12.0;
			sxy = 0.0;
			hull.clear();
			const float half = float(Math::sqrt(area) * 0.5);
			hull.push_back(Vector2(-half, -half));
			hull.push_back(Vector2(half, -half));
			hull.push_back(Vector2(half, half));
			hull.push_back(Vector2(-half, half));
			origin = (piece_centre[contact.a] + piece_centre[contact.b]) * 0.5f;
		}
		// The principal axes: the eigenvectors of [[sxx, sxy], [sxy, syy]].
		const double angle = 0.5 * Math::atan2(2.0 * sxy, sxx - syy);
		const double ca = Math::cos(angle), sa = Math::sin(angle);
		const double l1 = sxx * ca * ca + 2.0 * sxy * ca * sa + syy * sa * sa; // ∫ s1² along the first axis
		const double l2 = sxx * sa * sa - 2.0 * sxy * ca * sa + syy * ca * ca; // ∫ s2²
		bond.centre = origin + e1 * float(cx) + e2 * float(cy);
		bond.normal = normal;
		bond.axis1 = (e1 * float(ca) + e2 * float(sa)).normalized();
		bond.axis2 = normal.cross(bond.axis1);
		bond.area = area;
		bond.inertia1 = MAX(l2, area * area * 1e-4); // about axis1: the s2 coordinate's
		bond.inertia2 = MAX(l1, area * area * 1e-4);
		bond.length = MAX(double(Math::abs(ab.dot(normal))), 0.25 * Math::sqrt(area));
		for (const Vector2 &p : hull) {
			const Vector3 v = origin + e1 * p.x + e2 * p.y - bond.centre;
			bond.outline.push_back(Vector2(v.dot(bond.axis1), v.dot(bond.axis2)));
		}
		piece_bonds[contact.a].push_back(int(bonds.size()));
		piece_bonds[contact.b].push_back(int(bonds.size()));
		bonds.push_back(bond);
	}
}

// What stands: from the pieces on the ground through the bonds that hold.
void FellowDestructible3D::_supported(LocalVector<uint8_t> &r_reached) const {
	const uint32_t n = struct_pieces.size();
	r_reached.resize(n);
	LocalVector<int> queue;
	for (uint32_t k = 0; k < n; k++) {
		r_reached[k] = struct_pieces[k].alive && struct_pieces[k].anchored ? 1 : 0;
		if (r_reached[k]) {
			queue.push_back(int(k));
		}
	}
	for (uint32_t q = 0; q < queue.size(); q++) {
		const int k = queue[q];
		if (uint32_t(k) < piece_bonds.size()) {
			for (int bi : piece_bonds[k]) {
				const Bond &bond = bonds[bi];
				const int j = bond.a == k ? bond.b : bond.a;
				if (!bond.broken && struct_pieces[j].alive && !r_reached[j]) {
					r_reached[j] = 1;
					queue.push_back(j);
				}
			}
		} else {
			for (int j : struct_result->adjacency[k]) {
				if (struct_pieces[j].alive && !r_reached[j]) {
					r_reached[j] = 1;
					queue.push_back(j);
				}
			}
		}
	}
}

namespace {

constexpr double STRESS_E = 1.0e9; // Pa: the bonds' stiffness (their forces under the weight do not depend on it where
// the structure is determinate; where it is not, only the ratios between bonds matter)
constexpr double STRESS_G = 0.4e9;
constexpr double STRESS_COMPRESSION = 10.0; // the compressive strength, in multiples of the tensile

// Double-precision vectors for the solve (stiffnesses span orders of magnitude).
struct Vector3d {
	double x = 0.0, y = 0.0, z = 0.0;
	Vector3d() {}
	Vector3d(double p_x, double p_y, double p_z) :
			x(p_x), y(p_y), z(p_z) {}
	double &operator[](int p_i) { return p_i == 0 ? x : (p_i == 1 ? y : z); }
	Vector3d operator+(const Vector3d &p_o) const { return Vector3d(x + p_o.x, y + p_o.y, z + p_o.z); }
	Vector3d operator-(const Vector3d &p_o) const { return Vector3d(x - p_o.x, y - p_o.y, z - p_o.z); }
	Vector3d operator*(double p_s) const { return Vector3d(x * p_s, y * p_s, z * p_s); }
	double dot(const Vector3d &p_o) const { return x * p_o.x + y * p_o.y + z * p_o.z; }
	Vector3d cross(const Vector3d &p_o) const { return Vector3d(y * p_o.z - z * p_o.y, z * p_o.x - x * p_o.z, x * p_o.y - y * p_o.x); }
	double length_squared() const { return dot(*this); }
	double length() const { return Math::sqrt(length_squared()); }
};

inline Vector3d to_d(const Vector3 &p_v) {
	return Vector3d(p_v.x, p_v.y, p_v.z);
}

// A bond's springs in the global frame: K_lin = (E A/L) n nᵀ + (G A/L)(I - n nᵀ); K_rot = (G J/L) n nᵀ +
// (E I1/L) a1 a1ᵀ + (E I2/L) a2 a2ᵀ.
struct BondK {
	double kn, ks, kt, kb1, kb2;
	Vector3d n, a1, a2, c;
	Vector3d lin(const Vector3d &p_d) const {
		const double dn = p_d.dot(n);
		return n * (kn * dn) + (p_d - n * dn) * ks;
	}
	Vector3d rot(const Vector3d &p_p) const {
		return n * (kt * p_p.dot(n)) + a1 * (kb1 * p_p.dot(a1)) + a2 * (kb2 * p_p.dot(a2));
	}
};

// 6x6 symmetric positive definite blocks: the block Jacobi preconditioner (Cholesky).
struct Block6 {
	double m[6][6] = {};
	bool factor() {
		for (int j = 0; j < 6; j++) {
			double d = m[j][j];
			for (int k = 0; k < j; k++) {
				d -= m[j][k] * m[j][k];
			}
			if (d <= 1e-30) {
				return false;
			}
			m[j][j] = Math::sqrt(d);
			for (int i = j + 1; i < 6; i++) {
				double v = m[i][j];
				for (int k = 0; k < j; k++) {
					v -= m[i][k] * m[j][k];
				}
				m[i][j] = v / m[j][j];
			}
		}
		return true;
	}
	void solve(const double *p_b, double *r_x) const {
		double y[6];
		for (int i = 0; i < 6; i++) {
			double v = p_b[i];
			for (int k = 0; k < i; k++) {
				v -= m[i][k] * y[k];
			}
			y[i] = v / m[i][i];
		}
		for (int i = 5; i >= 0; i--) {
			double v = y[i];
			for (int k = i + 1; k < 6; k++) {
				v -= m[k][i] * r_x[k];
			}
			r_x[i] = v / m[i][i];
		}
	}
};

} // namespace

// The structure's small displacements under the pieces' weight, the pieces on the ground fixed: K x = f, solved by
// conjugate gradients with a block Jacobi preconditioner, started from the last solve's. Then each bond's force, moment
// and stresses at its outline's corners (normal: (E/L)(δ·n + (φ × s)·n), tension positive; shear: (G/L)|slip|), and
// its ratio to the limits. Returns the bond most past its limits, or -1.
int FellowDestructible3D::_solve_stress(const LocalVector<uint8_t> &p_supported) {
	const uint32_t n = struct_pieces.size();
	if (bonds.is_empty() || piece_bonds.size() != n) {
		return -1;
	}
	// The unknowns: the standing pieces not on the ground.
	LocalVector<int> index;
	index.resize(n);
	int m = 0;
	for (uint32_t k = 0; k < n; k++) {
		const bool free = p_supported[k] && struct_pieces[k].alive && !struct_pieces[k].anchored;
		index[k] = free ? m++ : -1;
	}
	if (m == 0) {
		return -1;
	}
	// The bonds that carry: holding, between standing pieces, at least one of them free.
	LocalVector<int> active;
	LocalVector<BondK> ks;
	for (uint32_t bi = 0; bi < bonds.size(); bi++) {
		Bond &bond = bonds[bi];
		bond.force = Vector3();
		bond.moment = Vector3();
		bond.tension = bond.compression = bond.shear = bond.ratio = 0.0;
		if (bond.broken || !p_supported[bond.a] || !p_supported[bond.b] || !struct_pieces[bond.a].alive || !struct_pieces[bond.b].alive || (index[bond.a] < 0 && index[bond.b] < 0)) {
			continue;
		}
		BondK k;
		k.kn = STRESS_E * bond.area / bond.length;
		k.ks = STRESS_G * bond.area / bond.length;
		k.kt = STRESS_G * (bond.inertia1 + bond.inertia2) / bond.length;
		k.kb1 = STRESS_E * bond.inertia1 / bond.length;
		k.kb2 = STRESS_E * bond.inertia2 / bond.length;
		k.n = to_d(bond.normal);
		k.a1 = to_d(bond.axis1);
		k.a2 = to_d(bond.axis2);
		k.c = to_d(bond.centre);
		active.push_back(int(bi));
		ks.push_back(k);
	}
	if (active.is_empty()) {
		return -1;
	}
	ProjectSettings *settings = ProjectSettings::get_singleton();
	const Vector3 g_dir = settings ? Vector3(GLOBAL_GET("physics/3d/default_gravity_vector")) : Vector3(0, -1, 0);
	const double g_len = settings ? double(GLOBAL_GET("physics/3d/default_gravity")) : 9.8;
	const Vector3d g = to_d(g_dir.normalized()) * g_len;

	// A x for x of 6 m: each bond's relative motion at its centre, its springs' forces back on its pieces.
	auto relative = [&](const BondK &k, const Bond &bond, const double *x, Vector3d &r_d, Vector3d &r_p) {
		Vector3d ua, wa, ub, wb;
		if (index[bond.a] >= 0) {
			const double *v = x + 6 * index[bond.a];
			ua = Vector3d(v[0], v[1], v[2]);
			wa = Vector3d(v[3], v[4], v[5]);
		}
		if (index[bond.b] >= 0) {
			const double *v = x + 6 * index[bond.b];
			ub = Vector3d(v[0], v[1], v[2]);
			wb = Vector3d(v[3], v[4], v[5]);
		}
		const Vector3d ra = k.c - to_d(piece_centre[bond.a]);
		const Vector3d rb = k.c - to_d(piece_centre[bond.b]);
		r_d = (ub + wb.cross(rb)) - (ua + wa.cross(ra));
		r_p = wb - wa;
	};
	auto apply = [&](const double *x, double *r_out) {
		memset(r_out, 0, sizeof(double) * 6 * m);
		for (uint32_t i = 0; i < active.size(); i++) {
			const Bond &bond = bonds[active[i]];
			const BondK &k = ks[i];
			Vector3d d, p;
			relative(k, bond, x, d, p);
			const Vector3d f = k.lin(d);
			const Vector3d t = k.rot(p);
			if (index[bond.b] >= 0) {
				double *o = r_out + 6 * index[bond.b];
				const Vector3d tq = (k.c - to_d(piece_centre[bond.b])).cross(f) + t;
				o[0] += f.x, o[1] += f.y, o[2] += f.z, o[3] += tq.x, o[4] += tq.y, o[5] += tq.z;
			}
			if (index[bond.a] >= 0) {
				double *o = r_out + 6 * index[bond.a];
				const Vector3d tq = (k.c - to_d(piece_centre[bond.a])).cross(f) + t;
				o[0] -= f.x, o[1] -= f.y, o[2] -= f.z, o[3] -= tq.x, o[4] -= tq.y, o[5] -= tq.z;
			}
		}
	};
	// The preconditioner: each free piece's own 6 x 6 block, [[K, -K [r]x], [[r]x K, -[r]x K [r]x + R]] over its bonds.
	LocalVector<Block6> blocks;
	blocks.resize(m);
	for (uint32_t i = 0; i < active.size(); i++) {
		const Bond &bond = bonds[active[i]];
		const BondK &k = ks[i];
		for (int end = 0; end < 2; end++) {
			const int piece = end ? bond.b : bond.a;
			if (index[piece] < 0) {
				continue;
			}
			Block6 &bl = blocks[index[piece]];
			const Vector3d r = k.c - to_d(piece_centre[piece]);
			// Columns: the response to a unit displacement or rotation of the piece alone.
			for (int c = 0; c < 6; c++) {
				Vector3d du, dw;
				if (c < 3) {
					du[c] = 1.0;
				} else {
					dw[c - 3] = 1.0;
				}
				const Vector3d d = du + dw.cross(r);
				const Vector3d f = k.lin(d);
				const Vector3d t = r.cross(f) + k.rot(dw);
				bl.m[0][c] += f.x;
				bl.m[1][c] += f.y;
				bl.m[2][c] += f.z;
				bl.m[3][c] += t.x;
				bl.m[4][c] += t.y;
				bl.m[5][c] += t.z;
			}
		}
	}
	LocalVector<uint8_t> factored;
	factored.resize(m);
	for (int i = 0; i < m; i++) {
		factored[i] = blocks[i].factor() ? 1 : 0;
	}
	auto precondition = [&](const double *r, double *z) {
		for (int i = 0; i < m; i++) {
			if (factored[i]) {
				blocks[i].solve(r + 6 * i, z + 6 * i);
			} else {
				for (int c = 0; c < 6; c++) {
					z[6 * i + c] = r[6 * i + c];
				}
			}
		}
	};
	const int dim = 6 * m;
	LocalVector<double> x, b, r, z, p, ap;
	x.resize(dim);
	b.resize(dim);
	r.resize(dim);
	z.resize(dim);
	p.resize(dim);
	ap.resize(dim);
	double b_norm = 0.0;
	for (uint32_t k = 0; k < n; k++) {
		if (index[k] < 0) {
			continue;
		}
		const Vector3d f = g * piece_mass[k];
		double *bk = b.ptr() + 6 * index[k];
		bk[0] = f.x, bk[1] = f.y, bk[2] = f.z, bk[3] = bk[4] = bk[5] = 0.0;
		b_norm += f.length_squared();
		for (int c = 0; c < 6; c++) {
			x[6 * index[k] + c] = stress_x[6 * k + c];
		}
	}
	// Loads resting on it: on the standing piece nearest each (none if that piece is on the ground).
	for (const Load &load : loads) {
		int nearest = -1;
		float best = 1e30f;
		for (uint32_t k = 0; k < n; k++) {
			if (!p_supported[k] || !struct_pieces[k].alive) {
				continue;
			}
			const float d = piece_centre[k].distance_squared_to(load.position);
			if (d < best) {
				best = d;
				nearest = int(k);
			}
		}
		if (nearest < 0 || index[nearest] < 0) {
			continue;
		}
		const Vector3d f = to_d(load.force);
		const Vector3d t = (to_d(load.position) - to_d(piece_centre[nearest])).cross(f);
		double *bk = b.ptr() + 6 * index[nearest];
		b_norm -= Vector3d(bk[0], bk[1], bk[2]).length_squared();
		bk[0] += f.x, bk[1] += f.y, bk[2] += f.z, bk[3] += t.x, bk[4] += t.y, bk[5] += t.z;
		b_norm += Vector3d(bk[0], bk[1], bk[2]).length_squared();
	}
	b_norm = Math::sqrt(MAX(0.0, b_norm));
	apply(x.ptr(), ap.ptr());
	for (int i = 0; i < dim; i++) {
		r[i] = b[i] - ap[i];
	}
	precondition(r.ptr(), z.ptr());
	double rz = 0.0;
	for (int i = 0; i < dim; i++) {
		p[i] = z[i];
		rz += r[i] * z[i];
	}
	const int max_iterations = MIN(4000, MAX(100, 2 * dim));
	int it = 0;
	for (; it < max_iterations; it++) {
		double rr = 0.0;
		for (int i = 0; i < dim; i++) {
			rr += r[i] * r[i];
		}
		if (Math::sqrt(rr) <= 1e-6 * MAX(b_norm, 1e-12)) { // the forces balanced to a millionth of the weight
			break;
		}
		apply(p.ptr(), ap.ptr());
		double pap = 0.0;
		for (int i = 0; i < dim; i++) {
			pap += p[i] * ap[i];
		}
		if (pap <= 0.0) {
			break;
		}
		const double alpha = rz / pap;
		for (int i = 0; i < dim; i++) {
			x[i] += alpha * p[i];
			r[i] -= alpha * ap[i];
		}
		precondition(r.ptr(), z.ptr());
		double rz_new = 0.0;
		for (int i = 0; i < dim; i++) {
			rz_new += r[i] * z[i];
		}
		const double beta = rz_new / MAX(rz, 1e-300);
		rz = rz_new;
		for (int i = 0; i < dim; i++) {
			p[i] = z[i] + beta * p[i];
		}
	}
	last_stress_iterations += it;
	stress_work += uint64_t(it + 2) * active.size();
	for (uint32_t k = 0; k < n; k++) {
		for (int c = 0; c < 6; c++) {
			stress_x[6 * k + c] = index[k] >= 0 ? x[6 * index[k] + c] : 0.0;
		}
	}
	// The bonds' forces and stresses.
	const double limit = MAX(1e-3, double(strength) * 1000.0);
	int worst = -1;
	double worst_ratio = 1.0;
	for (uint32_t i = 0; i < active.size(); i++) {
		Bond &bond = bonds[active[i]];
		const BondK &k = ks[i];
		Vector3d d, ph;
		relative(k, bond, x.ptr(), d, ph);
		const Vector3d f = k.lin(d);
		const Vector3d t = k.rot(ph);
		bond.force = Vector3(-f.x, -f.y, -f.z); // on b: the springs pull it back
		bond.moment = Vector3(-t.x, -t.y, -t.z);
		const double dn = d.dot(k.n);
		const Vector3d dt = d - k.n * dn;
		double tension = 0.0, compression = 0.0, shear = 0.0;
		for (const Vector2 &corner : bond.outline) {
			const Vector3d s = k.a1 * corner.x + k.a2 * corner.y;
			const Vector3d slip = ph.cross(s);
			const double opening = dn + slip.dot(k.n);
			const double sigma = STRESS_E / bond.length * opening;
			tension = MAX(tension, sigma);
			compression = MAX(compression, -sigma);
			shear = MAX(shear, STRESS_G / bond.length * (dt + slip - k.n * slip.dot(k.n)).length());
		}
		bond.tension = tension;
		bond.compression = compression;
		bond.shear = shear;
		bond.ratio = MAX(MAX(tension, shear) / limit, compression / (STRESS_COMPRESSION * limit));
		if (bond.ratio > worst_ratio) {
			worst_ratio = bond.ratio;
			worst = int(active[i]);
		}
	}
	return worst;
}

// After a blow (or update_structure()): the bonds the weight overloads crack, the worst first, the stresses solved
// again after each; then each part that no longer reaches the ground becomes one rigid body and falls.
void FellowDestructible3D::_settle(Array &r_spawned) {
	const uint32_t n = struct_pieces.size();
	LocalVector<uint8_t> reached;
	const uint64_t t0 = OS::get_singleton()->get_ticks_usec();
	int cracked = 0;
	last_stress_iterations = 0;
	settle_pending = false;
	stress_work = 0;
	uint64_t round_work = 0; // the last round's: the next one's likely cost
	for (int round = 0;; round++) {
		_supported(reached);
		if (strength <= 0.0f || bonds.is_empty()) {
			break;
		}
		// A collapse spread over frames: past the frame's budget (or 64 rounds) the rest is settled at the next physics
		// frame, from what stands then (the parts that lost their support falling now).
		if (round >= 64 || (round > 0 && stress_work + round_work > STRESS_FRAME_WORK)) {
			settle_pending = true;
			break;
		}
		const uint64_t work_before = stress_work;
		const int worst = _solve_stress(reached);
		round_work = stress_work - work_before;
		if (worst < 0) {
			break;
		}
		// The worst cracks, and with it those nearly as far past their limits (a crack runs along a line of bonds): fewer
		// solves for a collapse.
		const double worst_ratio = bonds[worst].ratio;
		for (Bond &bond : bonds) {
			if (!bond.broken && bond.ratio > 1.0 && bond.ratio >= 0.75 * worst_ratio) {
				bond.broken = true;
				cracked++;
			}
		}
	}
	last_cracked = cracked;
	cracked_total += cracked;
	last_stress_usec = strength > 0.0f ? OS::get_singleton()->get_ticks_usec() - t0 : 0;
	int fallen = 0;
	int chunk_count = 0;
	for (uint32_t k = 0; k < n; k++) {
		if (!struct_pieces[k].alive || reached[k]) {
			continue;
		}
		// An unsupported part: the pieces it still holds, together.
		LocalVector<int> part;
		part.push_back(int(k));
		reached[k] = 1;
		for (uint32_t q = 0; q < part.size(); q++) {
			const int piece = part[q];
			if (uint32_t(piece) < piece_bonds.size()) {
				for (int bi : piece_bonds[piece]) {
					const Bond &bond = bonds[bi];
					const int j = bond.a == piece ? bond.b : bond.a;
					if (!bond.broken && struct_pieces[j].alive && !reached[j]) {
						reached[j] = 1;
						part.push_back(j);
					}
				}
			} else {
				for (int j : struct_result->adjacency[piece]) {
					if (struct_pieces[j].alive && !reached[j]) {
						reached[j] = 1;
						part.push_back(j);
					}
				}
			}
		}
		for (int j : part) {
			_remove_struct_piece(j);
		}
		RigidBody3D *rb = _make_body(part, struct_mxf);
		if (rb) {
			_spawn_debris(rb);
			r_spawned.push_back(rb);
			_track_chunk(rb, part); // milestone 123
		}
		fallen += int(part.size());
		chunk_count++;
	}
	last_fallen = fallen;
	last_chunks = chunk_count;
	fallen_total += fallen;
}

// Milestone 122 (decision 0246): a prefractured structural object stood in its pieces and settled without a blow —
// what its weight breaks breaks, what loses its support falls (after its strength or its pieces changed, or to see at
// once whether it holds).
void FellowDestructible3D::update_structure() {
	if (!structural || broken || pending || !is_inside_tree()) {
		return;
	}
	if (!structure_built) {
		if (prefractured.is_null() || !prefractured->built || Object::cast_to<RigidBody3D>(_get_body())) {
			return;
		}
		_build_structure(prefractured);
	}
	for (Bond &bond : bonds) {
		bond.ratio = 0.0;
	}
	Array spawned;
	last_broken = 0;
	_settle(spawned);
	last_pieces = last_fallen;
	if (!spawned.is_empty()) {
		global_stats.breaks.increment();
		global_stats.pieces.add(uint64_t(last_pieces));
		emit_signal(SNAME("broken"), spawned);
	}
	_finish_if_empty();
}

// Milestone 123 (decision 0247): a part that fell, watched until it lands (or is gone).
void FellowDestructible3D::_track_chunk(RigidBody3D *p_body, const LocalVector<int> &p_pieces) {
	if (landing_break_speed <= 0.0f || !p_body || p_pieces.size() < 2) {
		return;
	}
	p_body->set_contact_monitor(true); // its contacts, where it lands
	p_body->set_max_contacts_reported(MAX(p_body->get_max_contacts_reported(), 4));
	Chunk chunk;
	chunk.body = p_body->get_instance_id();
	chunk.pieces = p_pieces;
	chunk.made = p_body->get_global_transform();
	chunks.push_back(chunk);
}

// Each part's change of velocity in the last step, less what gravity accounts for (as a rigid destructible's blow,
// decision 0182, but in m/s: a heavy part should not break on a light landing that a light one survives — the speed it
// stops from is what breaks it).
void FellowDestructible3D::_check_chunks() {
	PhysicsServer3D *ps = PhysicsServer3D::get_singleton();
	Array spawned;
	int breaks = 0;
	for (uint32_t i = 0; i < chunks.size();) {
		Chunk &chunk = chunks[i];
		RigidBody3D *rb = ObjectDB::get_instance<RigidBody3D>(chunk.body);
		if (!rb || !rb->is_inside_tree() || rb->is_freeze_enabled() || rb->is_sleeping() || landing_break_speed <= 0.0f) {
			chunks.remove_at_unordered(i); // gone, fading away, at rest, or the option turned off
			continue;
		}
		PhysicsDirectBodyState3D *st = ps->body_get_direct_state(rb->get_rid());
		if (!st) {
			i++;
			continue;
		}
		const Vector3 v = st->get_linear_velocity();
		const Vector3 dv = v - chunk.last_velocity;
		const bool had = chunk.has_last;
		chunk.last_velocity = v;
		chunk.has_last = true;
		if (!had || st->get_contact_count() == 0) {
			i++;
			continue;
		}
		const float change = dv.length() - st->get_total_gravity().length() * st->get_step();
		if (change < landing_break_speed) {
			i++;
			continue;
		}
		// Where: the contact that pushed hardest along the change.
		int best = 0;
		float best_dot = -1e30f;
		for (int c = 0; c < st->get_contact_count(); c++) {
			const float d = st->get_contact_impulse(c).dot(dv);
			if (d > best_dot) {
				best_dot = d;
				best = c;
			}
		}
		const Vector3 at = st->get_contact_local_position(best); // global (Godot 4)
		const Chunk broken_chunk = chunk;
		chunks.remove_at_unordered(i);
		_break_chunk(broken_chunk, rb, at, spawned);
		breaks++;
	}
	if (breaks > 0) {
		last_landing_breaks = breaks;
		landing_breaks_total += breaks;
		global_stats.breaks.add(uint64_t(breaks));
		global_stats.pieces.add(uint64_t(spawned.size()));
		emit_signal(SNAME("broken"), spawned);
	}
}

// A part broken where it landed: the pieces within break_radius of the contact (at least the nearest) each a body, the
// rest split along the bonds between its pieces that hold, each group a body (a group of several watched in turn);
// every new body where its pieces are and moving as they did.
void FellowDestructible3D::_break_chunk(const Chunk &p_chunk, RigidBody3D *p_body, const Vector3 &p_at, Array &r_spawned) {
	const Transform3D now = p_body->get_global_transform();
	const Transform3D mxf = now * p_chunk.made.affine_inverse() * struct_mxf; // the pieces' mesh transform now
	const Vector3 v = p_body->get_linear_velocity();
	const Vector3 w = p_body->get_angular_velocity();
	const Vector3 com = now.origin;
	HashSet<int> in_chunk;
	for (int k : p_chunk.pieces) {
		in_chunk.insert(k);
	}
	HashSet<int> hit;
	int nearest = -1;
	float best = 1e30f;
	for (int k : p_chunk.pieces) {
		const float d = mxf.xform(struct_result->pieces[k].centroid).distance_to(p_at);
		if (d < break_radius) {
			hit.insert(k);
		}
		if (d < best) {
			best = d;
			nearest = k;
		}
	}
	if (hit.is_empty() && nearest >= 0) {
		hit.insert(nearest);
	}
	auto launch = [&](RigidBody3D *p_new) {
		const Vector3 r = p_new->get_global_transform().origin - com;
		p_new->set_linear_velocity(v + w.cross(r));
		p_new->set_angular_velocity(w);
		_spawn_debris(p_new);
		r_spawned.push_back(p_new);
	};
	for (int k : hit) {
		LocalVector<int> one;
		one.push_back(k);
		if (RigidBody3D *rb = _make_body(one, mxf)) {
			launch(rb);
		}
	}
	// The rest: groups along the bonds that hold.
	HashSet<int> done;
	for (int start : p_chunk.pieces) {
		if (hit.has(start) || done.has(start)) {
			continue;
		}
		LocalVector<int> group;
		group.push_back(start);
		done.insert(start);
		for (uint32_t q = 0; q < group.size(); q++) {
			const int piece = group[q];
			if (uint32_t(piece) >= piece_bonds.size()) {
				continue;
			}
			for (int bi : piece_bonds[piece]) {
				const Bond &bond = bonds[bi];
				const int j = bond.a == piece ? bond.b : bond.a;
				if (!bond.broken && in_chunk.has(j) && !hit.has(j) && !done.has(j)) {
					done.insert(j);
					group.push_back(j);
				}
			}
		}
		if (RigidBody3D *rb = _make_body(group, mxf)) {
			launch(rb);
			_track_chunk(rb, group);
		}
	}
	own_bodies.erase(p_body->get_rid());
	p_body->queue_free();
}

// Milestone 122: a load resting on the structure (a body's weight on a floor, a balcony): global position and force
// (N), kept until clear_loads(); it counts at the next blow or update_structure().
void FellowDestructible3D::add_load(const Vector3 &p_position, const Vector3 &p_force) {
	Load load;
	load.position = p_position;
	load.force = p_force;
	loads.push_back(load);
}

// The bonds and their last solve: each one's pieces, face (centre, normal, area), whether it holds, the force and
// moment on its second piece (newtons, newton-metres) and its greatest tension, compression and shear (kPa), and its
// ratio to the limits.
Array FellowDestructible3D::get_bonds() const {
	Array out;
	for (const Bond &bond : bonds) {
		Dictionary d;
		d["a"] = bond.a;
		d["b"] = bond.b;
		d["centre"] = bond.centre;
		d["normal"] = bond.normal;
		d["area"] = bond.area;
		d["broken"] = bond.broken;
		d["force"] = bond.force;
		d["moment"] = bond.moment;
		d["tension"] = bond.tension / 1000.0;
		d["compression"] = bond.compression / 1000.0;
		d["shear"] = bond.shear / 1000.0;
		d["ratio"] = bond.ratio;
		out.push_back(d);
	}
	return out;
}

int FellowDestructible3D::get_standing_pieces() const {
	int n = 0;
	for (const StructPiece &sp : struct_pieces) {
		n += sp.alive ? 1 : 0;
	}
	return n;
}

// Pieces of the broken object as one rigid body (a piece knocked off, or a chunk that lost its support), where they
// were, their meshes and shapes about its centre of mass.
RigidBody3D *FellowDestructible3D::_make_body(const LocalVector<int> &p_pieces, const Transform3D &p_mxf) {
	MeshInstance3D *src = _get_mesh();
	CollisionObject3D *body = _get_body();
	Node *parent = body ? body->get_parent() : nullptr;
	if (!src || !parent || struct_result.is_null()) {
		return nullptr;
	}
	const Vector3 scale = p_mxf.basis.get_scale();
	const Basis rot = p_mxf.basis.orthonormalized();
	const float scale3 = Math::abs(scale.x * scale.y * scale.z);
	Vector<Ref<Material>> materials;
	for (int s = 0; s < struct_result->source_surfaces; s++) {
		materials.push_back(src->get_active_material(s));
	}
	const Ref<Material> interior = interior_material.is_valid() ? interior_material : (materials.is_empty() ? Ref<Material>() : materials[0]);
	float mass = 0.0f;
	Vector3 centre;
	for (int k : p_pieces) {
		const float m = MAX(0.02f, struct_result->pieces[k].volume * scale3 * density);
		mass += m;
		centre += p_mxf.xform(struct_result->pieces[k].centroid) * m;
	}
	centre /= MAX(mass, 1e-6f);
	RigidBody3D *rb = memnew(RigidBody3D);
	rb->set_name(vformat("%s%s%d", src->get_name(), p_pieces.size() > 1 ? "Chunk" : "Piece", p_pieces[0]));
	rb->set_mass(mass);
	rb->set_collision_layer(body->get_collision_layer());
	rb->set_collision_mask(body->get_collision_mask());
	const Basis inv = rot.inverse();
	for (int k : p_pieces) {
		const FellowFracture::Piece &piece = struct_result->pieces[k];
		const Transform3D local(Basis::from_scale(scale), inv.xform(p_mxf.xform(piece.centroid) - centre));
		MeshInstance3D *mi = memnew(MeshInstance3D);
		mi->set_mesh(struct_result->meshes[k]);
		mi->set_cast_shadows_setting(src->get_cast_shadows_setting());
		const Vector<int> &surfaces = struct_result->mesh_surfaces[k];
		for (int i = 0; i < surfaces.size(); i++) {
			const Ref<Material> m = surfaces[i] < materials.size() ? materials[surfaces[i]] : interior;
			if (m.is_valid()) {
				mi->set_surface_override_material(i, m);
			}
		}
		mi->set_transform(local);
		rb->add_child(mi);
		for (const Ref<ConvexPolygonShape3D> &shape : struct_result->shapes[k]) {
			CollisionShape3D *cs = memnew(CollisionShape3D);
			cs->set_shape(shape);
			cs->set_transform(local);
			rb->add_child(cs);
		}
	}
	const Node3D *parent_3d = Object::cast_to<Node3D>(parent);
	const Transform3D to_parent = parent_3d ? parent_3d->get_global_transform().affine_inverse() : Transform3D();
	rb->set_transform(to_parent * Transform3D(rot, centre));
	parent->add_child(rb);
	own_bodies.insert(rb->get_rid());
	// Its faces lie on the standing pieces' (the cells share them): touching all round, it would be held there by
	// friction. It passes through them while it leaves.
	rb->add_collision_exception_with(body);
	Exception e;
	e.body = rb->get_instance_id();
	e.until = debris_clock + (p_pieces.size() > 1 ? 0.15 : 0.3);
	exceptions.push_back(e);
	return rb;
}

void FellowDestructible3D::_expire_exceptions() {
	CollisionObject3D *body = _get_body();
	for (uint32_t i = 0; i < exceptions.size();) {
		if (debris_clock >= exceptions[i].until) {
			RigidBody3D *rb = ObjectDB::get_instance<RigidBody3D>(exceptions[i].body);
			if (rb && body) {
				rb->remove_collision_exception_with(body);
			}
			exceptions.remove_at_unordered(i);
		} else {
			i++;
		}
	}
}

// A blow on the standing object: the pieces within break_radius of it (at least the nearest) break off, thrown by it;
// then what no longer reaches the ground through the pieces that touch falls, each part that hangs together as one
// body.
void FellowDestructible3D::_strike(const Vector3 &p_position, const Vector3 &p_impulse) {
	if (!structure_built || struct_result.is_null()) {
		return;
	}
	const uint32_t n = struct_pieces.size();
	const Vector3 scale = struct_mxf.basis.get_scale();
	const float scale3 = Math::abs(scale.x * scale.y * scale.z);
	LocalVector<int> hit;
	int nearest = -1;
	float best = 1e30f;
	for (uint32_t k = 0; k < n; k++) {
		if (!struct_pieces[k].alive) {
			continue;
		}
		const float d = struct_mxf.xform(struct_result->pieces[k].centroid).distance_to(p_position);
		if (d < break_radius) {
			hit.push_back(int(k));
		}
		if (d < best) {
			best = d;
			nearest = int(k);
		}
	}
	if (hit.is_empty() && nearest >= 0) {
		hit.push_back(nearest);
	}
	if (hit.is_empty()) {
		return;
	}
	Array spawned;
	float hit_mass = 0.0f;
	for (int k : hit) {
		hit_mass += MAX(0.02f, struct_result->pieces[k].volume * scale3 * density);
	}
	for (int k : hit) {
		_remove_struct_piece(k);
		LocalVector<int> one;
		one.push_back(k);
		RigidBody3D *rb = _make_body(one, struct_mxf);
		if (!rb) {
			continue;
		}
		const Vector3 centre = struct_mxf.xform(struct_result->pieces[k].centroid);
		Vector3 out = centre - p_position;
		const float dist = out.length();
		out = dist > 1e-4f ? out / dist : Vector3(0, 1, 0);
		rb->set_linear_velocity(out * burst_speed + p_impulse / MAX(hit_mass, 1e-3f));
		_spawn_debris(rb);
		spawned.push_back(rb);
	}
	// Support (milestone 122: and stress), and the parts that lost it falling.
	_settle(spawned);
	last_broken = int(hit.size());
	last_pieces = int(hit.size()) + last_fallen;
	global_stats.breaks.increment();
	global_stats.pieces.add(uint64_t(last_pieces));
	emit_signal(SNAME("broken"), spawned);
	_finish_if_empty();
}

void FellowDestructible3D::_remove_struct_piece(int p_index) {
	StructPiece &sp = struct_pieces[p_index];
	sp.alive = false;
	if (sp.mesh) {
		sp.mesh->queue_free();
		sp.mesh = nullptr;
	}
	for (CollisionShape3D *cs : sp.shapes) {
		cs->set_disabled(true); // at once: the rest of this step's queries must not meet it
		cs->queue_free();
	}
	sp.shapes.clear();
}

// Nothing stands: the object is done (milestone 123: its body, empty, freed once the parts it dropped have landed).
void FellowDestructible3D::_finish_if_empty() {
	if (structure_built && get_standing_pieces() == 0) {
		broken = true;
		CollisionObject3D *body = _get_body();
		if (body && free_after_break && chunks.is_empty()) {
			body->queue_free();
		}
	}
}

void FellowDestructible3D::_check_impacts() {
	if (broken || pending || !break_on_impact || break_impulse <= 0.0f) {
		return;
	}
	CollisionObject3D *body = _get_body();
	if (!body || !body->is_inside_tree()) {
		return;
	}
	PhysicsServer3D *ps = PhysicsServer3D::get_singleton();
	if (RigidBody3D *rb = Object::cast_to<RigidBody3D>(body)) {
		// The blow the body felt last step: its change of velocity, less what gravity can account for, times its mass
		// (a body resting, or under a load, changes none; the contacts' impulses would count its weight every step).
		PhysicsDirectBodyState3D *st = ps->body_get_direct_state(rb->get_rid());
		if (!st || st->get_inverse_mass() <= 0.0f) {
			return;
		}
		const Vector3 v = st->get_linear_velocity();
		const Vector3 dv = v - last_velocity;
		const bool had = has_last_velocity;
		last_velocity = v;
		has_last_velocity = true;
		if (!had || st->get_contact_count() == 0) {
			return;
		}
		last_impact = MAX(0.0f, dv.length() - st->get_total_gravity().length() * st->get_step()) / st->get_inverse_mass();
		if (last_impact >= break_impulse) {
			// Where: the contact that pushed hardest along the change.
			int best = 0;
			float best_dot = -1e30f;
			for (int i = 0; i < st->get_contact_count(); i++) {
				const float d = st->get_contact_impulse(i).dot(dv);
				if (d > best_dot) {
					best_dot = d;
					best = i;
				}
			}
			// The body's pieces start at its velocity after the blow (the impulse is in it already).
			break_apart(st->get_contact_local_position(best), Vector3());
		}
		return;
	}
	// A static body: a rigid body striking it (near its shapes, its momentum towards it).
	PhysicsDirectSpaceState3D *space = body->get_world_3d().is_valid() ? body->get_world_3d()->get_direct_space_state() : nullptr;
	if (!space) {
		return;
	}
	for (int c = 0; c < body->get_child_count(); c++) {
		const CollisionShape3D *cs = Object::cast_to<CollisionShape3D>(body->get_child(c));
		if (!cs || cs->is_disabled() || cs->get_shape().is_null()) {
			continue;
		}
		PhysicsDirectSpaceState3D::ShapeParameters sp;
		sp.shape_rid = cs->get_shape()->get_rid();
		sp.transform = cs->get_global_transform();
		sp.margin = 0.3f; // a body up to 18 m/s seen the step before it strikes (60 Hz)
		sp.collide_with_bodies = true;
		sp.collide_with_areas = false;
		sp.exclude.insert(body->get_rid());
		PhysicsDirectSpaceState3D::ShapeResult res[8];
		const int n = space->intersect_shape(sp, res, 8);
		for (int i = 0; i < n; i++) {
			if (!Object::cast_to<RigidBody3D>(ObjectDB::get_instance(res[i].collider_id)) || own_bodies.has(res[i].rid)) {
				continue;
			}
			if (structure_built && res[i].collider_id == last_striker && debris_clock - last_strike_time < 0.25) {
				continue; // the body that just struck: going on through the gap it made
			}
			PhysicsDirectBodyState3D *st = ps->body_get_direct_state(res[i].rid);
			if (!st || st->get_inverse_mass() <= 0.0f) {
				continue;
			}
			const Vector3 v = st->get_linear_velocity();
			const Vector3 at = st->get_transform().origin;
			const float momentum = v.length() / st->get_inverse_mass();
			const bool towards = v.dot(sp.transform.origin - at) > 0.0f;
			if (towards) {
				last_impact = MAX(last_impact, momentum);
			}
			if (momentum >= break_impulse && (towards || momentum >= 2.0f * break_impulse)) {
				// No impulse given to the pieces when it all breaks: the body striking goes on into them. Standing in
				// pieces, those it knocks out take its momentum (they would rest wedged in the hole).
				last_striker = res[i].collider_id;
				last_strike_time = debris_clock;
				break_apart(at, structural ? v / st->get_inverse_mass() : Vector3());
				return;
			}
		}
	}
}

// A piece thrown out: debris (its lifetime, the budget); floating while the scene has water (milestone 176, decision
// 0318): a FellowBuoyancy3D, its points as fine as a third of the piece (at most 16).
void FellowDestructible3D::_spawn_debris(RigidBody3D *p_body) {
	_add_debris(p_body, debris_lifetime);
	if (!debris_floats || !FellowWater3D::has_instances()) {
		return;
	}
	AABB box;
	bool first = true;
	for (int i = 0; i < p_body->get_child_count(); i++) {
		const CollisionShape3D *cs = Object::cast_to<CollisionShape3D>(p_body->get_child(i));
		const Ref<ConvexPolygonShape3D> shape = cs ? Ref<ConvexPolygonShape3D>(cs->get_shape()) : Ref<ConvexPolygonShape3D>();
		if (shape.is_null()) {
			continue;
		}
		const Transform3D xf = cs->get_transform();
		for (const Vector3 &p : shape->get_points()) {
			if (first) {
				box = AABB(xf.xform(p), Vector3());
				first = false;
			} else {
				box.expand_to(xf.xform(p));
			}
		}
	}
	FellowBuoyancy3D *buoyancy = memnew(FellowBuoyancy3D);
	buoyancy->set_name("Buoyancy");
	buoyancy->set_point_spacing(CLAMP(box.get_longest_axis_size() / 3.0f, 0.04f, 0.25f));
	buoyancy->set_max_points(16);
	p_body->add_child(buoyancy);
}

void FellowDestructible3D::_add_debris(Node3D *p_body, float p_lifetime) {
	Debris d;
	d.body = p_body->get_instance_id();
	d.spawned = debris_clock;
	d.lifetime = p_lifetime;
	for (int i = 0; i < p_body->get_child_count(); i++) {
		if (const MeshInstance3D *mi = Object::cast_to<MeshInstance3D>(p_body->get_child(i))) {
			d.scale = mi->get_scale();
		}
	}
	debris.push_back(d);
	if (!debris_connected && SceneTree::get_singleton()) {
		SceneTree::get_singleton()->connect(SNAME("process_frame"), callable_mp_static(&FellowDestructible3D::_debris_tick));
		debris_connected = true;
	}
}

// The debris: past its lifetime and asleep (or twice its lifetime) a piece shrinks away; over the budget the oldest go.
void FellowDestructible3D::_debris_tick() {
	// Game time (a paused or slowed game's debris waits).
	const float dt = SceneTree::get_singleton() ? float(SceneTree::get_singleton()->get_process_time()) : 0.0f;
	debris_clock += dt;
	int active = 0;
	for (const Debris &d : debris) {
		active += d.fading ? 0 : 1;
	}
	for (uint32_t i = 0; i < debris.size() && active > max_debris; i++) {
		if (!debris[i].fading) {
			debris[i].fading = true;
			active--;
		}
	}
	for (uint32_t i = 0; i < debris.size();) {
		Debris &d = debris[i];
		RigidBody3D *rb = ObjectDB::get_instance<RigidBody3D>(d.body);
		if (!rb || !rb->is_inside_tree()) {
			debris.remove_at(i);
			continue;
		}
		if (!d.fading && d.lifetime > 0.0f) {
			const float age = float(debris_clock - d.spawned);
			if (age > d.lifetime && (rb->is_sleeping() || age > 2.0f * d.lifetime)) {
				d.fading = true;
			}
		}
		if (d.fading) {
			if (d.fade == 0.0f) {
				rb->set_freeze_enabled(true);
			}
			d.fade += MIN(dt, 0.1f) / 0.6f;
			for (int c = 0; c < rb->get_child_count(); c++) {
				if (MeshInstance3D *mi = Object::cast_to<MeshInstance3D>(rb->get_child(c))) {
					mi->set_scale(d.scale * MAX(0.001f, 1.0f - d.fade));
				}
			}
			if (d.fade >= 1.0f) {
				rb->queue_free();
				global_stats.faded.increment();
				debris.remove_at(i);
				continue;
			}
		}
		i++;
	}
}

int FellowDestructible3D::get_debris_count() {
	int n = 0;
	for (const Debris &d : debris) {
		n += ObjectDB::get_instance(d.body) != nullptr ? 1 : 0;
	}
	return n;
}

// One pattern for every destructible of the same mesh, seed and count: computed once (now, on this thread), its meshes
// and shapes shared.
void FellowDestructible3D::_prefracture() {
	if (prefractured.is_valid()) {
		return;
	}
	const String key = _cache_key();
	HashMap<String, Ref<FellowFractureResult>>::Iterator it = cache.find(key);
	if (it) {
		prefractured = it->value;
		return;
	}
	if (!_prepare_source()) {
		return;
	}
	const uint64_t t0 = OS::get_singleton()->get_ticks_usec();
	FellowFracture::make_sites(source, piece_count, uint64_t(int64_t(seed)), Vector3(), 0.0f, sites);
	prefractured.instantiate();
	prefractured->source_surfaces = params.interior_surface;
	FellowFracture::voronoi(source, sites, params, prefractured->pieces);
	const uint64_t t1 = OS::get_singleton()->get_ticks_usec();
	prefractured->build();
	global_stats.fractures.increment();
	global_stats.fracture_usec.add(t1 - t0);
	global_stats.build_usec.add(OS::get_singleton()->get_ticks_usec() - t1);
	task_usec = t1 - t0;
	cache.insert(key, prefractured);
}

void FellowDestructible3D::set_prefracture(bool p_enabled) {
	prefracture = p_enabled;
	if (prefracture && is_inside_tree() && is_ready() && !Engine::get_singleton()->is_editor_hint()) {
		_prefracture();
	}
}

void FellowDestructible3D::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_READY: {
			current_health = health;
			if (Engine::get_singleton()->is_editor_hint()) {
				break;
			}
			set_physics_process_internal(true);
			if (RigidBody3D *rb = Object::cast_to<RigidBody3D>(_get_body())) {
				if (break_on_impact) {
					rb->set_contact_monitor(true);
					rb->set_max_contacts_reported(MAX(rb->get_max_contacts_reported(), 4));
				}
			}
			if (prefracture) {
				_prefracture();
			}
		} break;
		case NOTIFICATION_INTERNAL_PHYSICS_PROCESS: {
			if (!exceptions.is_empty()) {
				_expire_exceptions();
			}
			if (!chunks.is_empty()) {
				_check_chunks(); // milestone 123: the parts that fell, landing
				if (chunks.is_empty() && broken && structure_built) {
					_finish_if_empty(); // the last of them landed: the empty body goes
				}
			}
			if (settle_pending && structure_built && !broken) {
				// Milestone 122: a collapse going on (the last frame's settling ran out of its budget).
				Array spawned;
				last_broken = 0;
				_settle(spawned);
				last_pieces = last_fallen;
				if (!spawned.is_empty()) {
					global_stats.breaks.increment();
					global_stats.pieces.add(uint64_t(last_pieces));
					emit_signal(SNAME("broken"), spawned);
				}
				_finish_if_empty();
			}
			_check_impacts();
		} break;
		case NOTIFICATION_INTERNAL_PROCESS: {
			if (pending && task != WorkerThreadPool::INVALID_TASK_ID && WorkerThreadPool::get_singleton()->is_task_completed(task)) {
				WorkerThreadPool::get_singleton()->wait_for_task_completion(task);
				task = WorkerThreadPool::INVALID_TASK_ID;
				global_stats.fractures.increment();
				global_stats.fracture_usec.add(task_usec);
				spawn_result = result;
			}
			if (pending && spawn_result.is_valid() && _spawn_allowed(int(spawn_result->pieces.size()))) {
				// Built when it may spawn: the meshes and shapes are the frame's budget's too.
				if (!spawn_result->built) {
					const uint64_t t0 = OS::get_singleton()->get_ticks_usec();
					spawn_result->build();
					global_stats.build_usec.add(OS::get_singleton()->get_ticks_usec() - t0);
				}
				_spawn(spawn_result, pending_position, pending_impulse);
				spawn_result.unref();
				set_process_internal(false);
			}
		} break;
		case NOTIFICATION_EXIT_TREE: {
			if (task != WorkerThreadPool::INVALID_TASK_ID) {
				WorkerThreadPool::get_singleton()->wait_for_task_completion(task);
				task = WorkerThreadPool::INVALID_TASK_ID;
				pending = false;
			}
		} break;
	}
}

Dictionary FellowDestructible3D::get_stats() const {
	Dictionary d;
	d["broken"] = broken;
	d["pieces"] = last_pieces;
	d["fracture_usec"] = int64_t(task_usec);
	d["spawn_usec"] = int64_t(last_spawn_usec);
	d["health"] = current_health;
	d["last_impact"] = last_impact;
	d["prefractured"] = prefractured.is_valid();
	d["structural"] = structure_built;
	d["standing"] = get_standing_pieces();
	d["last_broken"] = last_broken;
	d["last_fallen"] = last_fallen;
	d["last_chunks"] = last_chunks;
	d["last_cracked"] = last_cracked; // milestone 122
	d["stress_usec"] = int64_t(last_stress_usec);
	d["stress_iterations"] = last_stress_iterations;
	d["stress_work"] = int64_t(stress_work); // iterations × bonds, the last settling's (a frame's at most about 100 000)
	int holding = 0;
	for (const Bond &bond : bonds) {
		holding += bond.broken ? 0 : 1;
	}
	d["bonds"] = holding;
	d["settling"] = settle_pending; // a collapse going on over the next frames
	d["cracked_total"] = cracked_total;
	d["fallen_total"] = fallen_total;
	d["last_landing_breaks"] = last_landing_breaks; // milestone 123
	d["landing_breaks_total"] = landing_breaks_total;
	d["falling_parts"] = int(chunks.size());
	return d;
}

Dictionary FellowDestructible3D::get_global_stats() {
	Dictionary d;
	d["breaks"] = int64_t(global_stats.breaks.get());
	d["fractures"] = int64_t(global_stats.fractures.get());
	d["fracture_usec"] = int64_t(global_stats.fracture_usec.get());
	d["build_usec"] = int64_t(global_stats.build_usec.get());
	d["spawn_usec"] = int64_t(global_stats.spawn_usec.get());
	d["pieces"] = int64_t(global_stats.pieces.get());
	d["faded"] = int64_t(global_stats.faded.get());
	d["debris"] = get_debris_count();
	return d;
}

void FellowDestructible3D::reset_global_stats() {
	for (SafeNumeric<uint64_t> *v : { &global_stats.breaks, &global_stats.fractures, &global_stats.fracture_usec, &global_stats.build_usec, &global_stats.spawn_usec, &global_stats.pieces, &global_stats.faded }) {
		v->set(0);
	}
}

PackedStringArray FellowDestructible3D::get_configuration_warnings() const {
	PackedStringArray warnings = Node3D::get_configuration_warnings();
	if (!is_inside_tree()) {
		return warnings;
	}
	const MeshInstance3D *mi = _get_mesh();
	if (!mi) {
		warnings.push_back(RTR("A FellowDestructible3D breaks a MeshInstance3D: its parent, one of its parent's children, or Mesh Path's. None was found."));
	} else if (mi->get_mesh().is_null()) {
		warnings.push_back(RTR("The MeshInstance3D to break has no mesh."));
	}
	if (!body_path.is_empty() && !Object::cast_to<CollisionObject3D>(get_node_or_null(body_path))) {
		warnings.push_back(vformat(RTR("Body Path (%s) leads to no CollisionObject3D."), String(body_path)));
	}
	return warnings;
}

void FellowDestructible3D::set_mesh_path(const NodePath &p_path) {
	mesh_path = p_path;
	update_configuration_warnings();
}

void FellowDestructible3D::set_body_path(const NodePath &p_path) {
	body_path = p_path;
	update_configuration_warnings();
}

void FellowDestructible3D::set_health(float p_health) {
	health = MAX(0.0f, p_health);
	current_health = health;
}

void FellowDestructible3D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("break_apart", "position", "impulse"), &FellowDestructible3D::break_apart, DEFVAL(Vector3()), DEFVAL(Vector3()));
	ClassDB::bind_method(D_METHOD("damage", "amount", "position", "impulse"), &FellowDestructible3D::damage, DEFVAL(Vector3()), DEFVAL(Vector3()));
	ClassDB::bind_method(D_METHOD("is_broken"), &FellowDestructible3D::is_broken);
	ClassDB::bind_method(D_METHOD("get_current_health"), &FellowDestructible3D::get_current_health);
	ClassDB::bind_method(D_METHOD("get_stats"), &FellowDestructible3D::get_stats);
	ClassDB::bind_static_method("FellowDestructible3D", D_METHOD("get_global_stats"), &FellowDestructible3D::get_global_stats);
	ClassDB::bind_static_method("FellowDestructible3D", D_METHOD("reset_global_stats"), &FellowDestructible3D::reset_global_stats);
	ClassDB::bind_static_method("FellowDestructible3D", D_METHOD("set_max_debris", "count"), &FellowDestructible3D::set_max_debris);
	ClassDB::bind_static_method("FellowDestructible3D", D_METHOD("get_max_debris"), &FellowDestructible3D::get_max_debris);
	ClassDB::bind_static_method("FellowDestructible3D", D_METHOD("get_debris_count"), &FellowDestructible3D::get_debris_count);
	ClassDB::bind_static_method("FellowDestructible3D", D_METHOD("set_max_pieces_per_frame", "count"), &FellowDestructible3D::set_max_pieces_per_frame);
	ClassDB::bind_static_method("FellowDestructible3D", D_METHOD("get_max_pieces_per_frame"), &FellowDestructible3D::get_max_pieces_per_frame);
	ClassDB::bind_static_method("FellowDestructible3D", D_METHOD("clear_cache"), &FellowDestructible3D::clear_cache);

	ClassDB::bind_method(D_METHOD("set_mesh_path", "path"), &FellowDestructible3D::set_mesh_path);
	ClassDB::bind_method(D_METHOD("get_mesh_path"), &FellowDestructible3D::get_mesh_path);
	ClassDB::bind_method(D_METHOD("set_body_path", "path"), &FellowDestructible3D::set_body_path);
	ClassDB::bind_method(D_METHOD("get_body_path"), &FellowDestructible3D::get_body_path);
	ClassDB::bind_method(D_METHOD("set_piece_count", "count"), &FellowDestructible3D::set_piece_count);
	ClassDB::bind_method(D_METHOD("get_piece_count"), &FellowDestructible3D::get_piece_count);
	ClassDB::bind_method(D_METHOD("set_seed", "seed"), &FellowDestructible3D::set_seed);
	ClassDB::bind_method(D_METHOD("get_seed"), &FellowDestructible3D::get_seed);
	ClassDB::bind_method(D_METHOD("set_impact_focus", "focus"), &FellowDestructible3D::set_impact_focus);
	ClassDB::bind_method(D_METHOD("get_impact_focus"), &FellowDestructible3D::get_impact_focus);
	ClassDB::bind_method(D_METHOD("set_interior_material", "material"), &FellowDestructible3D::set_interior_material);
	ClassDB::bind_method(D_METHOD("get_interior_material"), &FellowDestructible3D::get_interior_material);
	ClassDB::bind_method(D_METHOD("set_interior_uv_scale", "scale"), &FellowDestructible3D::set_interior_uv_scale);
	ClassDB::bind_method(D_METHOD("get_interior_uv_scale"), &FellowDestructible3D::get_interior_uv_scale);
	ClassDB::bind_method(D_METHOD("set_health", "health"), &FellowDestructible3D::set_health);
	ClassDB::bind_method(D_METHOD("get_health"), &FellowDestructible3D::get_health);
	ClassDB::bind_method(D_METHOD("set_break_on_impact", "enabled"), &FellowDestructible3D::set_break_on_impact);
	ClassDB::bind_method(D_METHOD("is_break_on_impact"), &FellowDestructible3D::is_break_on_impact);
	ClassDB::bind_method(D_METHOD("set_break_impulse", "impulse"), &FellowDestructible3D::set_break_impulse);
	ClassDB::bind_method(D_METHOD("get_break_impulse"), &FellowDestructible3D::get_break_impulse);
	ClassDB::bind_method(D_METHOD("set_density", "density"), &FellowDestructible3D::set_density);
	ClassDB::bind_method(D_METHOD("get_density"), &FellowDestructible3D::get_density);
	ClassDB::bind_method(D_METHOD("set_burst_speed", "speed"), &FellowDestructible3D::set_burst_speed);
	ClassDB::bind_method(D_METHOD("get_burst_speed"), &FellowDestructible3D::get_burst_speed);
	ClassDB::bind_method(D_METHOD("set_debris_lifetime", "seconds"), &FellowDestructible3D::set_debris_lifetime);
	ClassDB::bind_method(D_METHOD("get_debris_lifetime"), &FellowDestructible3D::get_debris_lifetime);
	ClassDB::bind_method(D_METHOD("set_debris_floats", "enabled"), &FellowDestructible3D::set_debris_floats);
	ClassDB::bind_method(D_METHOD("is_debris_floats"), &FellowDestructible3D::is_debris_floats);
	ClassDB::bind_method(D_METHOD("set_prefracture", "enabled"), &FellowDestructible3D::set_prefracture);
	ClassDB::bind_method(D_METHOD("is_prefracture"), &FellowDestructible3D::is_prefracture);
	ClassDB::bind_method(D_METHOD("set_structural", "enabled"), &FellowDestructible3D::set_structural);
	ClassDB::bind_method(D_METHOD("is_structural"), &FellowDestructible3D::is_structural);
	ClassDB::bind_method(D_METHOD("set_break_radius", "radius"), &FellowDestructible3D::set_break_radius);
	ClassDB::bind_method(D_METHOD("get_break_radius"), &FellowDestructible3D::get_break_radius);
	ClassDB::bind_method(D_METHOD("set_anchor_height", "height"), &FellowDestructible3D::set_anchor_height);
	ClassDB::bind_method(D_METHOD("get_anchor_height"), &FellowDestructible3D::get_anchor_height);
	ClassDB::bind_method(D_METHOD("get_standing_pieces"), &FellowDestructible3D::get_standing_pieces);
	ClassDB::bind_method(D_METHOD("get_structure"), &FellowDestructible3D::get_structure);
	ClassDB::bind_method(D_METHOD("set_strength", "kpa"), &FellowDestructible3D::set_strength);
	ClassDB::bind_method(D_METHOD("get_strength"), &FellowDestructible3D::get_strength);
	ClassDB::bind_method(D_METHOD("update_structure"), &FellowDestructible3D::update_structure);
	ClassDB::bind_method(D_METHOD("set_anchor_mode", "mode"), &FellowDestructible3D::set_anchor_mode);
	ClassDB::bind_method(D_METHOD("set_landing_break_speed", "speed"), &FellowDestructible3D::set_landing_break_speed);
	ClassDB::bind_method(D_METHOD("get_landing_break_speed"), &FellowDestructible3D::get_landing_break_speed);
	ClassDB::bind_method(D_METHOD("get_anchor_mode"), &FellowDestructible3D::get_anchor_mode);
	ClassDB::bind_method(D_METHOD("get_bonds"), &FellowDestructible3D::get_bonds);
	ClassDB::bind_method(D_METHOD("add_load", "position", "force"), &FellowDestructible3D::add_load);
	ClassDB::bind_method(D_METHOD("clear_loads"), &FellowDestructible3D::clear_loads);
	ClassDB::bind_method(D_METHOD("set_free_after_break", "enabled"), &FellowDestructible3D::set_free_after_break);
	ClassDB::bind_method(D_METHOD("is_free_after_break"), &FellowDestructible3D::is_free_after_break);

	ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "mesh_path", PROPERTY_HINT_NODE_PATH_VALID_TYPES, "MeshInstance3D"), "set_mesh_path", "get_mesh_path");
	ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "body_path", PROPERTY_HINT_NODE_PATH_VALID_TYPES, "CollisionObject3D"), "set_body_path", "get_body_path");
	ADD_GROUP("Fracture", "");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "piece_count", PROPERTY_HINT_RANGE, "2,128,1,or_greater"), "set_piece_count", "get_piece_count");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "seed"), "set_seed", "get_seed");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "impact_focus", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_impact_focus", "get_impact_focus");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "interior_material", PROPERTY_HINT_RESOURCE_TYPE, "BaseMaterial3D,ShaderMaterial"), "set_interior_material", "get_interior_material");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "interior_uv_scale", PROPERTY_HINT_RANGE, "0.01,10,0.01,or_greater"), "set_interior_uv_scale", "get_interior_uv_scale");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "prefracture"), "set_prefracture", "is_prefracture");
	ADD_GROUP("Breaking", "");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "health", PROPERTY_HINT_RANGE, "0,1000,0.1,or_greater"), "set_health", "get_health");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "break_on_impact"), "set_break_on_impact", "is_break_on_impact");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "break_impulse", PROPERTY_HINT_RANGE, "0,1000,0.1,or_greater,suffix:N·s"), "set_break_impulse", "get_break_impulse");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "free_after_break"), "set_free_after_break", "is_free_after_break");
	ADD_GROUP("Structure", "");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "structural"), "set_structural", "is_structural");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "break_radius", PROPERTY_HINT_RANGE, "0,10,0.01,or_greater,suffix:m"), "set_break_radius", "get_break_radius");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "anchor_mode", PROPERTY_HINT_ENUM, "Lowest Point,Static Bodies"), "set_anchor_mode", "get_anchor_mode");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "anchor_height", PROPERTY_HINT_RANGE, "0,5,0.01,or_greater,suffix:m"), "set_anchor_height", "get_anchor_height");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "strength", PROPERTY_HINT_RANGE, "0,100000,0.1,or_greater,suffix:kPa"), "set_strength", "get_strength");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "landing_break_speed", PROPERTY_HINT_RANGE, "0,50,0.1,or_greater,suffix:m/s"), "set_landing_break_speed", "get_landing_break_speed");
	ADD_GROUP("Pieces", "");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "density", PROPERTY_HINT_RANGE, "1,10000,1,or_greater,suffix:kg/m³"), "set_density", "get_density");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "burst_speed", PROPERTY_HINT_RANGE, "0,20,0.01,or_greater,suffix:m/s"), "set_burst_speed", "get_burst_speed");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "debris_lifetime", PROPERTY_HINT_RANGE, "0,600,0.1,or_greater,suffix:s"), "set_debris_lifetime", "get_debris_lifetime");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "debris_floats"), "set_debris_floats", "is_debris_floats");

	ADD_SIGNAL(MethodInfo("broken", PropertyInfo(Variant::ARRAY, "pieces")));

	BIND_ENUM_CONSTANT(ANCHOR_LOWEST_POINT);
	BIND_ENUM_CONSTANT(ANCHOR_STATIC_BODIES);
	ADD_SIGNAL(MethodInfo("damaged", PropertyInfo(Variant::FLOAT, "amount"), PropertyInfo(Variant::FLOAT, "health")));
}
