/**************************************************************************/
/*  register_types.cpp                                                    */
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

#include "register_types.h"

#include "scene/resources/surface_tool.h"
#include "servers/rendering/fellow_cluster_dag_settings.h"

#include <thirdparty/meshoptimizer/meshoptimizer.h>

void initialize_meshoptimizer_module(ModuleInitializationLevel p_level) {
	if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
		return;
	}

	SurfaceTool::optimize_vertex_cache_func = meshopt_optimizeVertexCache;
	SurfaceTool::optimize_vertex_fetch_remap_func = meshopt_optimizeVertexFetchRemap;
	SurfaceTool::simplify_func = meshopt_simplify;
	SurfaceTool::simplify_with_attrib_func = meshopt_simplifyWithAttributes;
	SurfaceTool::simplify_scale_func = meshopt_simplifyScale;
	SurfaceTool::generate_remap_func = meshopt_generateVertexRemap;
	SurfaceTool::remap_vertex_func = meshopt_remapVertexBuffer;
	SurfaceTool::remap_index_func = meshopt_remapIndexBuffer;

	// FellowEngine cluster DAG (docs/decisions/0379-cluster-dag.md).
	static_assert(sizeof(FellowMeshopt::Meshlet) == sizeof(meshopt_Meshlet));
	static_assert(int(FellowMeshopt::SIMPLIFY_LOCK_BORDER) == int(meshopt_SimplifyLockBorder) && int(FellowMeshopt::SIMPLIFY_SPARSE) == int(meshopt_SimplifySparse) && int(FellowMeshopt::SIMPLIFY_ERROR_ABSOLUTE) == int(meshopt_SimplifyErrorAbsolute));
	FellowMeshopt::build_meshlets_bound = meshopt_buildMeshletsBound;
	FellowMeshopt::build_meshlets = [](FellowMeshopt::Meshlet *r_meshlets, unsigned int *r_meshlet_vertices, unsigned char *r_meshlet_triangles, const unsigned int *p_indices, size_t p_index_count, const float *p_positions, size_t p_vertex_count, size_t p_stride, size_t p_max_vertices, size_t p_max_triangles, float p_cone_weight) -> size_t {
		return meshopt_buildMeshlets(reinterpret_cast<meshopt_Meshlet *>(r_meshlets), r_meshlet_vertices, r_meshlet_triangles, p_indices, p_index_count, p_positions, p_vertex_count, p_stride, p_max_vertices, p_max_triangles, p_cone_weight);
	};
	FellowMeshopt::partition_clusters = meshopt_partitionClusters;
	FellowMeshopt::simplify = meshopt_simplify;
	FellowMeshopt::simplify_with_attributes = meshopt_simplifyWithAttributes;
}

void uninitialize_meshoptimizer_module(ModuleInitializationLevel p_level) {
	if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
		return;
	}

	SurfaceTool::optimize_vertex_cache_func = nullptr;
	SurfaceTool::optimize_vertex_fetch_remap_func = nullptr;
	SurfaceTool::simplify_func = nullptr;
	SurfaceTool::simplify_scale_func = nullptr;
	SurfaceTool::generate_remap_func = nullptr;
	SurfaceTool::remap_vertex_func = nullptr;
	SurfaceTool::remap_index_func = nullptr;

	FellowMeshopt::build_meshlets_bound = nullptr;
	FellowMeshopt::build_meshlets = nullptr;
	FellowMeshopt::partition_clusters = nullptr;
	FellowMeshopt::simplify = nullptr;
	FellowMeshopt::simplify_with_attributes = nullptr;
}
