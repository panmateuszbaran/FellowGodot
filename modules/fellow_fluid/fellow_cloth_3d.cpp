/**************************************************************************/
/*  fellow_cloth_3d.cpp                                                   */
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

#include "fellow_cloth_3d.h"

#include "fellow_editor_still.h"
#include "fellow_wind_3d.h"

#include "core/config/engine.h"
#include "core/config/project_settings.h"
#include "core/object/class_db.h"
#include "core/object/worker_thread_pool.h"
#include "core/os/os.h"
#include "core/templates/hash_map.h"
#include "scene/3d/camera_3d.h"
#include "scene/3d/physics/collision_object_3d.h"
#include "scene/3d/physics/collision_shape_3d.h"
#include "scene/3d/skeleton_3d.h"
#include "scene/main/viewport.h"
#include "scene/resources/3d/world_3d.h"
#include "servers/physics_3d/physics_server_3d.h"
#include "servers/rendering/rendering_server.h"

#include <limits>

FellowCloth3D::GlobalStats FellowCloth3D::global_stats;
LocalVector<FellowCloth3D *> FellowCloth3D::registry;
uint64_t FellowCloth3D::group_frame = UINT64_MAX;

namespace {

const float AIR_DENSITY = 1.2f; // kg/m³

uint64_t edge_key(uint32_t p_a, uint32_t p_b) {
	return p_a < p_b ? (uint64_t(p_a) << 32) | p_b : (uint64_t(p_b) << 32) | p_a;
}

// A minimal binary heap of (distance, particle) for the tethers' shortest paths.
struct HeapItem {
	float d;
	uint32_t i;
};

void heap_push(LocalVector<HeapItem> &r_heap, const HeapItem &p_item) {
	r_heap.push_back(p_item);
	uint32_t c = r_heap.size() - 1;
	while (c > 0) {
		const uint32_t p = (c - 1) / 2;
		if (r_heap[p].d <= r_heap[c].d) {
			break;
		}
		SWAP(r_heap[p], r_heap[c]);
		c = p;
	}
}

HeapItem heap_pop(LocalVector<HeapItem> &r_heap) {
	const HeapItem top = r_heap[0];
	r_heap[0] = r_heap[r_heap.size() - 1];
	r_heap.resize(r_heap.size() - 1);
	uint32_t c = 0;
	while (true) {
		const uint32_t l = c * 2 + 1;
		const uint32_t r = l + 1;
		uint32_t m = c;
		if (l < r_heap.size() && r_heap[l].d < r_heap[m].d) {
			m = l;
		}
		if (r < r_heap.size() && r_heap[r].d < r_heap[m].d) {
			m = r;
		}
		if (m == c) {
			break;
		}
		SWAP(r_heap[m], r_heap[c]);
		c = m;
	}
	return top;
}

uint16_t unorm16(float p_v) {
	return uint16_t(CLAMP(p_v * 65535.0f, 0.0f, 65535.0f));
}

} // namespace

FellowCloth3D::FellowCloth3D() {
	set_process_priority(100); // after the game's own processing has moved what the cloth hangs from
}

FellowCloth3D::~FellowCloth3D() {
	_clear();
	if (query_shape.is_valid() && PhysicsServer3D::get_singleton()) {
		PhysicsServer3D::get_singleton()->free_rid(query_shape);
	}
}

void FellowCloth3D::_clear() {
	if (mesh.is_valid() && RenderingServer::get_singleton()) {
		set_base(RID());
		RenderingServer::get_singleton()->free_rid(mesh);
	}
	mesh = RID();
	built = false;
}

void FellowCloth3D::_make_grid(PackedVector3Array &r_vertices, PackedVector3Array &r_normals, PackedVector2Array &r_uvs, PackedInt32Array &r_indices, PackedColorArray &r_colors) const {
	const float longest = MAX(MAX(size.x, size.y), 0.001f);
	const int nx = MAX(2, int(Math::round(resolution * size.x / longest)) + 1);
	const int ny = MAX(2, int(Math::round(resolution * size.y / longest)) + 1);
	r_vertices.resize(nx * ny);
	r_normals.resize(nx * ny);
	r_uvs.resize(nx * ny);
	r_colors.resize(nx * ny);
	for (int j = 0; j < ny; j++) {
		for (int i = 0; i < nx; i++) {
			const float u = float(i) / float(nx - 1);
			const float v = float(j) / float(ny - 1);
			r_vertices.set(j * nx + i, Vector3(u * size.x, -v * size.y, 0.0f));
			r_normals.set(j * nx + i, Vector3(0, 0, 1));
			r_uvs.set(j * nx + i, Vector2(u, v));
			r_colors.set(j * nx + i, Color(0, 0, 0));
		}
	}
	// Godot's front faces are clockwise; the diagonals alternate (no direction preferred).
	r_indices.resize((nx - 1) * (ny - 1) * 6);
	int k = 0;
	for (int j = 0; j < ny - 1; j++) {
		for (int i = 0; i < nx - 1; i++) {
			const int a = j * nx + i;
			const int b = a + 1;
			const int c = a + nx;
			const int d = c + 1;
			const int t[6] = { a, b, c, b, d, c };
			const int t2[6] = { a, b, d, a, d, c };
			const int *s = ((i + j) & 1) ? t2 : t;
			for (int m = 0; m < 6; m++) {
				r_indices.set(k++, s[m]);
			}
		}
	}
}

void FellowCloth3D::_build() {
	_clear();
	build_dirty = false;
	build_problem = String();
	rest_local.clear();
	stretch.clear();
	bend.clear();
	tethers.clear();
	tris.clear();
	render_particle.clear();
	render_rest_normal.clear();
	render_rest_tangent.clear();
	tris_all.clear();
	tri_edges.clear();
	edge_tri_start.clear();
	edge_tri_list.clear();
	bend_tris.clear();
	tri_index.clear();
	tri_alive.clear();
	edge_alive.clear();
	loose.clear();
	stretch_source.clear();
	tethers_rest.clear();
	index_bytes.clear();
	index_bytes_rest.clear();
	index_dirty = false;
	any_loose = false;
	torn_edges = 0;
	torn_pending = 0;
	torn_pending_sum = Vector3();

	PackedVector3Array vertices;
	PackedVector3Array normals;
	PackedFloat32Array tangents;
	PackedVector2Array uvs;
	PackedColorArray colors;
	PackedInt32Array indices;
	PackedInt32Array mesh_bones; // decision 0230
	PackedFloat32Array mesh_weights;
	if (source_mesh.is_valid()) {
		if (source_mesh->get_surface_count() == 0 || source_mesh->surface_get_primitive_type(0) != Mesh::PRIMITIVE_TRIANGLES) {
			build_problem = RTR("The source mesh has no surface of triangles.");
			return;
		}
		const Array arrays = source_mesh->surface_get_arrays(0);
		vertices = arrays[Mesh::ARRAY_VERTEX];
		normals = arrays[Mesh::ARRAY_NORMAL];
		tangents = arrays[Mesh::ARRAY_TANGENT];
		uvs = arrays[Mesh::ARRAY_TEX_UV];
		colors = arrays[Mesh::ARRAY_COLOR];
		indices = arrays[Mesh::ARRAY_INDEX];
		if (arrays[Mesh::ARRAY_BONES].get_type() == Variant::PACKED_INT32_ARRAY && arrays[Mesh::ARRAY_WEIGHTS].get_type() == Variant::PACKED_FLOAT32_ARRAY) {
			mesh_bones = arrays[Mesh::ARRAY_BONES];
			mesh_weights = arrays[Mesh::ARRAY_WEIGHTS];
		}
		if (indices.is_empty()) {
			indices.resize(vertices.size());
			for (int i = 0; i < vertices.size(); i++) {
				indices.set(i, i);
			}
		}
		if (pin_mode == PIN_VERTEX_COLOR && colors.size() != vertices.size()) {
			build_problem = RTR("Pin Mode is Vertex Color, but the source mesh has no vertex colors: nothing is pinned.");
		}
	} else {
		_make_grid(vertices, normals, uvs, indices, colors);
	}
	const int nv = vertices.size();
	if (nv < 3 || indices.size() < 3) {
		build_problem = RTR("The cloth has no triangles.");
		return;
	}
	has_tangents = tangents.size() == nv * 4;

	// Particles: the vertices welded by position (a mesh's seams of UVs or normals are one particle).
	HashMap<Vector3i, uint32_t> weld;
	render_particle.resize(nv);
	for (int i = 0; i < nv; i++) {
		const Vector3 p = vertices[i];
		const Vector3i key(int(Math::round(p.x * 1e5f)), int(Math::round(p.y * 1e5f)), int(Math::round(p.z * 1e5f)));
		HashMap<Vector3i, uint32_t>::Iterator it = weld.find(key);
		if (it) {
			render_particle[i] = it->value;
		} else {
			const uint32_t id = rest_local.size();
			weld.insert(key, id);
			rest_local.push_back(p);
			render_particle[i] = id;
		}
	}
	// Decision 0230: the source mesh's skin per particle — its first vertex's binds and weights (a seam's vertices share
	// them), the heaviest four of eight, normalized.
	skin_binds.clear();
	skin_weights.clear();
	skin_data = false;
	{
		const int per = mesh_bones.size() == nv * 8 && mesh_weights.size() == nv * 8 ? 8 : (mesh_bones.size() == nv * 4 && mesh_weights.size() == nv * 4 ? 4 : 0);
		if (per > 0) {
			const uint32_t npart = rest_local.size();
			skin_binds.resize(npart * 4);
			skin_weights.resize(npart * 4);
			LocalVector<uint8_t> set;
			set.resize(npart);
			for (uint32_t i = 0; i < npart; i++) {
				set[i] = 0;
			}
			for (int v = 0; v < nv; v++) {
				const uint32_t pi = render_particle[v];
				if (set[pi]) {
					continue;
				}
				set[pi] = 1;
				int32_t b[8];
				float wt[8];
				for (int k = 0; k < per; k++) {
					b[k] = mesh_bones[v * per + k];
					wt[k] = mesh_weights[v * per + k];
				}
				// The heaviest four first (a few elements: insertion sort).
				for (int k = 1; k < per; k++) {
					for (int m = k; m > 0 && wt[m] > wt[m - 1]; m--) {
						SWAP(wt[m], wt[m - 1]);
						SWAP(b[m], b[m - 1]);
					}
				}
				float sum = 0.0f;
				for (int k = 0; k < 4; k++) {
					sum += MAX(0.0f, wt[k]);
				}
				for (int k = 0; k < 4; k++) {
					skin_binds[pi * 4 + k] = b[k];
					skin_weights[pi * 4 + k] = sum > 0.0f ? MAX(0.0f, wt[k]) / sum : (k == 0 ? 1.0f : 0.0f);
				}
			}
			skin_data = true;
		}
	}
	const uint32_t np = rest_local.size();
	particle_mass.resize(np);
	for (uint32_t i = 0; i < np; i++) {
		particle_mass[i] = 0.0f;
	}
	// Triangles, edges, the bending pairs (the vertices opposite an edge two triangles share).
	HashMap<uint64_t, LocalVector<uint32_t>> edge_opposite;
	HashMap<uint64_t, LocalVector<uint32_t>> edge_tri; // the triangles on each edge (tearing)
	for (int t = 0; t + 2 < indices.size(); t += 3) {
		const uint32_t a = render_particle[indices[t]];
		const uint32_t b = render_particle[indices[t + 1]];
		const uint32_t c = render_particle[indices[t + 2]];
		if (a == b || b == c || a == c) {
			continue;
		}
		const uint32_t tri_id = tris.size() / 3;
		tri_index.push_back(uint32_t(t));
		tris.push_back(a);
		tris.push_back(b);
		tris.push_back(c);
		const float area = (rest_local[b] - rest_local[a]).cross(rest_local[c] - rest_local[a]).length() * 0.5f;
		particle_mass[a] += area * mass / 3.0f;
		particle_mass[b] += area * mass / 3.0f;
		particle_mass[c] += area * mass / 3.0f;
		const uint32_t e[3][3] = { { a, b, c }, { b, c, a }, { c, a, b } };
		for (int k = 0; k < 3; k++) {
			edge_opposite[edge_key(e[k][0], e[k][1])].push_back(e[k][2]);
			edge_tri[edge_key(e[k][0], e[k][1])].push_back(tri_id);
		}
	}
	if (tris.is_empty()) {
		build_problem = RTR("The cloth has no triangles.");
		return;
	}
	LocalVector<LocalVector<Pair<uint32_t, float>>> adjacency;
	adjacency.resize(np);
	// The bending pairs with the edge they bend across (its two triangles: tearing), sorted with it.
	struct BendKeyed {
		Edge e;
		uint64_t key;
	};
	LocalVector<BendKeyed> bend_keyed;
	for (const KeyValue<uint64_t, LocalVector<uint32_t>> &kv : edge_opposite) {
		const uint32_t a = uint32_t(kv.key >> 32);
		const uint32_t b = uint32_t(kv.key & 0xffffffffu);
		const float rest = rest_local[a].distance_to(rest_local[b]);
		stretch.push_back({ a, b, rest });
		adjacency[a].push_back(Pair<uint32_t, float>(b, rest));
		adjacency[b].push_back(Pair<uint32_t, float>(a, rest));
		if (kv.value.size() == 2 && kv.value[0] != kv.value[1]) {
			bend_keyed.push_back({ { kv.value[0], kv.value[1], float(rest_local[kv.value[0]].distance_to(rest_local[kv.value[1]])) }, kv.key });
		}
	}
	// A stable order (the hash map's is not): the solver is Gauss-Seidel.
	struct EdgeSort {
		bool operator()(const Edge &p_a, const Edge &p_b) const { return p_a.a != p_b.a ? p_a.a < p_b.a : p_a.b < p_b.b; }
	};
	struct BendKeyedSort {
		bool operator()(const BendKeyed &p_a, const BendKeyed &p_b) const { return p_a.e.a != p_b.e.a ? p_a.e.a < p_b.e.a : p_a.e.b < p_b.e.b; }
	};
	stretch.sort_custom<EdgeSort>();
	bend_keyed.sort_custom<BendKeyedSort>(); // the same comparisons as the pairs' alone: the same order
	for (const BendKeyed &bk : bend_keyed) {
		bend.push_back(bk.e);
		const LocalVector<uint32_t> &bt = edge_tri[bk.key];
		bend_tris.push_back(bt[0]);
		bend_tris.push_back(bt[1]);
	}
	// Each edge's triangles and each triangle's edges.
	{
		HashMap<uint64_t, uint32_t> edge_index;
		edge_tri_start.resize(stretch.size() + 1);
		for (uint32_t i = 0; i < stretch.size(); i++) {
			const uint64_t key = edge_key(stretch[i].a, stretch[i].b);
			edge_index.insert(key, i);
			edge_tri_start[i] = edge_tri_list.size();
			for (uint32_t t : edge_tri[key]) {
				edge_tri_list.push_back(t);
			}
		}
		edge_tri_start[stretch.size()] = edge_tri_list.size();
		tri_edges.resize(tris.size());
		for (uint32_t t = 0; t < tris.size(); t += 3) {
			for (uint32_t k = 0; k < 3; k++) {
				tri_edges[t + k] = edge_index[edge_key(tris[t + k], tris[t + (k + 1) % 3])];
			}
		}
	}

	// Pins.
	LocalVector<uint8_t> pinned;
	pinned.resize(np);
	for (uint32_t i = 0; i < np; i++) {
		pinned[i] = 0;
	}
	AABB rest_box(rest_local[0], Vector3());
	for (uint32_t i = 1; i < np; i++) {
		rest_box.expand_to(rest_local[i]);
	}
	const float eps = MAX(rest_box.get_longest_axis_size() * 1e-3f, 1e-5f);
	if (pin_mode == PIN_VERTEX_COLOR) {
		if (colors.size() == nv) {
			for (int i = 0; i < nv; i++) {
				if (colors[i].r >= 0.5f) {
					pinned[render_particle[i]] = 1;
				}
			}
		}
	} else if (pin_mode == PIN_TOP_EDGE || pin_mode == PIN_LEFT_EDGE) {
		for (uint32_t i = 0; i < np; i++) {
			const Vector3 &p = rest_local[i];
			if (pin_mode == PIN_TOP_EDGE ? p.y >= rest_box.position.y + rest_box.size.y - eps : p.x <= rest_box.position.x + eps) {
				pinned[i] = 1;
			}
		}
	} else if (pin_mode == PIN_TOP_CORNERS || pin_mode == PIN_CORNERS) {
		const float top = rest_box.position.y + rest_box.size.y;
		const float bottom = rest_box.position.y;
		const float left = rest_box.position.x;
		const float right = rest_box.position.x + rest_box.size.x;
		LocalVector<Vector2> corners;
		corners.push_back(Vector2(left, top));
		corners.push_back(Vector2(right, top));
		if (pin_mode == PIN_CORNERS) {
			corners.push_back(Vector2(left, bottom));
			corners.push_back(Vector2(right, bottom));
		}
		for (const Vector2 &c : corners) {
			uint32_t best = 0;
			float best_d = std::numeric_limits<float>::max();
			for (uint32_t i = 0; i < np; i++) {
				const float d = Vector2(rest_local[i].x, rest_local[i].y).distance_squared_to(c);
				if (d < best_d) {
					best_d = d;
					best = i;
				}
			}
			pinned[best] = 1;
		}
	}
	w.resize(np);
	for (uint32_t i = 0; i < np; i++) {
		particle_mass[i] = MAX(particle_mass[i], 1e-6f);
		w[i] = pinned[i] ? 0.0f : 1.0f / particle_mass[i];
	}

	stretch_all = stretch;
	bend_all = bend;
	tris_all = tris;
	mean_edge = 0.0f;
	for (const Edge &e : stretch_all) {
		mean_edge += e.rest;
	}
	mean_edge = stretch_all.is_empty() ? 0.0f : mean_edge / float(stretch_all.size());
	tri_alive.resize(tris.size() / 3);
	for (uint8_t &a : tri_alive) {
		a = 1;
	}
	edge_alive.resize(stretch_all.size());
	for (uint8_t &a : edge_alive) {
		a = 1;
	}
	loose.resize(np);
	for (uint8_t &l : loose) {
		l = 0;
	}
	_filter_constraints();

	// Tethers: each free particle no farther from its nearest pin (along the cloth) than the path at rest, with slack.
	if (tether_slack > 0.0f) {
		LocalVector<float> dist;
		LocalVector<uint32_t> source;
		dist.resize(np);
		source.resize(np);
		LocalVector<HeapItem> heap;
		for (uint32_t i = 0; i < np; i++) {
			dist[i] = pinned[i] ? 0.0f : std::numeric_limits<float>::max();
			source[i] = i;
			if (pinned[i]) {
				heap_push(heap, { 0.0f, i });
			}
		}
		while (!heap.is_empty()) {
			const HeapItem it = heap_pop(heap);
			if (it.d > dist[it.i]) {
				continue;
			}
			for (const Pair<uint32_t, float> &nb : adjacency[it.i]) {
				const float d = it.d + nb.second;
				if (d < dist[nb.first]) {
					dist[nb.first] = d;
					source[nb.first] = source[it.i];
					heap_push(heap, { d, nb.first });
				}
			}
		}
		for (uint32_t i = 0; i < np; i++) {
			if (!pinned[i] && dist[i] < std::numeric_limits<float>::max()) {
				tethers.push_back({ i, source[i], dist[i] });
			}
		}
	}
	tethers_rest = tethers;

	// Normals at rest (node space) and which way the triangles' winding faces against the mesh's normals.
	particle_rest_normal.resize(np);
	for (uint32_t i = 0; i < np; i++) {
		particle_rest_normal[i] = Vector3();
	}
	for (uint32_t t = 0; t < tris.size(); t += 3) {
		const Vector3 n = (rest_local[tris[t + 1]] - rest_local[tris[t]]).cross(rest_local[tris[t + 2]] - rest_local[tris[t]]);
		particle_rest_normal[tris[t]] += n;
		particle_rest_normal[tris[t + 1]] += n;
		particle_rest_normal[tris[t + 2]] += n;
	}
	float agree = 0.0f;
	if (normals.size() == nv) {
		for (int i = 0; i < nv; i++) {
			agree += particle_rest_normal[render_particle[i]].dot(normals[i]);
		}
	}
	normal_sign = agree < 0.0f ? -1.0f : 1.0f;
	for (uint32_t i = 0; i < np; i++) {
		const Vector3 n = particle_rest_normal[i] * normal_sign;
		particle_rest_normal[i] = n.length() > 1e-12f ? n.normalized() : Vector3(0, 0, 1);
	}
	render_rest_normal.resize(nv);
	render_rest_tangent.resize(nv);
	PackedFloat32Array tangent_out;
	tangent_out.resize(nv * 4);
	PackedVector3Array normal_out;
	normal_out.resize(nv);
	for (int i = 0; i < nv; i++) {
		const Vector3 n = normals.size() == nv ? Vector3(normals[i]).normalized() : particle_rest_normal[render_particle[i]];
		render_rest_normal[i] = n;
		normal_out.set(i, n);
		Vector4 t;
		if (has_tangents) {
			t = Vector4(tangents[i * 4], tangents[i * 4 + 1], tangents[i * 4 + 2], tangents[i * 4 + 3]);
		} else {
			// Along U where the cloth has its UVs from the grid; otherwise any direction in the surface.
			Vector3 tv = Vector3(1, 0, 0) - n * n.x;
			if (tv.length() < 1e-4f) {
				tv = n.get_any_perpendicular();
			}
			tv.normalize();
			t = Vector4(tv.x, tv.y, tv.z, 1.0f);
		}
		render_rest_tangent[i] = t;
		tangent_out.set(i * 4, t.x);
		tangent_out.set(i * 4 + 1, t.y);
		tangent_out.set(i * 4 + 2, t.z);
		tangent_out.set(i * 4 + 3, t.w);
	}

	// The mesh: one blend shape (no offset), so that each instance has its vertices in buffers of its own, swapped
	// each frame — the previous frame's positions are the motion vectors' (TAA, FSR).
	RenderingServer *rs = RenderingServer::get_singleton();
	Array arrays;
	arrays.resize(RSE::ARRAY_MAX);
	arrays[RSE::ARRAY_VERTEX] = vertices;
	arrays[RSE::ARRAY_NORMAL] = normal_out;
	arrays[RSE::ARRAY_TANGENT] = tangent_out;
	if (uvs.size() == nv) {
		arrays[RSE::ARRAY_TEX_UV] = uvs;
	}
	arrays[RSE::ARRAY_INDEX] = indices;
	Array shape;
	shape.resize(RSE::ARRAY_MAX);
	shape[RSE::ARRAY_VERTEX] = vertices;
	shape[RSE::ARRAY_NORMAL] = normal_out;
	shape[RSE::ARRAY_TANGENT] = tangent_out;
	Array shapes;
	shapes.push_back(shape);
	mesh = rs->mesh_create();
	rs->mesh_set_blend_shape_count(mesh, 1);
	rs->mesh_add_surface_from_arrays(mesh, RSE::PRIMITIVE_TRIANGLES, arrays, shapes);
	if (material.is_null()) {
		Ref<StandardMaterial3D> m;
		m.instantiate();
		m->set_cull_mode(BaseMaterial3D::CULL_DISABLED);
		m->set_albedo(Color(0.82f, 0.8f, 0.76f));
		m->set_roughness(0.9f);
		default_material = m;
	} else {
		default_material.unref();
	}
	rs->mesh_surface_set_material(mesh, 0, material.is_valid() ? material->get_rid() : default_material->get_rid());
	const RenderingServerTypes::SurfaceData sd = rs->mesh_get_surface(mesh, 0);
	surface_format = sd.format;
	vertex_stride = rs->mesh_surface_get_format_vertex_stride(surface_format, nv);
	normal_stride = rs->mesh_surface_get_format_normal_tangent_stride(surface_format, nv);
	normal_offset = rs->mesh_surface_get_format_offset(surface_format, nv, RSE::ARRAY_NORMAL);
	vertex_bytes = sd.vertex_data;
	if (vertex_stride != 12 || normal_stride != 8 || uint32_t(vertex_bytes.size()) != uint32_t(nv) * 20u) {
		build_problem = vformat("Unexpected vertex layout (%d, %d, %d bytes).", vertex_stride, normal_stride, vertex_bytes.size());
		_clear();
		return;
	}
	index_bytes_rest = sd.index_data;
	index_bytes = sd.index_data;
	index_size = sd.index_count > 0 ? uint32_t(sd.index_data.size()) / sd.index_count : 2;
	local_bounds = sd.aabb;
	set_base(mesh);
	built = true;
	_init_particles();
	update_gizmos();
}

// The constraints between two pinned particles do nothing: left out of the solver's lists (again when a pin changes).
void FellowCloth3D::_filter_constraints() {
	// Torn edges, and the bending pairs across a torn triangle, are left out too (decision 0220).
	const bool edges_known = edge_alive.size() == stretch_all.size();
	const bool bends_known = bend_tris.size() == bend_all.size() * 2 && !tri_alive.is_empty();
	stretch_source.clear();
	for (int k = 0; k < 2; k++) {
		LocalVector<Edge> &out = k == 0 ? stretch : bend;
		const LocalVector<Edge> &in = k == 0 ? stretch_all : bend_all;
		out.clear();
		for (uint32_t i = 0; i < in.size(); i++) {
			if (k == 0 ? (edges_known && !edge_alive[i]) : (bends_known && (!tri_alive[bend_tris[i * 2]] || !tri_alive[bend_tris[i * 2 + 1]]))) {
				continue;
			}
			Edge e = in[i];
			const float ws = w[e.a] + w[e.b];
			if (ws > 0.0f) {
				e.fa = w[e.a] / ws;
				e.fb = w[e.b] / ws;
				out.push_back(e);
				if (k == 0) {
					stretch_source.push_back(i);
				}
			}
		}
	}
}

void FellowCloth3D::_init_particles() {
	const uint32_t np = rest_local.size();
	const Transform3D xf = is_inside_tree() ? get_global_transform() : get_transform();
	x.resize(np);
	prev.resize(np);
	vel.resize(np);
	acc.resize(np);
	normal.resize(np);
	for (uint32_t i = 0; i < np; i++) {
		x[i] = xf.xform(rest_local[i]);
		prev[i] = x[i];
		vel[i] = Vector3();
		acc[i] = Vector3();
		normal[i] = xf.basis.xform(particle_rest_normal[i]).normalized();
	}
	bounds = AABB(x.is_empty() ? Vector3() : x[0], Vector3());
	for (const Vector3 &p : x) {
		bounds.expand_to(p);
	}
	xf_prev = xf;
	xf_cur = xf;
	have_prev = true;
	step_acc = 0.0;
	_bind_bones();
}

// Milestone 76: the skeleton the pinned particles follow (skeleton_path), or none.
Skeleton3D *FellowCloth3D::_get_skeleton() const {
	if (skeleton_path.is_empty() || !is_inside_tree()) {
		return nullptr;
	}
	return Object::cast_to<Skeleton3D>(get_node_or_null(skeleton_path));
}

// Each pinned particle bound to the nearest of the allowed bones (the segment from the bone to its first child, as the
// skeleton is posed now), its place kept in that bone's space; without a skeleton they follow the node.
void FellowCloth3D::_bind_bones() {
	const uint32_t np = x.size();
	pin_bone.resize(np);
	pin_offset.resize(np);
	pin_from.resize(np);
	pin_to.resize(np);
	bone_pins = 0;
	bound_skeleton = ObjectID();
	skin_active = false;
	for (uint32_t i = 0; i < np; i++) {
		pin_bone[i] = -1;
	}
	Skeleton3D *sk = _get_skeleton();
	if (!sk || sk->get_bone_count() == 0) {
		return;
	}
	bound_skeleton = sk->get_instance_id();
	if (follow_skin && skin_data && _bind_skin(sk)) {
		// Decision 0230: every particle starts at its skinned place; the pinned ones follow it (pin_bone 0: on the pins'
		// interpolation, their places given by the skin).
		for (uint32_t i = 0; i < np; i++) {
			x[i] = skin_to[i];
			prev[i] = skin_to[i];
			vel[i] = Vector3();
			if (w[i] > 0.0f) {
				continue;
			}
			pin_bone[i] = 0;
			pin_from[i] = skin_to[i];
			pin_to[i] = skin_to[i];
			bone_pins++;
		}
		return;
	}
	const Transform3D skx = sk->get_global_transform();
	LocalVector<int> bones;
	for (int b = 0; b < sk->get_bone_count(); b++) {
		if (pin_bones.is_empty() || pin_bones.has(sk->get_bone_name(b))) {
			bones.push_back(b);
		}
	}
	if (bones.is_empty()) {
		return;
	}
	LocalVector<Transform3D> bone_xf;
	LocalVector<Vector3> seg_a;
	LocalVector<Vector3> seg_b;
	for (int b : bones) {
		const Transform3D gx = skx * sk->get_bone_global_pose(b);
		bone_xf.push_back(gx);
		seg_a.push_back(gx.origin);
		const Vector<int> children = sk->get_bone_children(b);
		seg_b.push_back(children.is_empty() ? gx.origin : (skx * sk->get_bone_global_pose(children[0])).origin);
	}
	for (uint32_t i = 0; i < np; i++) {
		if (w[i] > 0.0f) {
			continue;
		}
		const Vector3 p = x[i];
		float best = std::numeric_limits<float>::max();
		uint32_t best_k = 0;
		for (uint32_t k = 0; k < bones.size(); k++) {
			const Vector3 ab = seg_b[k] - seg_a[k];
			const float l2 = ab.length_squared();
			const float t = l2 > 1e-12f ? CLAMP((p - seg_a[k]).dot(ab) / l2, 0.0f, 1.0f) : 0.0f;
			const float d = p.distance_squared_to(seg_a[k] + ab * t);
			if (d < best) {
				best = d;
				best_k = k;
			}
		}
		pin_bone[i] = bones[best_k];
		pin_offset[i] = bone_xf[best_k].affine_inverse().xform(p);
		pin_from[i] = p;
		pin_to[i] = p;
		bone_pins++;
	}
}

// Decision 0230: the skin's binds mapped to the skeleton's bones (by the Skin's bones or names, or bone i with the
// inverse of its global rest without a Skin), and each particle's skinned place now. False when a bind's bone is not
// there.
bool FellowCloth3D::_bind_skin(Skeleton3D *p_skeleton) {
	skin_bind_bone.clear();
	skin_bind_pose.clear();
	int binds = 0;
	for (int32_t b : skin_binds) {
		binds = MAX(binds, b + 1);
	}
	if (skin.is_valid()) {
		if (skin->get_bind_count() < binds) {
			return false;
		}
		for (int j = 0; j < skin->get_bind_count(); j++) {
			int bone = skin->get_bind_bone(j);
			if (skin->get_bind_name(j) != StringName()) {
				bone = p_skeleton->find_bone(skin->get_bind_name(j));
			}
			if (bone < 0 || bone >= p_skeleton->get_bone_count()) {
				if (j < binds) {
					return false;
				}
				bone = 0;
			}
			skin_bind_bone.push_back(bone);
			skin_bind_pose.push_back(skin->get_bind_pose(j));
		}
	} else {
		if (p_skeleton->get_bone_count() < binds) {
			return false;
		}
		for (int j = 0; j < p_skeleton->get_bone_count(); j++) {
			skin_bind_bone.push_back(j);
			skin_bind_pose.push_back(p_skeleton->get_bone_global_rest(j).affine_inverse());
		}
	}
	const uint32_t np = rest_local.size();
	skin_from.resize(np);
	skin_to.resize(np);
	skin_active = true;
	_skin_places(p_skeleton);
	for (uint32_t i = 0; i < np; i++) {
		skin_from[i] = skin_to[i];
	}
	return true;
}

// Decision 0230: each particle's skinned place this frame (main thread; linear blend skinning of its rest place, the
// source mesh being in the skeleton's space): the last one kept as where it was.
void FellowCloth3D::_skin_places(Skeleton3D *p_skeleton) {
	const Transform3D skx = p_skeleton->get_global_transform();
	const uint32_t nb = skin_bind_bone.size();
	skin_matrix.resize(nb);
	for (uint32_t j = 0; j < nb; j++) {
		skin_matrix[j] = skx * p_skeleton->get_bone_global_pose(skin_bind_bone[j]) * skin_bind_pose[j];
	}
	const uint32_t np = rest_local.size();
	const Transform3D *M = skin_matrix.ptr();
	for (uint32_t i = 0; i < np; i++) {
		skin_from[i] = skin_to[i];
		Vector3 p;
		for (int k = 0; k < 4; k++) {
			const float wt = skin_weights[i * 4 + k];
			const int32_t b = skin_binds[i * 4 + k];
			if (wt > 0.0f && b >= 0 && uint32_t(b) < nb) {
				p += M[b].xform(rest_local[i]) * wt;
			}
		}
		skin_to[i] = p;
	}
}

// The bone pins' places this frame (main thread): where they were, where their bones are now.
void FellowCloth3D::_update_bone_pins() {
	if (bone_pins == 0 && !skin_active) {
		return;
	}
	Skeleton3D *sk = ObjectDB::get_instance<Skeleton3D>(bound_skeleton);
	if (!sk || !sk->is_inside_tree()) {
		return;
	}
	if (skin_active) {
		// Decision 0230: the skin gives every particle's place; the pins follow theirs.
		_skin_places(sk);
		const uint32_t np = x.size();
		for (uint32_t i = 0; i < np; i++) {
			if (pin_bone[i] >= 0) {
				pin_from[i] = pin_to[i];
				pin_to[i] = skin_to[i];
			}
		}
		return;
	}
	const Transform3D skx = sk->get_global_transform();
	HashMap<int, Transform3D> bone_xf;
	const uint32_t np = x.size();
	for (uint32_t i = 0; i < np; i++) {
		const int b = pin_bone[i];
		if (b < 0 || b >= sk->get_bone_count()) {
			continue;
		}
		HashMap<int, Transform3D>::Iterator it = bone_xf.find(b);
		if (!it) {
			it = bone_xf.insert(b, skx * sk->get_bone_global_pose(b));
		}
		pin_from[i] = pin_to[i];
		pin_to[i] = it->value.xform(pin_offset[i]);
	}
}

void FellowCloth3D::reset() {
	if (!built) {
		return;
	}
	if (torn_edges > 0 || any_loose) {
		// Whole again (decision 0220).
		for (uint8_t &a : tri_alive) {
			a = 1;
		}
		for (uint8_t &a : edge_alive) {
			a = 1;
		}
		for (uint8_t &l : loose) {
			l = 0;
		}
		any_loose = false;
		torn_edges = 0;
		torn_pending = 0;
		torn_pending_sum = Vector3();
		tris = tris_all;
		tethers = tethers_rest;
		_filter_constraints();
		index_bytes = index_bytes_rest;
		index_dirty = true;
		_upload_indices();
	}
	_init_particles();
	if (is_inside_tree()) {
		_write_vertices();
		_finish();
	}
}

void FellowCloth3D::_add_collider(RID p_shape, const Transform3D &p_xf) {
	PhysicsServer3D *ps = PhysicsServer3D::get_singleton();
	Collider c;
	const Variant shape_data = ps->shape_get_data(p_shape);
	float reach = 0.0f;
	switch (ps->shape_get_type(p_shape)) {
		case PhysicsServer3D::SHAPE_SPHERE: {
			c.type = Collider::SPHERE;
			c.radius = shape_data;
			reach = c.radius;
		} break;
		case PhysicsServer3D::SHAPE_CAPSULE: {
			const Dictionary d = shape_data;
			c.type = Collider::CAPSULE;
			c.radius = d.get("radius", 0.5);
			c.height = d.get("height", 2.0);
			reach = MAX(c.radius, c.height * 0.5f);
		} break;
		case PhysicsServer3D::SHAPE_BOX: {
			c.type = Collider::BOX;
			c.half = shape_data;
			reach = c.half.length();
		} break;
		case PhysicsServer3D::SHAPE_CYLINDER: {
			const Dictionary d = shape_data;
			c.type = Collider::CYLINDER;
			c.radius = d.get("radius", 0.5);
			c.height = d.get("height", 2.0);
			reach = Vector2(c.radius, c.height * 0.5f).length();
		} break;
		case PhysicsServer3D::SHAPE_WORLD_BOUNDARY: {
			c.type = Collider::PLANE;
			c.plane = p_xf.xform(Plane(shape_data));
		} break;
		case PhysicsServer3D::SHAPE_HEIGHTMAP: {
			const Dictionary d = shape_data;
			c.type = Collider::HEIGHTMAP;
			c.map_width = d.get("width", 0);
			c.map_depth = d.get("depth", 0);
			const Variant h = d.get("heights", Variant());
			if (h.get_type() == Variant::PACKED_FLOAT32_ARRAY) {
				c.heights = PackedFloat32Array(h);
			} else if (h.get_type() == Variant::PACKED_FLOAT64_ARRAY) {
				const PackedFloat64Array hd = h;
				c.heights.resize(hd.size());
				for (int i = 0; i < hd.size(); i++) {
					c.heights.write[i] = float(hd[i]);
				}
			}
			if (c.map_width < 2 || c.map_depth < 2 || c.heights.size() != c.map_width * c.map_depth) {
				return;
			}
		} break;
		default:
			return; // convex and concave shapes are not collided with
	}
	c.xf = p_xf;
	c.inv = p_xf.affine_inverse();
	if (reach > 0.0f && !bounds.grow(reach + thickness + 0.25f).has_point(p_xf.origin)) {
		return; // too far to reach the cloth
	}
	colliders_now.push_back(c);
}

void FellowCloth3D::_gather_colliders() {
	colliders_now.clear();
	for (int i = 0; i < colliders.size(); i++) {
		const Node *n = get_node_or_null(colliders[i]);
		if (const CollisionShape3D *cs = Object::cast_to<CollisionShape3D>(n)) {
			if (!cs->is_disabled() && cs->get_shape().is_valid()) {
				_add_collider(cs->get_shape()->get_rid(), cs->get_global_transform());
			}
		} else if (const CollisionObject3D *co = Object::cast_to<CollisionObject3D>(n)) {
			for (int k = 0; k < co->get_child_count(); k++) {
				const CollisionShape3D *s = Object::cast_to<CollisionShape3D>(co->get_child(k));
				if (s && !s->is_disabled() && s->get_shape().is_valid()) {
					_add_collider(s->get_shape()->get_rid(), s->get_global_transform());
				}
			}
		}
	}
	if (!collide_with_world || !is_inside_tree() || get_world_3d().is_null()) {
		return;
	}
	PhysicsServer3D *ps = PhysicsServer3D::get_singleton();
	PhysicsDirectSpaceState3D *space = get_world_3d()->get_direct_space_state();
	if (!space) {
		return;
	}
	if (query_shape.is_null()) {
		query_shape = ps->box_shape_create();
	}
	const AABB qb = bounds.grow(0.5f + thickness);
	ps->shape_set_data(query_shape, qb.size * 0.5f);
	PhysicsDirectSpaceState3D::ShapeParameters sp;
	sp.shape_rid = query_shape;
	sp.transform = Transform3D(Basis(), qb.get_center());
	sp.collision_mask = collision_mask;
	sp.collide_with_bodies = true;
	sp.collide_with_areas = false;
	PhysicsDirectSpaceState3D::ShapeResult results[32];
	const int count = space->intersect_shape(sp, results, 32);
	for (int i = 0; i < count; i++) {
		const RID body = results[i].rid;
		if (!body.is_valid() || results[i].shape < 0) {
			continue;
		}
		const RID s = ps->body_get_shape(body, results[i].shape);
		if (!s.is_valid()) {
			continue;
		}
		const Transform3D bxf = ps->body_get_state(body, PhysicsServer3D::BODY_STATE_TRANSFORM);
		_add_collider(s, bxf * ps->body_get_shape_transform(body, results[i].shape));
	}
}

bool FellowCloth3D::_prepare(double p_delta) {
	if (build_dirty) {
		_build();
	}
	if (!built || !simulating || fellow_editor_still() || !is_visible_in_tree()) {
		return false;
	}
	xf_cur = get_global_transform();
	if (!have_prev) {
		xf_prev = xf_cur;
		have_prev = true;
	}
	if (xf_cur.origin.distance_to(xf_prev.origin) > 5.0f) {
		_init_particles(); // a teleport
	}
	{
		Skeleton3D *sk_now = _get_skeleton();
		const bool bound = bone_pins > 0 || skin_active;
		if (bound != (sk_now != nullptr) || (bound && ObjectDB::get_instance(bound_skeleton) != sk_now) || (follow_skin && skin_data && sk_now && !skin_active)) {
			_bind_bones(); // the skeleton set, changed or gone; the skin to follow
		}
	}
	_update_bone_pins();
	// Level of detail by the camera's distance: past lod_distance half the substeps, past simulation_distance none
	// (the cloth carried with the node).
	carry_only = false;
	lod_now = false;
	if (!Engine::get_singleton()->is_editor_hint() && get_viewport()) {
		const Camera3D *cam = get_viewport()->get_camera_3d();
		if (cam) {
			const float d = cam->get_global_position().distance_to(bounds.get_center()) - bounds.get_longest_axis_size() * 0.5f;
			carry_only = simulation_distance > 0.0f && d > simulation_distance;
			lod_now = lod_distance > 0.0f && d > lod_distance;
		}
	}
	rate_now = lod_now ? MAX(60, substep_rate / 2) : substep_rate;
	const double dt = MIN(p_delta, 0.25);
	time += dt;
	step_acc += dt;
	steps_now = int(step_acc * rate_now + 1e-6);
	step_acc -= double(steps_now) / double(rate_now);
	if (steps_now > max_substeps) {
		steps_now = max_substeps; // slower than real time rather than ever more work
		step_acc = 0.0;
	}
	if (carry_only) {
		return true;
	}
	if (steps_now == 0) {
		return false;
	}
	wind_now = wind;
	FellowWind3D *fw = FellowWind3D::get_active();
	if (fw && fw->is_inside_tree()) {
		wind_now += fw->get_velocity_at(bounds.get_center()) * wind_scale;
	}
	const float g = GLOBAL_GET_CACHED(float, "physics/3d/default_gravity");
	const Vector3 gv = GLOBAL_GET_CACHED(Vector3, "physics/3d/default_gravity_vector");
	gravity_now = gv * g * gravity_scale;
	_gather_colliders();
	return true;
}

void FellowCloth3D::_collide(uint32_t p_i) {
	Vector3 p = x[p_i];
	bool hit = false;
	Vector3 hit_n;
	const float t = thickness;
	for (const Collider &c : colliders_now) {
		switch (c.type) {
			case Collider::SPHERE: {
				const Vector3 d = p - c.xf.origin;
				const float l = d.length();
				const float r = c.radius + t;
				if (l < r) {
					const Vector3 n = l > 1e-9f ? d / l : Vector3(0, 1, 0);
					p = c.xf.origin + n * r;
					hit = true;
					hit_n = n;
				}
			} break;
			case Collider::CAPSULE: {
				const Vector3 axis = c.xf.basis.get_column(1).normalized();
				const float half = MAX(0.0f, c.height * 0.5f - c.radius);
				const float s = CLAMP((p - c.xf.origin).dot(axis), -half, half);
				const Vector3 q = c.xf.origin + axis * s;
				const Vector3 d = p - q;
				const float l = d.length();
				const float r = c.radius + t;
				if (l < r) {
					const Vector3 n = l > 1e-9f ? d / l : axis.get_any_perpendicular();
					p = q + n * r;
					hit = true;
					hit_n = n;
				}
			} break;
			case Collider::BOX: {
				Vector3 lp = c.inv.xform(p);
				const Vector3 e = c.half + Vector3(t, t, t);
				if (Math::abs(lp.x) < e.x && Math::abs(lp.y) < e.y && Math::abs(lp.z) < e.z) {
					const Vector3 pen(e.x - Math::abs(lp.x), e.y - Math::abs(lp.y), e.z - Math::abs(lp.z));
					const int axis = pen.x < pen.y ? (pen.x < pen.z ? 0 : 2) : (pen.y < pen.z ? 1 : 2);
					const float sign = lp[axis] < 0.0f ? -1.0f : 1.0f;
					lp[axis] = e[axis] * sign;
					p = c.xf.xform(lp);
					Vector3 ln;
					ln[axis] = sign;
					hit = true;
					hit_n = c.xf.basis.xform(ln).normalized();
				}
			} break;
			case Collider::CYLINDER: {
				Vector3 lp = c.inv.xform(p);
				const float r = Math::sqrt(lp.x * lp.x + lp.z * lp.z);
				const float rr = c.radius + t;
				const float hh = c.height * 0.5f + t;
				if (r < rr && Math::abs(lp.y) < hh) {
					Vector3 ln;
					if (rr - r < hh - Math::abs(lp.y)) {
						const Vector2 dir = r > 1e-9f ? Vector2(lp.x, lp.z) / r : Vector2(1, 0);
						lp.x = dir.x * rr;
						lp.z = dir.y * rr;
						ln = Vector3(dir.x, 0, dir.y);
					} else {
						const float sign = lp.y < 0.0f ? -1.0f : 1.0f;
						lp.y = hh * sign;
						ln = Vector3(0, sign, 0);
					}
					p = c.xf.xform(lp);
					hit = true;
					hit_n = c.xf.basis.xform(ln).normalized();
				}
			} break;
			case Collider::PLANE: {
				const float d = c.plane.distance_to(p) - t;
				if (d < 0.0f) {
					p -= c.plane.normal * d;
					hit = true;
					hit_n = c.plane.normal;
				}
			} break;
			case Collider::HEIGHTMAP: {
				Vector3 lp = c.inv.xform(p);
				const float fx = lp.x + (c.map_width - 1) * 0.5f;
				const float fz = lp.z + (c.map_depth - 1) * 0.5f;
				if (fx >= 0.0f && fz >= 0.0f && fx < c.map_width - 1 && fz < c.map_depth - 1) {
					const int ix = int(fx);
					const int iz = int(fz);
					const float tx = fx - ix;
					const float tz = fz - iz;
					const float *hp = c.heights.ptr();
					const int wdt = c.map_width;
					const float h = Math::lerp(Math::lerp(hp[iz * wdt + ix], hp[iz * wdt + ix + 1], tx), Math::lerp(hp[(iz + 1) * wdt + ix], hp[(iz + 1) * wdt + ix + 1], tx), tz);
					if (lp.y < h + t) {
						lp.y = h + t;
						p = c.xf.xform(lp);
						hit = true;
						hit_n = c.xf.basis.get_column(1).normalized();
					}
				}
			} break;
		}
	}
	if (hit && friction > 0.0f) {
		// The motion along the surface this substep, held back.
		const Vector3 disp = p - prev[p_i];
		p -= (disp - hit_n * disp.dot(hit_n)) * friction;
	}
	x[p_i] = p;
}

void FellowCloth3D::_substep(float p_h, const Transform3D &p_pin_xf, float p_f) {
	// Raw pointers in the loops: the vectors' checked indexing costs a branch each access.
	const uint32_t np = x.size();
	Vector3 *__restrict X = x.ptr();
	Vector3 *__restrict P = prev.ptr();
	Vector3 *__restrict V = vel.ptr();
	Vector3 *__restrict A = acc.ptr();
	const float *__restrict W = w.ptr();
	// Gravity, and the air on each triangle: drag along its normal, lift across it (the turbulent wind of the frame).
	for (uint32_t i = 0; i < np; i++) {
		A[i] = W[i] > 0.0f ? gravity_now : Vector3();
	}
	if (drag > 0.0f || lift > 0.0f) {
		const uint32_t *T = tris.ptr();
		const Vector3 *AIR = air.ptr();
		const uint32_t nt = tris.size() / 3;
		const float k = 0.5f * AIR_DENSITY * 0.5f / 3.0f; // ½ρ, the area is half the cross product, a third to each vertex
		for (uint32_t t = 0; t < nt; t++) {
			const uint32_t a = T[t * 3];
			const uint32_t b = T[t * 3 + 1];
			const uint32_t c = T[t * 3 + 2];
			const Vector3 nraw = (X[b] - X[a]).cross(X[c] - X[a]);
			const float len = nraw.length();
			if (len < 1e-12f) {
				continue;
			}
			const Vector3 n = nraw / len;
			const Vector3 vr = AIR[t] - (V[a] + V[b] + V[c]) * (1.0f / 3.0f);
			const float vn = vr.dot(n);
			const Vector3 f = (n * (drag * vn * vr.length()) + (vr - n * vn) * (lift * Math::abs(vn))) * (k * len);
			A[a] += f * W[a];
			A[b] += f * W[b];
			A[c] += f * W[c];
		}
	}
	// Integrate (pinned particles follow the node, or their bones).
	const float keep = MAX(0.0f, 1.0f - damping * p_h);
	const Vector3 *R = rest_local.ptr();
	const int *PB = bone_pins > 0 ? pin_bone.ptr() : nullptr;
	const uint8_t *LO = any_loose ? loose.ptr() : nullptr; // torn free of every triangle: left where it is
	for (uint32_t i = 0; i < np; i++) {
		P[i] = X[i];
		if (LO && LO[i]) {
			continue;
		}
		if (W[i] > 0.0f) {
			V[i] = (V[i] + A[i] * p_h) * keep;
			X[i] += V[i] * p_h;
		} else if (PB && PB[i] >= 0) {
			X[i] = pin_from[i].lerp(pin_to[i], p_f);
		} else {
			X[i] = p_pin_xf.xform(R[i]);
		}
	}
	// Constraints: Gauss-Seidel, `iterations` passes over the stretch edges (alternating their order: a pass's bias
	// towards its first edges evens out), one over the bending pairs; small substeps converge.
	auto solve = [X](const Edge *p_edges, int p_count, float p_k, bool p_reverse) {
		for (int k = 0; k < p_count; k++) {
			const Edge &e = p_edges[p_reverse ? p_count - 1 - k : k];
			const Vector3 d = X[e.a] - X[e.b];
			const float l2 = d.length_squared();
			if (l2 < 1e-18f) {
				continue;
			}
			const Vector3 corr = d * (p_k * (1.0f - e.rest / Math::sqrt(l2)));
			X[e.a] -= corr * e.fa;
			X[e.b] += corr * e.fb;
		}
	};
	for (int it = 0; it < iterations; it++) {
		solve(stretch.ptr(), int(stretch.size()), stretch_stiffness, it & 1);
	}
	if (bend_stiffness > 0.0f) {
		solve(bend.ptr(), int(bend.size()), bend_stiffness, false);
	}
	const float mu = self_collision_friction;
	if (!self_pairs.is_empty()) {
		// Decision 0221: the pairs of the frame closer than the distance pushed apart along their line, by their
		// inverse masses. Decision 0229: and their sliding along each other in the substep taken away by the friction.
		const float d = self_distance_now;
		const float d2 = d * d;
		const Vector3 *NO = normal.ptr();
		int contacts = 0;
		for (const SelfPair &sp : self_pairs) {
			Vector3 dv = X[sp.a] - X[sp.b];
			float l2 = dv.length_squared();
			if (l2 >= d2) {
				continue;
			}
			if (l2 < 1e-18f) {
				dv = NO[sp.a] * 1e-6f; // on top of each other: apart along the cloth's normal
				l2 = 1e-12f;
			}
			const float l = Math::sqrt(l2);
			const Vector3 corr = dv * ((d - l) / l);
			X[sp.a] += corr * sp.fa;
			X[sp.b] -= corr * sp.fb;
			if (mu > 0.0f) {
				const Vector3 nn = dv / l;
				const Vector3 rel = (X[sp.a] - P[sp.a]) - (X[sp.b] - P[sp.b]);
				const Vector3 tang = (rel - nn * rel.dot(nn)) * mu;
				X[sp.a] -= tang * sp.fa;
				X[sp.b] += tang * sp.fb;
			}
			contacts++;
		}
		self_contacts = contacts;
	}
	if (!self_tri_pairs.is_empty()) {
		// Decision 0228: a particle closer to another layer's triangle than the distance, on the side it was found on,
		// pushed out to the distance along the triangle's normal — the particle and the triangle's corners by their
		// inverse masses and the corners' weights at the nearest point (a point-triangle constraint); not where the
		// point is beyond the triangle's edges (the neighbouring triangle's, or a particle pair's). Decision 0229: with
		// the friction, their sliding along each other.
		const float d = self_distance_now;
		const uint32_t *T = tris.ptr();
		int contacts = 0;
		for (const SelfTriPair &tp : self_tri_pairs) {
			const uint32_t ia = T[tp.t];
			const uint32_t ib = T[tp.t + 1];
			const uint32_t ic = T[tp.t + 2];
			const Vector3 a = X[ia];
			const Vector3 e1 = X[ib] - a;
			const Vector3 e2 = X[ic] - a;
			Vector3 n = e1.cross(e2);
			const float nl = n.length();
			if (nl < 1e-12f) {
				continue;
			}
			n *= tp.side / nl; // towards the particle's side
			const Vector3 ap = X[tp.p] - a;
			const float s = ap.dot(n);
			if (s >= d) {
				continue;
			}
			// The projection's barycentric weights (inside the triangle only).
			const float d00 = e1.dot(e1);
			const float d01 = e1.dot(e2);
			const float d11 = e2.dot(e2);
			const float d20 = ap.dot(e1);
			const float d21 = ap.dot(e2);
			const float den = d00 * d11 - d01 * d01;
			if (den < 1e-24f) {
				continue;
			}
			const float bv = (d11 * d20 - d01 * d21) / den;
			const float bw = (d00 * d21 - d01 * d20) / den;
			const float bu = 1.0f - bv - bw;
			if (bu < 0.0f || bv < 0.0f || bw < 0.0f) {
				continue;
			}
			const float wp = W[tp.p];
			const float wa = W[ia] * bu;
			const float wb = W[ib] * bv;
			const float wc = W[ic] * bw;
			const float wsum = wp + wa * bu + wb * bv + wc * bw;
			if (wsum <= 0.0f) {
				continue;
			}
			const Vector3 dl = n * ((d - s) / wsum);
			X[tp.p] += dl * wp;
			X[ia] -= dl * wa;
			X[ib] -= dl * wb;
			X[ic] -= dl * wc;
			if (mu > 0.0f) {
				const Vector3 rel = (X[tp.p] - P[tp.p]) - ((X[ia] - P[ia]) * bu + (X[ib] - P[ib]) * bv + (X[ic] - P[ic]) * bw);
				const Vector3 tang = (rel - n * rel.dot(n)) * (mu / wsum);
				X[tp.p] -= tang * wp;
				X[ia] += tang * wa;
				X[ib] += tang * wb;
				X[ic] += tang * wc;
			}
			contacts++;
		}
		self_tri_contacts = contacts;
	}
	if (tether_slack > 0.0f) {
		const Tether *TT = tethers.ptr();
		const uint32_t ntt = tethers.size();
		for (uint32_t k = 0; k < ntt; k++) {
			const Tether &t = TT[k];
			if (W[t.pin] > 0.0f) {
				continue; // its pin was let go
			}
			const Vector3 d = X[t.p] - X[t.pin];
			const float m = t.rest * tether_slack;
			const float l2 = d.length_squared();
			if (l2 > m * m) {
				X[t.p] = X[t.pin] + d * (m / Math::sqrt(l2));
			}
		}
	}
	if (skin_active && skin_max_distance > 0.0f) {
		// Decision 0230: a free particle kept within the distance of its skinned place (interpolated over the substeps).
		const Vector3 *SF = skin_from.ptr();
		const Vector3 *ST = skin_to.ptr();
		const float lim = skin_max_distance;
		const float lim2 = lim * lim;
		int limited = 0;
		for (uint32_t i = 0; i < np; i++) {
			if (W[i] <= 0.0f) {
				continue;
			}
			const Vector3 t = SF[i].lerp(ST[i], p_f);
			const Vector3 d = X[i] - t;
			const float l2 = d.length_squared();
			if (l2 > lim2) {
				X[i] = t + d * (lim / Math::sqrt(l2));
				limited++;
			}
		}
		skin_limited = limited;
	}
	if (!colliders_now.is_empty()) {
		for (uint32_t i = 0; i < np; i++) {
			if (W[i] > 0.0f) {
				_collide(i);
			}
		}
	}
	const float inv_h = 1.0f / p_h;
	for (uint32_t i = 0; i < np; i++) {
		Vector3 v = (X[i] - P[i]) * inv_h;
		const float s2 = v.length_squared();
		if (s2 > 3600.0f) {
			v *= 60.0f / Math::sqrt(s2); // a clamp against an explosion (a collider teleported into the cloth)
		}
		V[i] = v;
	}
}

void FellowCloth3D::_simulate() {
	const uint64_t t0 = OS::get_singleton()->get_ticks_usec();
	const uint32_t np = x.size();
	if (carry_only) {
		// Far from the camera: the cloth keeps its shape, carried with the node.
		const Transform3D d = xf_cur * xf_prev.affine_inverse();
		for (uint32_t i = 0; i < np; i++) {
			x[i] = skin_active ? skin_to[i] : (bone_pins > 0 && pin_bone[i] >= 0 ? pin_to[i] : d.xform(x[i])); // decision 0230: skinned
			prev[i] = x[i];
			vel[i] = Vector3();
		}
		global_stats.skipped.increment();
	} else {
		const float h = 1.0f / float(rate_now);
		// The air on each triangle this frame: the wind, and turbulence (a field of sines moving with time).
		const uint32_t nt = tris.size() / 3;
		air.resize(nt);
		const float wind_speed = wind_now.length();
		const float tt = float(time);
		for (uint32_t t = 0; t < nt; t++) {
			Vector3 a = wind_now;
			if (turbulence > 0.0f && wind_speed > 0.0f) {
				const Vector3 cen = (x[tris[t * 3]] + x[tris[t * 3 + 1]] + x[tris[t * 3 + 2]]) * (1.0f / 3.0f);
				const Vector3 noise(Math::sin(cen.x * 1.7f + cen.y * 0.9f + tt * 3.1f), Math::sin(cen.y * 1.3f - cen.z * 1.1f - tt * 2.7f), Math::sin(cen.z * 1.5f + cen.x * 0.7f + tt * 3.7f));
				a += noise * (turbulence * wind_speed * 0.5f);
			}
			air[t] = a;
		}
		if (self_collision) {
			_find_self_pairs();
		} else {
			self_pairs.clear();
			self_tri_pairs.clear();
			self_contacts = 0;
			self_tri_contacts = 0;
		}
		for (int s = 0; s < steps_now; s++) {
			const real_t f = real_t(s + 1) / real_t(steps_now);
			_substep(h, xf_prev.interpolate_with(xf_cur, f), float(f));
		}
		global_stats.substeps.add(steps_now);
		global_stats.particles.add(np);
		// Decision 0220: the edges stretched past tear_strain break; a piece fallen 100 m below the cloth is gone.
		if (tear_strain > 0.0f || any_loose) {
			LocalVector<uint32_t> broken;
			if (tear_strain > 0.0f) {
				const float lim = 1.0f + tear_strain;
				for (uint32_t k = 0; k < stretch.size(); k++) {
					const Edge &e = stretch[k];
					if (x[e.a].distance_squared_to(x[e.b]) > lim * lim * e.rest * e.rest) {
						broken.push_back(stretch_source[k]);
					}
				}
			}
			if (any_loose) {
				const real_t floor_y = xf_cur.origin.y - 100.0;
				for (uint32_t t = 0; t < tris.size(); t += 3) {
					if (x[tris[t]].y < floor_y || x[tris[t + 1]].y < floor_y || x[tris[t + 2]].y < floor_y) {
						for (int k = 0; k < 3; k++) {
							const uint32_t a = tris[t + k];
							const uint32_t b = tris[t + (k + 1) % 3];
							for (uint32_t e = 0; e < stretch_all.size(); e++) {
								if ((stretch_all[e].a == a && stretch_all[e].b == b) || (stretch_all[e].a == b && stretch_all[e].b == a)) {
									broken.push_back(e);
									break;
								}
							}
						}
					}
				}
			}
			if (!broken.is_empty()) {
				Vector3 sum;
				const int n = _tear(broken, &sum);
				torn_pending += n;
				torn_pending_sum += sum;
			}
		}
	}
	// Normals and bounds.
	for (uint32_t i = 0; i < np; i++) {
		normal[i] = Vector3();
	}
	for (uint32_t t = 0; t < tris.size(); t += 3) {
		const Vector3 n = (x[tris[t + 1]] - x[tris[t]]).cross(x[tris[t + 2]] - x[tris[t]]);
		normal[tris[t]] += n;
		normal[tris[t + 1]] += n;
		normal[tris[t + 2]] += n;
	}
	bool first = true;
	bounds = AABB(np ? x[0] : Vector3(), Vector3());
	for (uint32_t i = 0; i < np; i++) {
		const float l = normal[i].length();
		normal[i] = l > 1e-12f ? normal[i] * (normal_sign / l) : Vector3(0, 1, 0);
		if (any_loose && loose[i]) {
			continue; // not drawn, not reaching anything
		}
		if (first) {
			bounds = AABB(x[i], Vector3());
			first = false;
		} else {
			bounds.expand_to(x[i]);
		}
	}
	_write_vertices();
	last_usec = OS::get_singleton()->get_ticks_usec() - t0;
	global_stats.simulations.increment();
	global_stats.simulate_usec.add(last_usec);
}

void FellowCloth3D::_write_vertices() {
	const uint32_t nv = render_particle.size();
	const Transform3D inv = xf_cur.affine_inverse();
	const Basis inv_basis = inv.basis;
	uint8_t *wp = vertex_bytes.ptrw();
	AABB lb;
	for (uint32_t v = 0; v < nv; v++) {
		const uint32_t p = render_particle[v];
		const Vector3 lp = inv.xform(x[p]);
		const float pos[3] = { float(lp.x), float(lp.y), float(lp.z) };
		memcpy(wp + v * 12, pos, 12);
		if (v == 0) {
			lb = AABB(lp, Vector3());
		} else {
			lb.expand_to(lp);
		}
		Vector3 ln = inv_basis.xform(normal[p]);
		ln = ln.length() > 1e-12f ? ln.normalized() : particle_rest_normal[p];
		// The vertex's own normal and tangent at rest, turned as the cloth turned there.
		const Quaternion q(particle_rest_normal[p], ln);
		const Vector3 n = q.xform(render_rest_normal[v]).normalized();
		const Vector4 &rt = render_rest_tangent[v];
		const Vector3 t = q.xform(Vector3(rt.x, rt.y, rt.z)).normalized();
		const Vector2 ne = n.octahedron_encode();
		Vector2 te = t.octahedron_tangent_encode(rt.w);
		uint16_t nt[4] = { unorm16(ne.x), unorm16(ne.y), unorm16(te.x), unorm16(te.y) };
		if (nt[2] == 0 && nt[3] == 65535) {
			nt[2] = 65535; // as RenderingServer stores it
		}
		memcpy(wp + normal_offset + v * normal_stride, nt, 8);
	}
	local_bounds = lb.grow(thickness);
}

void FellowCloth3D::_finish() {
	RenderingServer *rs = RenderingServer::get_singleton();
	_upload_indices();
	rs->mesh_surface_update_vertex_region(mesh, 0, 0, vertex_bytes);
	rs->mesh_set_custom_aabb(mesh, local_bounds);
	rs->instance_set_blend_shape_weight(get_instance(), 0, 0.0f); // the instance's vertices made again (the buffers swap)
	xf_prev = xf_cur;
	if (torn_pending > 0) {
		const int n = torn_pending;
		const Vector3 at = torn_pending_sum / float(n);
		torn_pending = 0;
		torn_pending_sum = Vector3();
		emit_signal(SNAME("torn"), at, n);
	}
}

// Decision 0221: the pairs of particles that may meet in this frame — within twice the self-collision distance now,
// not within it of each other at rest (neighbours, whose own constraints keep them) — from a spatial hash of cells
// twice the distance across (a pair in the same or a neighbouring cell).
void FellowCloth3D::_find_self_pairs() {
	self_pairs.clear();
	self_tri_pairs.clear();
	self_contacts = 0;
	self_tri_contacts = 0;
	const uint32_t np = x.size();
	const float d = self_collision_distance > 0.0f ? self_collision_distance : (self_collision_triangles ? 0.25f : 0.75f) * mean_edge;
	self_distance_now = d;
	if (d <= 0.0f || np < 2) {
		return;
	}
	const float cell = 2.0f * d;
	const float inv = 1.0f / cell;
	uint32_t table = 64;
	while (table < np * 2) {
		table <<= 1;
	}
	self_head.resize(table);
	for (uint32_t &h : self_head) {
		h = UINT32_MAX;
	}
	self_next.resize(np);
	const uint8_t *LO = any_loose ? loose.ptr() : nullptr;
	auto cell_of = [inv](const Vector3 &p_p) {
		return Vector3i(int(Math::floor(p_p.x * inv)), int(Math::floor(p_p.y * inv)), int(Math::floor(p_p.z * inv)));
	};
	auto bucket = [table](const Vector3i &p_c) {
		return (uint32_t(p_c.x) * 73856093u ^ uint32_t(p_c.y) * 19349663u ^ uint32_t(p_c.z) * 83492791u) & (table - 1);
	};
	self_cell.resize(np);
	for (uint32_t i = 0; i < np; i++) {
		if (LO && LO[i]) {
			continue;
		}
		self_cell[i] = cell_of(x[i]);
		const uint32_t b = bucket(self_cell[i]);
		self_next[i] = self_head[b];
		self_head[b] = i;
	}
	const float reach2 = cell * cell;
	const Vector3i *CE = self_cell.ptr();
	for (uint32_t i = 0; i < np; i++) {
		if (LO && LO[i]) {
			continue;
		}
		// The particle's own cell (the later particles in it) and half of the 26 around it (each pair of cells once).
		const Vector3i c = CE[i];
		for (int k = 13; k < 27; k++) {
			const Vector3i cc = c + Vector3i(k % 3 - 1, (k / 3) % 3 - 1, k / 9 - 1);
			for (uint32_t j = self_head[bucket(cc)]; j != UINT32_MAX; j = self_next[j]) {
				// Only the particles of that cell (a bucket may hold others' too).
				if ((k == 13 && j <= i) || CE[j] != cc || x[i].distance_squared_to(x[j]) >= reach2 || rest_local[i].distance_squared_to(rest_local[j]) < reach2) {
					continue;
				}
				const float ws = w[i] + w[j];
				if (ws > 0.0f) {
					self_pairs.push_back({ i, j, w[i] / ws, w[j] / ws });
				}
			}
		}
	}
	if (self_collision_triangles) {
		_find_self_tri_pairs(d);
	}
}

// Decision 0228: the closest point of a triangle to a point (Ericson's regions), its barycentric weights in r_bary.
static Vector3 _cloth_closest_on_triangle(const Vector3 &p_p, const Vector3 &p_a, const Vector3 &p_b, const Vector3 &p_c, Vector3 &r_bary) {
	const Vector3 ab = p_b - p_a;
	const Vector3 ac = p_c - p_a;
	const Vector3 ap = p_p - p_a;
	const float d1 = ab.dot(ap);
	const float d2 = ac.dot(ap);
	if (d1 <= 0.0f && d2 <= 0.0f) {
		r_bary = Vector3(1, 0, 0);
		return p_a;
	}
	const Vector3 bp = p_p - p_b;
	const float d3 = ab.dot(bp);
	const float d4 = ac.dot(bp);
	if (d3 >= 0.0f && d4 <= d3) {
		r_bary = Vector3(0, 1, 0);
		return p_b;
	}
	const float vc = d1 * d4 - d3 * d2;
	if (vc <= 0.0f && d1 >= 0.0f && d3 <= 0.0f) {
		const float v = d1 / (d1 - d3);
		r_bary = Vector3(1.0f - v, v, 0);
		return p_a + ab * v;
	}
	const Vector3 cp = p_p - p_c;
	const float d5 = ab.dot(cp);
	const float d6 = ac.dot(cp);
	if (d6 >= 0.0f && d5 <= d6) {
		r_bary = Vector3(0, 0, 1);
		return p_c;
	}
	const float vb = d5 * d2 - d1 * d6;
	if (vb <= 0.0f && d2 >= 0.0f && d6 <= 0.0f) {
		const float w2 = d2 / (d2 - d6);
		r_bary = Vector3(1.0f - w2, 0, w2);
		return p_a + ac * w2;
	}
	const float va = d3 * d6 - d5 * d4;
	if (va <= 0.0f && (d4 - d3) >= 0.0f && (d5 - d6) >= 0.0f) {
		const float w2 = (d4 - d3) / ((d4 - d3) + (d5 - d6));
		r_bary = Vector3(0, 1.0f - w2, w2);
		return p_b + (p_c - p_b) * w2;
	}
	const float den = va + vb + vc;
	if (Math::abs(den) < 1e-30f) {
		r_bary = Vector3(1, 0, 0);
		return p_a;
	}
	const float v = vb / den;
	const float w2 = vc / den;
	r_bary = Vector3(1.0f - v - w2, v, w2);
	return p_a + ab * v + ac * w2;
}

// Decision 0228: the particles within the frame's reach (twice the distance) of a live triangle that is not theirs
// and was not within that reach of them at rest (the fabric's own neighbours), with the side of it they are on. The
// triangles are hashed into every cell their bounds, grown by the reach, cover; a particle looks in its own cell only.
void FellowCloth3D::_find_self_tri_pairs(float p_d) {
	const uint32_t np = x.size();
	const uint32_t nt = tris.size() / 3;
	if (nt == 0 || np < 4) {
		return;
	}
	const float reach = 2.0f * p_d;
	const float reach2 = reach * reach;
	const float cell = 2.0f * MAX(reach, mean_edge); // twice: fewer cells a triangle, the bounds test the extra candidates
	const float inv = 1.0f / cell;
	const uint32_t *T = tris.ptr();
	const Vector3 *X = x.ptr();
	const Vector3 *R = rest_local.ptr();
	self_tri_entries.clear();
	self_tri_bounds.resize(nt * 2);
	Vector3 *TB = self_tri_bounds.ptr();
	auto floor_i = [](float p_v) { return int(Math::floor(p_v)); };
	for (uint32_t t = 0; t < nt; t++) {
		const Vector3 &a = X[T[t * 3]];
		const Vector3 &b = X[T[t * 3 + 1]];
		const Vector3 &c = X[T[t * 3 + 2]];
		const Vector3 lo = a.min(b).min(c) - Vector3(reach, reach, reach);
		const Vector3 hi = a.max(b).max(c) + Vector3(reach, reach, reach);
		TB[t * 2] = lo;
		TB[t * 2 + 1] = hi;
		const Vector3i c0(floor_i(lo.x * inv), floor_i(lo.y * inv), floor_i(lo.z * inv));
		const Vector3i c1(floor_i(hi.x * inv), floor_i(hi.y * inv), floor_i(hi.z * inv));
		if ((c1.x - c0.x + 1) * (c1.y - c0.y + 1) * (c1.z - c0.z + 1) > 64) {
			continue; // a triangle stretched across the scene (a broken simulation): left out
		}
		for (int cz = c0.z; cz <= c1.z; cz++) {
			for (int cy = c0.y; cy <= c1.y; cy++) {
				for (int cx = c0.x; cx <= c1.x; cx++) {
					self_tri_entries.push_back({ Vector3i(cx, cy, cz), t, UINT32_MAX });
				}
			}
		}
	}
	uint32_t table = 64;
	while (table < self_tri_entries.size() * 2) {
		table <<= 1;
	}
	self_tri_head.resize(table);
	for (uint32_t &h : self_tri_head) {
		h = UINT32_MAX;
	}
	auto bucket = [table](const Vector3i &p_c) {
		return (uint32_t(p_c.x) * 73856093u ^ uint32_t(p_c.y) * 19349663u ^ uint32_t(p_c.z) * 83492791u) & (table - 1);
	};
	SelfTriEntry *E = self_tri_entries.ptr();
	for (uint32_t k = 0; k < self_tri_entries.size(); k++) {
		const uint32_t b = bucket(E[k].cell);
		E[k].next = self_tri_head[b];
		self_tri_head[b] = k;
	}
	const uint8_t *LO = any_loose ? loose.ptr() : nullptr;
	const float *Wt = w.ptr();
	for (uint32_t i = 0; i < np; i++) {
		if (LO && LO[i]) {
			continue;
		}
		const Vector3 &p = X[i];
		const Vector3i cc(floor_i(p.x * inv), floor_i(p.y * inv), floor_i(p.z * inv));
		for (uint32_t k = self_tri_head[bucket(cc)]; k != UINT32_MAX; k = E[k].next) {
			if (E[k].cell != cc) {
				continue;
			}
			const uint32_t t = E[k].tri;
			const uint32_t ia = T[t * 3];
			const uint32_t ib = T[t * 3 + 1];
			const uint32_t ic = T[t * 3 + 2];
			if (ia == i || ib == i || ic == i) {
				continue;
			}
			const Vector3 &lo = TB[t * 2];
			const Vector3 &hi = TB[t * 2 + 1];
			if (p.x < lo.x || p.y < lo.y || p.z < lo.z || p.x > hi.x || p.y > hi.y || p.z > hi.z) {
				continue; // outside the triangle's bounds grown by the reach
			}
			if (Wt[i] <= 0.0f && Wt[ia] <= 0.0f && Wt[ib] <= 0.0f && Wt[ic] <= 0.0f) {
				continue; // all pinned
			}
			Vector3 bary;
			const Vector3 q = _cloth_closest_on_triangle(p, X[ia], X[ib], X[ic], bary);
			if (p.distance_squared_to(q) >= reach2) {
				continue;
			}
			const Vector3 qr = _cloth_closest_on_triangle(R[i], R[ia], R[ib], R[ic], bary);
			if (R[i].distance_squared_to(qr) < reach2) {
				continue; // the fabric's own neighbourhood
			}
			const Vector3 n = (X[ib] - X[ia]).cross(X[ic] - X[ia]);
			const float side = (p - q).dot(n);
			self_tri_pairs.push_back({ i, t * 3, side >= 0.0f ? 1.0f : -1.0f });
		}
	}
}

// Decision 0220: tearing. The edges given break; their triangles go (not drawn, no wind), and with them every edge
// left without a triangle and every bending pair across one; a particle of no live triangle is left where it is. The
// tethers are found again along what is left (a piece cut from its pins falls free). Returns the edges broken (not
// counting those that went with their triangles); r_sum the sum of their midpoints.
int FellowCloth3D::_tear(const LocalVector<uint32_t> &p_edges, Vector3 *r_sum) {
	int broken = 0;
	Vector3 sum;
	LocalVector<uint32_t> dead_tris;
	for (uint32_t e : p_edges) {
		if (e >= edge_alive.size() || !edge_alive[e]) {
			continue;
		}
		edge_alive[e] = 0;
		broken++;
		sum += (x[stretch_all[e].a] + x[stretch_all[e].b]) * 0.5f;
		for (uint32_t k = edge_tri_start[e]; k < edge_tri_start[e + 1]; k++) {
			const uint32_t t = edge_tri_list[k];
			if (tri_alive[t]) {
				tri_alive[t] = 0;
				dead_tris.push_back(t);
			}
		}
	}
	if (broken == 0) {
		return 0;
	}
	for (uint32_t t : dead_tris) {
		for (int k = 0; k < 3; k++) {
			const uint32_t e = tri_edges[t * 3 + k];
			if (!edge_alive[e]) {
				continue;
			}
			bool held = false;
			for (uint32_t j = edge_tri_start[e]; j < edge_tri_start[e + 1]; j++) {
				held = held || tri_alive[edge_tri_list[j]];
			}
			if (!held) {
				edge_alive[e] = 0;
			}
		}
	}
	_after_tear();
	if (r_sum) {
		*r_sum = sum;
	}
	return broken;
}

void FellowCloth3D::_after_tear() {
	const uint32_t np = x.size();
	tris.clear();
	for (uint8_t &l : loose) {
		l = 1;
	}
	for (uint32_t t = 0; t < tri_alive.size(); t++) {
		if (tri_alive[t]) {
			for (int k = 0; k < 3; k++) {
				tris.push_back(tris_all[t * 3 + k]);
				loose[tris_all[t * 3 + k]] = 0;
			}
		}
	}
	any_loose = false;
	for (uint32_t i = 0; i < np; i++) {
		if (loose[i]) {
			any_loose = true;
			vel[i] = Vector3();
		}
	}
	torn_edges = 0;
	for (uint8_t a : edge_alive) {
		torn_edges += a ? 0 : 1;
	}
	_filter_constraints();
	_compute_tethers();
	// The torn triangles made degenerate in the surface's indices (uploaded on the main thread).
	uint8_t *ib = index_bytes.ptrw();
	const uint8_t *rb = index_bytes_rest.ptr();
	for (uint32_t t = 0; t < tri_alive.size(); t++) {
		const uint32_t o = tri_index[t] * index_size;
		if (o + index_size * 3 > uint32_t(index_bytes.size())) {
			continue;
		}
		if (tri_alive[t]) {
			memcpy(ib + o, rb + o, index_size * 3);
		} else {
			memcpy(ib + o + index_size, rb + o, index_size);
			memcpy(ib + o + index_size * 2, rb + o, index_size);
		}
	}
	index_dirty = true;
}

// The tethers along the edges left: each free particle to its nearest pin (as _build() finds them).
void FellowCloth3D::_compute_tethers() {
	tethers.clear();
	if (tether_slack <= 0.0f) {
		return;
	}
	const uint32_t np = x.size();
	LocalVector<LocalVector<Pair<uint32_t, float>>> adjacency;
	adjacency.resize(np);
	for (uint32_t e = 0; e < stretch_all.size(); e++) {
		if (edge_alive[e]) {
			const Edge &ed = stretch_all[e];
			adjacency[ed.a].push_back(Pair<uint32_t, float>(ed.b, ed.rest));
			adjacency[ed.b].push_back(Pair<uint32_t, float>(ed.a, ed.rest));
		}
	}
	LocalVector<float> dist;
	LocalVector<uint32_t> source;
	dist.resize(np);
	source.resize(np);
	LocalVector<HeapItem> heap;
	for (uint32_t i = 0; i < np; i++) {
		const bool pinned = w[i] == 0.0f && !loose[i];
		dist[i] = pinned ? 0.0f : std::numeric_limits<float>::max();
		source[i] = i;
		if (pinned) {
			heap_push(heap, { 0.0f, i });
		}
	}
	while (!heap.is_empty()) {
		const HeapItem it = heap_pop(heap);
		if (it.d > dist[it.i]) {
			continue;
		}
		for (const Pair<uint32_t, float> &nb : adjacency[it.i]) {
			const float d = it.d + nb.second;
			if (d < dist[nb.first]) {
				dist[nb.first] = d;
				source[nb.first] = source[it.i];
				heap_push(heap, { d, nb.first });
			}
		}
	}
	for (uint32_t i = 0; i < np; i++) {
		if (w[i] > 0.0f && !loose[i] && dist[i] < std::numeric_limits<float>::max()) {
			tethers.push_back({ i, source[i], dist[i] });
		}
	}
}

void FellowCloth3D::_upload_indices() {
	if (index_dirty && mesh.is_valid() && !index_bytes.is_empty()) {
		RenderingServer::get_singleton()->mesh_surface_update_index_region(mesh, 0, 0, index_bytes);
	}
	index_dirty = false;
}

int FellowCloth3D::tear(const Vector3 &p_position, float p_radius) {
	if (!built) {
		return 0;
	}
	// Every edge passing within the radius (a cut, a shot through the cloth).
	const float r2 = p_radius * p_radius;
	LocalVector<uint32_t> edges;
	for (uint32_t e = 0; e < stretch_all.size(); e++) {
		if (!edge_alive[e]) {
			continue;
		}
		const Vector3 a = x[stretch_all[e].a];
		const Vector3 ab = x[stretch_all[e].b] - a;
		const float l2 = ab.length_squared();
		const float t = l2 > 1e-12f ? CLAMP((p_position - a).dot(ab) / l2, 0.0f, 1.0f) : 0.0f;
		if ((a + ab * t).distance_squared_to(p_position) <= r2) {
			edges.push_back(e);
		}
	}
	Vector3 sum;
	const int n = _tear(edges, &sum);
	if (n > 0) {
		_upload_indices();
		emit_signal(SNAME("torn"), sum / float(n), n);
	}
	return n;
}

void FellowCloth3D::_group_task(uint32_t p_index, const LocalVector<FellowCloth3D *> *p_list) {
	(*p_list)[p_index]->_simulate();
}

void FellowCloth3D::_group_process(double p_delta) {
	const uint64_t t0 = OS::get_singleton()->get_ticks_usec();
	LocalVector<FellowCloth3D *> list;
	for (FellowCloth3D *c : registry) {
		if (c->is_inside_tree() && c->_prepare(p_delta)) {
			list.push_back(c);
		}
	}
	if (list.is_empty()) {
		return;
	}
	if (list.size() == 1) {
		list[0]->_simulate();
	} else {
		WorkerThreadPool::GroupID g = WorkerThreadPool::get_singleton()->add_template_group_task(list[0], &FellowCloth3D::_group_task, (const LocalVector<FellowCloth3D *> *)&list, list.size(), -1, true, SNAME("FellowCloth3D"));
		WorkerThreadPool::get_singleton()->wait_for_group_task_completion(g);
	}
	for (FellowCloth3D *c : list) {
		c->_finish();
	}
	global_stats.frames.increment();
	global_stats.frame_usec.add(OS::get_singleton()->get_ticks_usec() - t0);
}

void FellowCloth3D::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_ENTER_TREE: {
			registry.push_back(this);
			if (build_dirty || !built) {
				_build();
			} else {
				_init_particles();
			}
			set_process_internal(true);
		} break;
		case NOTIFICATION_EXIT_TREE: {
			registry.erase(this);
			have_prev = false;
		} break;
		case NOTIFICATION_INTERNAL_PROCESS: {
			const uint64_t frame = Engine::get_singleton()->get_process_frames();
			if (frame != group_frame) {
				group_frame = frame;
				_group_process(get_process_delta_time());
			}
		} break;
	}
}

AABB FellowCloth3D::get_aabb() const {
	return local_bounds;
}

void FellowCloth3D::apply_impulse(const Vector3 &p_position, const Vector3 &p_impulse, float p_radius) {
	// The impulse (N·s) shared by the free particles within the radius, more near its centre: the momentum it adds is
	// the impulse.
	const float r = MAX(p_radius, 1e-4f);
	float total = 0.0f;
	for (uint32_t i = 0; i < x.size(); i++) {
		const float d = x[i].distance_to(p_position);
		if (d < r && w[i] > 0.0f) {
			total += particle_mass[i] * (1.0f - d / r);
		}
	}
	if (total <= 0.0f) {
		return;
	}
	for (uint32_t i = 0; i < x.size(); i++) {
		const float d = x[i].distance_to(p_position);
		if (d < r && w[i] > 0.0f) {
			vel[i] += p_impulse * ((1.0f - d / r) / total);
		}
	}
}

Vector3 FellowCloth3D::get_particle_position(int p_index) const {
	ERR_FAIL_INDEX_V(p_index, int(x.size()), Vector3());
	return x[p_index];
}

void FellowCloth3D::set_skeleton_path(const NodePath &p_path) {
	skeleton_path = p_path;
	if (built && is_inside_tree()) {
		_bind_bones();
	}
	update_configuration_warnings();
}

void FellowCloth3D::set_pin_bones(const PackedStringArray &p_bones) {
	pin_bones = p_bones;
	if (built && is_inside_tree()) {
		_bind_bones();
	}
}

void FellowCloth3D::set_follow_skin(bool p_enabled) {
	follow_skin = p_enabled;
	if (built && is_inside_tree()) {
		_bind_bones();
	}
	update_configuration_warnings();
}

void FellowCloth3D::set_skin(const Ref<Skin> &p_skin) {
	skin = p_skin;
	if (built && is_inside_tree()) {
		_bind_bones();
	}
	update_configuration_warnings();
}

void FellowCloth3D::set_particle_pinned(int p_index, bool p_pinned) {
	ERR_FAIL_INDEX(p_index, int(x.size()));
	if (p_pinned) {
		w[p_index] = 0.0f;
		rest_local[p_index] = (is_inside_tree() ? get_global_transform() : get_transform()).affine_inverse().xform(x[p_index]);
		vel[p_index] = Vector3();
	} else {
		w[p_index] = 1.0f / particle_mass[p_index];
	}
	_filter_constraints();
	if (_get_skeleton()) {
		_bind_bones(); // the pins on the bones again, this one with them or not
	}
}

bool FellowCloth3D::is_particle_pinned(int p_index) const {
	ERR_FAIL_INDEX_V(p_index, int(x.size()), false);
	return w[p_index] == 0.0f;
}

Dictionary FellowCloth3D::get_stats() const {
	Dictionary d;
	d["particles"] = int(x.size());
	d["triangles"] = int(tris.size() / 3);
	d["stretch_constraints"] = int(stretch.size());
	d["bend_constraints"] = int(bend.size());
	d["tethers"] = int(tethers.size());
	d["substeps"] = steps_now;
	d["colliders"] = int(colliders_now.size());
	d["simulate_usec"] = int64_t(last_usec);
	d["far"] = carry_only;
	d["lod"] = lod_now;
	d["wind"] = wind_now;
	d["bone_pins"] = bone_pins;
	d["skin_particles"] = skin_active ? int(rest_local.size()) : 0;
	d["skin_limited"] = skin_active ? skin_limited : 0;
	d["torn_edges"] = torn_edges;
	d["triangles_all"] = int(tris_all.size() / 3);
	int nl = 0;
	for (uint8_t l : loose) {
		nl += l;
	}
	d["loose_particles"] = nl;
	d["self_pairs"] = int(self_pairs.size());
	d["self_tri_pairs"] = int(self_tri_pairs.size());
	d["self_tri_contacts"] = self_tri_contacts;
	d["self_contacts"] = self_contacts;
	d["self_distance"] = self_distance_now;
	return d;
}

Dictionary FellowCloth3D::get_global_stats() {
	Dictionary d;
	d["frames"] = int64_t(global_stats.frames.get());
	d["simulations"] = int64_t(global_stats.simulations.get());
	d["substeps"] = int64_t(global_stats.substeps.get());
	d["particles"] = int64_t(global_stats.particles.get());
	d["simulate_usec"] = int64_t(global_stats.simulate_usec.get());
	d["frame_usec"] = int64_t(global_stats.frame_usec.get());
	d["skipped"] = int64_t(global_stats.skipped.get());
	return d;
}

void FellowCloth3D::reset_global_stats() {
	for (SafeNumeric<uint64_t> *v : { &global_stats.frames, &global_stats.simulations, &global_stats.substeps, &global_stats.particles, &global_stats.simulate_usec, &global_stats.frame_usec, &global_stats.skipped }) {
		v->set(0);
	}
}

PackedStringArray FellowCloth3D::get_configuration_warnings() const {
	PackedStringArray warnings = GeometryInstance3D::get_configuration_warnings();
	if (!build_problem.is_empty()) {
		warnings.push_back(build_problem);
	}
	for (int i = 0; i < colliders.size(); i++) {
		const Node *n = is_inside_tree() ? get_node_or_null(colliders[i]) : nullptr;
		if (is_inside_tree() && !Object::cast_to<CollisionShape3D>(n) && !Object::cast_to<CollisionObject3D>(n)) {
			warnings.push_back(vformat(RTR("Colliders[%d] (%s) leads to no CollisionShape3D or CollisionObject3D."), i, String(colliders[i])));
		}
	}
	if (is_inside_tree() && !skeleton_path.is_empty()) {
		const Skeleton3D *sk = Object::cast_to<Skeleton3D>(get_node_or_null(skeleton_path));
		if (!sk) {
			warnings.push_back(vformat(RTR("Skeleton Path (%s) leads to no Skeleton3D: the pinned particles follow the node."), String(skeleton_path)));
		} else {
			for (const String &b : pin_bones) {
				if (sk->find_bone(b) < 0) {
					warnings.push_back(vformat(RTR("Pin Bones: the skeleton has no bone \"%s\"."), b));
				}
			}
		}
	}
	if (follow_skin) {
		// Decision 0230.
		if (skeleton_path.is_empty()) {
			warnings.push_back(RTR("Follow Skin needs a Skeleton Path: the cloth hangs from its node."));
		} else if (built && !skin_data) {
			warnings.push_back(RTR("Follow Skin: the source mesh has no bone weights (a skinned mesh's surface): the pins follow their nearest bones."));
		} else if (built && is_inside_tree() && Object::cast_to<Skeleton3D>(get_node_or_null(skeleton_path)) && !skin_active) {
			warnings.push_back(RTR("Follow Skin: a bone of the skin is not in the skeleton (or the Skin has too few binds): the pins follow their nearest bones."));
		}
	}
	return warnings;
}

void FellowCloth3D::_set_build_dirty() {
	build_dirty = true;
	if (is_inside_tree()) {
		_build();
	}
	update_configuration_warnings();
}

void FellowCloth3D::set_source_mesh(const Ref<Mesh> &p_mesh) {
	source_mesh = p_mesh;
	_set_build_dirty();
}

void FellowCloth3D::set_size(const Vector2 &p_size) {
	size = Vector2(MAX(0.01f, p_size.x), MAX(0.01f, p_size.y));
	_set_build_dirty();
}

void FellowCloth3D::set_resolution(int p_resolution) {
	resolution = CLAMP(p_resolution, 1, 256);
	_set_build_dirty();
}

void FellowCloth3D::set_pin_mode(PinMode p_mode) {
	pin_mode = p_mode;
	_set_build_dirty();
}

void FellowCloth3D::set_material(const Ref<Material> &p_material) {
	material = p_material;
	if (mesh.is_valid()) {
		if (material.is_null() && default_material.is_null()) {
			_set_build_dirty();
			return;
		}
		RenderingServer::get_singleton()->mesh_surface_set_material(mesh, 0, material.is_valid() ? material->get_rid() : default_material->get_rid());
	}
}

void FellowCloth3D::set_mass(float p_mass) {
	mass = MAX(0.001f, p_mass);
	_set_build_dirty();
}

void FellowCloth3D::set_tether_slack(float p_slack) {
	tether_slack = p_slack <= 0.0f ? 0.0f : MAX(1.0f, p_slack);
	_set_build_dirty();
}

void FellowCloth3D::set_colliders(const TypedArray<NodePath> &p_colliders) {
	colliders = p_colliders;
	update_configuration_warnings();
}

void FellowCloth3D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("reset"), &FellowCloth3D::reset);
	ClassDB::bind_method(D_METHOD("apply_impulse", "position", "impulse", "radius"), &FellowCloth3D::apply_impulse);
	ClassDB::bind_method(D_METHOD("tear", "position", "radius"), &FellowCloth3D::tear);
	ClassDB::bind_method(D_METHOD("get_torn_edge_count"), &FellowCloth3D::get_torn_edge_count);
	ClassDB::bind_method(D_METHOD("get_particle_count"), &FellowCloth3D::get_particle_count);
	ClassDB::bind_method(D_METHOD("get_particle_position", "index"), &FellowCloth3D::get_particle_position);
	ClassDB::bind_method(D_METHOD("set_particle_pinned", "index", "pinned"), &FellowCloth3D::set_particle_pinned);
	ClassDB::bind_method(D_METHOD("is_particle_pinned", "index"), &FellowCloth3D::is_particle_pinned);
	ClassDB::bind_method(D_METHOD("get_stats"), &FellowCloth3D::get_stats);
	ClassDB::bind_static_method("FellowCloth3D", D_METHOD("get_global_stats"), &FellowCloth3D::get_global_stats);
	ClassDB::bind_static_method("FellowCloth3D", D_METHOD("reset_global_stats"), &FellowCloth3D::reset_global_stats);

	ClassDB::bind_method(D_METHOD("set_source_mesh", "mesh"), &FellowCloth3D::set_source_mesh);
	ClassDB::bind_method(D_METHOD("get_source_mesh"), &FellowCloth3D::get_source_mesh);
	ClassDB::bind_method(D_METHOD("set_size", "size"), &FellowCloth3D::set_size);
	ClassDB::bind_method(D_METHOD("get_size"), &FellowCloth3D::get_size);
	ClassDB::bind_method(D_METHOD("set_resolution", "resolution"), &FellowCloth3D::set_resolution);
	ClassDB::bind_method(D_METHOD("get_resolution"), &FellowCloth3D::get_resolution);
	ClassDB::bind_method(D_METHOD("set_skeleton_path", "path"), &FellowCloth3D::set_skeleton_path);
	ClassDB::bind_method(D_METHOD("get_skeleton_path"), &FellowCloth3D::get_skeleton_path);
	ClassDB::bind_method(D_METHOD("set_pin_bones", "bones"), &FellowCloth3D::set_pin_bones);
	ClassDB::bind_method(D_METHOD("get_pin_bones"), &FellowCloth3D::get_pin_bones);
	ClassDB::bind_method(D_METHOD("set_follow_skin", "enabled"), &FellowCloth3D::set_follow_skin);
	ClassDB::bind_method(D_METHOD("is_following_skin"), &FellowCloth3D::is_following_skin);
	ClassDB::bind_method(D_METHOD("set_skin_max_distance", "distance"), &FellowCloth3D::set_skin_max_distance);
	ClassDB::bind_method(D_METHOD("get_skin_max_distance"), &FellowCloth3D::get_skin_max_distance);
	ClassDB::bind_method(D_METHOD("set_skin", "skin"), &FellowCloth3D::set_skin);
	ClassDB::bind_method(D_METHOD("get_skin"), &FellowCloth3D::get_skin);
	ClassDB::bind_method(D_METHOD("set_pin_mode", "mode"), &FellowCloth3D::set_pin_mode);
	ClassDB::bind_method(D_METHOD("get_pin_mode"), &FellowCloth3D::get_pin_mode);
	ClassDB::bind_method(D_METHOD("set_material", "material"), &FellowCloth3D::set_material);
	ClassDB::bind_method(D_METHOD("get_material"), &FellowCloth3D::get_material);
	ClassDB::bind_method(D_METHOD("set_simulating", "enabled"), &FellowCloth3D::set_simulating);
	ClassDB::bind_method(D_METHOD("is_simulating"), &FellowCloth3D::is_simulating);
	ClassDB::bind_method(D_METHOD("set_mass", "mass"), &FellowCloth3D::set_mass);
	ClassDB::bind_method(D_METHOD("get_mass"), &FellowCloth3D::get_mass);
	ClassDB::bind_method(D_METHOD("set_stretch_stiffness", "stiffness"), &FellowCloth3D::set_stretch_stiffness);
	ClassDB::bind_method(D_METHOD("get_stretch_stiffness"), &FellowCloth3D::get_stretch_stiffness);
	ClassDB::bind_method(D_METHOD("set_bend_stiffness", "stiffness"), &FellowCloth3D::set_bend_stiffness);
	ClassDB::bind_method(D_METHOD("get_bend_stiffness"), &FellowCloth3D::get_bend_stiffness);
	ClassDB::bind_method(D_METHOD("set_damping", "damping"), &FellowCloth3D::set_damping);
	ClassDB::bind_method(D_METHOD("get_damping"), &FellowCloth3D::get_damping);
	ClassDB::bind_method(D_METHOD("set_tether_slack", "slack"), &FellowCloth3D::set_tether_slack);
	ClassDB::bind_method(D_METHOD("get_tether_slack"), &FellowCloth3D::get_tether_slack);
	ClassDB::bind_method(D_METHOD("set_gravity_scale", "scale"), &FellowCloth3D::set_gravity_scale);
	ClassDB::bind_method(D_METHOD("get_gravity_scale"), &FellowCloth3D::get_gravity_scale);
	ClassDB::bind_method(D_METHOD("set_substep_rate", "rate"), &FellowCloth3D::set_substep_rate);
	ClassDB::bind_method(D_METHOD("get_substep_rate"), &FellowCloth3D::get_substep_rate);
	ClassDB::bind_method(D_METHOD("set_iterations", "iterations"), &FellowCloth3D::set_iterations);
	ClassDB::bind_method(D_METHOD("get_iterations"), &FellowCloth3D::get_iterations);
	ClassDB::bind_method(D_METHOD("set_max_substeps", "steps"), &FellowCloth3D::set_max_substeps);
	ClassDB::bind_method(D_METHOD("get_max_substeps"), &FellowCloth3D::get_max_substeps);
	ClassDB::bind_method(D_METHOD("set_wind_scale", "scale"), &FellowCloth3D::set_wind_scale);
	ClassDB::bind_method(D_METHOD("get_wind_scale"), &FellowCloth3D::get_wind_scale);
	ClassDB::bind_method(D_METHOD("set_wind", "wind"), &FellowCloth3D::set_wind);
	ClassDB::bind_method(D_METHOD("get_wind"), &FellowCloth3D::get_wind);
	ClassDB::bind_method(D_METHOD("set_drag", "drag"), &FellowCloth3D::set_drag);
	ClassDB::bind_method(D_METHOD("get_drag"), &FellowCloth3D::get_drag);
	ClassDB::bind_method(D_METHOD("set_lift", "lift"), &FellowCloth3D::set_lift);
	ClassDB::bind_method(D_METHOD("get_lift"), &FellowCloth3D::get_lift);
	ClassDB::bind_method(D_METHOD("set_turbulence", "turbulence"), &FellowCloth3D::set_turbulence);
	ClassDB::bind_method(D_METHOD("get_turbulence"), &FellowCloth3D::get_turbulence);
	ClassDB::bind_method(D_METHOD("set_thickness", "thickness"), &FellowCloth3D::set_thickness);
	ClassDB::bind_method(D_METHOD("get_thickness"), &FellowCloth3D::get_thickness);
	ClassDB::bind_method(D_METHOD("set_friction", "friction"), &FellowCloth3D::set_friction);
	ClassDB::bind_method(D_METHOD("get_friction"), &FellowCloth3D::get_friction);
	ClassDB::bind_method(D_METHOD("set_colliders", "colliders"), &FellowCloth3D::set_colliders);
	ClassDB::bind_method(D_METHOD("get_colliders"), &FellowCloth3D::get_colliders);
	ClassDB::bind_method(D_METHOD("set_collide_with_world", "enabled"), &FellowCloth3D::set_collide_with_world);
	ClassDB::bind_method(D_METHOD("is_collide_with_world"), &FellowCloth3D::is_collide_with_world);
	ClassDB::bind_method(D_METHOD("set_collision_mask", "mask"), &FellowCloth3D::set_collision_mask);
	ClassDB::bind_method(D_METHOD("get_collision_mask"), &FellowCloth3D::get_collision_mask);
	ClassDB::bind_method(D_METHOD("set_lod_distance", "distance"), &FellowCloth3D::set_lod_distance);
	ClassDB::bind_method(D_METHOD("get_lod_distance"), &FellowCloth3D::get_lod_distance);
	ClassDB::bind_method(D_METHOD("set_simulation_distance", "distance"), &FellowCloth3D::set_simulation_distance);
	ClassDB::bind_method(D_METHOD("get_simulation_distance"), &FellowCloth3D::get_simulation_distance);
	ClassDB::bind_method(D_METHOD("set_self_collision", "enabled"), &FellowCloth3D::set_self_collision);
	ClassDB::bind_method(D_METHOD("is_self_collision"), &FellowCloth3D::is_self_collision);
	ClassDB::bind_method(D_METHOD("set_self_collision_distance", "distance"), &FellowCloth3D::set_self_collision_distance);
	ClassDB::bind_method(D_METHOD("get_self_collision_distance"), &FellowCloth3D::get_self_collision_distance);
	ClassDB::bind_method(D_METHOD("set_self_collision_triangles", "enabled"), &FellowCloth3D::set_self_collision_triangles);
	ClassDB::bind_method(D_METHOD("is_self_collision_triangles"), &FellowCloth3D::is_self_collision_triangles);
	ClassDB::bind_method(D_METHOD("set_self_collision_friction", "friction"), &FellowCloth3D::set_self_collision_friction);
	ClassDB::bind_method(D_METHOD("get_self_collision_friction"), &FellowCloth3D::get_self_collision_friction);
	ClassDB::bind_method(D_METHOD("set_tear_strain", "strain"), &FellowCloth3D::set_tear_strain);
	ClassDB::bind_method(D_METHOD("get_tear_strain"), &FellowCloth3D::get_tear_strain);

	ADD_GROUP("Shape", "");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "source_mesh", PROPERTY_HINT_RESOURCE_TYPE, "Mesh"), "set_source_mesh", "get_source_mesh");
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR2, "size", PROPERTY_HINT_NONE, "suffix:m"), "set_size", "get_size");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "resolution", PROPERTY_HINT_RANGE, "1,128,1,or_greater"), "set_resolution", "get_resolution");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "pin_mode", PROPERTY_HINT_ENUM, "None,Top Edge,Left Edge,Top Corners,Corners,Vertex Color"), "set_pin_mode", "get_pin_mode");
	ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "skeleton_path", PROPERTY_HINT_NODE_PATH_VALID_TYPES, "Skeleton3D"), "set_skeleton_path", "get_skeleton_path");
	ADD_PROPERTY(PropertyInfo(Variant::PACKED_STRING_ARRAY, "pin_bones"), "set_pin_bones", "get_pin_bones");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "follow_skin"), "set_follow_skin", "is_following_skin");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "skin_max_distance", PROPERTY_HINT_RANGE, "0,1,0.001,or_greater,suffix:m"), "set_skin_max_distance", "get_skin_max_distance");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "skin", PROPERTY_HINT_RESOURCE_TYPE, "Skin"), "set_skin", "get_skin");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "material", PROPERTY_HINT_RESOURCE_TYPE, "BaseMaterial3D,ShaderMaterial"), "set_material", "get_material");
	ADD_GROUP("Fabric", "");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "simulating"), "set_simulating", "is_simulating");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "mass", PROPERTY_HINT_RANGE, "0.01,5,0.01,or_greater,suffix:kg/m²"), "set_mass", "get_mass");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "stretch_stiffness", PROPERTY_HINT_RANGE, "0,1,0.001"), "set_stretch_stiffness", "get_stretch_stiffness");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "bend_stiffness", PROPERTY_HINT_RANGE, "0,1,0.001"), "set_bend_stiffness", "get_bend_stiffness");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "damping", PROPERTY_HINT_RANGE, "0,10,0.01,or_greater"), "set_damping", "get_damping");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "tether_slack", PROPERTY_HINT_RANGE, "0,2,0.01"), "set_tether_slack", "get_tether_slack");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "gravity_scale", PROPERTY_HINT_RANGE, "-2,4,0.01,or_less,or_greater"), "set_gravity_scale", "get_gravity_scale");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "substep_rate", PROPERTY_HINT_RANGE, "60,2000,1,or_greater,suffix:Hz"), "set_substep_rate", "get_substep_rate");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "iterations", PROPERTY_HINT_RANGE, "1,8,1"), "set_iterations", "get_iterations");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "max_substeps", PROPERTY_HINT_RANGE, "1,64,1"), "set_max_substeps", "get_max_substeps");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "tear_strain", PROPERTY_HINT_RANGE, "0,4,0.01,or_greater"), "set_tear_strain", "get_tear_strain");
	ADD_GROUP("Air", "");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "wind_scale", PROPERTY_HINT_RANGE, "0,4,0.01,or_greater"), "set_wind_scale", "get_wind_scale");
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR3, "wind", PROPERTY_HINT_NONE, "suffix:m/s"), "set_wind", "get_wind");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "drag", PROPERTY_HINT_RANGE, "0,4,0.01,or_greater"), "set_drag", "get_drag");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "lift", PROPERTY_HINT_RANGE, "0,4,0.01,or_greater"), "set_lift", "get_lift");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "turbulence", PROPERTY_HINT_RANGE, "0,2,0.01,or_greater"), "set_turbulence", "get_turbulence");
	ADD_GROUP("Collision", "");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "thickness", PROPERTY_HINT_RANGE, "0,0.2,0.001,or_greater,suffix:m"), "set_thickness", "get_thickness");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "friction", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_friction", "get_friction");
	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "colliders", PROPERTY_HINT_TYPE_STRING, String::num_int64(Variant::NODE_PATH) + "/" + itos(PROPERTY_HINT_NODE_PATH_VALID_TYPES) + ":CollisionShape3D,CollisionObject3D"), "set_colliders", "get_colliders");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "collide_with_world"), "set_collide_with_world", "is_collide_with_world");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "collision_mask", PROPERTY_HINT_LAYERS_3D_PHYSICS), "set_collision_mask", "get_collision_mask");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "self_collision"), "set_self_collision", "is_self_collision");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "self_collision_distance", PROPERTY_HINT_RANGE, "0,0.5,0.001,or_greater,suffix:m"), "set_self_collision_distance", "get_self_collision_distance");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "self_collision_triangles"), "set_self_collision_triangles", "is_self_collision_triangles");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "self_collision_friction", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_self_collision_friction", "get_self_collision_friction");
	ADD_GROUP("", "");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "lod_distance", PROPERTY_HINT_RANGE, "0,500,0.1,or_greater,suffix:m"), "set_lod_distance", "get_lod_distance");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "simulation_distance", PROPERTY_HINT_RANGE, "0,500,0.1,or_greater,suffix:m"), "set_simulation_distance", "get_simulation_distance");

	ADD_SIGNAL(MethodInfo("torn", PropertyInfo(Variant::VECTOR3, "position"), PropertyInfo(Variant::INT, "edges")));

	BIND_ENUM_CONSTANT(PIN_NONE);
	BIND_ENUM_CONSTANT(PIN_TOP_EDGE);
	BIND_ENUM_CONSTANT(PIN_LEFT_EDGE);
	BIND_ENUM_CONSTANT(PIN_TOP_CORNERS);
	BIND_ENUM_CONSTANT(PIN_CORNERS);
	BIND_ENUM_CONSTANT(PIN_VERTEX_COLOR);
}
