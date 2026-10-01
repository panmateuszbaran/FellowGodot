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
#include "servers/rendering/rendering_device.h"

// FellowEngine mesh clusters (docs/decisions/0007-mesh-clusters.md).
//
// A k-d tree over the triangles of one large static surface (median split of the triangle centroids on the longest
// axis, leaves of at most N triangles). The tree's nodes are stored in depth-first pre-order and the surface gets a
// second copy of each of its index buffers (base and every LOD) in which the triangles are ordered by leaf, so every
// node of the tree is one contiguous index range in every copy. The original index buffers stay untouched (they are
// still used for whole-surface draws, in particular alpha-blended ones, whose result depends on the triangle order).
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
		RID index_buffer; // Leaf-ordered copy of the level's index buffer.
		RID index_array; // Whole copy (bound before drawing ranges of it).
		uint32_t index_count = 0;
	};

	LocalVector<Node> nodes;
	LocalVector<Level> levels; // [0]: base index buffer, [1 + i]: LOD i.
	uint32_t leaf_count = 0;
	uint64_t memory_bytes = 0; // GPU memory of the leaf-ordered index buffer copies.

	// Appends (first index, index count) ranges of p_level that intersect the convex volume bounded by p_planes
	// (local space, normals pointing outwards). Returns the number of indices emitted.
	uint32_t cull(uint32_t p_level, const Plane *p_planes, int p_plane_count, LocalVector<uint32_t> &r_ranges) const;

	void free_rids();
};

// Builds the clusters of a triangle surface from its vertex and index data (as passed to mesh_add_surface) and
// creates the leaf-ordered index buffer copies. p_lods: index data of each LOD. Returns nullptr when the surface
// cannot be clustered (format without 3D positions, too few triangles).
FellowMeshClusters *fellow_mesh_clusters_build(uint64_t p_format, const Vector<uint8_t> &p_vertex_data, uint32_t p_vertex_count, const AABB &p_aabb, const Vector<uint8_t> &p_index_data, uint32_t p_index_count, const Vector<Vector<uint8_t>> &p_lods, uint32_t p_max_leaf_triangles);
