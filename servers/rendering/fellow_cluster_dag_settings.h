/**************************************************************************/
/*  fellow_cluster_dag_settings.h                                         */
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

#include "core/templates/safe_refcount.h"
#include "servers/rendering/fellow_settings.h"

// FellowEngine cluster DAG (docs/decisions/0379-cluster-dag.md): its settings, counters and the meshoptimizer functions
// the build uses, apart from fellow_settings.h (included by the whole engine through rendering_server.h).
namespace Fellow {

// Large static surfaces (the mesh clusters' surfaces, decision 0007) also get a hierarchy of clusters simplified from
// each other (Nanite's scheme: groups of clusters simplified with their borders locked, each group's error and bounds
// those of the clusters made from it), and are drawn as the cut through it whose every cluster is just fine enough on
// screen (its error under Godot's LOD threshold, its parent's not) from the main camera, in every pass. Changes the
// image (as LOD does). Read when a surface's clusters are built (whether to build the hierarchy) and every frame.
inline bool cluster_dag() {
	return mesh_clusters() && GLOBAL_GET_CACHED(bool, "rendering/fellow/geometry/cluster_dag");
}

// The hierarchies kept in user://fellow_dag_cache (Unreal's derived data cache): a surface whose positions, normals and
// indices were seen before loads its hierarchy instead of building it. Read when a hierarchy is built.
inline bool cluster_dag_cache() {
	return GLOBAL_GET_CACHED(bool, "rendering/fellow/geometry/cluster_dag_cache");
}

// Each surface instance's cut drawn as one indirect draw of its ranges, the commands written before the pass
// (decision 0381); off: one draw call per range. The same image. Read every frame.
inline bool cluster_dag_indirect() {
	return GLOBAL_GET_CACHED(bool, "rendering/fellow/geometry/cluster_dag_indirect");
}

// The walks of a pass's cuts spread over the render thread and the CPU worker tasks (decision 0384: on the RTX laptop's
// CPU the prepare step −52 to −59 %; nothing on a 2-core container); off: walked one after the other on the render
// thread. The same cuts. Read every frame.
inline bool cluster_dag_parallel() {
	return GLOBAL_GET_CACHED(bool, "rendering/fellow/geometry/cluster_dag_parallel");
}

struct ClusterDagStats {
	SafeNumeric<uint64_t> surfaces; // surfaces with a hierarchy in use
	SafeNumeric<uint64_t> build_usec; // CPU time of the hierarchies' builds (worker threads)
	SafeNumeric<uint64_t> clusters; // clusters in the hierarchies built (all levels)
	SafeNumeric<uint64_t> index_bytes; // GPU memory of the hierarchies' index buffers
	SafeNumeric<uint64_t> walks; // cuts walked (a surface instance in a pass; the camera passes of one render share one)
	SafeNumeric<uint64_t> clusters_drawn; // clusters drawn (summed over the passes)
	SafeNumeric<uint64_t> triangles_drawn;
	SafeNumeric<uint64_t> draws; // ranges drawn (adjacent clusters drawn as one): draw calls, or indirect commands
	SafeNumeric<uint64_t> indirect_draws; // indirect draw calls (geometry/cluster_dag_indirect: one a surface instance)
	SafeNumeric<uint64_t> run_instances; // instances of Godot's instanced runs drawn one by one with their own cuts
	SafeNumeric<uint64_t> walk_usec; // CPU time of the walks
	SafeNumeric<uint64_t> prepare_usec; // the render thread's time writing the passes' indirect commands, walks included
	SafeNumeric<uint64_t> parallel_walks; // walks done in a pass's parallel group (decision 0384)
	SafeNumeric<uint64_t> cache_hits; // hierarchies loaded from user://fellow_dag_cache
	SafeNumeric<uint64_t> cache_writes; // hierarchies written to it
};
ClusterDagStats &cluster_dag_stats();

} // namespace Fellow

// meshoptimizer's functions the build needs, set by modules/meshoptimizer (null without the module: no hierarchy).
struct FellowMeshopt {
	struct Meshlet {
		uint32_t vertex_offset;
		uint32_t triangle_offset;
		uint32_t vertex_count;
		uint32_t triangle_count;
	};
	enum {
		SIMPLIFY_LOCK_BORDER = 1 << 0,
		SIMPLIFY_SPARSE = 1 << 1,
		SIMPLIFY_ERROR_ABSOLUTE = 1 << 2,
	};
	typedef size_t (*BuildMeshletsBound)(size_t p_index_count, size_t p_max_vertices, size_t p_max_triangles);
	typedef size_t (*BuildMeshlets)(Meshlet *r_meshlets, unsigned int *r_meshlet_vertices, unsigned char *r_meshlet_triangles, const unsigned int *p_indices, size_t p_index_count, const float *p_positions, size_t p_vertex_count, size_t p_stride, size_t p_max_vertices, size_t p_max_triangles, float p_cone_weight);
	typedef size_t (*PartitionClusters)(unsigned int *r_partition, const unsigned int *p_cluster_indices, size_t p_total_index_count, const unsigned int *p_cluster_index_counts, size_t p_cluster_count, const float *p_positions, size_t p_vertex_count, size_t p_stride, size_t p_target_partition_size);
	typedef size_t (*Simplify)(unsigned int *r_indices, const unsigned int *p_indices, size_t p_index_count, const float *p_positions, size_t p_vertex_count, size_t p_stride, size_t p_target_index_count, float p_target_error, unsigned int p_options, float *r_error);
	typedef size_t (*SimplifyWithAttributes)(unsigned int *r_indices, const unsigned int *p_indices, size_t p_index_count, const float *p_positions, size_t p_vertex_count, size_t p_stride, const float *p_attributes, size_t p_attribute_stride, const float *p_attribute_weights, size_t p_attribute_count, const unsigned char *p_vertex_lock, size_t p_target_index_count, float p_target_error, unsigned int p_options, float *r_error);
	static BuildMeshletsBound build_meshlets_bound;
	static BuildMeshlets build_meshlets;
	static PartitionClusters partition_clusters;
	static Simplify simplify;
	static SimplifyWithAttributes simplify_with_attributes;
	static bool available() { return build_meshlets_bound && build_meshlets && partition_clusters && simplify && simplify_with_attributes; }
};
