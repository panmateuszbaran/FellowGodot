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
#include "scene/main/scene_tree.h"
#include "scene/resources/shader.h"
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
static constexpr float BURN_HEAT_OF_COMBUSTION = 15.0e6f; // J per kg of the volatiles
static constexpr int BURN_MARGIN = 2; // voxels around the mesh
static constexpr int BURN_TRIANGLE_BUDGET = 300000;

Mutex FellowBurnable3D::body_mutex;
HashMap<uint64_t, int> FellowBurnable3D::burnable_bodies;

FellowBurnable3D::FellowBurnable3D() {
	burn_id = FellowGas::allocate_id();
	totals.instantiate();
	debug.instantiate();
	display_texture.instantiate();
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
	voxel = longest / float(resolution);
	for (int a = 0; a < 3; a++) {
		res[a] = MAX(1, int(Math::ceil(box.size[a] / voxel - 1e-4f))) + 2 * BURN_MARGIN;
	}
	origin = box.get_center() - Vector3(res) * voxel * 0.5f;
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

// 1 on the cracks between cells (a cellular pattern: the distance to the nearest cell's point against the second's).
float fellow_burn_cracks(vec3 p_p) {
	vec3 i = floor(p_p);
	vec3 f = p_p - i;
	float d1 = 8.0;
	float d2 = 8.0;
	for (int z = -1; z <= 1; z++) {
		for (int y = -1; y <= 1; y++) {
			for (int x = -1; x <= 1; x++) {
				vec3 o = vec3(float(x), float(y), float(z));
				vec3 r = o + fellow_burn_hash3(i + o) - f;
				float d = dot(r, r);
				if (d < d1) {
					d2 = d1;
					d1 = d;
				} else if (d < d2) {
					d2 = d;
				}
			}
		}
	}
	return 1.0 - smoothstep(0.0, 0.08, sqrt(d2) - sqrt(d1));
}

// A blackbody's colour (linear, its brightest channel 1), roughly, 800-2000 K: deep red to orange-yellow.
vec3 fellow_burn_blackbody(float p_t) {
	float x = clamp((p_t - 800.0) / 1200.0, 0.0, 1.0);
	return vec3(1.0, mix(0.07, 0.45, x), mix(0.003, 0.12, x * x));
}
)";

static const char *FELLOW_BURN_VERTEX = R"(
	// FellowEngine burning: the vertex marches into the voxels (along its stored direction) to the material's
	// surface now, and moves by how far that surface has receded since the start; where it finds none the object is
	// burned through there and the vertex falls onto what is left below.
	fellow_burn_local = VERTEX;
	fellow_burn_gone = 0.0;
	fellow_burn_mat = vec4(0.0, 1.0, 0.0, 0.0);
	{
		vec4 fb_c = CUSTOM_CHANNEL;
		if (fb_c.y > 0.0) {
			vec3 fb_d = fellow_burn_oct_decode(fb_c.zw);
			float fb_thr = 0.5 * fb_c.y;
			float fb_prev = fellow_burn_fill(VERTEX);
			float fb_t = -1.0;
			if (fb_prev > fb_thr) {
				fb_t = 0.0;
			} else {
				for (int i = 1; i <= fellow_burn_steps; i++) {
					float t = float(i) * fellow_burn_step;
					float f = fellow_burn_fill(VERTEX + fb_d * t);
					if (f > fb_thr) {
						fb_t = t - fellow_burn_step * (f - fb_thr) / max(f - fb_prev, 1e-6);
						break;
					}
					fb_prev = f;
				}
			}
			float fb_disp = 0.0;
			vec3 fb_move = vec3(0.0);
			vec3 fb_in = fb_d;
			if (fb_t < 0.0) {
				// Burned through along its direction: the vertex falls (the world's down) onto what is left below it,
				// the ash heap or the char; with nothing below, the triangles touching it are not drawn.
				vec3 fb_down = normalize((inverse(MODEL_MATRIX) * vec4(0.0, -1.0, 0.0, 0.0)).xyz);
				float fb_prev2 = fellow_burn_fill(VERTEX);
				for (int i = 1; i <= fellow_burn_steps; i++) {
					float t = float(i) * fellow_burn_step;
					float f = fellow_burn_fill(VERTEX + fb_down * t);
					if (f > fb_thr) {
						fb_t = t - fellow_burn_step * (f - fb_thr) / max(f - fb_prev2, 1e-6);
						break;
					}
					fb_prev2 = f;
				}
				if (fb_t < 0.0) {
					fellow_burn_gone = 1.0;
				} else {
					fb_disp = fb_t;
					fb_move = fb_down * fb_t;
					fb_in = fb_down;
				}
			} else {
				fb_disp = max(fb_t - fb_c.x - 0.15 * fellow_burn_voxel, 0.0);
				fb_move = fb_d * fb_disp;
			}
			VERTEX += fb_move;
			if (fb_disp > 0.0) {
				float h = fellow_burn_voxel;
				vec3 g = vec3(fellow_burn_fill(VERTEX + vec3(h, 0.0, 0.0)) - fellow_burn_fill(VERTEX - vec3(h, 0.0, 0.0)),
						fellow_burn_fill(VERTEX + vec3(0.0, h, 0.0)) - fellow_burn_fill(VERTEX - vec3(0.0, h, 0.0)),
						fellow_burn_fill(VERTEX + vec3(0.0, 0.0, h)) - fellow_burn_fill(VERTEX - vec3(0.0, 0.0, h)));
				if (length(g) > 1e-4) {
					NORMAL = normalize(mix(NORMAL, -normalize(g), smoothstep(0.0, fellow_burn_voxel, fb_disp)));
				}
			}
			fellow_burn_mat = fellow_burn_sample(VERTEX + fb_in * (0.5 * fellow_burn_voxel));
		}
	}
)";

static const char *FELLOW_BURN_FRAGMENT = R"(
	// FellowEngine burning: scorched, charred, ash; the embers glow in the char's cracks and the hot ash.
	if (fellow_burn_gone > 0.001) {
		discard;
	}
	{
		vec4 fb = fellow_burn_mat;
		float fb_sum = max(fb.y + fb.z + fb.w, 1e-4);
		float fb_char = fb.z / fb_sum;
		float fb_ash = fb.w / fb_sum;
		float fb_dark = smoothstep(0.0, 0.35, fb_char + fb_ash);
		ALBEDO = mix(ALBEDO, fellow_burn_char_color, fb_dark);
		ALBEDO = mix(ALBEDO, fellow_burn_ash_color, smoothstep(0.15, 0.7, fb_ash));
		ROUGHNESS = mix(ROUGHNESS, 0.95, fb_dark);
		// Embers: the char's surface no hotter than glowing charcoal (it sits in the flame; the gas's temperature is the
		// flame's), brightest in the cracks between its cells.
		float fb_temperature = min(fb.x + 293.0, 1300.0);
		float fb_hot = smoothstep(750.0, 1000.0, fb_temperature) * pow(fb_temperature / 1300.0, 3.0);
		if (fb_hot > 0.0) {
			vec3 fb_p = fellow_burn_local / max(fellow_burn_crack_scale, 1e-4);
			// Fewer pixels than cells: the pattern's average (no sparkle).
			float fb_crack = mix(fellow_burn_cracks(fb_p), 0.3, smoothstep(0.25, 0.8, length(fwidth(fb_p))));
			float fb_patch = 0.35 + 0.65 * fellow_burn_hash3(floor(fb_p * 0.5)).x; // some cells glow, some do not
			EMISSION += fellow_burn_blackbody(fb_temperature) * (fellow_burn_glow * fb_hot * (fb_char * (0.08 + 0.92 * fb_crack) * fb_patch + 0.5 * fb_ash));
		}
	}
)";

// The end of a shader function's body (the index of its closing brace), or -1.
static int _fellow_function_end(const String &p_code, const String &p_name) {
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

static String _fellow_burn_code(const String &p_code, int p_custom) {
	String code = p_code;
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
	code = code.insert(insert + 1, String("\n") + FELLOW_BURN_DECLARATIONS);
	const bool world_vertex = rm >= 0 && code.substr(rm, code.find(";", rm) - rm).contains("world_vertex_coords");
	String vertex = String(FELLOW_BURN_VERTEX).replace("CUSTOM_CHANNEL", "CUSTOM" + itos(p_custom));
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

Ref<ShaderMaterial> FellowBurnable3D::_make_burn_material(const Ref<Material> &p_source, int p_custom) const {
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
	const String burn_code = _fellow_burn_code(code, p_custom);
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
	return m;
}

void FellowBurnable3D::_update_materials() {
	for (Ref<ShaderMaterial> &m : burn_materials) {
		if (m.is_null()) {
			continue;
		}
		m->set_shader_parameter("fellow_burn_char_color", char_color);
		m->set_shader_parameter("fellow_burn_ash_color", ash_color);
		m->set_shader_parameter("fellow_burn_glow", glow_intensity);
		m->set_shader_parameter("fellow_burn_crack_scale", crack_scale);
	}
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
	for (uint32_t si = 0; si < surfaces.size(); si++) {
		const SurfaceInfo &info = surfaces[si];
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
		Ref<ShaderMaterial> bm;
		if (info.custom >= 0) {
			Ref<ShaderMaterial> *f = made.getptr(info.material.ptr());
			if (f) {
				bm = *f;
			} else {
				bm = _make_burn_material(info.material, info.custom);
				made.insert(info.material.ptr(), bm);
			}
		}
		if (bm.is_valid()) {
			display_mesh->surface_set_material(ds, bm);
			burn_materials.push_back(bm);
		} else {
			display_mesh->surface_set_material(ds, info.material);
		}
	}
}

// ------------------------------------------------------------------ render thread

void FellowBurnable3D::_rt_configure(uint32_t p_id, Vector3i p_res, PackedByteArray p_initial, Ref<FellowGasReadback> p_totals, Ref<Texture3DRD> p_texture) {
	FellowGas *gas = FellowGas::get_singleton();
	if (!gas) {
		ERR_PRINT_ONCE("FellowBurnable3D requires a RenderingDevice renderer (Forward+ or Mobile).");
		return;
	}
	gas->burnable_configure(p_id, p_res, p_initial);
	gas->burnable_set_totals_callback(p_id, callable_mp_static(&FellowBurnable3D::_rt_store).bind(p_totals));
	if (p_texture.is_valid()) {
		p_texture->set_texture_rd_rid(gas->burnable_get_display(p_id));
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
	if (draw_burning) {
		_build_display(target);
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
	RS::get_singleton()->call_on_render_thread(callable_mp_static(&FellowBurnable3D::_rt_free).bind(burn_id));
	MeshInstance3D *target = Object::cast_to<MeshInstance3D>(ObjectDB::get_instance(target_id));
	if (target && display_mesh.is_valid() && target->get_mesh() == display_mesh) {
		target->set_mesh(original_mesh);
		for (int s = 0; s < MIN(target->get_surface_override_material_count(), original_overrides.size()); s++) {
			target->set_surface_override_material(s, original_overrides[s]);
		}
		target->set_material_override(original_material_override);
	}
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
	sp.flame_heat = flame_feedback * BURN_HEAT_OF_COMBUSTION;
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
	last_step["flame_heat"] = sp.flame_heat;
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
	ClassDB::bind_method(D_METHOD("set_flame_feedback", "fraction"), &FellowBurnable3D::set_flame_feedback);
	ClassDB::bind_method(D_METHOD("get_flame_feedback"), &FellowBurnable3D::get_flame_feedback);
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
	ADD_GROUP("Gas", "");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "flame_strength", PROPERTY_HINT_RANGE, "0,10,0.01,or_greater"), "set_flame_strength", "get_flame_strength");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "smoke", PROPERTY_HINT_RANGE, "0,4,0.01,or_greater"), "set_smoke", "get_smoke");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "surface_heating", PROPERTY_HINT_RANGE, "0,1000,0.1,or_greater,suffix:1/s"), "set_surface_heating", "get_surface_heating");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "flame_attachment", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_flame_attachment", "get_flame_attachment");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "flame_feedback", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_flame_feedback", "get_flame_feedback");
	ADD_GROUP("Look", "");
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "char_color", PROPERTY_HINT_COLOR_NO_ALPHA), "set_char_color", "get_char_color");
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "ash_color", PROPERTY_HINT_COLOR_NO_ALPHA), "set_ash_color", "get_ash_color");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "glow_intensity", PROPERTY_HINT_RANGE, "0,50,0.01,or_greater"), "set_glow_intensity", "get_glow_intensity");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "crack_scale", PROPERTY_HINT_RANGE, "0.001,1,0.001,suffix:m"), "set_crack_scale", "get_crack_scale");

	ADD_SIGNAL(MethodInfo("ignited"));
	ADD_SIGNAL(MethodInfo("burned_out"));
}
