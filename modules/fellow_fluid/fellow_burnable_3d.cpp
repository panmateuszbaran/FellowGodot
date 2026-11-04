/**************************************************************************/
/*  fellow_burnable_3d.cpp                                                */
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

#include "fellow_burnable_3d.h"

#include "core/config/engine.h"
#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "core/templates/hash_set.h"
#include "scene/3d/mesh_instance_3d.h"
#include "scene/3d/physics/collision_object_3d.h"
#include "scene/3d/physics/collision_shape_3d.h"
#include "scene/3d/physics/rigid_body_3d.h"
#include "scene/main/scene_tree.h"
#include "scene/resources/3d/world_3d.h"
#include "scene/resources/shader.h"
#include "servers/physics_3d/physics_server_3d.h"
#include "servers/rendering/fellow_settings.h"
#include "servers/rendering/renderer_rd/effects/fellow_gas.h"
#include "servers/rendering/rendering_server.h"

using FellowGas = RendererRD::FellowGas;

// The model's fixed constants (tests/burn_reference.py's Material).
static constexpr float BURN_AMBIENT = 293.0f;
static constexpr float BURN_CHAR_CONDUCTIVITY = 0.08f;
static constexpr float BURN_ASH_CONDUCTIVITY = 0.04f;
static constexpr float BURN_CHAR_SPECIFIC_HEAT = 1000.0f;
static constexpr float BURN_ASH_DENSITY = 60.0f;
static constexpr float BURN_ASH_SPECIFIC_HEAT = 800.0f;
static constexpr float BURN_PYROLYSIS_HEAT = 2.0e5f;
static constexpr float BURN_CHAR_VOLUME = 0.85f;
static constexpr float BURN_GLOW_HEAT = 4.0e6f;
static constexpr float BURN_CONVECTION = 20.0f;
static constexpr float BURN_EMISSIVITY = 0.9f;
static constexpr float BURN_SETTLE_RATE = 2.0f;
static constexpr float BURN_FUEL_PER_KG = 2.7f; // the gas's fuel per kg of volatiles at flame_strength 1 (each unit heats 1 m^3 of gas by 1400 K, 1.7 MJ: 30 % of the volatiles' 15 MJ/kg in the cells they burn in; Burn Lab's logs then have flames of the kindling's size)
static constexpr float BURN_FLAME_FLUX = 0.005f; // kg/(m^2 s): the volatiles' flux for a surface fully in its flame (wood's critical mass flux for flaming, 2.5-5 g/(m^2 s))
static constexpr int BURN_MARGIN = 2; // voxels around the mesh
static constexpr int BURN_GROUND_LAYERS = 64; // at most this many layers added down to the ground
static constexpr float BURN_LOOSE_ASH = 0.6f; // a voxel without wood has crumbled with this much ash per char, in ash volumes (fellow_burn_sim.glsl)
static constexpr int BURN_TRIANGLE_BUDGET = 300000;
static constexpr int BURN_VERTEX_ROW = 1024; // vertices per row of the marched vertices' texture (three texels each; FELLOW_BURN_VERTEX_CACHED)

Mutex FellowBurnable3D::body_mutex;
HashMap<uint64_t, int> FellowBurnable3D::burnable_bodies;

FellowBurnable3D::FellowBurnable3D() {
	burn_id = FellowGas::allocate_id();
	totals.instantiate();
	debug.instantiate();
	display_texture.instantiate();
	vertex_texture.instantiate();
}

FellowBurnable3D::~FellowBurnable3D() {
}

bool FellowBurnable3D::is_burnable_body(uint64_t p_object_id) {
	MutexLock lock(body_mutex);
	return burnable_bodies.has(p_object_id);
}

MeshInstance3D *FellowBurnable3D::_get_target() const {
	if (mesh_path.is_empty()) {
		return Object::cast_to<MeshInstance3D>(get_parent());
	}
	return Object::cast_to<MeshInstance3D>(get_node_or_null(mesh_path));
}

FellowGasVolume3D *FellowBurnable3D::_find_gas_volume() const {
	if (!gas_volume_path.is_empty()) {
		return Object::cast_to<FellowGasVolume3D>(get_node_or_null(gas_volume_path));
	}
	MeshInstance3D *target = _get_target();
	if (!target || !is_inside_tree()) {
		return nullptr;
	}
	const Vector3 centre = target->get_global_transform().xform(origin + Vector3(res) * voxel * 0.5f);
	const Vector<Node *> nodes = get_tree()->get_nodes_in_group(SNAME("fellow_gas_volumes"));
	for (Node *n : nodes) {
		FellowGasVolume3D *v = Object::cast_to<FellowGasVolume3D>(n);
		if (!v || !v->is_configured()) {
			continue;
		}
		const Vector3 g = v->get_grid_transform().affine_inverse().xform(centre);
		const Vector3i gs = v->get_grid_size();
		if (g.x >= 0.0f && g.y >= 0.0f && g.z >= 0.0f && g.x <= gs.x && g.y <= gs.y && g.z <= gs.z) {
			return v;
		}
	}
	return nullptr;
}

// ------------------------------------------------------------------ voxelization

// The mesh's volume fraction per voxel: along each axis, rays through 2 x 2 points per voxel column, the inside
// intervals between their crossings (parity) give each voxel's covered length; the voxel's fill is the median of the
// three axes' estimates (a hole in the mesh spoils at most the rays along one axis).
bool FellowBurnable3D::_voxelize(const Ref<Mesh> &p_mesh) {
	LocalVector<Vector3> tris;
	for (int s = 0; s < p_mesh->get_surface_count(); s++) {
		if (p_mesh->surface_get_primitive_type(s) != Mesh::PRIMITIVE_TRIANGLES) {
			continue;
		}
		const Array arrays = p_mesh->surface_get_arrays(s);
		const PackedVector3Array v = arrays[Mesh::ARRAY_VERTEX];
		const PackedInt32Array idx = arrays[Mesh::ARRAY_INDEX];
		if (idx.is_empty()) {
			for (int i = 0; i + 2 < v.size(); i += 3) {
				tris.push_back(v[i]);
				tris.push_back(v[i + 1]);
				tris.push_back(v[i + 2]);
			}
		} else {
			for (int i = 0; i + 2 < idx.size(); i += 3) {
				if (idx[i] < 0 || idx[i + 1] < 0 || idx[i + 2] < 0 || idx[i] >= v.size() || idx[i + 1] >= v.size() || idx[i + 2] >= v.size()) {
					continue;
				}
				tris.push_back(v[idx[i]]);
				tris.push_back(v[idx[i + 1]]);
				tris.push_back(v[idx[i + 2]]);
			}
		}
	}
	if (tris.size() < 3) {
		return false;
	}
	AABB box(tris[0], Vector3());
	for (const Vector3 &p : tris) {
		box.expand_to(p);
	}
	const float longest = box.get_longest_axis_size();
	if (longest < 1e-6f) {
		return false;
	}
	grain = Vector3();
	grain[box.get_longest_axis_index()] = 1.0f; // the wood's grain along its longest side (its cracks)
	voxel = longest / float(resolution);
	for (int a = 0; a < 3; a++) {
		res[a] = MAX(1, int(Math::ceil(box.size[a] / voxel - 1e-4f))) + 2 * BURN_MARGIN;
	}
	origin = box.get_center() - Vector3(res) * voxel * 0.5f;
	// The box reaches down to the ground (the physics bodies below the object), so the ash falls onto it: its floor is
	// the layer just above the ground, not the margin below it.
	ground_axis = -1;
	ground_floor = 0;
	MeshInstance3D *target = Object::cast_to<MeshInstance3D>(ObjectDB::get_instance(target_id));
	if (target) {
		int axis = 1, sign = -1;
		float below = 0.0f;
		const bool ground = _ground_distance(target, box, axis, sign, below);
		heap_axis = axis;
		heap_sign = sign;
		if (ground) {
			const float g = sign < 0 ? box.position[axis] - below : box.position[axis] + box.size[axis] + below; // local
			const float k = (g - origin[axis]) / voxel;
			origin[axis] += (k - Math::round(k)) * voxel; // a face between layers on the ground (half a voxel at most)
			if (sign < 0 && g < origin[axis]) {
				const int extra = MIN(int(Math::ceil((origin[axis] - g) / voxel - 1e-4f)), BURN_GROUND_LAYERS);
				res[axis] += extra;
				origin[axis] -= extra * voxel;
			} else if (sign > 0 && g > origin[axis] + res[axis] * voxel) {
				res[axis] += MIN(int(Math::ceil((g - origin[axis] - res[axis] * voxel) / voxel - 1e-4f)), BURN_GROUND_LAYERS);
			}
			const int at = int(Math::round((g - origin[axis]) / voxel)); // the ground's face between layers at-1 and at
			ground_floor = CLAMP(sign < 0 ? at : at - 1, 0, res[axis] - 1);
			ground_axis = axis;
			ground_sign = sign;
		}
	}
	const int64_t count = int64_t(res.x) * res.y * res.z;
	LocalVector<float> cov[3];
	for (int a = 0; a < 3; a++) {
		cov[a].resize(count);
		for (int64_t i = 0; i < count; i++) {
			cov[a][i] = 0.0f;
		}
	}
	for (int a = 0; a < 3; a++) {
		const int u = (a + 1) % 3;
		const int w = (a + 2) % 3;
		const int nu = res[u] * 2;
		const int nw = res[w] * 2;
		LocalVector<LocalVector<float>> hits;
		hits.resize(nu * nw);
		// The rays at x = k, y = l in these units (offset a little: no ray through a vertex or along an edge).
		const double ju = 0.01373;
		const double jw = 0.02917;
		const double half = double(voxel) * 0.5;
		for (uint32_t t = 0; t + 2 < tris.size(); t += 3) {
			double x[3], y[3], z[3];
			for (int i = 0; i < 3; i++) {
				const Vector3 &p = tris[t + i];
				x[i] = (double(p[u]) - origin[u]) / half - 0.5 - ju;
				y[i] = (double(p[w]) - origin[w]) / half - 0.5 - jw;
				z[i] = (double(p[a]) - origin[a]) / double(voxel);
			}
			const double area = (x[1] - x[0]) * (y[2] - y[0]) - (x[2] - x[0]) * (y[1] - y[0]);
			if (Math::abs(area) < 1e-14) {
				continue;
			}
			const int k0 = MAX(0, int(Math::ceil(MIN(x[0], MIN(x[1], x[2])))));
			const int k1 = MIN(nu - 1, int(Math::floor(MAX(x[0], MAX(x[1], x[2])))));
			const int l0 = MAX(0, int(Math::ceil(MIN(y[0], MIN(y[1], y[2])))));
			const int l1 = MIN(nw - 1, int(Math::floor(MAX(y[0], MAX(y[1], y[2])))));
			for (int l = l0; l <= l1; l++) {
				for (int k = k0; k <= k1; k++) {
					const double e0 = (x[2] - x[1]) * (l - y[1]) - (y[2] - y[1]) * (k - x[1]);
					const double e1 = (x[0] - x[2]) * (l - y[2]) - (y[0] - y[2]) * (k - x[2]);
					const double e2 = (x[1] - x[0]) * (l - y[0]) - (y[1] - y[0]) * (k - x[0]);
					const bool in = (e0 >= 0.0 && e1 >= 0.0 && e2 >= 0.0) || (e0 <= 0.0 && e1 <= 0.0 && e2 <= 0.0);
					if (!in) {
						continue;
					}
					const double zz = (e0 * z[0] + e1 * z[1] + e2 * z[2]) / area;
					hits[k + l * nu].push_back(float(zz));
				}
			}
		}
		for (int l = 0; l < nw; l++) {
			for (int k = 0; k < nu; k++) {
				LocalVector<float> &h = hits[k + l * nu];
				if (h.is_empty() || (h.size() & 1u)) {
					continue; // an odd count: an open mesh along this ray
				}
				h.sort();
				Vector3i c;
				c[u] = k >> 1;
				c[w] = l >> 1;
				for (uint32_t i = 0; i + 1 < h.size(); i += 2) {
					const float z0 = CLAMP(h[i], 0.0f, float(res[a]));
					const float z1 = CLAMP(h[i + 1], 0.0f, float(res[a]));
					for (int v = int(Math::floor(z0)); v < int(Math::ceil(z1)) && v < res[a]; v++) {
						const float o = MIN(z1, float(v + 1)) - MAX(z0, float(v));
						if (o <= 0.0f) {
							continue;
						}
						c[a] = v;
						cov[a][c.x + int64_t(res.x) * (c.y + int64_t(res.y) * c.z)] += o * 0.25f;
					}
				}
			}
		}
	}
	initial_fill.resize(count);
	float *f = initial_fill.ptrw();
	initial_volume = 0.0;
	for (int a = 0; a < 3; a++) {
		occupied_min[a] = res[a];
		occupied_max[a] = -1;
	}
	for (int64_t i = 0; i < count; i++) {
		const float c0 = cov[0][i], c1 = cov[1][i], c2 = cov[2][i];
		float m = MAX(MIN(c0, c1), MIN(MAX(c0, c1), c2)); // the median
		m = CLAMP(m, 0.0f, 1.0f);
		if (m < 0.02f) {
			m = 0.0f;
		}
		f[i] = m;
		initial_volume += m;
		if (m > 0.0f) {
			const int x = int(i % res.x);
			const int y = int((i / res.x) % res.y);
			const int z = int(i / (int64_t(res.x) * res.y));
			const int c[3] = { x, y, z };
			for (int a = 0; a < 3; a++) {
				occupied_min[a] = MIN(occupied_min[a], c[a]);
				occupied_max[a] = MAX(occupied_max[a], c[a]);
			}
		}
	}
	return initial_volume > 0.0;
}

// The ground below the mesh's box (along the voxel axis nearest to the world's down: r_axis, r_sign): a ray down from
// the box's lowest point against the physics bodies (not the object's own), at most 2 m. r_below: how far below the
// box's lowest point it is, in the mesh's units. False without ground.
bool FellowBurnable3D::_ground_distance(MeshInstance3D *p_target, const AABB &p_box, int &r_axis, int &r_sign, float &r_below) const {
	if (!p_target->is_inside_tree()) {
		return false;
	}
	const Transform3D m = p_target->get_global_transform();
	const Vector3 down = m.basis.inverse().xform(Vector3(0, -1, 0));
	r_axis = 1;
	for (int a = 0; a < 3; a++) {
		if (Math::abs(down[a]) > Math::abs(down[r_axis]) + 1e-6f) {
			r_axis = a;
		}
	}
	r_sign = down[r_axis] < 0.0f ? -1 : 1;
	Ref<World3D> world = p_target->get_world_3d();
	PhysicsDirectSpaceState3D *space = world.is_valid() ? world->get_direct_space_state() : nullptr;
	if (!space) {
		return false;
	}
	float low = 1e30f;
	Vector3 centre = m.xform(p_box.get_center());
	for (int c = 0; c < 8; c++) {
		low = MIN(low, m.xform(p_box.get_endpoint(c)).y);
	}
	const float scale = Math::pow(Math::abs(m.basis.determinant()), 1.0f / 3.0f);
	const float h = voxel * scale;
	PhysicsDirectSpaceState3D::RayParameters params;
	params.from = Vector3(centre.x, low + 0.25f * h, centre.z);
	params.to = params.from - Vector3(0, 2.0f, 0);
	HashSet<RID> exclude;
	if (CollisionObject3D *own = Object::cast_to<CollisionObject3D>(ObjectDB::get_instance(body_id))) {
		exclude.insert(own->get_rid());
	}
	for (Node *n = p_target; n; n = n->get_parent()) {
		if (CollisionObject3D *co = Object::cast_to<CollisionObject3D>(n)) {
			exclude.insert(co->get_rid());
			break;
		}
	}
	params.exclude = exclude;
	PhysicsDirectSpaceState3D::RayResult hit;
	if (!space->intersect_ray(params, hit)) {
		return false;
	}
	r_below = (low - hit.position.y) / MAX(scale, 1e-6f);
	return true;
}

float FellowBurnable3D::_fill_at(const Vector3 &p_local) const {
	// As the GPU's trilinear sample of the display texture (clamped to the edge texels).
	const Vector3 g = (p_local - origin) / voxel - Vector3(0.5f, 0.5f, 0.5f);
	const int x0 = int(Math::floor(g.x)), y0 = int(Math::floor(g.y)), z0 = int(Math::floor(g.z));
	const float fx = g.x - x0, fy = g.y - y0, fz = g.z - z0;
	const float *f = initial_fill.ptr();
	auto at = [&](int p_x, int p_y, int p_z) {
		p_x = CLAMP(p_x, 0, res.x - 1);
		p_y = CLAMP(p_y, 0, res.y - 1);
		p_z = CLAMP(p_z, 0, res.z - 1);
		return f[p_x + int64_t(res.x) * (p_y + int64_t(res.y) * p_z)];
	};
	const float c00 = Math::lerp(at(x0, y0, z0), at(x0 + 1, y0, z0), fx);
	const float c10 = Math::lerp(at(x0, y0 + 1, z0), at(x0 + 1, y0 + 1, z0), fx);
	const float c01 = Math::lerp(at(x0, y0, z0 + 1), at(x0 + 1, y0, z0 + 1), fx);
	const float c11 = Math::lerp(at(x0, y0 + 1, z0 + 1), at(x0 + 1, y0 + 1, z0 + 1), fx);
	return Math::lerp(Math::lerp(c00, c10, fy), Math::lerp(c01, c11, fy), fz);
}

// ------------------------------------------------------------------ the display mesh

static Vector2 _fellow_oct_encode(Vector3 p_n) {
	p_n /= MAX(Math::abs(p_n.x) + Math::abs(p_n.y) + Math::abs(p_n.z), 1e-8f);
	Vector2 e(p_n.x, p_n.y);
	if (p_n.z < 0.0f) {
		e = Vector2((1.0f - Math::abs(p_n.y)) * (p_n.x >= 0.0f ? 1.0f : -1.0f), (1.0f - Math::abs(p_n.x)) * (p_n.y >= 0.0f ? 1.0f : -1.0f));
	}
	return e;
}

namespace {
struct DisplayBuilder {
	// All surfaces' vertices in one array (the edges shared between surfaces split together: no cracks).
	LocalVector<Vector3> pos;
	LocalVector<Vector3> nrm;
	LocalVector<Plane> tan;
	LocalVector<Color> col;
	LocalVector<Vector2> uv;
	LocalVector<Vector2> uv2;
	LocalVector<uint32_t> pid; // the position's id (copies of a vertex at a seam share it)
	LocalVector<uint32_t> tris; // 3 per triangle
	LocalVector<int> tri_surface;
	HashMap<Vector3, uint32_t> pos_ids;
	HashMap<uint64_t, uint32_t> mids;

	uint32_t position_id(const Vector3 &p_p) {
		const Vector3 k(p_p.x + 0.0f, p_p.y + 0.0f, p_p.z + 0.0f); // -0 as 0
		const uint32_t *f = pos_ids.getptr(k);
		if (f) {
			return *f;
		}
		const uint32_t id = pos_ids.size();
		pos_ids.insert(k, id);
		return id;
	}
	uint32_t add(const Vector3 &p_p, const Vector3 &p_n, const Plane &p_t, const Color &p_c, const Vector2 &p_uv, const Vector2 &p_uv2) {
		pos.push_back(p_p);
		nrm.push_back(p_n);
		tan.push_back(p_t);
		col.push_back(p_c);
		uv.push_back(p_uv);
		uv2.push_back(p_uv2);
		pid.push_back(position_id(p_p));
		return pos.size() - 1;
	}
	uint32_t mid(uint32_t p_a, uint32_t p_b) {
		const uint64_t key = (uint64_t(MIN(p_a, p_b)) << 32) | uint64_t(MAX(p_a, p_b));
		const uint32_t *f = mids.getptr(key);
		if (f) {
			return *f;
		}
		const Vector3 n = (nrm[p_a] + nrm[p_b]);
		const Vector3 t = (tan[p_a].normal + tan[p_b].normal);
		const uint32_t m = add((pos[p_a] + pos[p_b]) * 0.5f, n.length() > 1e-8f ? n.normalized() : nrm[p_a],
				Plane(t.length() > 1e-8f ? t.normalized() : tan[p_a].normal, tan[p_a].d), (col[p_a] + col[p_b]) * 0.5f,
				(uv[p_a] + uv[p_b]) * 0.5f, (uv2[p_a] + uv2[p_b]) * 0.5f);
		mids.insert(key, m);
		return m;
	}
	uint64_t edge_key(uint32_t p_a, uint32_t p_b) const {
		const uint32_t a = pid[p_a], b = pid[p_b];
		return (uint64_t(MIN(a, b)) << 32) | uint64_t(MAX(a, b));
	}
	// Conforming longest-edge refinement until no edge is longer than p_max (or the budget is reached): per pass the
	// longest edge of every triangle above it is marked, then every triangle with a marked edge has its longest edge
	// marked too (repeated until nothing changes), then each triangle is bisected along its longest edge and its
	// halves along their other marked edges. Edges are marked by their positions (seams split together).
	void refine(float p_max, int p_budget) {
		const float max2 = p_max * p_max;
		for (int pass = 0; pass < 24; pass++) {
			const uint32_t nt = tris.size() / 3;
			LocalVector<uint8_t> longest;
			longest.resize(nt);
			HashSet<uint64_t> marked;
			for (uint32_t t = 0; t < nt; t++) {
				float best = -1.0f;
				uint8_t L = 0;
				for (int e = 0; e < 3; e++) {
					const float l2 = pos[tris[t * 3 + e]].distance_squared_to(pos[tris[t * 3 + (e + 1) % 3]]);
					if (l2 > best) {
						best = l2;
						L = uint8_t(e);
					}
				}
				longest[t] = L;
				if (best > max2) {
					marked.insert(edge_key(tris[t * 3 + L], tris[t * 3 + (L + 1) % 3]));
				}
			}
			if (marked.is_empty()) {
				return;
			}
			bool changed = true;
			while (changed) {
				changed = false;
				for (uint32_t t = 0; t < nt; t++) {
					const uint32_t *v = &tris[t * 3];
					const uint64_t kl = edge_key(v[longest[t]], v[(longest[t] + 1) % 3]);
					if (marked.has(kl)) {
						continue;
					}
					if (marked.has(edge_key(v[0], v[1])) || marked.has(edge_key(v[1], v[2])) || marked.has(edge_key(v[2], v[0]))) {
						marked.insert(kl);
						changed = true;
					}
				}
			}
			int64_t total = 0;
			for (uint32_t t = 0; t < nt; t++) {
				const uint32_t *v = &tris[t * 3];
				total += 1 + int(marked.has(edge_key(v[0], v[1]))) + int(marked.has(edge_key(v[1], v[2]))) + int(marked.has(edge_key(v[2], v[0])));
			}
			if (total > p_budget) {
				return;
			}
			LocalVector<uint32_t> out;
			LocalVector<int> out_surface;
			out.reserve(total * 3);
			auto emit = [&](uint32_t p_a, uint32_t p_b, uint32_t p_c, int p_s) {
				out.push_back(p_a);
				out.push_back(p_b);
				out.push_back(p_c);
				out_surface.push_back(p_s);
			};
			for (uint32_t t = 0; t < nt; t++) {
				const int L = longest[t];
				const uint32_t a = tris[t * 3 + L];
				const uint32_t b = tris[t * 3 + (L + 1) % 3];
				const uint32_t c = tris[t * 3 + (L + 2) % 3];
				const int s = tri_surface[t];
				if (!marked.has(edge_key(a, b))) {
					emit(a, b, c, s);
					continue;
				}
				const uint32_t m = mid(a, b);
				if (marked.has(edge_key(c, a))) {
					const uint32_t n = mid(c, a);
					emit(a, m, n, s);
					emit(m, c, n, s);
				} else {
					emit(a, m, c, s);
				}
				if (marked.has(edge_key(b, c))) {
					const uint32_t q = mid(b, c);
					emit(m, b, q, s);
					emit(m, q, c, s);
				} else {
					emit(m, b, c, s);
				}
			}
			tris = out;
			tri_surface = out_surface;
		}
	}
};
} // namespace

static const char *FELLOW_BURN_DECLARATIONS = R"(
// FellowEngine burning (FellowBurnable3D, docs/decisions/0120-burning.md).
uniform sampler3D fellow_burn_state : filter_linear, repeat_disable; // temperature above ambient, wood, char, ash
uniform vec3 fellow_burn_origin; // the voxels' corner (the mesh's local space)
uniform vec3 fellow_burn_inv_extent; // 1 / the box's size
uniform float fellow_burn_voxel = 0.05;
uniform float fellow_burn_step = 0.025; // the march's step
uniform int fellow_burn_steps = 64;
uniform vec3 fellow_burn_char_color : source_color = vec3(0.045, 0.038, 0.034);
uniform vec3 fellow_burn_ash_color : source_color = vec3(0.56, 0.54, 0.51);
uniform float fellow_burn_glow = 4.0;
uniform float fellow_burn_crack_scale = 0.025;
uniform float fellow_burn_loose_ash = 0.09; // ash per char at which a voxel without wood has crumbled (the simulation's)
uniform vec3 fellow_burn_grain = vec3(0.0, 1.0, 0.0); // the wood's grain: the mesh's longest axis (local)
varying vec3 fellow_burn_local;
varying vec4 fellow_burn_mat;
varying float fellow_burn_gone;

vec4 fellow_burn_sample(vec3 p_local) {
	return texture(fellow_burn_state, (p_local - fellow_burn_origin) * fellow_burn_inv_extent);
}

float fellow_burn_fill(vec3 p_local) {
	vec4 s = fellow_burn_sample(p_local);
	return s.y + s.z + s.w;
}

// Whether a voxel's material is loose: crumbled (no wood; its char burned far, or next to none), as the simulation's
// SETTLE tells.
bool fellow_burn_loose(vec4 p_s) {
	return p_s.y < 0.05 && (p_s.y + p_s.z < 0.05 || p_s.w >= fellow_burn_loose_ash * p_s.z);
}

// The wood and char standing (not the ash, nor the crumbled debris lying in the heap).
float fellow_burn_solid(vec3 p_local) {
	vec4 s = fellow_burn_sample(p_local);
	return fellow_burn_loose(s) ? 0.0 : s.y + s.z;
}

vec3 fellow_burn_oct_decode(vec2 p_e) {
	vec3 n = vec3(p_e, 1.0 - abs(p_e.x) - abs(p_e.y));
	float t = max(-n.z, 0.0);
	n.x += n.x >= 0.0 ? -t : t;
	n.y += n.y >= 0.0 ? -t : t;
	return normalize(n);
}

vec3 fellow_burn_hash3(vec3 p_p) {
	p_p = vec3(dot(p_p, vec3(127.1, 311.7, 74.7)), dot(p_p, vec3(269.5, 183.3, 246.1)), dot(p_p, vec3(113.5, 271.9, 124.6)));
	return fract(sin(p_p) * 43758.5453123);
}

// A cellular pattern at p_p: x 1 on the cracks between cells, y a random value per cell, z the distance to the cell's
// point, w to its border (the second point's distance less the first's).
vec4 fellow_burn_cells(vec3 p_p) {
	vec3 i = floor(p_p);
	vec3 f = p_p - i;
	float d1 = 8.0;
	float d2 = 8.0;
	vec3 best = i;
	for (int z = -1; z <= 1; z++) {
		for (int y = -1; y <= 1; y++) {
			for (int x = -1; x <= 1; x++) {
				vec3 o = vec3(float(x), float(y), float(z));
				vec3 r = o + fellow_burn_hash3(i + o) - f;
				float d = dot(r, r);
				if (d < d1) {
					d2 = d1;
					d1 = d;
					best = i + o;
				} else if (d < d2) {
					d2 = d;
				}
			}
		}
	}
	float e1 = sqrt(d1);
	float e = sqrt(d2) - e1;
	return vec4(1.0 - smoothstep(0.0, 0.06, e), fellow_burn_hash3(best + vec3(17.0, 31.0, 7.0)).x, e1, e);
}

// The wood's own space for its cracks: cells 1.8 times as long along the grain as across (charred wood's alligator
// pattern).
vec3 fellow_burn_grain_space(vec3 p_p) {
	return p_p - 0.45 * dot(p_p, fellow_burn_grain) * fellow_burn_grain;
}

// Smooth value noise, 0..1.
float fellow_burn_noise(vec3 p_p) {
	vec3 i = floor(p_p);
	vec3 f = p_p - i;
	f = f * f * (3.0 - 2.0 * f);
	float n000 = fellow_burn_hash3(i).x;
	float n100 = fellow_burn_hash3(i + vec3(1.0, 0.0, 0.0)).x;
	float n010 = fellow_burn_hash3(i + vec3(0.0, 1.0, 0.0)).x;
	float n110 = fellow_burn_hash3(i + vec3(1.0, 1.0, 0.0)).x;
	float n001 = fellow_burn_hash3(i + vec3(0.0, 0.0, 1.0)).x;
	float n101 = fellow_burn_hash3(i + vec3(1.0, 0.0, 1.0)).x;
	float n011 = fellow_burn_hash3(i + vec3(0.0, 1.0, 1.0)).x;
	float n111 = fellow_burn_hash3(i + vec3(1.0, 1.0, 1.0)).x;
	return mix(mix(mix(n000, n100, f.x), mix(n010, n110, f.x), f.y), mix(mix(n001, n101, f.x), mix(n011, n111, f.x), f.y), f.z);
}

// A blackbody's colour (linear, its brightest channel 1), roughly, 800-2000 K: deep red to orange-yellow.
vec3 fellow_burn_blackbody(float p_t) {
	float x = clamp((p_t - 800.0) / 1200.0, 0.0, 1.0);
	return vec3(1.0, mix(0.07, 0.45, x), mix(0.003, 0.12, x * x));
}
)";

static const char *FELLOW_BURN_VERTEX = R"(
	// FellowEngine burning: the vertex marches into the voxels (along its stored direction) to the wood and char's
	// surface now (through any ash on it), and moves by how far that surface has receded since the start. Where it
	// finds none (burned through: only ash or nothing left), the triangles touching it are not drawn; the ash on the
	// ground is drawn by its own mesh (FellowAshHeap).
	fellow_burn_local = VERTEX;
	fellow_burn_gone = 0.0;
	fellow_burn_mat = vec4(0.0, 1.0, 0.0, 0.0);
	{
		vec4 fb_c = CUSTOM_CHANNEL;
		if (fb_c.y > 0.0) {
			vec3 fb_d = fellow_burn_oct_decode(fb_c.zw);
			float fb_thr = 0.5 * fb_c.y;
			float fb_prev = fellow_burn_solid(VERTEX);
			float fb_t = -1.0;
			if (fb_prev > fb_thr) {
				fb_t = 0.0;
			} else {
				for (int i = 1; i <= fellow_burn_steps; i++) {
					float t = float(i) * fellow_burn_step;
					float f = fellow_burn_solid(VERTEX + fb_d * t);
					if (f > fb_thr) {
						fb_t = t - fellow_burn_step * (f - fb_thr) / max(f - fb_prev, 1e-6);
						break;
					}
					fb_prev = f;
				}
			}
			if (fb_t < 0.0) {
				fellow_burn_gone = 1.0;
			} else {
				float fb_disp = max(fb_t - fb_c.x - 0.15 * fellow_burn_voxel, 0.0);
				VERTEX += fb_d * fb_disp;
				if (fb_disp > 0.0) {
					float h = fellow_burn_voxel;
					vec3 g = vec3(fellow_burn_solid(VERTEX + vec3(h, 0.0, 0.0)) - fellow_burn_solid(VERTEX - vec3(h, 0.0, 0.0)),
							fellow_burn_solid(VERTEX + vec3(0.0, h, 0.0)) - fellow_burn_solid(VERTEX - vec3(0.0, h, 0.0)),
							fellow_burn_solid(VERTEX + vec3(0.0, 0.0, h)) - fellow_burn_solid(VERTEX - vec3(0.0, 0.0, h)));
					if (length(g) > 1e-4) {
						NORMAL = normalize(mix(NORMAL, -normalize(g), smoothstep(0.0, fellow_burn_voxel, fb_disp)));
					}
				}
				// The composition at the surface (the ash on it greys it) and just inside.
				fellow_burn_mat = fellow_burn_sample(VERTEX + fb_d * (0.25 * fellow_burn_voxel));
			}
		}
	}
)";

// The same with the march done once per frame with steps (FellowGas's VERTICES, the same expressions) for every vertex
// of the display, read here by the vertex's index: the passes drawing the mesh (the depth prepass, the opaque pass, each
// shadow) fetch three texels instead of marching. Only where VERTEX and NORMAL are the mesh's here (the source's
// vertex code does not change them, nor works in world space).
static const char *FELLOW_BURN_VERTEX_CACHED = R"(
	// FellowEngine burning: the vertex where the march into the voxels moved it (FellowGas's VERTICES, once per frame
	// with steps; FellowBurnable3D's FELLOW_BURN_VERTEX does the same here). Where the march found no surface, the
	// triangles touching it are not drawn.
	fellow_burn_local = VERTEX;
	fellow_burn_gone = 0.0;
	fellow_burn_mat = vec4(0.0, 1.0, 0.0, 0.0);
	if (CUSTOM_CHANNEL.y > 0.0) {
		int fb_id = VERTEX_ID + fellow_burn_vertex_offset;
		ivec2 fb_at = ivec2((fb_id % BURN_VERTEX_ROW) * 3, fb_id / BURN_VERTEX_ROW);
		vec4 fb_v = texelFetch(fellow_burn_vertices, fb_at, 0);
		vec4 fb_n = texelFetch(fellow_burn_vertices, fb_at + ivec2(1, 0), 0);
		VERTEX = fb_v.xyz;
		fellow_burn_gone = fb_v.w;
		if (fb_n.w >= 0.0) { // the surface's normal and how far the vertex's turns to it
			NORMAL = normalize(mix(NORMAL, fb_n.xyz, fb_n.w));
		}
		fellow_burn_mat = texelFetch(fellow_burn_vertices, fb_at + ivec2(2, 0), 0);
	}
)";

static const char *FELLOW_BURN_VERTEX_CACHED_DECLARATIONS = R"(
uniform sampler2D fellow_burn_vertices : filter_nearest, repeat_disable; // the marched vertices: position + gone, normal + weight, composition
uniform int fellow_burn_vertex_offset = 0; // the surface's first vertex among the display's
)";

// The ash heap (FellowAshHeap): a grid over the box's floor (two vertices per voxel along each side), each vertex risen by
// the loose material in the columns around it (the ash and the crumbled char, the simulation's SETTLE: their volume
// over a column's area, smoothed), so the heap thins out at its edges, which are ragged; lumps of charcoal in it (as
// many as the char's share) raised and cracked, the ash between them powdery; embers glow in the lumps' cracks and
// dully through the ash while hot. The columns without loose material stay at the floor, their triangles not drawn.
static const char *FELLOW_HEAP_CODE = R"(
uniform vec3 fellow_heap_up; // the mesh's local space, away from the floor
uniform vec3 fellow_heap_u; // the grid's axes, a voxel long
uniform vec3 fellow_heap_v;

// The loose material's height in the column standing at p_floor (from the floor up to the first voxel of standing wood
// or char, the shell's, or less than a quarter full: the heap's top), the char's in it, the top's temperature (above
// ambient).
vec3 fellow_heap_column(vec3 p_floor) {
	vec3 r = vec3(0.0);
	for (int i = 0; i < fellow_burn_steps; i++) {
		vec4 s = fellow_burn_sample(p_floor + fellow_heap_up * ((float(i) + 0.5) * fellow_burn_voxel));
		float f = s.y + s.z + s.w;
		if (f < 0.02 || !fellow_burn_loose(s)) {
			break;
		}
		r.x += f * fellow_burn_voxel;
		r.y += (s.y + s.z) * fellow_burn_voxel;
		r.z = s.x;
		if (f < 0.25) {
			break;
		}
	}
	return r;
}

// The pieces of charcoal in the heap at p_local: cells of 1.6 crack scales, longer along the burned wood's grain, each
// a piece shrunk from its cell's border by a random amount (pieces of many sizes, the ash between them), as many as the
// char's share p_char allows (at most seven in ten); x the piece (0..1), y its relief (local units), z its cell's random
// value.
vec3 fellow_heap_pieces(vec3 p_local, float p_char) {
	vec4 c = fellow_burn_cells(fellow_burn_grain_space(p_local) / max(1.6 * fellow_burn_crack_scale, 1e-4));
	float shrink = 0.04 + 0.3 * fract(c.y * 13.7);
	float piece = (c.y < min(p_char * 1.2, 0.7) ? 1.0 : 0.0) * smoothstep(shrink, shrink + 0.06, c.w);
	return vec3(piece, piece * smoothstep(shrink, shrink + 0.3, c.w) * 0.7 * fellow_burn_crack_scale, c.y);
}

varying float fellow_heap_margin; // how much thicker the heap is than its (ragged) edge: below 0 not drawn

void vertex() {
	fellow_burn_local = VERTEX;
	fellow_burn_gone = 0.0;
	fellow_burn_mat = vec4(0.0, 0.0, 0.0, 1.0);
	// The heights of the 3 x 3 columns around: smoothed (1 2 1) and their slope (Sobel).
	float fb_c[9];
	vec3 fb_mid = vec3(0.0);
	for (int j = 0; j < 3; j++) {
		for (int i = 0; i < 3; i++) {
			vec3 col = fellow_heap_column(VERTEX + fellow_heap_u * float(i - 1) + fellow_heap_v * float(j - 1));
			fb_c[j * 3 + i] = col.x;
			if (i == 1 && j == 1) {
				fb_mid = col;
			}
		}
	}
	float fb_h = (fb_c[0] + fb_c[2] + fb_c[6] + fb_c[8] + 2.0 * (fb_c[1] + fb_c[3] + fb_c[5] + fb_c[7]) + 4.0 * fb_c[4]) / 16.0;
	// Lumpy, not a pillow: the height times a smooth noise (and its slope into the normal's).
	float fb_ls = 1.0 / max(4.0 * fellow_burn_crack_scale, 1e-4);
	vec3 fb_lo = vec3(5.3, 1.7, 9.1);
	float fb_m = 0.75 + 0.5 * fellow_burn_noise(VERTEX * fb_ls + fb_lo);
	vec3 fb_eu = normalize(fellow_heap_u) * (0.25 / fb_ls);
	vec3 fb_ev = normalize(fellow_heap_v) * (0.25 / fb_ls);
	float fb_mu = 0.5 * (fellow_burn_noise((VERTEX + fb_eu) * fb_ls + fb_lo) - fellow_burn_noise((VERTEX - fb_eu) * fb_ls + fb_lo)) / (0.5 / fb_ls);
	float fb_mv = 0.5 * (fellow_burn_noise((VERTEX + fb_ev) * fb_ls + fb_lo) - fellow_burn_noise((VERTEX - fb_ev) * fb_ls + fb_lo)) / (0.5 / fb_ls);
	float fb_h0 = fb_h;
	fb_h *= fb_m;
	// Ragged edges: the heap ends where it is thinner than a noisy threshold (the outline where the margin, interpolated
	// across the triangles, crosses zero), meeting the floor there.
	float fb_edge = (0.06 + 0.35 * fellow_burn_noise(VERTEX / max(3.0 * fellow_burn_crack_scale, 1e-4))) * fellow_burn_voxel;
	fellow_heap_margin = fb_h - fb_edge;
	float fb_share = fb_mid.y / max(fb_mid.x, 1e-6);
	vec3 fb_piece = fellow_heap_pieces(VERTEX, fb_share);
	float fb_hold = smoothstep(0.0, 0.6 * fellow_burn_voxel, fellow_heap_margin); // a thin heap holds no pieces
	VERTEX += fellow_heap_up * (max(fellow_heap_margin, 0.0) + 0.002 + fb_piece.y * fb_hold);
	float fb_gu = ((fb_c[2] + 2.0 * fb_c[5] + fb_c[8]) - (fb_c[0] + 2.0 * fb_c[3] + fb_c[6])) / (8.0 * fellow_burn_voxel) * fb_m + fb_h0 * fb_mu;
	float fb_gv = ((fb_c[6] + 2.0 * fb_c[7] + fb_c[8]) - (fb_c[0] + 2.0 * fb_c[1] + fb_c[2])) / (8.0 * fellow_burn_voxel) * fb_m + fb_h0 * fb_mv;
	NORMAL = normalize(fellow_heap_up - normalize(fellow_heap_u) * fb_gu - normalize(fellow_heap_v) * fb_gv);
	fellow_burn_mat = vec4(fb_mid.z, 0.0, fb_mid.y, max(fb_mid.x - fb_mid.y, 0.0));
}

void fragment() {
	if (fellow_heap_margin < 0.0) {
		discard;
	}
	vec4 fb = fellow_burn_mat;
	float fb_sum = max(fb.y + fb.z + fb.w, 1e-4);
	float fb_char = (fb.y + fb.z) / fb_sum;
	// Pieces of charcoal, a few small bits between them, half buried in the ash and dusted with it; further away, their
	// average. The ash grey, mottled, specked with char dust.
	vec3 fb_q = fellow_burn_local / max(1.6 * fellow_burn_crack_scale, 1e-4);
	float fb_blur = smoothstep(0.25, 0.8, length(fwidth(fb_q)));
	vec3 fb_piece = fellow_heap_pieces(fellow_burn_local, fb_char);
	vec4 fb_bits = fellow_burn_cells(fellow_burn_local / max(0.6 * fellow_burn_crack_scale, 1e-4));
	float fb_bit = (fb_bits.y < fb_char * 0.3 ? 1.0 : 0.0) * smoothstep(0.15, 0.25, fb_bits.w) * (1.0 - fb_piece.x);
	float fb_dust = smoothstep(0.4, 0.85, fellow_burn_noise(fellow_burn_local / max(0.5 * fellow_burn_crack_scale, 1e-4)));
	float fb_coal_share = mix(max(fb_piece.x, fb_bit) * (1.0 - 0.5 * fb_dust), min(fb_char * 1.2, 0.7) * 0.6, fb_blur);
	float fb_tone = fellow_burn_noise(fellow_burn_local / max(5.0 * fellow_burn_crack_scale, 1e-4));
	float fb_grain = fellow_burn_noise(fellow_burn_local / max(0.15 * fellow_burn_crack_scale, 1e-4));
	float fb_specks = smoothstep(0.62, 0.8, fb_grain) * clamp(fb_char * 1.5, 0.2, 0.8);
	vec3 fb_ash = fellow_burn_ash_color * (0.55 + 0.45 * fb_tone) * (1.0 - 0.6 * fb_specks);
	vec3 fb_coal = fellow_burn_char_color * (0.6 + 0.8 * fract(fb_piece.z * 7.31 + fb_bits.y));
	ALBEDO = mix(fb_ash, fb_coal, fb_coal_share);
	ROUGHNESS = mix(0.98, 0.55, fb_coal_share);
	// Relief: the pieces' plateaus, the bits, the ash's grain (the normal bumped by the height's screen derivatives).
	float fb_rh = (fb_piece.y + fb_bit * smoothstep(0.15, 0.35, fb_bits.w) * 0.25 * fellow_burn_crack_scale + (fb_grain - 0.5) * 0.03 * fellow_burn_crack_scale) * (1.0 - fb_blur);
	vec3 fb_dpx = dFdx(VERTEX);
	vec3 fb_dpy = dFdy(VERTEX);
	vec3 fb_r1 = cross(fb_dpy, NORMAL);
	vec3 fb_r2 = cross(NORMAL, fb_dpx);
	float fb_det = dot(fb_dpx, fb_r1);
	if (abs(fb_det) > 1e-14) {
		NORMAL = normalize(abs(fb_det) * NORMAL - sign(fb_det) * (dFdx(fb_rh) * fb_r1 + dFdy(fb_rh) * fb_r2));
	}
	// Embers: in the pieces' cracks, breathing; dull red through the ash.
	float fb_temperature = min(fb.x + 293.0, 1300.0);
	float fb_hot = smoothstep(750.0, 1000.0, fb_temperature) * pow(fb_temperature / 1300.0, 3.0);
	if (fb_hot > 0.0) {
		float fb_breath = mix(1.0, 0.55 + 0.45 * sin(TIME * (0.9 + 1.4 * fb_piece.z) + 6.2832 * fb_piece.z), 1.0 - fb_blur);
		float fb_crack = mix(max(fb_bits.x, 1.0 - smoothstep(0.0, 0.1, fellow_burn_cells(fb_q).w)), 0.3, fb_blur);
		float fb_glow = fb_coal_share * (0.06 + 0.94 * fb_crack) * fb_breath * (1.0 - 0.7 * fb_dust) + (1.0 - fb_coal_share) * 0.1 * fb_tone;
		EMISSION = fellow_burn_blackbody(fb_temperature) * (fellow_burn_glow * fb_hot * fb_glow);
	}
}
)";

static const char *FELLOW_BURN_FRAGMENT = R"(
	// FellowEngine burning: scorched brown, then charred black (alligator cracks along the grain, each block a little
	// different, a faint sheen), a white-grey film of ash on the char about to crumble; the embers glow in the cracks,
	// breathing, softer and even under the ash.
	if (fellow_burn_gone > 0.001) {
		discard;
	}

	{
		vec4 fb = fellow_burn_mat;
		float fb_sum = max(fb.y + fb.z + fb.w, 1e-4);
		float fb_char = fb.z / fb_sum;
		float fb_ash = fb.w / fb_sum;
		float fb_film = smoothstep(0.2, 1.0, fb.w / max(fellow_burn_loose_ash * fb.z, 1e-5)); // how near it is to crumbling
		vec3 fb_p = fellow_burn_grain_space(fellow_burn_local) / max(fellow_burn_crack_scale, 1e-4);
		vec4 fb_cell = fellow_burn_cells(fb_p);
		float fb_blur = smoothstep(0.25, 0.8, length(fwidth(fb_p))); // fewer pixels than cells: the pattern's average
		float fb_crack = mix(fb_cell.x, 0.3, fb_blur);
		// The char's front ragged (noise on the share it shows at), not along the mesh's triangles.
		float fb_front = fb_char + fb_ash + 0.3 * (fellow_burn_noise(fb_p * 0.25) + 0.5 * fellow_burn_noise(fb_p * 0.7) - 0.75);
		float fb_dark = smoothstep(0.0, 0.35, fb_front);
		// The ash coats the char in patches as it burns towards crumbling (on the blocks, not in the cracks).
		float fb_coat = mix(smoothstep(0.45, 0.75, fellow_burn_noise(fb_p * 0.8) + 0.5 * fb_film - 0.2), 0.6, fb_blur) * fb_film * fb_dark * (1.0 - fb_crack);
		ALBEDO = mix(ALBEDO, ALBEDO * vec3(0.5, 0.36, 0.26), smoothstep(-0.02, 0.1, fb_front));
		vec3 fb_char_albedo = fellow_burn_char_color * (0.85 + 0.3 * fb_cell.y) * (1.0 - 0.7 * fb_crack * (1.0 - fb_blur));
		ALBEDO = mix(ALBEDO, fb_char_albedo, smoothstep(0.08, 0.4, fb_front));
		ALBEDO = mix(ALBEDO, fellow_burn_ash_color * (0.6 + 0.25 * fb_cell.y), 0.85 * fb_coat);
		ROUGHNESS = mix(ROUGHNESS, mix(0.6, 0.97, fb_coat), fb_dark);
		// The blocks between the cracks raised (the normal bumped by the height's screen derivatives).
		float fb_bh = smoothstep(0.0, 0.12, fb_cell.w) * fb_dark * (1.0 - fb_blur) * 0.08 * fellow_burn_crack_scale;
		vec3 fb_dpx = dFdx(VERTEX);
		vec3 fb_dpy = dFdy(VERTEX);
		vec3 fb_r1 = cross(fb_dpy, NORMAL);
		vec3 fb_r2 = cross(NORMAL, fb_dpx);
		float fb_det = dot(fb_dpx, fb_r1);
		if (abs(fb_det) > 1e-14) {
			NORMAL = normalize(abs(fb_det) * NORMAL - sign(fb_det) * (dFdx(fb_bh) * fb_r1 + dFdy(fb_bh) * fb_r2));
		}
		// Embers: the char's surface no hotter than glowing charcoal (it sits in the flame; the gas's temperature is the
		// flame's), brightest in the cracks between its blocks; some blocks glow, some do not, each breathing slowly
		// (the air moving over the embers).
		float fb_temperature = min(fb.x + 293.0, 1300.0);
		float fb_hot = smoothstep(750.0, 1000.0, fb_temperature) * pow(fb_temperature / 1300.0, 3.0);
		if (fb_hot > 0.0) {
			float fb_patch = 0.35 + 0.65 * fellow_burn_hash3(floor(fb_p * 0.5)).x;
			float fb_breath = mix(1.0, 0.55 + 0.45 * sin(TIME * (0.9 + 1.4 * fb_cell.y) + 6.2832 * fb_cell.y), 1.0 - fb_blur);
			// The faces glow dimly, more at the hottest; through the ash coat, softly.
			float fb_face = 0.04 + 0.3 * smoothstep(1100.0, 1300.0, fb_temperature) + 0.06 * fb_coat;
			float fb_glow = fb_char * (fb_face + (1.0 - fb_face) * fb_crack) * fb_patch * fb_breath + 0.15 * fb_ash;
			EMISSION += fellow_burn_blackbody(fb_temperature) * (fellow_burn_glow * fb_hot * fb_glow);
		}
	}
)";

// The end of a shader function's body (the index of its closing brace), or -1.
static int _fellow_function_end(const String &p_code, const String &p_name, int *r_open = nullptr) {
	int at = 0;
	while (true) {
		at = p_code.find(p_name, at);
		if (at < 0) {
			return -1;
		}
		// "void <name> (" with only spaces between.
		int i = at + p_name.length();
		while (i < p_code.length() && (p_code[i] == ' ' || p_code[i] == '\t' || p_code[i] == '\n' || p_code[i] == '\r')) {
			i++;
		}
		bool is_def = i < p_code.length() && p_code[i] == '(';
		int b = at - 1;
		while (b >= 0 && (p_code[b] == ' ' || p_code[b] == '\t')) {
			b--;
		}
		is_def = is_def && b >= 3 && p_code.substr(b - 3, 4) == "void";
		if (!is_def) {
			at += p_name.length();
			continue;
		}
		const int open = p_code.find("{", i);
		if (open < 0) {
			return -1;
		}
		if (r_open) {
			*r_open = open;
		}
		int depth = 0;
		for (int k = open; k < p_code.length(); k++) {
			const char32_t c = p_code[k];
			if (c == '/' && k + 1 < p_code.length() && p_code[k + 1] == '/') {
				const int e = p_code.find("\n", k);
				if (e < 0) {
					return -1;
				}
				k = e;
				continue;
			}
			if (c == '/' && k + 1 < p_code.length() && p_code[k + 1] == '*') {
				const int e = p_code.find("*/", k + 2);
				if (e < 0) {
					return -1;
				}
				k = e + 1;
				continue;
			}
			if (c == '{') {
				depth++;
			} else if (c == '}') {
				depth--;
				if (depth == 0) {
					return k;
				}
			}
		}
		return -1;
	}
}

static bool _fellow_is_ident(char32_t p_c) {
	return is_ascii_alphanumeric_char(p_c) || p_c == '_';
}

// Whether the shader's vertex() may change VERTEX or NORMAL (an assignment to them or to a component, an increment;
// comments skipped). Conservative: what is not a plain read counts.
static bool _fellow_vertex_changes_geometry(const String &p_code) {
	int open = -1;
	const int end = _fellow_function_end(p_code, "vertex", &open);
	if (end < 0 || open < 0) {
		return false;
	}
	String body;
	for (int k = open; k < end; k++) {
		const char32_t c = p_code[k];
		if (c == '/' && k + 1 < end && p_code[k + 1] == '/') {
			const int e = p_code.find("\n", k);
			k = e < 0 ? end : e - 1;
			continue;
		}
		if (c == '/' && k + 1 < end && p_code[k + 1] == '*') {
			const int e = p_code.find("*/", k + 2);
			k = e < 0 ? end : e + 1;
			body += " ";
			continue;
		}
		body += c;
	}
	const char *names[] = { "VERTEX", "NORMAL" };
	for (const char *name : names) {
		const int len = String(name).length();
		int at = 0;
		while ((at = body.find(name, at)) >= 0) {
			const int s = at;
			at += len;
			if ((s > 0 && _fellow_is_ident(body[s - 1])) || (at < body.length() && _fellow_is_ident(body[at]))) {
				continue; // another identifier (VERTEX_ID, ...)
			}
			int i = at;
			while (i < body.length() && (body[i] == ' ' || body[i] == '\t' || body[i] == '\n' || body[i] == '\r')) {
				i++;
			}
			// A component or an element.
			if (i < body.length() && body[i] == '.') {
				i++;
				while (i < body.length() && _fellow_is_ident(body[i])) {
					i++;
				}
			} else if (i < body.length() && body[i] == '[') {
				const int e = body.find("]", i);
				i = e < 0 ? body.length() : e + 1;
			}
			while (i < body.length() && (body[i] == ' ' || body[i] == '\t' || body[i] == '\n' || body[i] == '\r')) {
				i++;
			}
			if (i >= body.length()) {
				continue;
			}
			const char32_t c = body[i];
			const char32_t n = i + 1 < body.length() ? body[i + 1] : ' ';
			if (c == '=' && n != '=') {
				return true;
			}
			if ((c == '+' || c == '-' || c == '*' || c == '/') && (n == '=' || n == c)) {
				return true;
			}
			// Before it: an increment, or passed to a function (it might be an out parameter).
			int b = s - 1;
			while (b >= 0 && (body[b] == ' ' || body[b] == '\t' || body[b] == '\n' || body[b] == '\r')) {
				b--;
			}
			if (b >= 1 && ((body[b] == '+' && body[b - 1] == '+') || (body[b] == '-' && body[b - 1] == '-'))) {
				return true;
			}
			if (b >= 0 && (body[b] == '(' || body[b] == ',') && (c == ')' || c == ',')) {
				// An argument by itself: a constructor's or a built-in function's is read only; another function's
				// may be out. The call's name: before the unmatched '('.
				int depth = 0;
				int k = b;
				if (body[b] == ',') {
					for (k = b - 1; k >= 0; k--) {
						if (body[k] == ')') {
							depth++;
						} else if (body[k] == '(') {
							if (depth == 0) {
								break;
							}
							depth--;
						}
					}
				}
				int fe = k - 1;
				while (fe >= 0 && (body[fe] == ' ' || body[fe] == '\t')) {
					fe--;
				}
				int fs = fe;
				while (fs >= 0 && _fellow_is_ident(body[fs])) {
					fs--;
				}
				const String fn = fe >= 0 ? body.substr(fs + 1, fe - fs) : String();
				static const char *read_only[] = { "vec2", "vec3", "vec4", "mat3", "mat4", "normalize", "length", "dot", "cross", "abs", "pow",
					"mix", "max", "min", "clamp", "texture", "textureLod", "sign", "floor", "fract", "sin", "cos", "inverse", "transpose",
					"distance", "reflect", "step", "smoothstep", "exp", "log", "sqrt", "mod", "if", "while", "for", "return" };
				bool ro = fn.is_empty(); // a parenthesised expression
				for (const char *r : read_only) {
					ro = ro || fn == r;
				}
				if (!ro) {
					return true;
				}
			}
		}
	}
	return false;
}

static String _fellow_burn_code(const String &p_code, int p_custom, bool p_cached, bool &r_cached) {
	String code = p_code;
	r_cached = false;
	if (!code.contains("shader_type spatial")) {
		return String();
	}
	// The declarations after the render modes (or the shader type).
	int insert = -1;
	const int rm = code.find("render_mode");
	if (rm >= 0) {
		insert = code.find(";", rm);
	}
	if (insert < 0) {
		insert = code.find(";", code.find("shader_type"));
	}
	if (insert < 0) {
		return String();
	}
	const bool world_vertex = rm >= 0 && code.substr(rm, code.find(";", rm) - rm).contains("world_vertex_coords");
	r_cached = p_cached && !world_vertex && !_fellow_vertex_changes_geometry(code);
	code = code.insert(insert + 1, String("\n") + FELLOW_BURN_DECLARATIONS + (r_cached ? FELLOW_BURN_VERTEX_CACHED_DECLARATIONS : ""));
	String vertex = String(r_cached ? FELLOW_BURN_VERTEX_CACHED : FELLOW_BURN_VERTEX).replace("CUSTOM_CHANNEL", "CUSTOM" + itos(p_custom)).replace("BURN_VERTEX_ROW", itos(BURN_VERTEX_ROW));
	if (world_vertex) {
		// Vertices in world space: no deformation (the look only, at the undeformed surface).
		vertex = "\n\tfellow_burn_local = (inverse(MODEL_MATRIX) * vec4(VERTEX, 1.0)).xyz;\n\tfellow_burn_gone = 0.0;\n\tfellow_burn_mat = fellow_burn_sample(fellow_burn_local);\n";
	}
	int ve = _fellow_function_end(code, "vertex");
	if (ve >= 0) {
		code = code.insert(ve, vertex);
	} else {
		code += "\nvoid vertex() {" + vertex + "}\n";
	}
	int fe = _fellow_function_end(code, "fragment");
	if (fe >= 0) {
		code = code.insert(fe, FELLOW_BURN_FRAGMENT);
	} else {
		code += String("\nvoid fragment() {") + FELLOW_BURN_FRAGMENT + "}\n";
	}
	return code;
}

Ref<ShaderMaterial> FellowBurnable3D::_make_burn_material(const Ref<Material> &p_source, int p_custom, bool p_cached, bool &r_cached) const {
	r_cached = false;
	Ref<Material> src = p_source;
	if (src.is_null()) {
		Ref<StandardMaterial3D> def;
		def.instantiate();
		src = def;
	}
	String code;
	Ref<ShaderMaterial> sm = src;
	RS *rs = RS::get_singleton();
	if (sm.is_valid()) {
		if (sm->get_shader().is_null()) {
			return Ref<ShaderMaterial>();
		}
		code = sm->get_shader()->get_code();
	} else if (Object::cast_to<BaseMaterial3D>(src.ptr())) {
		const RID shader = src->get_shader_rid();
		if (shader.is_null()) {
			return Ref<ShaderMaterial>();
		}
		code = rs->shader_get_code(shader);
	} else {
		return Ref<ShaderMaterial>();
	}
	bool cached = false;
	const String burn_code = _fellow_burn_code(code, p_custom, p_cached, cached);
	if (burn_code.is_empty()) {
		return Ref<ShaderMaterial>();
	}
	Ref<Shader> shader;
	shader.instantiate();
	shader->set_code(burn_code);
	Ref<ShaderMaterial> m;
	m.instantiate();
	m->set_shader(shader);
	// The source's parameters.
	if (sm.is_valid()) {
		List<PropertyInfo> params;
		sm->get_shader()->get_shader_uniform_list(&params);
		for (const PropertyInfo &pi : params) {
			m->set_shader_parameter(pi.name, sm->get_shader_parameter(pi.name));
		}
	} else {
		List<PropertyInfo> params;
		rs->get_shader_parameter_list(src->get_shader_rid(), &params);
		for (const PropertyInfo &pi : params) {
			if (pi.usage & (PROPERTY_USAGE_GROUP | PROPERTY_USAGE_SUBGROUP | PROPERTY_USAGE_CATEGORY)) {
				continue;
			}
			const Variant v = rs->material_get_param(src->get_rid(), pi.name);
			if (v.get_type() != Variant::NIL) {
				m->set_shader_parameter(pi.name, v);
			}
		}
	}
	m->set_render_priority(src->get_render_priority());
	m->set_next_pass(src->get_next_pass());
	m->set_shader_parameter("fellow_burn_state", display_texture);
	m->set_shader_parameter("fellow_burn_origin", origin);
	const Vector3 extent = Vector3(res) * voxel;
	m->set_shader_parameter("fellow_burn_inv_extent", Vector3(1.0f / extent.x, 1.0f / extent.y, 1.0f / extent.z));
	m->set_shader_parameter("fellow_burn_voxel", voxel);
	m->set_shader_parameter("fellow_burn_step", voxel * 0.5f);
	m->set_shader_parameter("fellow_burn_steps", int(Math::ceil(extent.length() / (voxel * 0.5f))));
	m->set_shader_parameter("fellow_burn_char_color", char_color);
	m->set_shader_parameter("fellow_burn_ash_color", ash_color);
	m->set_shader_parameter("fellow_burn_glow", glow_intensity);
	m->set_shader_parameter("fellow_burn_crack_scale", crack_scale);
	m->set_shader_parameter("fellow_burn_loose_ash", BURN_LOOSE_ASH * ash_volume);
	m->set_shader_parameter("fellow_burn_grain", grain);
	if (cached) {
		m->set_shader_parameter("fellow_burn_vertices", vertex_texture);
		r_cached = true;
	}
	return m;
}

void FellowBurnable3D::_update_materials() {
	LocalVector<Ref<ShaderMaterial>> all;
	for (const Ref<ShaderMaterial> &m : burn_materials) {
		all.push_back(m);
	}
	all.push_back(heap_material);
	for (Ref<ShaderMaterial> &m : all) {
		if (m.is_null()) {
			continue;
		}
		m->set_shader_parameter("fellow_burn_char_color", char_color);
		m->set_shader_parameter("fellow_burn_ash_color", ash_color);
		m->set_shader_parameter("fellow_burn_glow", glow_intensity);
		m->set_shader_parameter("fellow_burn_crack_scale", crack_scale);
		m->set_shader_parameter("fellow_burn_loose_ash", BURN_LOOSE_ASH * ash_volume);
	}
}

void FellowBurnable3D::_build_heap(MeshInstance3D *p_target) {
	const int a = CLAMP(heap_axis, 0, 2);
	const int u = (a + 1) % 3;
	const int v = (a + 2) % 3;
	const int nu = res[u] * 2; // two vertices per voxel along each side
	const int nv = res[v] * 2;
	const float floor_face = origin[a] + float(heap_sign < 0 ? heap_floor : heap_floor + 1) * voxel;
	PackedVector3Array pv;
	PackedVector3Array pn;
	Vector3 up;
	up[a] = heap_sign < 0 ? 1.0f : -1.0f;
	for (int j = 0; j < nv; j++) {
		for (int i = 0; i < nu; i++) {
			Vector3 p;
			p[u] = origin[u] + (i + 0.5f) * voxel * 0.5f;
			p[v] = origin[v] + (j + 0.5f) * voxel * 0.5f;
			p[a] = floor_face;
			pv.push_back(p);
			pn.push_back(up);
		}
	}
	// Front faces (clockwise, seen from above) up: u x v is the axis a, so the winding turns with the heap's up.
	const bool flip = up[a] > 0.0f;
	PackedInt32Array idx;
	for (int j = 0; j + 1 < nv; j++) {
		for (int i = 0; i + 1 < nu; i++) {
			const int q = i + j * nu;
			idx.push_back(q);
			idx.push_back(flip ? q + 1 + nu : q + 1);
			idx.push_back(flip ? q + 1 : q + 1 + nu);
			idx.push_back(q);
			idx.push_back(flip ? q + nu : q + 1 + nu);
			idx.push_back(flip ? q + 1 + nu : q + nu);
		}
	}
	if (idx.is_empty()) {
		return;
	}
	Array arrays;
	arrays.resize(Mesh::ARRAY_MAX);
	arrays[Mesh::ARRAY_VERTEX] = pv;
	arrays[Mesh::ARRAY_NORMAL] = pn;
	arrays[Mesh::ARRAY_INDEX] = idx;
	Ref<ArrayMesh> mesh;
	mesh.instantiate();
	mesh->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, arrays);
	// The heap's vertices rise up to the box's height: a custom box so it is not culled.
	mesh->set_custom_aabb(AABB(origin, Vector3(res) * voxel));
	Ref<Shader> shader;
	shader.instantiate();
	shader->set_code(String("shader_type spatial;\nrender_mode cull_disabled;\n") + FELLOW_BURN_DECLARATIONS + FELLOW_HEAP_CODE);
	heap_material.instantiate();
	heap_material->set_shader(shader);
	heap_material->set_shader_parameter("fellow_burn_state", display_texture);
	heap_material->set_shader_parameter("fellow_burn_origin", origin);
	const Vector3 extent = Vector3(res) * voxel;
	heap_material->set_shader_parameter("fellow_burn_inv_extent", Vector3(1.0f / extent.x, 1.0f / extent.y, 1.0f / extent.z));
	heap_material->set_shader_parameter("fellow_burn_voxel", voxel);
	heap_material->set_shader_parameter("fellow_burn_step", voxel * 0.5f);
	heap_material->set_shader_parameter("fellow_burn_steps", res[a]);
	heap_material->set_shader_parameter("fellow_heap_up", up);
	Vector3 du;
	du[u] = voxel;
	Vector3 dv;
	dv[v] = voxel;
	heap_material->set_shader_parameter("fellow_heap_u", du);
	heap_material->set_shader_parameter("fellow_heap_v", dv);
	heap_material->set_shader_parameter("fellow_burn_char_color", char_color);
	heap_material->set_shader_parameter("fellow_burn_ash_color", ash_color);
	heap_material->set_shader_parameter("fellow_burn_glow", glow_intensity);
	heap_material->set_shader_parameter("fellow_burn_crack_scale", crack_scale);
	heap_material->set_shader_parameter("fellow_burn_loose_ash", BURN_LOOSE_ASH * ash_volume);
	heap_material->set_shader_parameter("fellow_burn_grain", grain);
	mesh->surface_set_material(0, heap_material);
	MeshInstance3D *heap = memnew(MeshInstance3D);
	heap->set_name("FellowAshHeap");
	heap->set_mesh(mesh);
	heap->set_cast_shadows_setting(p_target->get_cast_shadows_setting());
	heap->set_layer_mask(p_target->get_layer_mask());
	p_target->add_child(heap, false, INTERNAL_MODE_BACK);
	heap_id = heap->get_instance_id();
}

void FellowBurnable3D::_build_display(MeshInstance3D *p_target) {
	const Ref<Mesh> mesh = original_mesh;
	DisplayBuilder b;
	struct SurfaceInfo {
		int source = -1;
		uint64_t format = 0;
		Ref<Material> material;
		int custom = 0;
	};
	LocalVector<SurfaceInfo> surfaces;
	for (int s = 0; s < mesh->get_surface_count(); s++) {
		if (mesh->surface_get_primitive_type(s) != Mesh::PRIMITIVE_TRIANGLES) {
			continue;
		}
		const Array arrays = mesh->surface_get_arrays(s);
		const PackedVector3Array v = arrays[Mesh::ARRAY_VERTEX];
		if (v.is_empty()) {
			continue;
		}
		const uint64_t format = mesh->surface_get_format(s);
		if (format & (Mesh::ARRAY_FORMAT_BONES | Mesh::ARRAY_FORMAT_WEIGHTS)) {
			WARN_PRINT_ONCE("FellowBurnable3D: skinned meshes are not drawn burning (the simulation runs, the mesh keeps its look).");
			display_mesh.unref();
			burn_materials.clear();
			return;
		}
		SurfaceInfo info;
		info.source = s;
		info.format = format;
		info.material = p_target->get_active_material(s);
		// The first custom channel the surface does not use.
		info.custom = -1;
		for (int c = 0; c < 4; c++) {
			if (!(format & (uint64_t(Mesh::ARRAY_FORMAT_CUSTOM0) << c))) {
				info.custom = c;
				break;
			}
		}
		const int si = surfaces.size();
		surfaces.push_back(info);
		const PackedVector3Array n = arrays[Mesh::ARRAY_NORMAL];
		const PackedFloat32Array t = arrays[Mesh::ARRAY_TANGENT];
		const PackedColorArray c = arrays[Mesh::ARRAY_COLOR];
		const PackedVector2Array uv = arrays[Mesh::ARRAY_TEX_UV];
		const PackedVector2Array uv2 = arrays[Mesh::ARRAY_TEX_UV2];
		const uint32_t base = b.pos.size();
		for (int i = 0; i < v.size(); i++) {
			b.add(v[i], i < n.size() ? n[i] : Vector3(0, 1, 0), (i * 4 + 3) < t.size() ? Plane(t[i * 4], t[i * 4 + 1], t[i * 4 + 2], t[i * 4 + 3]) : Plane(1, 0, 0, 1),
					i < c.size() ? c[i] : Color(1, 1, 1, 1), i < uv.size() ? uv[i] : Vector2(), i < uv2.size() ? uv2[i] : Vector2());
		}
		const PackedInt32Array idx = arrays[Mesh::ARRAY_INDEX];
		const int ni = idx.is_empty() ? v.size() : idx.size();
		for (int i = 0; i + 2 < ni; i += 3) {
			for (int k = 0; k < 3; k++) {
				b.tris.push_back(base + uint32_t(idx.is_empty() ? i + k : idx[i + k]));
			}
			b.tri_surface.push_back(si);
		}
	}
	if (subdivide) {
		b.refine(voxel * 1.5f, BURN_TRIANGLE_BUDGET);
	}
	// The march's start per vertex (computed once per position: the copies at a seam move together).
	LocalVector<Vector3> smooth;
	smooth.resize(b.pos_ids.size());
	for (Vector3 &v : smooth) {
		v = Vector3();
	}
	for (uint32_t i = 0; i < b.pos.size(); i++) {
		smooth[b.pid[i]] += b.nrm[i]; // the vertex normals (outwards), the copies' average
	}
	const float step = voxel * 0.5f;
	const int steps = int(Math::ceil((Vector3(res) * voxel).length() / step));
	const Vector3 centre = origin + Vector3(res) * voxel * 0.5f;
	LocalVector<Vector4> start;
	start.resize(b.pos_ids.size());
	LocalVector<uint8_t> done;
	done.resize(b.pos_ids.size());
	for (uint8_t &d : done) {
		d = 0;
	}
	LocalVector<Vector4> custom;
	custom.resize(b.pos.size());
	for (uint32_t i = 0; i < b.pos.size(); i++) {
		const uint32_t id = b.pid[i];
		if (!done[id]) {
			done[id] = 1;
			const Vector3 p = b.pos[i];
			const float h = voxel;
			Vector3 g(_fill_at(p + Vector3(h, 0, 0)) - _fill_at(p - Vector3(h, 0, 0)), _fill_at(p + Vector3(0, h, 0)) - _fill_at(p - Vector3(0, h, 0)),
					_fill_at(p + Vector3(0, 0, h)) - _fill_at(p - Vector3(0, 0, h)));
			Vector3 d;
			if (g.length() > 1e-3f) {
				d = g.normalized();
			} else if (smooth[id].length() > 1e-12f) {
				d = -smooth[id].normalized();
			} else {
				d = (centre - p).normalized();
			}
			float ref = 0.0f;
			for (int k = 0; k <= 6; k++) {
				ref = MAX(ref, _fill_at(p + d * (k * step)));
			}
			float t0 = -1.0f;
			if (ref >= 0.05f) {
				const float thr = 0.5f * ref;
				float prev = _fill_at(p);
				if (prev > thr) {
					t0 = 0.0f;
				} else {
					for (int k = 1; k <= steps; k++) {
						const float t = k * step;
						const float f = _fill_at(p + d * t);
						if (f > thr) {
							t0 = t - step * (f - thr) / MAX(f - prev, 1e-6f);
							break;
						}
						prev = f;
					}
				}
			}
			const Vector2 e = _fellow_oct_encode(d);
			start[id] = t0 < 0.0f ? Vector4(0.0f, 0.0f, e.x, e.y) : Vector4(t0, ref, e.x, e.y);
		}
		custom[i] = start[id];
	}
	// One surface per source surface.
	display_mesh.instantiate();
	burn_materials.clear();
	HashMap<Material *, Ref<ShaderMaterial>> made;
	HashMap<Material *, bool> made_cached;
	HashSet<ShaderMaterial *> offset_set;
	const bool cache = GLOBAL_GET_CACHED(bool, "rendering/fellow/fluid/burn_vertices_cached"); // (fellow_settings.cpp)
	LocalVector<float> marched; // the vertices of the surfaces marched once per frame: position, 0, the march's custom data
	for (uint32_t si = 0; si < surfaces.size(); si++) {
		const SurfaceInfo &info = surfaces[si];
		Ref<ShaderMaterial> bm;
		bool bm_cached = false;
		if (info.custom >= 0) {
			Ref<ShaderMaterial> *f = made.getptr(info.material.ptr());
			if (f) {
				bm = *f;
				bm_cached = made_cached[info.material.ptr()];
			} else {
				bm = _make_burn_material(info.material, info.custom, cache, bm_cached);
				made.insert(info.material.ptr(), bm);
				made_cached.insert(info.material.ptr(), bm_cached);
			}
		}
		bm_cached = bm_cached && bm.is_valid();
		const uint32_t first_marched = marched.size() / 12;
		HashMap<uint32_t, int> remap;
		PackedVector3Array pv;
		PackedVector3Array pn;
		PackedFloat32Array pt;
		PackedColorArray pc;
		PackedVector2Array puv;
		PackedVector2Array puv2;
		PackedFloat32Array pcu;
		PackedInt32Array pidx;
		for (uint32_t t = 0; t < b.tri_surface.size(); t++) {
			if (b.tri_surface[t] != int(si)) {
				continue;
			}
			for (int k = 0; k < 3; k++) {
				const uint32_t v = b.tris[t * 3 + k];
				int *f = remap.getptr(v);
				int ni;
				if (f) {
					ni = *f;
				} else {
					ni = pv.size();
					remap.insert(v, ni);
					pv.push_back(b.pos[v]);
					pn.push_back(b.nrm[v]);
					const Plane &tp = b.tan[v];
					pt.push_back(tp.normal.x);
					pt.push_back(tp.normal.y);
					pt.push_back(tp.normal.z);
					pt.push_back(tp.d);
					pc.push_back(b.col[v]);
					puv.push_back(b.uv[v]);
					puv2.push_back(b.uv2[v]);
					const Vector4 &cu = custom[v];
					pcu.push_back(cu.x);
					pcu.push_back(cu.y);
					pcu.push_back(cu.z);
					pcu.push_back(cu.w);
					if (bm_cached) {
						const float rec[12] = { b.pos[v].x, b.pos[v].y, b.pos[v].z, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, cu.x, cu.y, cu.z, cu.w };
						for (float x : rec) {
							marched.push_back(x);
						}
					}
				}
				pidx.push_back(ni);
			}
		}
		if (pv.is_empty()) {
			continue;
		}
		Array arrays;
		arrays.resize(Mesh::ARRAY_MAX);
		arrays[Mesh::ARRAY_VERTEX] = pv;
		if (info.format & Mesh::ARRAY_FORMAT_NORMAL) {
			arrays[Mesh::ARRAY_NORMAL] = pn;
		}
		if (info.format & Mesh::ARRAY_FORMAT_TANGENT) {
			arrays[Mesh::ARRAY_TANGENT] = pt;
		}
		if (info.format & Mesh::ARRAY_FORMAT_COLOR) {
			arrays[Mesh::ARRAY_COLOR] = pc;
		}
		if (info.format & Mesh::ARRAY_FORMAT_TEX_UV) {
			arrays[Mesh::ARRAY_TEX_UV] = puv;
		}
		if (info.format & Mesh::ARRAY_FORMAT_TEX_UV2) {
			arrays[Mesh::ARRAY_TEX_UV2] = puv2;
		}
		uint64_t flags = 0;
		if (info.custom >= 0) {
			arrays[Mesh::ARRAY_CUSTOM0 + info.custom] = pcu;
			flags |= uint64_t(Mesh::ARRAY_CUSTOM_RGBA_FLOAT) << (Mesh::ARRAY_FORMAT_CUSTOM0_SHIFT + info.custom * Mesh::ARRAY_FORMAT_CUSTOM_BITS);
		}
		arrays[Mesh::ARRAY_INDEX] = pidx;
		display_mesh->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, arrays, TypedArray<Array>(), Dictionary(), flags);
		const int ds = display_mesh->get_surface_count() - 1;
		if (bm_cached) {
			// The surface's vertices among the marched ones: a material per surface (the shader shared).
			if (offset_set.has(bm.ptr())) {
				bm = bm->duplicate();
			}
			offset_set.insert(bm.ptr());
			bm->set_shader_parameter("fellow_burn_vertex_offset", int(first_marched));
		}
		if (bm.is_valid()) {
			display_mesh->surface_set_material(ds, bm);
			burn_materials.push_back(bm);
		} else {
			display_mesh->surface_set_material(ds, info.material);
		}
	}
	const uint32_t count = marched.size() / 12;
	print_verbose(vformat("FellowBurnable3D %s: %d of the display's vertices marched once per frame (fluid/burn_vertices_cached %s).", get_path(), count, cache ? "on" : "off"));
	if (count > 0) {
		PackedByteArray vertices;
		vertices.resize(int64_t(count) * 48);
		memcpy(vertices.ptrw(), marched.ptr(), vertices.size());
		// FellowGas's VertexParams (std140): the march's parameters, as the materials' (_make_burn_material).
		const Vector3 extent = Vector3(res) * voxel;
		const float pf[8] = { origin.x, origin.y, origin.z, voxel, 1.0f / extent.x, 1.0f / extent.y, 1.0f / extent.z, voxel * 0.5f };
		const int32_t pi[4] = { int32_t(Math::ceil(extent.length() / (voxel * 0.5f))), int32_t(count), BURN_VERTEX_ROW, 0 };
		const float pl[4] = { BURN_LOOSE_ASH * ash_volume, 0.0f, 0.0f, 0.0f };
		PackedByteArray params;
		params.resize(64);
		memcpy(params.ptrw(), pf, 32);
		memcpy(params.ptrw() + 32, pi, 16);
		memcpy(params.ptrw() + 48, pl, 16);
		RS::get_singleton()->call_on_render_thread(callable_mp_static(&FellowBurnable3D::_rt_vertices).bind(burn_id, vertices, count, params, vertex_texture));
	}
}

// ------------------------------------------------------------------ render thread

void FellowBurnable3D::_rt_configure(uint32_t p_id, Vector3i p_res, PackedByteArray p_initial, Ref<FellowGasReadback> p_totals, Ref<Texture3DRD> p_texture) {
	FellowGas *gas = FellowGas::get_singleton();
	if (!gas) {
		WARN_PRINT_ONCE("FellowBurnable3D needs a RenderingDevice renderer (Forward+ or Mobile): with the Compatibility renderer nothing burns.");
		return;
	}
	gas->burnable_configure(p_id, p_res, p_initial);
	gas->burnable_set_totals_callback(p_id, callable_mp_static(&FellowBurnable3D::_rt_store).bind(p_totals));
	if (p_texture.is_valid()) {
		p_texture->set_texture_rd_rid(gas->burnable_get_display(p_id));
	}
}

void FellowBurnable3D::_rt_vertices(uint32_t p_id, PackedByteArray p_vertices, uint32_t p_count, PackedByteArray p_params, Ref<Texture2DRD> p_texture) {
	FellowGas *gas = FellowGas::get_singleton_if_exists();
	if (!gas) {
		return;
	}
	const RID tex = gas->burnable_set_vertices(p_id, p_vertices, p_count, BURN_VERTEX_ROW, p_params);
	if (tex.is_valid() && p_texture.is_valid()) {
		p_texture->set_texture_rd_rid(tex);
	}
}

void FellowBurnable3D::_rt_free(uint32_t p_id) {
	if (FellowGas *gas = FellowGas::get_singleton_if_exists()) {
		gas->burnable_free(p_id);
	}
}

void FellowBurnable3D::_rt_step(uint32_t p_id, PackedByteArray p_step) {
	FellowGas *gas = FellowGas::get_singleton_if_exists();
	if (!gas || p_step.size() != int64_t(sizeof(FellowGas::BurnStepParams))) {
		return;
	}
	FellowGas::BurnStepParams sp;
	memcpy(&sp, p_step.ptr(), sizeof(FellowGas::BurnStepParams));
	gas->burnable_queue_step(p_id, sp);
}

void FellowBurnable3D::_rt_debug(uint32_t p_id, int p_what, Ref<FellowGasReadback> p_target) {
	if (FellowGas *gas = FellowGas::get_singleton_if_exists()) {
		gas->burnable_debug_read(p_id, p_what, callable_mp_static(&FellowBurnable3D::_rt_store).bind(p_target));
	}
}

void FellowBurnable3D::_rt_store(const PackedByteArray &p_data, Ref<FellowGasReadback> p_target) {
	if (p_target.is_valid()) {
		p_target->store(p_data);
	}
}

// ------------------------------------------------------------------ main thread

void FellowBurnable3D::_configure() {
	MeshInstance3D *target = _get_target();
	if (!target || target->get_mesh().is_null()) {
		return; // tried again next tick
	}
	original_mesh = target->get_mesh();
	target_id = target->get_instance_id();
	if (!_voxelize(original_mesh)) {
		ERR_PRINT(vformat("FellowBurnable3D %s: the mesh has no volume to burn.", get_path()));
		failed = true;
		original_mesh.unref();
		return;
	}
	target_id = target->get_instance_id();
	// The initial state: wood where the mesh is.
	const int64_t count = int64_t(res.x) * res.y * res.z;
	PackedByteArray bytes;
	bytes.resize(count * 16);
	float *f = reinterpret_cast<float *>(bytes.ptrw());
	for (int64_t i = 0; i < count; i++) {
		f[i * 4 + 0] = 0.0f;
		f[i * 4 + 1] = initial_fill[i];
		f[i * 4 + 2] = 0.0f;
		f[i * 4 + 3] = 0.0f;
	}
	RS::get_singleton()->call_on_render_thread(callable_mp_static(&FellowBurnable3D::_rt_configure).bind(burn_id, res, bytes, totals, display_texture));
	// The display: the mesh subdivided, its materials burning.
	heap_floor = heap_sign < 0 ? occupied_min[CLAMP(heap_axis, 0, 2)] : occupied_max[CLAMP(heap_axis, 0, 2)];
	if (heap_axis == ground_axis && heap_sign == ground_sign) {
		heap_floor = ground_floor;
	}
	if (draw_burning) {
		_build_display(target); // the ash heap's mesh once there is ash (_read_totals)
	}
	if (display_mesh.is_valid()) {
		_swap_in(target);
	}
	// The physics body it belongs to is not the gas's obstacle (its voxels are).
	for (Node *n = target; n; n = n->get_parent()) {
		if (CollisionObject3D *co = Object::cast_to<CollisionObject3D>(n)) {
			body_id = co->get_instance_id();
			MutexLock lock(body_mutex);
			int *c = burnable_bodies.getptr(uint64_t(body_id));
			if (c) {
				(*c)++;
			} else {
				burnable_bodies.insert(uint64_t(body_id), 1);
			}
			break;
		}
	}
	configured = true;
	ticks = 0;
	wood_fraction = 1.0f;
	char_fraction = 0.0f;
	ash_fraction = 0.0f;
	max_temperature = BURN_AMBIENT;
	burning_voxels = 0;
	has_ignited = false;
	has_burned_out = false;
}

void FellowBurnable3D::_swap_in(MeshInstance3D *p_target) {
	original_overrides.clear();
	for (int s = 0; s < p_target->get_surface_override_material_count(); s++) {
		original_overrides.push_back(p_target->get_surface_override_material(s));
	}
	original_material_override = p_target->get_material_override();
	p_target->set_mesh(display_mesh);
	for (int s = 0; s < p_target->get_surface_override_material_count(); s++) {
		p_target->set_surface_override_material(s, Ref<Material>());
	}
	p_target->set_material_override(Ref<Material>());
}

void FellowBurnable3D::_unconfigure() {
	if (!configured) {
		return;
	}
	MeshInstance3D *target = Object::cast_to<MeshInstance3D>(ObjectDB::get_instance(target_id));
	if (target && display_mesh.is_valid() && target->get_mesh() == display_mesh) {
		target->set_mesh(original_mesh);
		for (int s = 0; s < MIN(target->get_surface_override_material_count(), original_overrides.size()); s++) {
			target->set_surface_override_material(s, original_overrides[s]);
		}
		target->set_material_override(original_material_override);
	}
	_restore_collision();
	if (body_id.is_valid()) {
		MutexLock lock(body_mutex);
		int *c = burnable_bodies.getptr(uint64_t(body_id));
		if (c && --(*c) <= 0) {
			burnable_bodies.erase(uint64_t(body_id));
		}
		body_id = ObjectID();
	}
	original_mesh.unref();
	original_overrides.clear();
	original_material_override.unref();
	display_mesh.unref();
	burn_materials.clear();
	if (MeshInstance3D *heap = Object::cast_to<MeshInstance3D>(ObjectDB::get_instance(heap_id))) {
		heap->set_mesh(Ref<Mesh>()); // its material goes now: it samples the voxels freed below
		heap->queue_free();
	}
	heap_id = ObjectID();
	heap_material.unref();
	// The voxels last, once no material samples them.
	RS::get_singleton()->call_on_render_thread(callable_mp_static(&FellowBurnable3D::_rt_free).bind(burn_id));
	target_id = ObjectID();
	configured = false;
}

void FellowBurnable3D::_queue_step() {
	MeshInstance3D *target = Object::cast_to<MeshInstance3D>(ObjectDB::get_instance(target_id));
	if (!target || !target->is_inside_tree()) {
		return;
	}
	FellowGas::BurnStepParams sp;
	const double tick = get_physics_process_delta_time();
	sp.dt = float(tick * burn_speed);
	if (sp.dt <= 0.0f) {
		return;
	}
	const Transform3D m = target->get_global_transform();
	const float scale = Math::pow(Math::abs(m.basis.determinant()), 1.0f / 3.0f);
	const float h = voxel * scale; // metres
	sp.voxel_size = h;
	sp.settle = 1.0f - Math::exp(-BURN_SETTLE_RATE * sp.dt);
	const float char_density = density * char_yield / BURN_CHAR_VOLUME;
	sp.densities[0] = density;
	sp.densities[1] = char_density;
	sp.densities[2] = BURN_ASH_DENSITY;
	sp.densities[3] = 0.05f * density * specific_heat;
	sp.heats[0] = specific_heat;
	sp.heats[1] = BURN_CHAR_SPECIFIC_HEAT;
	sp.heats[2] = BURN_ASH_SPECIFIC_HEAT;
	sp.heats[3] = BURN_PYROLYSIS_HEAT;
	sp.conduct[0] = conductivity;
	sp.conduct[1] = BURN_CHAR_CONDUCTIVITY;
	sp.conduct[2] = BURN_ASH_CONDUCTIVITY;
	sp.conduct[3] = BURN_CONVECTION;
	sp.pyro[0] = pyrolysis_temperature - BURN_AMBIENT;
	sp.pyro[1] = pyrolysis_rate;
	sp.pyro[2] = char_yield;
	sp.pyro[3] = BURN_CHAR_VOLUME;
	sp.glow[0] = glow_temperature - BURN_AMBIENT;
	sp.glow[1] = glow_rate;
	sp.glow[2] = BURN_GLOW_HEAT;
	sp.glow[3] = ash_volume;
	sp.misc[0] = BURN_EMISSIVITY;
	sp.misc[1] = BURN_FUEL_PER_KG * flame_strength;
	sp.misc[2] = 1.5f;
	sp.misc[3] = BURN_AMBIENT;
	sp.flame_temperature = MAX(flame_temperature - BURN_AMBIENT, 0.0f);
	sp.flame_flux = BURN_FLAME_FLUX;
	sp.flame_reach = flame_reach / MAX(h, 1e-6f);
	// Voxel coordinates -> world.
	const Transform3D voxel_to_world = m * Transform3D(Basis::from_scale(Vector3(voxel, voxel, voxel)), origin);
	// The ash falls along the voxel axis nearest to the world's down.
	const Vector3 down = voxel_to_world.basis.inverse().xform(Vector3(0, -1, 0));
	int axis = 1;
	for (int a = 0; a < 3; a++) {
		if (Math::abs(down[a]) > Math::abs(down[axis]) + 1e-6f) {
			axis = a;
		}
	}
	sp.settle_axis = axis;
	sp.settle_sign = down[axis] < 0.0f ? -1 : 1;
	sp.settle_floor = sp.settle_sign < 0 ? occupied_min[axis] : occupied_max[axis];
	if (axis == ground_axis && sp.settle_sign == ground_sign) {
		sp.settle_floor = ground_floor; // the layer on the ground the box reaches down to
	}
	// The gas.
	FellowGasVolume3D *gas = _find_gas_volume();
	Transform3D v2g;
	if (gas && gas->is_configured() && gas->is_inside_tree()) {
		const Transform3D g2w = gas->get_grid_transform();
		v2g = g2w.affine_inverse() * voxel_to_world;
		const Transform3D g2v = v2g.affine_inverse();
		for (int r = 0; r < 3; r++) {
			for (int c = 0; c < 3; c++) {
				sp.voxel_to_gas[r * 4 + c] = v2g.basis.rows[r][c];
				sp.gas_to_voxel[r * 4 + c] = g2v.basis.rows[r][c];
			}
			sp.voxel_to_gas[r * 4 + 3] = v2g.origin[r];
			sp.gas_to_voxel[r * 4 + 3] = g2v.origin[r];
		}
		// The gas cells around the box (the fuel's footprint and a cell).
		AABB box;
		for (int c = 0; c < 8; c++) {
			const Vector3 g = v2g.xform(Vector3((c & 1) ? res.x : 0, (c & 2) ? res.y : 0, (c & 4) ? res.z : 0));
			if (c == 0) {
				box = AABB(g, Vector3());
			} else {
				box.expand_to(g);
			}
		}
		const float pad = sp.footprint * 0.5f + 1.0f;
		const Vector3i gs = gas->get_grid_size();
		int lo[3], hi[3];
		for (int a = 0; a < 3; a++) {
			lo[a] = CLAMP(int(Math::floor(box.position[a] - pad)), 0, gs[a]);
			hi[a] = CLAMP(int(Math::ceil(box.position[a] + box.size[a] + pad)), 0, gs[a]);
			sp.region_origin[a] = lo[a];
			sp.region_size[a] = MAX(hi[a] - lo[a], 0);
		}
		sp.gas_id = gas->get_volume_id();
		// The gas sampled a cell outside the surface (at least 1.5 voxels).
		const float gas_dx = g2w.basis.get_column(0).length();
		sp.misc[2] = MAX(1.5f, gas_dx / MAX(h, 1e-6f));
		sp.smoke_per_fuel = smoke;
		sp.heat_rate = surface_heating;
		sp.attached = flame_attachment;
		sp.flicker = flame_flicker;
	}
	// Ignition (one per step).
	if (!pending_ignitions.is_empty()) {
		const Vector4 ig = pending_ignitions[0];
		const Vector3 v = voxel_to_world.affine_inverse().xform(Vector3(ig.x, ig.y, ig.z));
		sp.ignite[0] = v.x;
		sp.ignite[1] = v.y;
		sp.ignite[2] = v.z;
		sp.ignite[3] = MAX(ig.w / MAX(h, 1e-6f), 0.5f);
		sp.ignite_temperature = pending_ignition_temperatures[0];
		pending_ignitions.remove_at(0);
		pending_ignition_temperatures.remove_at(0);
	}
	sp.read_totals = (ticks % 6) == 0 ? 1 : 0;
	sp.tick = Engine::get_singleton()->get_physics_frames();
	PackedByteArray bytes;
	bytes.resize(sizeof(FellowGas::BurnStepParams));
	memcpy(bytes.ptrw(), &sp, sizeof(FellowGas::BurnStepParams));
	RS::get_singleton()->call_on_render_thread(callable_mp_static(&FellowBurnable3D::_rt_step).bind(burn_id, bytes));
	ticks++;
	// For the tests.
	last_step.clear();
	last_step["dt"] = sp.dt;
	last_step["voxel_size"] = h;
	last_step["settle"] = sp.settle;
	last_step["settle_axis"] = sp.settle_axis;
	last_step["settle_sign"] = sp.settle_sign;
	last_step["settle_floor"] = sp.settle_floor;
	last_step["gas_id"] = int64_t(sp.gas_id);
	last_step["voxel_to_gas"] = v2g;
	last_step["region_origin"] = Vector3i(sp.region_origin[0], sp.region_origin[1], sp.region_origin[2]);
	last_step["region_size"] = Vector3i(sp.region_size[0], sp.region_size[1], sp.region_size[2]);
	last_step["sample_distance"] = sp.misc[2];
	last_step["ignite"] = Vector4(sp.ignite[0], sp.ignite[1], sp.ignite[2], sp.ignite[3]);
	last_step["ignite_temperature"] = sp.ignite_temperature;
	PackedFloat32Array mat;
	for (const float *a : { sp.densities, sp.heats, sp.conduct, sp.pyro, sp.glow, sp.misc }) {
		for (int i = 0; i < 4; i++) {
			mat.push_back(a[i]);
		}
	}
	last_step["material"] = mat;
	last_step["footprint"] = sp.footprint;
	last_step["smoke_per_fuel"] = sp.smoke_per_fuel;
	last_step["heat_rate"] = sp.heat_rate;
	last_step["attached"] = sp.attached;
	last_step["flicker"] = sp.flicker;
	last_step["flame_temperature"] = sp.flame_temperature;
	last_step["flame_flux"] = sp.flame_flux;
	last_step["flame_reach"] = CLAMP(Math::round(sp.flame_reach), 0.0f, 8.0f);
	last_step["tick"] = int64_t(sp.tick);
}

void FellowBurnable3D::_read_totals() {
	PackedByteArray bytes;
	{
		MutexLock lock(totals->mutex);
		if (!totals->fresh) {
			return;
		}
		bytes = totals->data;
		totals->fresh = false;
	}
	const float *f = reinterpret_cast<const float *>(bytes.ptr());
	const int groups = int(bytes.size() / 32);
	double sum[4] = { 0.0, 0.0, 0.0, 0.0 };
	float tmax = 0.0f;
	double burning = 0.0;
	for (int i = 0; i < groups; i++) {
		for (int k = 0; k < 4; k++) {
			sum[k] += f[i * 8 + k];
		}
		tmax = MAX(tmax, f[i * 8 + 4]);
		burning += f[i * 8 + 5];
	}
	const double v0 = MAX(initial_volume, 1e-9);
	wood_fraction = float(sum[0] / v0);
	char_fraction = float(sum[1] / v0);
	ash_fraction = float(sum[2] / v0);
	fuel_release = float(sum[3]);
	max_temperature = tmax + BURN_AMBIENT;
	burning_voxels = int(burning);
	if (!has_ignited && burning_voxels > 0) {
		has_ignited = true;
		emit_signal(SNAME("ignited"));
	}
	if (has_ignited && !has_burned_out && burning_voxels == 0 && max_temperature < pyrolysis_temperature) {
		has_burned_out = true;
		emit_signal(SNAME("burned_out"));
	}
	if (draw_burning && heap_id.is_null() && ash_fraction > 0.001f) {
		if (MeshInstance3D *target = Object::cast_to<MeshInstance3D>(ObjectDB::get_instance(target_id))) {
			_build_heap(target);
		}
	}
	if (has_ignited && !has_collapsed && collapse_fraction > 0.0f && wood_fraction + char_fraction <= collapse_fraction) {
		_collapse();
	}
}

// Less wood and char left than collapse_fraction: the body it belongs to stops colliding (its shapes disabled; a
// rigid body frozen first, so it does not fall through the ground). Restored when the node leaves the tree or the
// burning is reset.
void FellowBurnable3D::_collapse() {
	has_collapsed = true;
	CollisionObject3D *body = Object::cast_to<CollisionObject3D>(ObjectDB::get_instance(body_id));
	if (body) {
		if (RigidBody3D *rb = Object::cast_to<RigidBody3D>(body)) {
			if (!rb->is_freeze_enabled()) {
				rb->set_freeze_enabled(true);
				froze_body = true;
			}
		}
		for (int i = 0; i < body->get_child_count(); i++) {
			CollisionShape3D *cs = Object::cast_to<CollisionShape3D>(body->get_child(i));
			if (cs && !cs->is_disabled()) {
				cs->set_disabled(true);
				disabled_shapes.push_back(cs->get_instance_id());
			}
		}
	}
	emit_signal(SNAME("collapsed"));
}

void FellowBurnable3D::_restore_collision() {
	for (const ObjectID &id : disabled_shapes) {
		if (CollisionShape3D *cs = Object::cast_to<CollisionShape3D>(ObjectDB::get_instance(id))) {
			cs->set_disabled(false);
		}
	}
	disabled_shapes.clear();
	if (froze_body) {
		if (RigidBody3D *rb = Object::cast_to<RigidBody3D>(ObjectDB::get_instance(body_id))) {
			rb->set_freeze_enabled(false);
		}
		froze_body = false;
	}
	has_collapsed = false;
}

// ------------------------------------------------------------------ API

void FellowBurnable3D::set_mesh_path(const NodePath &p_path) {
	mesh_path = p_path;
	if (configured) {
		_unconfigure();
	}
	failed = false;
}

void FellowBurnable3D::set_resolution(int p_resolution) {
	ERR_FAIL_COND_MSG(p_resolution < 4 || p_resolution > 128, "Resolution must be in [4, 128].");
	resolution = p_resolution;
	if (configured) {
		_unconfigure();
	}
	failed = false;
}

void FellowBurnable3D::set_subdivide(bool p_enable) {
	subdivide = p_enable;
	if (configured) {
		_unconfigure();
	}
}

void FellowBurnable3D::set_draw_burning(bool p_enable) {
	draw_burning = p_enable;
	if (configured) {
		_unconfigure();
	}
}

void FellowBurnable3D::set_char_color(const Color &p_color) {
	char_color = p_color;
	_update_materials();
}

void FellowBurnable3D::set_ash_color(const Color &p_color) {
	ash_color = p_color;
	_update_materials();
}

void FellowBurnable3D::set_glow_intensity(float p_value) {
	glow_intensity = MAX(p_value, 0.0f);
	_update_materials();
}

void FellowBurnable3D::set_crack_scale(float p_value) {
	crack_scale = MAX(p_value, 0.001f);
	_update_materials();
}

void FellowBurnable3D::ignite(const Vector3 &p_position, float p_radius, float p_temperature) {
	pending_ignitions.push_back(Vector4(p_position.x, p_position.y, p_position.z, MAX(p_radius, 0.0f)));
	pending_ignition_temperatures.push_back(MAX(p_temperature - BURN_AMBIENT, 0.0f));
}

void FellowBurnable3D::reset_burning() {
	if (!configured) {
		return;
	}
	_unconfigure();
	failed = false;
	pending_ignitions.clear();
	pending_ignition_temperatures.clear();
	_configure();
}

PackedFloat32Array FellowBurnable3D::get_initial_fill() const {
	PackedFloat32Array a;
	a.resize(initial_fill.size());
	if (!initial_fill.is_empty()) {
		memcpy(a.ptrw(), initial_fill.ptr(), initial_fill.size() * sizeof(float));
	}
	return a;
}

int FellowBurnable3D::get_display_triangle_count() const {
	if (display_mesh.is_null()) {
		return 0;
	}
	int n = 0;
	for (int s = 0; s < display_mesh->get_surface_count(); s++) {
		n += display_mesh->surface_get_array_index_len(s) / 3;
	}
	return n;
}

void FellowBurnable3D::request_debug_data(int p_what) {
	{
		MutexLock lock(debug->mutex);
		debug->fresh = false;
	}
	if (configured) {
		RS::get_singleton()->call_on_render_thread(callable_mp_static(&FellowBurnable3D::_rt_debug).bind(burn_id, p_what, debug));
	}
}

bool FellowBurnable3D::is_debug_data_ready() const {
	MutexLock lock(debug->mutex);
	return debug->fresh;
}

PackedByteArray FellowBurnable3D::get_debug_data() const {
	MutexLock lock(debug->mutex);
	return debug->data;
}

Dictionary FellowBurnable3D::get_last_step_info() const {
	return last_step;
}

void FellowBurnable3D::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_ENTER_TREE: {
			if (Engine::get_singleton()->is_editor_hint()) {
				return;
			}
			set_physics_process_internal(true);
			set_process_internal(true);
		} break;
		case NOTIFICATION_EXIT_TREE: {
			_unconfigure();
			failed = false;
		} break;
		case NOTIFICATION_INTERNAL_PHYSICS_PROCESS: {
			if (!Fellow::fluid_enabled()) {
				return;
			}
			if (!configured && !failed) {
				_configure();
			}
			if (configured && simulation_enabled) {
				_queue_step();
			}
		} break;
		case NOTIFICATION_INTERNAL_PROCESS: {
			if (configured) {
				_read_totals();
			}
		} break;
	}
}

void FellowBurnable3D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_mesh_path", "path"), &FellowBurnable3D::set_mesh_path);
	ClassDB::bind_method(D_METHOD("get_mesh_path"), &FellowBurnable3D::get_mesh_path);
	ClassDB::bind_method(D_METHOD("set_gas_volume_path", "path"), &FellowBurnable3D::set_gas_volume_path);
	ClassDB::bind_method(D_METHOD("get_gas_volume_path"), &FellowBurnable3D::get_gas_volume_path);
	ClassDB::bind_method(D_METHOD("set_resolution", "voxels"), &FellowBurnable3D::set_resolution);
	ClassDB::bind_method(D_METHOD("get_resolution"), &FellowBurnable3D::get_resolution);
	ClassDB::bind_method(D_METHOD("set_subdivide", "enable"), &FellowBurnable3D::set_subdivide);
	ClassDB::bind_method(D_METHOD("is_subdivide"), &FellowBurnable3D::is_subdivide);
	ClassDB::bind_method(D_METHOD("set_draw_burning", "enable"), &FellowBurnable3D::set_draw_burning);
	ClassDB::bind_method(D_METHOD("is_draw_burning"), &FellowBurnable3D::is_draw_burning);
	ClassDB::bind_method(D_METHOD("set_simulation_enabled", "enable"), &FellowBurnable3D::set_simulation_enabled);
	ClassDB::bind_method(D_METHOD("is_simulation_enabled"), &FellowBurnable3D::is_simulation_enabled);
	ClassDB::bind_method(D_METHOD("set_burn_speed", "speed"), &FellowBurnable3D::set_burn_speed);
	ClassDB::bind_method(D_METHOD("get_burn_speed"), &FellowBurnable3D::get_burn_speed);
	ClassDB::bind_method(D_METHOD("set_density", "density"), &FellowBurnable3D::set_density);
	ClassDB::bind_method(D_METHOD("get_density"), &FellowBurnable3D::get_density);
	ClassDB::bind_method(D_METHOD("set_specific_heat", "specific_heat"), &FellowBurnable3D::set_specific_heat);
	ClassDB::bind_method(D_METHOD("get_specific_heat"), &FellowBurnable3D::get_specific_heat);
	ClassDB::bind_method(D_METHOD("set_conductivity", "conductivity"), &FellowBurnable3D::set_conductivity);
	ClassDB::bind_method(D_METHOD("get_conductivity"), &FellowBurnable3D::get_conductivity);
	ClassDB::bind_method(D_METHOD("set_pyrolysis_temperature", "kelvin"), &FellowBurnable3D::set_pyrolysis_temperature);
	ClassDB::bind_method(D_METHOD("get_pyrolysis_temperature"), &FellowBurnable3D::get_pyrolysis_temperature);
	ClassDB::bind_method(D_METHOD("set_pyrolysis_rate", "rate"), &FellowBurnable3D::set_pyrolysis_rate);
	ClassDB::bind_method(D_METHOD("get_pyrolysis_rate"), &FellowBurnable3D::get_pyrolysis_rate);
	ClassDB::bind_method(D_METHOD("set_char_yield", "fraction"), &FellowBurnable3D::set_char_yield);
	ClassDB::bind_method(D_METHOD("get_char_yield"), &FellowBurnable3D::get_char_yield);
	ClassDB::bind_method(D_METHOD("set_glow_temperature", "kelvin"), &FellowBurnable3D::set_glow_temperature);
	ClassDB::bind_method(D_METHOD("get_glow_temperature"), &FellowBurnable3D::get_glow_temperature);
	ClassDB::bind_method(D_METHOD("set_glow_rate", "rate"), &FellowBurnable3D::set_glow_rate);
	ClassDB::bind_method(D_METHOD("get_glow_rate"), &FellowBurnable3D::get_glow_rate);
	ClassDB::bind_method(D_METHOD("set_ash_volume", "fraction"), &FellowBurnable3D::set_ash_volume);
	ClassDB::bind_method(D_METHOD("get_ash_volume"), &FellowBurnable3D::get_ash_volume);
	ClassDB::bind_method(D_METHOD("set_flame_strength", "strength"), &FellowBurnable3D::set_flame_strength);
	ClassDB::bind_method(D_METHOD("get_flame_strength"), &FellowBurnable3D::get_flame_strength);
	ClassDB::bind_method(D_METHOD("set_smoke", "smoke"), &FellowBurnable3D::set_smoke);
	ClassDB::bind_method(D_METHOD("get_smoke"), &FellowBurnable3D::get_smoke);
	ClassDB::bind_method(D_METHOD("set_surface_heating", "rate"), &FellowBurnable3D::set_surface_heating);
	ClassDB::bind_method(D_METHOD("get_surface_heating"), &FellowBurnable3D::get_surface_heating);
	ClassDB::bind_method(D_METHOD("set_flame_attachment", "fraction"), &FellowBurnable3D::set_flame_attachment);
	ClassDB::bind_method(D_METHOD("get_flame_attachment"), &FellowBurnable3D::get_flame_attachment);
	ClassDB::bind_method(D_METHOD("set_flame_flicker", "amount"), &FellowBurnable3D::set_flame_flicker);
	ClassDB::bind_method(D_METHOD("get_flame_flicker"), &FellowBurnable3D::get_flame_flicker);
	ClassDB::bind_method(D_METHOD("set_flame_temperature", "kelvin"), &FellowBurnable3D::set_flame_temperature);
	ClassDB::bind_method(D_METHOD("get_flame_temperature"), &FellowBurnable3D::get_flame_temperature);
	ClassDB::bind_method(D_METHOD("set_flame_reach", "metres"), &FellowBurnable3D::set_flame_reach);
	ClassDB::bind_method(D_METHOD("get_flame_reach"), &FellowBurnable3D::get_flame_reach);
	ClassDB::bind_method(D_METHOD("set_collapse_fraction", "fraction"), &FellowBurnable3D::set_collapse_fraction);
	ClassDB::bind_method(D_METHOD("get_collapse_fraction"), &FellowBurnable3D::get_collapse_fraction);
	ClassDB::bind_method(D_METHOD("is_collapsed"), &FellowBurnable3D::is_collapsed);
	ClassDB::bind_method(D_METHOD("set_char_color", "color"), &FellowBurnable3D::set_char_color);
	ClassDB::bind_method(D_METHOD("get_char_color"), &FellowBurnable3D::get_char_color);
	ClassDB::bind_method(D_METHOD("set_ash_color", "color"), &FellowBurnable3D::set_ash_color);
	ClassDB::bind_method(D_METHOD("get_ash_color"), &FellowBurnable3D::get_ash_color);
	ClassDB::bind_method(D_METHOD("set_glow_intensity", "intensity"), &FellowBurnable3D::set_glow_intensity);
	ClassDB::bind_method(D_METHOD("get_glow_intensity"), &FellowBurnable3D::get_glow_intensity);
	ClassDB::bind_method(D_METHOD("set_crack_scale", "metres"), &FellowBurnable3D::set_crack_scale);
	ClassDB::bind_method(D_METHOD("get_crack_scale"), &FellowBurnable3D::get_crack_scale);

	ClassDB::bind_method(D_METHOD("ignite", "position", "radius", "temperature"), &FellowBurnable3D::ignite, DEFVAL(900.0f));
	ClassDB::bind_method(D_METHOD("reset_burning"), &FellowBurnable3D::reset_burning);
	ClassDB::bind_method(D_METHOD("get_wood_fraction"), &FellowBurnable3D::get_wood_fraction);
	ClassDB::bind_method(D_METHOD("get_char_fraction"), &FellowBurnable3D::get_char_fraction);
	ClassDB::bind_method(D_METHOD("get_ash_fraction"), &FellowBurnable3D::get_ash_fraction);
	ClassDB::bind_method(D_METHOD("get_max_temperature"), &FellowBurnable3D::get_max_temperature);
	ClassDB::bind_method(D_METHOD("get_burning_voxels"), &FellowBurnable3D::get_burning_voxels);
	ClassDB::bind_method(D_METHOD("is_burning"), &FellowBurnable3D::is_burning);
	ClassDB::bind_method(D_METHOD("is_burned_out"), &FellowBurnable3D::is_burned_out);
	ClassDB::bind_method(D_METHOD("get_voxel_grid_size"), &FellowBurnable3D::get_voxel_grid_size);
	ClassDB::bind_method(D_METHOD("get_voxel_size"), &FellowBurnable3D::get_voxel_size);
	ClassDB::bind_method(D_METHOD("get_voxel_origin"), &FellowBurnable3D::get_voxel_origin);
	ClassDB::bind_method(D_METHOD("get_initial_fill"), &FellowBurnable3D::get_initial_fill);
	ClassDB::bind_method(D_METHOD("get_display_triangle_count"), &FellowBurnable3D::get_display_triangle_count);
	ClassDB::bind_method(D_METHOD("get_steps_simulated"), &FellowBurnable3D::get_steps_simulated);
	ClassDB::bind_method(D_METHOD("request_debug_data", "what"), &FellowBurnable3D::request_debug_data);
	ClassDB::bind_method(D_METHOD("is_debug_data_ready"), &FellowBurnable3D::is_debug_data_ready);
	ClassDB::bind_method(D_METHOD("get_debug_data"), &FellowBurnable3D::get_debug_data);
	ClassDB::bind_method(D_METHOD("get_last_step_info"), &FellowBurnable3D::get_last_step_info);

	ADD_GROUP("Object", "");
	ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "mesh_path", PROPERTY_HINT_NODE_PATH_VALID_TYPES, "MeshInstance3D"), "set_mesh_path", "get_mesh_path");
	ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "gas_volume_path", PROPERTY_HINT_NODE_PATH_VALID_TYPES, "FellowGasVolume3D"), "set_gas_volume_path", "get_gas_volume_path");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "resolution", PROPERTY_HINT_RANGE, "4,128,1"), "set_resolution", "get_resolution");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "subdivide"), "set_subdivide", "is_subdivide");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "draw_burning"), "set_draw_burning", "is_draw_burning");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "simulation_enabled"), "set_simulation_enabled", "is_simulation_enabled");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "burn_speed", PROPERTY_HINT_RANGE, "0,100,0.1,or_greater"), "set_burn_speed", "get_burn_speed");
	ADD_GROUP("Material", "");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "density", PROPERTY_HINT_RANGE, "50,1500,1,suffix:kg/m³"), "set_density", "get_density");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "specific_heat", PROPERTY_HINT_RANGE, "100,5000,1,suffix:J/(kg K)"), "set_specific_heat", "get_specific_heat");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "conductivity", PROPERTY_HINT_RANGE, "0,2,0.001,suffix:W/(m K)"), "set_conductivity", "get_conductivity");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "pyrolysis_temperature", PROPERTY_HINT_RANGE, "300,1200,1,suffix:K"), "set_pyrolysis_temperature", "get_pyrolysis_temperature");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "pyrolysis_rate", PROPERTY_HINT_RANGE, "0,1,0.001,suffix:1/s"), "set_pyrolysis_rate", "get_pyrolysis_rate");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "char_yield", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_char_yield", "get_char_yield");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "glow_temperature", PROPERTY_HINT_RANGE, "300,1500,1,suffix:K"), "set_glow_temperature", "get_glow_temperature");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "glow_rate", PROPERTY_HINT_RANGE, "0,1,0.001,suffix:1/s"), "set_glow_rate", "get_glow_rate");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "ash_volume", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_ash_volume", "get_ash_volume");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "collapse_fraction", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_collapse_fraction", "get_collapse_fraction");
	ADD_GROUP("Gas", "");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "flame_strength", PROPERTY_HINT_RANGE, "0,10,0.01,or_greater"), "set_flame_strength", "get_flame_strength");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "smoke", PROPERTY_HINT_RANGE, "0,4,0.01,or_greater"), "set_smoke", "get_smoke");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "surface_heating", PROPERTY_HINT_RANGE, "0,1000,0.1,or_greater,suffix:1/s"), "set_surface_heating", "get_surface_heating");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "flame_attachment", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_flame_attachment", "get_flame_attachment");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "flame_flicker", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_flame_flicker", "get_flame_flicker");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "flame_temperature", PROPERTY_HINT_RANGE, "0,2000,1,suffix:K"), "set_flame_temperature", "get_flame_temperature");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "flame_reach", PROPERTY_HINT_RANGE, "0,1,0.01,or_greater,suffix:m"), "set_flame_reach", "get_flame_reach");
	ADD_GROUP("Look", "");
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "char_color", PROPERTY_HINT_COLOR_NO_ALPHA), "set_char_color", "get_char_color");
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "ash_color", PROPERTY_HINT_COLOR_NO_ALPHA), "set_ash_color", "get_ash_color");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "glow_intensity", PROPERTY_HINT_RANGE, "0,50,0.01,or_greater"), "set_glow_intensity", "get_glow_intensity");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "crack_scale", PROPERTY_HINT_RANGE, "0.001,1,0.001,suffix:m"), "set_crack_scale", "get_crack_scale");

	ADD_SIGNAL(MethodInfo("ignited"));
	ADD_SIGNAL(MethodInfo("burned_out"));
	ADD_SIGNAL(MethodInfo("collapsed"));
}
