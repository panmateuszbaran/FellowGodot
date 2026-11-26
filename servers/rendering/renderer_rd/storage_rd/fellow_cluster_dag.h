/**************************************************************************/
/*  fellow_cluster_dag.h                                                  */
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

// FellowEngine cluster DAG (docs/decisions/0379-cluster-dag.md): Nanite's level-of-detail scheme for one large static
// surface, drawn with ordinary indexed draws.
//
// The surface's triangles are split into clusters of up to 128 (meshoptimizer's meshlets); neighbouring clusters are
// grouped (about 8), each group's triangles simplified to half with the group's border locked and split into clusters
// again, and so on up to a few clusters. Every cluster made from a group carries that group's bounds and error (the
// largest error of the group's clusters plus the simplification's: it only grows going up), and every cluster of a
// group carries it as its parent's. A cluster is drawn when its own error is fine enough on screen and its parent's
// is not: since a group's clusters all share their parent, and the clusters made from it all share it as their own,
// every point of the surface is drawn by exactly one level, and where two levels meet they share the locked border:
// no cracks, no overlaps. Simplification only removes vertices, so every level indexes the surface's own vertices.
//
// Each level's clusters are ordered by a bounding volume tree over them (its leaves' order), and their indices written
// in that order into one index buffer of all the levels: the clusters a walk emits in a level come in index order and
// adjacent ones are drawn as one range.
struct FellowClusterDag {
	struct Cluster {
		uint32_t first = 0; // first index in the hierarchy's index data
		uint32_t count = 0; // index count
		AABB aabb; // its triangles' bounds (local space)
		Vector3 center; // its own: the group it was made from (level 0: its own bounds, error 0)
		float radius = 0.0f;
		float error = 0.0f;
		Vector3 parent_center; // its parent's: the group it was simplified in (error FLT_MAX: none, it is a root)
		float parent_radius = 0.0f;
		float parent_error = 0.0f;
	};
	// A level's tree, nodes in depth-first pre-order (the left child follows its parent).
	struct Node {
		AABB aabb; // the clusters' triangles' bounds
		Vector3 center; // a sphere enclosing the clusters' own spheres
		float radius = 0.0f;
		float min_error = 0.0f; // the smallest own error of the clusters
		Vector3 parent_center; // a sphere enclosing their parents' spheres
		float parent_radius = 0.0f;
		float max_parent_error = 0.0f; // the largest parent error (FLT_MAX: some cluster is a root)
		uint32_t right = 0; // the right child (0: a leaf)
		uint32_t first_cluster = 0; // a leaf's clusters
		uint32_t cluster_count = 0;
	};
	struct Level {
		uint32_t first_node = 0;
		uint32_t node_count = 0;
		uint32_t first_cluster = 0;
		uint32_t cluster_count = 0;
		uint32_t index_count = 0;
	};

	LocalVector<Cluster> clusters; // level after level, each level in its tree's leaf order
	LocalVector<Node> nodes;
	LocalVector<Level> levels;
	uint32_t index_count = 0; // all levels
	uint32_t base_index_count = 0; // the surface's own (level 0)
	bool is_16 = false;
	Vector<uint8_t> pending_indices; // the index data until the GPU buffer is made

	// Selection: Godot's LOD test (as MeshStorage::mesh_surface_get_lod()) with a cluster's error in place of a LOD's
	// edge length and its sphere's distance from the main camera in place of the instance's.
	struct Params {
		Vector3 eye; // the main camera, in the surface's local space
		real_t min_scale = 1.0; // the instance's smallest axis scale
		float model_scale = 1.0; // the instance's LOD model scale times its LOD bias
		float distance_multiplier = 1.0;
		float distance_bias = 0.0;
		float threshold = 1.0;
	};
	// Appends the (first index, index count) ranges of the cut that intersect the convex volume bounded by p_planes
	// (local space, normals pointing out), adjacent clusters merged. Returns the indices emitted; r_clusters (optional)
	// the clusters.
	uint32_t select(const Plane *p_planes, int p_plane_count, const Params &p_params, LocalVector<uint32_t> &r_ranges, uint32_t *r_clusters = nullptr) const;
};

// Builds the hierarchy of a triangle surface (as passed to mesh_add_surface) with its index data pending. Null when it
// cannot be built (no meshoptimizer, a format without 3D positions, too few triangles).
FellowClusterDag *fellow_cluster_dag_build(uint64_t p_format, const Vector<uint8_t> &p_vertex_data, uint32_t p_vertex_count, const AABB &p_aabb, const Vector<uint8_t> &p_index_data, uint32_t p_index_count);
