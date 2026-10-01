/**************************************************************************/
/*  fellow_mesh_clusters.h                                                */
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

#include "core/math/aabb.h"
#include "core/math/plane.h"
#include "core/templates/local_vector.h"
#include "core/templates/vector.h"

// FellowEngine mesh clusters (docs/decisions/0007-mesh-clusters.md).
//
// A k-d tree over the triangles of one large static surface (median split of the triangle centroids on the longest
// axis, leaves of at most N triangles). The tree's nodes are stored in depth-first pre-order and every index buffer of
// the surface (base and each LOD) is reordered by leaf, so every node is one contiguous index range in every buffer.
//
// The reordered index data is built when the surface is created but kept on the CPU (pending) until the renderer
// sets the surface up for drawing: with materials that never blend it, the surface's own index buffers are replaced
// by the reordered ones (no extra GPU memory); with a blending material (whose result depends on the triangle order
// inside a draw) the clusters are dropped and the surface keeps stock's order.
//
// Culling walks the tree against planes given in the surface's local space: a node whose bounds lie entirely
// outside one plane is skipped with its whole subtree, a node entirely inside all planes is emitted as one range,
// and adjacent ranges are merged, so a frustum yields a handful of draw calls.
struct FellowMeshClusters {
	struct Node {
		uint32_t right = 0; // Index of the right child (the left child is the next node); 0 for a leaf.
		uint32_t axis = 0;
		float split = 0.0; // Triangles whose centroid is below the split go left.
	};

	struct Range {
		AABB aabb; // Local-space bounds of the node's triangles in this index buffer (slightly inflated).
		uint32_t first = 0; // First index.
		uint32_t count = 0; // Index count (0: no triangles of this level in the node).
	};

	struct Level {
		LocalVector<Range> nodes;
		uint32_t index_count = 0;
	};

	LocalVector<Node> nodes;
	LocalVector<Level> levels; // [0]: base index buffer, [1 + i]: LOD i.
	uint32_t leaf_count = 0;

	// Leaf-ordered index data waiting for the draw-time decision (see above); empty once decided.
	Vector<uint8_t> pending_base;
	Vector<Vector<uint8_t>> pending_lods;
	bool active = false; // The surface's index buffers are in leaf order and the clusters may be drawn.

	uint64_t pending_bytes() const {
		uint64_t b = pending_base.size();
		for (const Vector<uint8_t> &l : pending_lods) {
			b += l.size();
		}
		return b;
	}

	// Appends (first index, index count) ranges of p_level that intersect the convex volume bounded by p_planes
	// (local space, normals pointing outwards). Returns the number of indices emitted.
	uint32_t cull(uint32_t p_level, const Plane *p_planes, int p_plane_count, LocalVector<uint32_t> &r_ranges) const;
};

// Builds the clusters of a triangle surface from its vertex and index data (as passed to mesh_add_surface), with the
// leaf-ordered index data pending. p_lods: index data of each LOD. Returns nullptr when the surface cannot be
// clustered (format without 3D positions, too few triangles).
FellowMeshClusters *fellow_mesh_clusters_build(uint64_t p_format, const Vector<uint8_t> &p_vertex_data, uint32_t p_vertex_count, const AABB &p_aabb, const Vector<uint8_t> &p_index_data, uint32_t p_index_count, const Vector<Vector<uint8_t>> &p_lods, uint32_t p_max_leaf_triangles);
