/**************************************************************************/
/*  fellow_fracture.h                                                     */
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

#include "core/math/plane.h"
#include "core/templates/local_vector.h"
#include "scene/resources/mesh.h"

// FellowEngine (milestone 74, decision 0182): a mesh broken into pieces along the cells of a Voronoi diagram — its
// triangles clipped by each cell's planes, each cut capped with an interior surface — for FellowDestructible3D. Pure
// arithmetic: it runs on a worker thread.
namespace FellowFracture {

struct Vertex {
	Vector3 p;
	Vector3 n;
	Vector2 uv;
};

struct Triangle {
	Vertex v[3];
	int surface = 0; // the source's surface, or the interior's (one past them)
};

struct Piece {
	LocalVector<Triangle> tris;
	Vector3 centroid;
	float volume = 0.0f;
	AABB aabb;
	Vector<Vector<Vector3>> hulls; // its collision: convex hulls' vertices, around the centroid
};

struct Params {
	int interior_surface = 1;
	float interior_uv_scale = 1.0f;
	// The sign of cross(b - a, c - a) against a front face's outward normal in the source (Godot's front faces are
	// clockwise: -1).
	float winding = -1.0f;
	bool split_components = true; // parts of a cell that do not touch: pieces of their own
	bool collision_parts = true; // a piece far from convex: the hulls of its halves
};

// The source's triangles (every surface of triangles), and the sign of its winding against its normals.
void mesh_triangles(const Ref<Mesh> &p_mesh, LocalVector<Triangle> &r_triangles, float &r_winding);

// The cells' sites: a share of them (p_focus) gathered around p_impact (smaller pieces there), the rest anywhere in the
// solid, none nearer another than 2 % of its size; a thin object's on its middle (cut across, not split in layers).
void make_sites(const LocalVector<Triangle> &p_source, int p_count, uint64_t p_seed, const Vector3 &p_impact, float p_focus, LocalVector<Vector3> &r_sites);

// Whether a point is inside the source (a closed mesh): a ray's crossings.
bool contains(const LocalVector<Triangle> &p_source, const Vector3 &p_point);

// One piece per site: the source's surface inside the site's cell (the planes halfway to every other site), and on
// each face of the cell the source's cross-section there (its own loops, holes and all, clipped to the face), in the
// interior surface. A cell missing the source gives no piece.
void voronoi(const LocalVector<Triangle> &p_source, const LocalVector<Vector3> &p_sites, const Params &p_params, LocalVector<Piece> &r_pieces);

// The piece's centroid, volume, bounds and collision hulls (from its triangles).
void finish(Piece &r_piece, const Params &p_params);

} // namespace FellowFracture
