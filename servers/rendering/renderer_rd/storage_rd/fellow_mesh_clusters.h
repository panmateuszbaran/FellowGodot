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
#include "core/object/worker_thread_pool.h"
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

	// geometry/cluster_lod (decision 0063), surfaces with LODs only: each node's bounds over all levels, and each leaf's
	// neighbours (leaves whose such bounds intersect its own), by node index (neighbors[neighbor_offsets[n]] to
	// neighbors[neighbor_offsets[n + 1]]).
	// A leaf's coarsest usable level: its triangles stay near the leaf's own (base) bounds in every level up to it.
	// The last LODs Godot generates have triangles that span much of the surface; a leaf holding one of them would
	// neighbour every other leaf.
	LocalVector<AABB> union_bounds;
	LocalVector<uint8_t> max_level;
	LocalVector<uint32_t> neighbor_offsets;
	LocalVector<uint32_t> neighbors;

	// Leaf-ordered index data waiting for the draw-time decision (see above); empty once decided.
	Vector<uint8_t> pending_base;
	Vector<Vector<uint8_t>> pending_lods;
	bool active = false; // The clusters match the surface's index buffers and may be drawn.
	bool preserves_order = false; // Order-preserving variant (chunks of consecutive triangles, no reordering).

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

	// The same ranges, ordered from the node nearest to p_eye (local space) to the farthest: at every node the child on
	// the eye's side of the split comes first, and nodes entirely inside the volume are split further while they hold
	// more than p_max_indices indices (geometry/cluster_front_to_back, decision 0058). Adjacent ranges are merged.
	uint32_t cull_ordered(uint32_t p_level, const Plane *p_planes, int p_plane_count, const Vector3 &p_eye, uint32_t p_max_indices, LocalVector<uint32_t> &r_ranges) const;

	// geometry/cluster_lod (decision 0063): each node at its own mesh LOD, chosen as stock chooses a surface's LOD for an
	// instance (Godot's screen-space edge length against the threshold) but from the node's distance to the camera
	// instead of the whole instance's. Never finer than p_lod.min_level (the instance's own level).
	struct LodParams {
		Vector3 eye; // the camera, in the surface's local space
		real_t min_scale = 1.0; // the instance's smallest axis scale (local distances times it bound world distances from below)
		const float *edges = nullptr; // each LOD's edge length (local units)
		uint32_t lod_count = 0;
		float model_scale = 1.0; // the instance's LOD model scale times its LOD bias
		float distance_multiplier = 1.0;
		float distance_bias = 0.0;
		float threshold = 1.0;
		uint32_t min_level = 0;
	};
	// Appends to r_level_ranges[level] the (first index, index count) ranges drawn at each level (levels.size() lists);
	// returns the indices emitted. Each leaf in view is drawn at its own level and at every finer level of its
	// neighbours: a point of the surface is then covered by the finest level of the leaves around it (adjacent leaves at
	// different levels would leave cracks between their triangles, which each level's mesh does not).
	uint32_t cull_lod(const Plane *p_planes, int p_plane_count, const LodParams &p_lod, LocalVector<uint32_t> *r_level_ranges) const;
	uint32_t lod_level_for_distance(real_t p_local_distance, const LodParams &p_lod) const;
};

// Builds the clusters of a triangle surface from its vertex and index data (as passed to mesh_add_surface), with the
// leaf-ordered index data pending. p_lods: index data of each LOD. Returns nullptr when the surface cannot be
// clustered (format without 3D positions, too few triangles).
// r_ordered (optional) receives the order-preserving variant: the same kind of tree over chunks of consecutive
// triangles of each index buffer in its existing order, usable without reordering anything (for surfaces drawn with
// blending, whose result depends on the triangle order inside a draw).
FellowMeshClusters *fellow_mesh_clusters_build(uint64_t p_format, const Vector<uint8_t> &p_vertex_data, uint32_t p_vertex_count, const AABB &p_aabb, const Vector<uint8_t> &p_index_data, uint32_t p_index_count, const Vector<Vector<uint8_t>> &p_lods, uint32_t p_max_leaf_triangles, FellowMeshClusters **r_ordered = nullptr);

// Deferred, asynchronous build. The inputs (copy-on-write references to the surface data) are captured when the
// surface is created; the build task is started only once the surface has existed for a few frames (surfaces that are
// recreated every frame never pay for it) and runs on the WorkerThreadPool; the render thread never waits for it: the
// result is applied by the per-frame update once the task has completed and the renderer has decided how the surface
// is drawn (cluster order, or stock order with the order-preserving variant).
struct FellowMeshClusterBuild {
	enum Decision {
		UNDECIDED,
		CLUSTER_ORDER, // Reorder the surface's index buffers (k-d clusters).
		KEEP_ORDER, // Keep stock's order (order-preserving clusters).
		DROP, // No clusters (vertex data was updated).
	};

	uint64_t format = 0;
	Vector<uint8_t> vertex_data;
	uint32_t vertex_count = 0;
	AABB aabb;
	Vector<uint8_t> index_data;
	uint32_t index_count = 0;
	Vector<Vector<uint8_t>> lods;
	uint32_t max_leaf_triangles = 0;

	uint64_t created_frame = 0;
	Decision decision = UNDECIDED;
	bool started = false;

	FellowMeshClusters *clusters = nullptr;
	FellowMeshClusters *ordered = nullptr;
	uint64_t build_usec = 0; // CPU time of the build (worker thread).
	WorkerThreadPool::TaskID task = WorkerThreadPool::INVALID_TASK_ID;

	~FellowMeshClusterBuild();
};

FellowMeshClusterBuild *fellow_mesh_clusters_build_create(uint64_t p_format, const Vector<uint8_t> &p_vertex_data, uint32_t p_vertex_count, const AABB &p_aabb, const Vector<uint8_t> &p_index_data, uint32_t p_index_count, const Vector<Vector<uint8_t>> &p_lods, uint32_t p_max_leaf_triangles, uint64_t p_frame);
void fellow_mesh_clusters_build_start(FellowMeshClusterBuild *p_build);
// True once the task finished (joins it); false while it runs or before it was started.
bool fellow_mesh_clusters_build_try_join(FellowMeshClusterBuild *p_build);
// Blocks until the task finished (shutdown only).
void fellow_mesh_clusters_build_wait(FellowMeshClusterBuild *p_build);
