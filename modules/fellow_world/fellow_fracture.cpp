/**************************************************************************/
/*  fellow_fracture.cpp                                                   */
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

#include "fellow_fracture.h"

#include "core/math/convex_hull.h"
#include "core/math/geometry_2d.h"
#include "core/math/geometry_3d.h"
#include "core/math/random_pcg.h"
#include "core/templates/hash_map.h"
#include "core/templates/hash_set.h"

#include <thirdparty/clipper2/include/clipper2/clipper.h>
#include <thirdparty/misc/polypartition.h>

namespace FellowFracture {

namespace {

const float WELD = 1e-5f; // cut points nearer than this are one (10 µm)
const int CLIPPER_PRECISION = 6; // decimal places (1 µm)

Vertex lerp_vertex(const Vertex &p_a, const Vertex &p_b, float p_t) {
	Vertex v;
	v.p = p_a.p.lerp(p_b.p, p_t);
	v.n = p_a.n.lerp(p_b.n, p_t);
	v.uv = p_a.uv.lerp(p_b.uv, p_t);
	return v;
}

// Where an edge crosses a plane, computed from its ends in a fixed order: the same point from both triangles of the
// edge, so the cuts of a mesh close exactly.
Vertex cut_edge(const Vertex &p_a, float p_da, const Vertex &p_b, float p_db) {
	if (p_b.p < p_a.p) {
		return lerp_vertex(p_b, p_a, p_db / (p_db - p_da));
	}
	return lerp_vertex(p_a, p_b, p_da / (p_da - p_db));
}

// Points merged within WELD (a spatial hash, the neighbouring cells searched).
struct PointWeld {
	LocalVector<Vector3> points;
	HashMap<Vector3i, LocalVector<uint32_t>> cells;

	Vector3i cell(const Vector3 &p_p) const {
		return Vector3i(int(Math::floor(p_p.x / WELD)), int(Math::floor(p_p.y / WELD)), int(Math::floor(p_p.z / WELD)));
	}

	uint32_t id(const Vector3 &p_p) {
		const Vector3i c = cell(p_p);
		for (int x = -1; x <= 1; x++) {
			for (int y = -1; y <= 1; y++) {
				for (int z = -1; z <= 1; z++) {
					HashMap<Vector3i, LocalVector<uint32_t>>::Iterator it = cells.find(c + Vector3i(x, y, z));
					if (!it) {
						continue;
					}
					for (uint32_t i : it->value) {
						if (points[i].distance_squared_to(p_p) <= WELD * WELD) {
							return i;
						}
					}
				}
			}
		}
		const uint32_t i = points.size();
		points.push_back(p_p);
		cells[c].push_back(i);
		return i;
	}
};

AABB triangles_aabb(const LocalVector<Triangle> &p_triangles) {
	AABB box(p_triangles[0].v[0].p, Vector3());
	for (const Triangle &t : p_triangles) {
		for (int k = 0; k < 3; k++) {
			box.expand_to(t.v[k].p);
		}
	}
	return box;
}

// The part of the triangles on the plane's negative side (no caps: they come from the cross-sections).
void clip_surface(const LocalVector<Triangle> &p_in, LocalVector<Triangle> &r_out, const Plane &p_plane) {
	r_out.clear();
	for (const Triangle &t : p_in) {
		float d[3];
		bool in[3];
		int inside = 0;
		for (int k = 0; k < 3; k++) {
			d[k] = p_plane.distance_to(t.v[k].p);
			in[k] = d[k] <= 0.0f;
			inside += in[k];
		}
		if (inside == 3) {
			r_out.push_back(t);
			continue;
		}
		if (inside == 0) {
			continue;
		}
		Vertex poly[4];
		int pn = 0;
		for (int k = 0; k < 3; k++) {
			const int j = (k + 1) % 3;
			if (in[k]) {
				poly[pn++] = t.v[k];
			}
			if (in[k] != in[j]) {
				poly[pn++] = cut_edge(t.v[k], d[k], t.v[j], d[j]);
			}
		}
		for (int k = 1; k + 1 < pn; k++) {
			Triangle nt;
			nt.surface = t.surface;
			nt.v[0] = poly[0];
			nt.v[1] = poly[k];
			nt.v[2] = poly[k + 1];
			if ((nt.v[1].p - nt.v[0].p).cross(nt.v[2].p - nt.v[0].p).length_squared() > 1e-24f) {
				r_out.push_back(nt);
			}
		}
	}
}

// A plane's frame: its normal, offset and two axes in it (t1 x t2 = n).
struct Frame {
	Vector3 n;
	float d = 0.0f;
	Vector3 t1;
	Vector3 t2;

	Frame() {}
	Frame(const Vector3 &p_n, float p_d) {
		n = p_n;
		d = p_d;
		t1 = n.get_any_perpendicular().normalized();
		t2 = n.cross(t1).normalized();
	}
	Vector3 to_3d(double p_x, double p_y) const { return n * d + t1 * float(p_x) + t2 * float(p_y); }
};

// Each source triangle's bounding sphere (a plane missing it is skipped without its vertices).
struct Bounds {
	Vector3 c;
	float r = 0.0f;
};

// The source solid's cross-section by a plane: the loops its triangles' cuts make, in the plane's frame (even-odd: a
// loop inside a loop is a hole). The cuts of an edge are the same point from both its triangles, so they are matched
// exactly; a mesh whose shared vertices differ slightly is matched within WELD. Open chains (an open mesh) are dropped.
void cross_section(const LocalVector<Triangle> &p_source, const LocalVector<Bounds> &p_bounds, const Frame &p_frame, Clipper2Lib::PathsD &r_loops, bool p_tolerant = false) {
	r_loops.clear();
	const Plane plane(p_frame.n, p_frame.d);
	PointWeld weld;
	HashMap<Vector3, uint32_t> exact;
	LocalVector<Vector3> points;
	auto id = [&](const Vector3 &p_p) -> uint32_t {
		if (p_tolerant) {
			return weld.id(p_p);
		}
		HashMap<Vector3, uint32_t>::Iterator it = exact.find(p_p);
		if (it) {
			return it->value;
		}
		points.push_back(p_p);
		exact.insert(p_p, points.size() - 1);
		return points.size() - 1;
	};
	LocalVector<Pair<uint32_t, uint32_t>> segs;
	for (uint32_t ti = 0; ti < p_source.size(); ti++) {
		if (Math::abs(plane.distance_to(p_bounds[ti].c)) > p_bounds[ti].r) {
			continue;
		}
		const Triangle &t = p_source[ti];
		float d[3];
		int above = 0;
		for (int k = 0; k < 3; k++) {
			d[k] = plane.distance_to(t.v[k].p);
			above += d[k] > 0.0f;
		}
		if (above == 0 || above == 3) {
			continue;
		}
		uint32_t ends[2];
		int en = 0;
		for (int k = 0; k < 3 && en < 2; k++) {
			const int j = (k + 1) % 3;
			if ((d[k] > 0.0f) != (d[j] > 0.0f)) {
				ends[en++] = id(cut_edge(t.v[k], d[k], t.v[j], d[j]).p);
			}
		}
		if (en == 2 && ends[0] != ends[1]) {
			segs.push_back(Pair<uint32_t, uint32_t>(ends[0], ends[1]));
		}
	}
	if (segs.size() < 3) {
		return;
	}
	const LocalVector<Vector3> &pts = p_tolerant ? weld.points : points;
	LocalVector<LocalVector<uint32_t>> at;
	at.resize(pts.size());
	for (uint32_t i = 0; i < segs.size(); i++) {
		at[segs[i].first].push_back(i);
		at[segs[i].second].push_back(i);
	}
	LocalVector<uint8_t> used;
	used.resize(segs.size());
	memset(used.ptr(), 0, used.size());
	bool open = false;
	for (uint32_t s0 = 0; s0 < segs.size(); s0++) {
		if (used[s0]) {
			continue;
		}
		used[s0] = 1;
		LocalVector<uint32_t> loop;
		loop.push_back(segs[s0].first);
		uint32_t cur = segs[s0].second;
		bool closed = false;
		for (uint32_t guard = 0; guard <= segs.size(); guard++) {
			if (cur == loop[0]) {
				closed = true;
				break;
			}
			loop.push_back(cur);
			uint32_t next = UINT32_MAX;
			for (uint32_t si : at[cur]) {
				if (!used[si]) {
					next = si;
					break;
				}
			}
			if (next == UINT32_MAX) {
				break;
			}
			used[next] = 1;
			cur = segs[next].first == cur ? segs[next].second : segs[next].first;
		}
		if (!closed) {
			open = true;
			continue;
		}
		if (loop.size() < 3) {
			continue;
		}
		Clipper2Lib::PathD path;
		path.reserve(loop.size());
		for (uint32_t i : loop) {
			const Vector3 &p = pts[i];
			path.push_back(Clipper2Lib::PointD(p.dot(p_frame.t1), p.dot(p_frame.t2)));
		}
		r_loops.push_back(path);
	}
	if (open && !p_tolerant) {
		cross_section(p_source, p_bounds, p_frame, r_loops, true); // shared vertices not quite the same: within WELD
	}
}

// A convex polygon (the plane's frame) clipped to a*x + b*y <= c.
void clip_convex(Clipper2Lib::PathD &r_poly, double p_a, double p_b, double p_c) {
	Clipper2Lib::PathD out;
	const size_t n = r_poly.size();
	for (size_t k = 0; k < n; k++) {
		const Clipper2Lib::PointD &p = r_poly[k];
		const Clipper2Lib::PointD &q = r_poly[(k + 1) % n];
		const double dp = p_a * p.x + p_b * p.y - p_c;
		const double dq = p_a * q.x + p_b * q.y - p_c;
		if (dp <= 0.0) {
			out.push_back(p);
		}
		if ((dp <= 0.0) != (dq <= 0.0)) {
			const double t = dp / (dp - dq);
			out.push_back(Clipper2Lib::PointD(p.x + (q.x - p.x) * t, p.y + (q.y - p.y) * t));
		}
	}
	r_poly = out;
}

// The cap's triangles: a region of the plane (a Clipper tree: outlines, their holes, islands in those) filled, facing
// p_out.
void fill(const Clipper2Lib::PolyPathD &p_node, const Frame &p_frame, const Vector3 &p_out, const Params &p_params, LocalVector<Triangle> &r_triangles) {
	auto emit = [&](const Vector2 &p_a, const Vector2 &p_b, const Vector2 &p_c) {
		Triangle t;
		t.surface = p_params.interior_surface;
		const Vector2 q[3] = { p_a, p_b, p_c };
		for (int m = 0; m < 3; m++) {
			t.v[m].p = p_frame.to_3d(q[m].x, q[m].y);
			t.v[m].n = p_out;
			t.v[m].uv = q[m] * p_params.interior_uv_scale;
		}
		const Vector3 c = (t.v[1].p - t.v[0].p).cross(t.v[2].p - t.v[0].p);
		if (c.length_squared() < 1e-24f) {
			return;
		}
		if ((c.dot(p_out) < 0.0f ? -1.0f : 1.0f) != p_params.winding) {
			SWAP(t.v[1], t.v[2]);
		}
		r_triangles.push_back(t);
	};
	auto to_tppl = [](const Clipper2Lib::PathD &p_path, TPPLOrientation p_orientation, bool p_hole) {
		TPPLPoly poly;
		poly.Init(long(p_path.size()));
		for (size_t k = 0; k < p_path.size(); k++) {
			poly[int(k)] = Vector2(real_t(p_path[k].x), real_t(p_path[k].y));
		}
		poly.SetOrientation(p_orientation);
		poly.SetHole(p_hole);
		return poly;
	};
	for (size_t i = 0; i < p_node.Count(); i++) {
		const Clipper2Lib::PolyPathD *outer = p_node.Child(i);
		if (outer->Polygon().size() < 3) {
			continue;
		}
		TPPLPolyList in;
		in.push_back(to_tppl(outer->Polygon(), TPPL_ORIENTATION_CCW, false));
		for (size_t h = 0; h < outer->Count(); h++) {
			if (outer->Child(h)->Polygon().size() >= 3) {
				in.push_back(to_tppl(outer->Child(h)->Polygon(), TPPL_ORIENTATION_CW, true));
			}
		}
		TPPLPartition part;
		TPPLPolyList tris;
		if (part.Triangulate_EC(&in, &tris)) {
			for (const TPPLPoly &tp : tris) {
				if (tp.GetNumPoints() == 3) {
					emit(tp[0], tp[1], tp[2]);
				}
			}
		} else {
			// Ear clipping gave up (a degenerate outline): the outline alone.
			Vector<Vector2> poly;
			for (const Clipper2Lib::PointD &p : outer->Polygon()) {
				poly.push_back(Vector2(real_t(p.x), real_t(p.y)));
			}
			const Vector<int> idx = Geometry2D::triangulate_polygon(poly);
			for (int k = 0; k + 2 < idx.size(); k += 3) {
				emit(poly[idx[k]], poly[idx[k + 1]], poly[idx[k + 2]]);
			}
		}
		for (size_t h = 0; h < outer->Count(); h++) {
			fill(*outer->Child(h), p_frame, p_out, p_params, r_triangles); // islands inside the holes
		}
	}
}

// The triangles split into the groups that share vertices.
void components(const LocalVector<Triangle> &p_tris, LocalVector<LocalVector<Triangle>> &r_parts) {
	PointWeld weld;
	LocalVector<uint32_t> ids;
	ids.resize(p_tris.size() * 3);
	for (uint32_t i = 0; i < p_tris.size(); i++) {
		for (int k = 0; k < 3; k++) {
			ids[i * 3 + k] = weld.id(p_tris[i].v[k].p);
		}
	}
	LocalVector<uint32_t> parent;
	parent.resize(weld.points.size());
	for (uint32_t i = 0; i < parent.size(); i++) {
		parent[i] = i;
	}
	auto root = [&](uint32_t p_i) {
		while (parent[p_i] != p_i) {
			parent[p_i] = parent[parent[p_i]];
			p_i = parent[p_i];
		}
		return p_i;
	};
	for (uint32_t i = 0; i < p_tris.size(); i++) {
		const uint32_t a = root(ids[i * 3]);
		for (int k = 1; k < 3; k++) {
			const uint32_t b = root(ids[i * 3 + k]);
			if (a != b) {
				parent[b] = a;
			}
		}
	}
	HashMap<uint32_t, uint32_t> group;
	for (uint32_t i = 0; i < p_tris.size(); i++) {
		const uint32_t r = root(ids[i * 3]);
		HashMap<uint32_t, uint32_t>::Iterator it = group.find(r);
		if (!it) {
			it = group.insert(r, r_parts.size());
			r_parts.push_back(LocalVector<Triangle>());
		}
		r_parts[it->value].push_back(p_tris[i]);
	}
}

float volume_of(const LocalVector<Triangle> &p_tris, Vector3 *r_centroid = nullptr);
Vector<Vector3> hull_of(const LocalVector<Triangle> &p_tris, float *r_volume);

} // namespace

void mesh_triangles(const Ref<Mesh> &p_mesh, LocalVector<Triangle> &r_triangles, float &r_winding) {
	r_triangles.clear();
	float vote = 0.0f;
	if (p_mesh.is_null()) {
		r_winding = -1.0f;
		return;
	}
	for (int s = 0; s < p_mesh->get_surface_count(); s++) {
		if (p_mesh->surface_get_primitive_type(s) != Mesh::PRIMITIVE_TRIANGLES) {
			continue;
		}
		const Array arrays = p_mesh->surface_get_arrays(s);
		const PackedVector3Array verts = arrays[Mesh::ARRAY_VERTEX];
		const PackedVector3Array normals = arrays[Mesh::ARRAY_NORMAL];
		const PackedVector2Array uvs = arrays[Mesh::ARRAY_TEX_UV];
		PackedInt32Array idx = arrays[Mesh::ARRAY_INDEX];
		if (idx.is_empty()) {
			idx.resize(verts.size());
			for (int i = 0; i < verts.size(); i++) {
				idx.set(i, i);
			}
		}
		const bool has_n = normals.size() == verts.size();
		const bool has_uv = uvs.size() == verts.size();
		for (int i = 0; i + 2 < idx.size(); i += 3) {
			Triangle t;
			t.surface = s;
			for (int k = 0; k < 3; k++) {
				const int vi = idx[i + k];
				t.v[k].p = verts[vi];
				t.v[k].n = has_n ? normals[vi] : Vector3();
				t.v[k].uv = has_uv ? uvs[vi] : Vector2();
			}
			const Vector3 c = (t.v[1].p - t.v[0].p).cross(t.v[2].p - t.v[0].p);
			if (has_n) {
				vote += c.dot(t.v[0].n + t.v[1].n + t.v[2].n) < 0.0f ? -1.0f : 1.0f;
			}
			r_triangles.push_back(t);
		}
	}
	r_winding = vote > 0.0f ? 1.0f : -1.0f;
	for (Triangle &t : r_triangles) {
		if (t.v[0].n == Vector3()) {
			const Vector3 c = (t.v[1].p - t.v[0].p).cross(t.v[2].p - t.v[0].p);
			const Vector3 n = c.length() > 0.0f ? c.normalized() * r_winding : Vector3(0, 1, 0);
			t.v[0].n = t.v[1].n = t.v[2].n = n;
		}
	}
}

bool contains(const LocalVector<Triangle> &p_source, const Vector3 &p_point) {
	// A ray along a slightly skewed axis: crossings odd inside (a closed mesh).
	const Vector3 dir = Vector3(1.0f, 0.0123f, 0.0071f).normalized();
	int crossings = 0;
	for (const Triangle &t : p_source) {
		if (Geometry3D::ray_intersects_triangle(p_point, dir, t.v[0].p, t.v[1].p, t.v[2].p)) {
			crossings++;
		}
	}
	return (crossings & 1) == 1;
}

void make_sites(const LocalVector<Triangle> &p_source, int p_count, uint64_t p_seed, const Vector3 &p_impact, float p_focus, LocalVector<Vector3> &r_sites) {
	r_sites.clear();
	if (p_source.is_empty() || p_count <= 0) {
		return;
	}
	const AABB box = triangles_aabb(p_source);
	const float longest = MAX(box.get_longest_axis_size(), 1e-4f);
	// A closed mesh (its faces' area vectors cancel) has an inside to put the sites in; an open one only its bounds.
	Vector3 area_sum;
	float area = 0.0f;
	for (const Triangle &t : p_source) {
		const Vector3 c = (t.v[1].p - t.v[0].p).cross(t.v[2].p - t.v[0].p);
		area_sum += c;
		area += c.length();
	}
	const bool closed = area > 0.0f && area_sum.length() < 1e-3f * area;
	// A thin object (a pane, a plank) is cut across: its sites on its middle along the thin axes.
	Vector3 flat;
	for (int a = 0; a < 3; a++) {
		flat[a] = box.size[a] < 0.15f * longest ? 1.0f : 0.0f;
	}
	const Vector3 centre = box.get_center();
	auto flatten = [&](Vector3 p_p) {
		p_p = p_p.clamp(box.position, box.get_end());
		for (int a = 0; a < 3; a++) {
			if (flat[a] > 0.0f) {
				p_p[a] = centre[a];
			}
		}
		return p_p;
	};
	// The impact, moved in from the surface (or from outside: towards the middle) to the solid.
	Vector3 c = flatten(p_impact);
	if (closed && !contains(p_source, c)) {
		for (int k = 1; k <= 16; k++) {
			const Vector3 q = flatten(c.lerp(centre, k / 16.0f));
			if (contains(p_source, q)) {
				c = q;
				break;
			}
		}
	}
	RandomPCG rng(p_seed * 2654435761u + 12345u);
	const int focused = int(Math::round(p_count * CLAMP(p_focus, 0.0f, 1.0f)));
	const float r = longest * 0.25f;
	const float spacing = longest * 0.02f; // no dust: sites at least this far apart
	for (int i = 0; i < p_count; i++) {
		Vector3 best;
		for (int attempt = 0; attempt < 80; attempt++) {
			Vector3 p;
			if (i < focused && attempt < 40) {
				const Vector3 dir = Vector3(rng.randf() * 2.0f - 1.0f, rng.randf() * 2.0f - 1.0f, rng.randf() * 2.0f - 1.0f).normalized();
				p = c + dir * (r * rng.randf() * rng.randf());
			} else {
				p = box.position + Vector3(rng.randf(), rng.randf(), rng.randf()) * box.size;
			}
			p = flatten(p);
			best = p;
			bool near = false;
			for (const Vector3 &q : r_sites) {
				if (q.distance_squared_to(p) < spacing * spacing) {
					near = true;
					break;
				}
			}
			if (!near && (!closed || contains(p_source, p))) {
				break;
			}
		}
		r_sites.push_back(best);
	}
}

void voronoi(const LocalVector<Triangle> &p_source, const LocalVector<Vector3> &p_sites, const Params &p_params, LocalVector<Piece> &r_pieces) {
	r_pieces.clear();
	if (p_source.is_empty()) {
		return;
	}
	const AABB box = triangles_aabb(p_source);
	const Vector3 box_centre = box.get_center();
	const double half = double(box.size.length()) + 1.0;
	LocalVector<Bounds> bounds;
	bounds.resize(p_source.size());
	for (uint32_t i = 0; i < p_source.size(); i++) {
		const Triangle &t = p_source[i];
		bounds[i].c = (t.v[0].p + t.v[1].p + t.v[2].p) / 3.0f;
		bounds[i].r = MAX(bounds[i].c.distance_to(t.v[0].p), MAX(bounds[i].c.distance_to(t.v[1].p), bounds[i].c.distance_to(t.v[2].p))) * 1.0001f + 1e-6f;
	}
	// A convex source's cells are convex and whole: no parts to split, no hulls to divide.
	Params params = p_params;
	if (params.split_components || params.collision_parts) {
		float hull_volume = 0.0f;
		hull_of(p_source, &hull_volume);
		if (hull_volume <= 1.001f * volume_of(p_source)) {
			params.split_components = false;
			params.collision_parts = false;
		}
	}
	struct ByDistance {
		bool operator()(const Pair<float, uint32_t> &p_a, const Pair<float, uint32_t> &p_b) const { return p_a.first < p_b.first; }
	};
	// Each plane between two sites is shared by their cells: its frame and cross-section made once.
	struct Section {
		Frame frame;
		Clipper2Lib::PathsD loops;
	};
	HashMap<uint64_t, Section> sections;
	for (uint32_t i = 0; i < p_sites.size(); i++) {
		LocalVector<Pair<float, uint32_t>> order;
		for (uint32_t j = 0; j < p_sites.size(); j++) {
			if (j != i && !p_sites[j].is_equal_approx(p_sites[i])) {
				order.push_back(Pair<float, uint32_t>(p_sites[i].distance_squared_to(p_sites[j]), j));
			}
		}
		order.sort_custom<ByDistance>();
		// The cell's planes (the nearest first: they cut the most away), each kept by its negative side.
		LocalVector<Plane> planes;
		planes.reserve(order.size());
		for (const Pair<float, uint32_t> &o : order) {
			const Vector3 n = (p_sites[o.second] - p_sites[i]).normalized();
			planes.push_back(Plane(n, n.dot((p_sites[i] + p_sites[o.second]) * 0.5f)));
		}
		// The surface inside the cell.
		LocalVector<Triangle> tris;
		LocalVector<Triangle> spare;
		const LocalVector<Triangle> *current = &p_source;
		for (const Plane &plane : planes) {
			if (current->is_empty()) {
				break;
			}
			const AABB tb = triangles_aabb(*current);
			if (plane.distance_to(tb.get_support(plane.normal)) <= 0.0f) {
				continue;
			}
			if (plane.distance_to(tb.get_support(-plane.normal)) > 0.0f) {
				spare.clear();
				current = &spare;
				break;
			}
			LocalVector<Triangle> &out = current == &tris ? spare : tris;
			clip_surface(*current, out, plane);
			current = &out;
		}
		if (current != &tris) {
			tris = *current;
		}
		// The caps: on each face of the cell, the source's cross-section there.
		for (uint32_t k = 0; k < planes.size(); k++) {
			const Plane &plane = planes[k];
			if (plane.distance_to(box.get_support(-plane.normal)) >= 0.0f || plane.distance_to(box.get_support(plane.normal)) <= 0.0f) {
				continue; // the plane misses the source
			}
			const uint32_t j = order[k].second;
			const uint32_t lo = MIN(i, j);
			const uint32_t hi = MAX(i, j);
			const Vector3 cn = (p_sites[hi] - p_sites[lo]).normalized();
			const Frame frame(cn, cn.dot((p_sites[lo] + p_sites[hi]) * 0.5f));
			// The cell's face: a square around the source in the plane, cut by the cell's other planes.
			const double cx = box_centre.dot(frame.t1);
			const double cy = box_centre.dot(frame.t2);
			Clipper2Lib::PathD face = { Clipper2Lib::PointD(cx - half, cy - half), Clipper2Lib::PointD(cx + half, cy - half), Clipper2Lib::PointD(cx + half, cy + half), Clipper2Lib::PointD(cx - half, cy + half) };
			for (uint32_t m = 0; m < planes.size() && face.size() >= 3; m++) {
				if (m != k) {
					const Plane &o = planes[m];
					clip_convex(face, o.normal.dot(frame.t1), o.normal.dot(frame.t2), double(o.d) - double(frame.d) * double(o.normal.dot(frame.n)));
				}
			}
			if (face.size() < 3 || Math::abs(Clipper2Lib::Area(face)) < 1e-12) {
				continue;
			}
			const uint64_t key = (uint64_t(lo) << 32) | hi;
			HashMap<uint64_t, Section>::Iterator it = sections.find(key);
			if (!it) {
				Section sec;
				sec.frame = frame;
				cross_section(p_source, bounds, frame, sec.loops);
				it = sections.insert(key, sec);
			}
			if (it->value.loops.empty()) {
				continue;
			}
			Clipper2Lib::ClipperD clipper(CLIPPER_PRECISION);
			clipper.AddSubject(it->value.loops);
			clipper.AddClip({ face });
			Clipper2Lib::PolyTreeD tree;
			clipper.Execute(Clipper2Lib::ClipType::Intersection, Clipper2Lib::FillRule::EvenOdd, tree);
			fill(tree, frame, plane.normal, params, tris);
		}
		if (tris.is_empty()) {
			continue;
		}
		// A cell through a concave mesh may hold parts that do not touch (both sides of a window): each a piece.
		LocalVector<LocalVector<Triangle>> parts;
		if (params.split_components) {
			components(tris, parts);
		} else {
			parts.push_back(LocalVector<Triangle>(tris));
		}
		for (LocalVector<Triangle> &part : parts) {
			Piece piece;
			piece.tris = part;
			finish(piece, params);
			if (piece.volume > 1e-9f && !piece.hulls.is_empty() && piece.hulls[0].size() >= 4) {
				r_pieces.push_back(piece);
			}
		}
	}
}

namespace {

// The triangles' volume and centroid (a closed surface; Godot's winding).
float volume_of(const LocalVector<Triangle> &p_tris, Vector3 *r_centroid) {
	float vol6 = 0.0f;
	Vector3 cen;
	for (const Triangle &t : p_tris) {
		const float v = t.v[0].p.dot(t.v[1].p.cross(t.v[2].p));
		vol6 += v;
		cen += (t.v[0].p + t.v[1].p + t.v[2].p) * v;
	}
	if (r_centroid && Math::abs(vol6) > 1e-12f) {
		*r_centroid = cen / (4.0f * vol6);
	}
	return Math::abs(vol6) / 6.0f;
}

// The convex hull of the triangles' vertices, and its volume.
Vector<Vector3> hull_of(const LocalVector<Triangle> &p_tris, float *r_volume) {
	HashSet<Vector3> seen;
	Vector<Vector3> points;
	for (const Triangle &t : p_tris) {
		for (int k = 0; k < 3; k++) {
			if (!seen.has(t.v[k].p)) {
				seen.insert(t.v[k].p);
				points.push_back(t.v[k].p);
			}
		}
	}
	*r_volume = 0.0f;
	Geometry3D::MeshData md;
	if (points.size() < 4 || ConvexHullComputer::convex_hull(points, md) != OK || md.vertices.size() < 4) {
		return points;
	}
	float vol6 = 0.0f;
	for (const Geometry3D::MeshData::Face &f : md.faces) {
		for (uint32_t k = 1; k + 1 < f.indices.size(); k++) {
			vol6 += md.vertices[f.indices[0]].dot(md.vertices[f.indices[k]].cross(md.vertices[f.indices[k + 1]]));
		}
	}
	*r_volume = Math::abs(vol6) / 6.0f;
	Vector<Vector3> hull;
	hull.resize(md.vertices.size());
	for (uint32_t i = 0; i < md.vertices.size(); i++) {
		hull.write[i] = md.vertices[i];
	}
	return hull;
}

// A piece's collision: its convex hull, or, when that is larger than the piece (a piece bent round a window's corner,
// an arc of a ring: hulls overlapping their neighbours would throw the pieces apart), the hulls of its parts, cut by
// the plane of one of its own source faces that has the piece on both sides (a reflex face: the window's side), down
// to eight parts.
void convex_parts(const LocalVector<Triangle> &p_tris, float p_volume, const Params &p_params, int p_depth, Vector<Vector<Vector3>> &r_hulls) {
	float hull_volume = 0.0f;
	const Vector<Vector3> hull = hull_of(p_tris, &hull_volume);
	if (p_depth >= 3 || p_volume <= 0.0f || hull_volume <= 1.02f * p_volume) {
		r_hulls.push_back(hull);
		return;
	}
	const AABB box = triangles_aabb(p_tris);
	const float size = MAX(box.get_longest_axis_size(), 1e-4f);
	const float eps = size * 1e-3f;
	// The split: the source face plane with the most of the piece's vertices on its smaller side.
	Plane best;
	int best_score = 0;
	HashMap<Vector4i, bool> seen;
	for (const Triangle &t : p_tris) {
		if (t.surface == p_params.interior_surface) {
			continue; // a cap: a face of the convex cell, never reflex
		}
		const Vector3 c = (t.v[1].p - t.v[0].p).cross(t.v[2].p - t.v[0].p);
		if (c.length_squared() < 1e-16f) {
			continue;
		}
		const Vector3 n = c.normalized();
		const Plane plane(n, n.dot(t.v[0].p));
		const Vector4i key(int(Math::round(n.x * 1000.0f)), int(Math::round(n.y * 1000.0f)), int(Math::round(n.z * 1000.0f)), int(Math::round(plane.d / eps)));
		if (seen.has(key)) {
			continue;
		}
		seen.insert(key, true);
		int pos = 0;
		int neg = 0;
		for (const Vector3 &v : hull) {
			const float d = plane.distance_to(v);
			pos += d > eps;
			neg += d < -eps;
		}
		const int score = MIN(pos, neg);
		if (score > best_score) {
			best_score = score;
			best = plane;
		}
	}
	if (best_score == 0) {
		// No face plane through the piece (curved): across its longest side.
		Vector3 axis;
		axis[box.get_longest_axis_index()] = 1.0f;
		best = Plane(axis, axis.dot(box.get_center()));
	}
	// Two sites mirrored in the plane: their cells' boundary is the plane.
	const Vector3 c = box.get_center();
	const Vector3 on = c - best.normal * best.distance_to(c);
	LocalVector<Vector3> sites;
	sites.push_back(on - best.normal * (size * 0.5f));
	sites.push_back(on + best.normal * (size * 0.5f));
	LocalVector<Piece> halves;
	Params params = p_params;
	params.split_components = true;
	params.collision_parts = false;
	voronoi(p_tris, sites, params, halves);
	if (halves.size() < 2) {
		r_hulls.push_back(hull);
		return;
	}
	for (const Piece &h : halves) {
		convex_parts(h.tris, h.volume, p_params, p_depth + 1, r_hulls);
	}
}

} // namespace

void finish(Piece &r_piece, const Params &p_params) {
	r_piece.aabb = triangles_aabb(r_piece.tris);
	r_piece.centroid = r_piece.aabb.get_center();
	r_piece.volume = volume_of(r_piece.tris, &r_piece.centroid);
	if (r_piece.volume <= 1e-12f) {
		// Not closed (an open source mesh): the bounds' volume.
		r_piece.volume = r_piece.aabb.get_volume();
	}
	if (!r_piece.aabb.has_point(r_piece.centroid)) {
		r_piece.centroid = r_piece.aabb.get_center();
	}
	Vector<Vector<Vector3>> hulls;
	if (p_params.collision_parts) {
		convex_parts(r_piece.tris, r_piece.volume, p_params, 0, hulls);
	} else {
		float hv = 0.0f;
		hulls.push_back(hull_of(r_piece.tris, &hv));
	}
	// Flat hulls (a sliver's, a part's) are left out: physics engines cannot build them.
	r_piece.hulls.clear();
	const float min_volume = MAX(1e-9f, r_piece.volume * 1e-4f);
	for (Vector<Vector3> &h : hulls) {
		float hv = 0.0f;
		if (h.size() >= 4) {
			Geometry3D::MeshData md;
			if (ConvexHullComputer::convex_hull(h, md) == OK) {
				float vol6 = 0.0f;
				for (const Geometry3D::MeshData::Face &f : md.faces) {
					for (uint32_t k = 1; k + 1 < f.indices.size(); k++) {
						vol6 += md.vertices[f.indices[0]].dot(md.vertices[f.indices[k]].cross(md.vertices[f.indices[k + 1]]));
					}
				}
				hv = Math::abs(vol6) / 6.0f;
			}
		}
		if (hv < min_volume) {
			continue;
		}
		for (int i = 0; i < h.size(); i++) {
			h.write[i] -= r_piece.centroid;
		}
		r_piece.hulls.push_back(h);
	}
}

} // namespace FellowFracture
