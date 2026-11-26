/**************************************************************************/
/*  fellow_cluster_dag.cpp                                                */
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

#include "fellow_cluster_dag.h"

#include "core/crypto/crypto_core.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/object/worker_thread_pool.h"
#include "core/os/os.h"
#include "core/templates/sort_array.h"
#include "servers/rendering/fellow_cluster_dag_settings.h"
#include "servers/rendering/rendering_server.h"
#include "servers/rendering/rendering_server_enums.h"

#include <cfloat>

namespace {

constexpr uint32_t DAG_MAX_TRIANGLES = 128; // a cluster's
constexpr uint32_t DAG_MAX_VERTICES = 255;
constexpr uint32_t DAG_GROUP_SIZE = 8; // clusters simplified together
constexpr float DAG_MIN_REDUCTION = 0.85f; // a group simplified to more than this share of its indices is left alone
constexpr uint32_t DAG_MAX_LEVELS = 32;
constexpr uint32_t DAG_LEAF_CLUSTERS = 4; // clusters in a leaf of a level's tree
// The normals' weight in the simplification's error, as Godot's own LOD generation weighs them (ImporterMesh: 1, against
// positions relative to the mesh's extent): a cluster whose shading the simplification changes counts as coarser.
constexpr float DAG_NORMAL_WEIGHT = 1.0f;

struct Sphere {
	Vector3 c;
	float r = 0.0f;
};

// A cluster while the hierarchy is built.
struct Work {
	LocalVector<uint32_t> indices;
	AABB aabb;
	Sphere self;
	float error = 0.0f;
	Sphere parent;
	float parent_error = FLT_MAX;
};

Sphere enclose(const Sphere *p_spheres, uint32_t p_count) {
	AABB box(p_spheres[0].c - Vector3(p_spheres[0].r, p_spheres[0].r, p_spheres[0].r), Vector3(2, 2, 2) * p_spheres[0].r);
	for (uint32_t i = 1; i < p_count; i++) {
		box.merge_with(AABB(p_spheres[i].c - Vector3(p_spheres[i].r, p_spheres[i].r, p_spheres[i].r), Vector3(2, 2, 2) * p_spheres[i].r));
	}
	Sphere s;
	s.c = box.get_center();
	for (uint32_t i = 0; i < p_count; i++) {
		s.r = MAX(s.r, float(s.c.distance_to(p_spheres[i].c)) + p_spheres[i].r);
	}
	return s;
}

struct Builder {
	const float *pos = nullptr; // 3 floats a vertex
	uint32_t vertex_count = 0;
	FellowClusterDag *dag = nullptr;
	LocalVector<uint32_t> out_indices; // all levels, in cluster order

	Vector3 p(uint32_t p_v) const { return Vector3(pos[p_v * 3], pos[p_v * 3 + 1], pos[p_v * 3 + 2]); }

	// Splits triangles into clusters (meshoptimizer's meshlets), each with its triangles' bounds.
	void clusterize(const uint32_t *p_indices, uint32_t p_index_count, LocalVector<Work> &r_out) {
		const size_t bound = FellowMeshopt::build_meshlets_bound(p_index_count, DAG_MAX_VERTICES, DAG_MAX_TRIANGLES);
		LocalVector<FellowMeshopt::Meshlet> meshlets;
		meshlets.resize(bound);
		LocalVector<uint32_t> mv;
		mv.resize(bound * DAG_MAX_VERTICES);
		LocalVector<uint8_t> mt;
		mt.resize(bound * DAG_MAX_TRIANGLES * 3);
		const size_t n = FellowMeshopt::build_meshlets(meshlets.ptr(), mv.ptr(), mt.ptr(), p_indices, p_index_count, pos, vertex_count, sizeof(float) * 3, DAG_MAX_VERTICES, DAG_MAX_TRIANGLES, 0.0f);
		for (size_t m = 0; m < n; m++) {
			const FellowMeshopt::Meshlet &ml = meshlets[m];
			Work w;
			w.indices.resize(ml.triangle_count * 3);
			for (uint32_t j = 0; j < ml.triangle_count * 3; j++) {
				w.indices[j] = mv[ml.vertex_offset + mt[ml.triangle_offset + j]];
			}
			w.aabb = AABB(p(w.indices[0]), Vector3());
			for (uint32_t v : w.indices) {
				w.aabb.expand_to(p(v));
			}
			r_out.push_back(w);
		}
	}

	// A level's tree over its clusters (median split of their centres on the widest axis); its leaves' order is the
	// clusters' order in the hierarchy and in the index data.
	struct Item {
		Vector3 c;
		uint32_t id;
	};
	struct ByAxis {
		int axis = 0;
		bool operator()(const Item &p_a, const Item &p_b) const { return p_a.c[axis] < p_b.c[axis]; }
	};

	uint32_t build_node(Item *p_items, uint32_t p_count, const LocalVector<Work> &p_level) {
		const uint32_t node = dag->nodes.size();
		dag->nodes.push_back(FellowClusterDag::Node());
		if (p_count <= DAG_LEAF_CLUSTERS) {
			FellowClusterDag::Node &nd = dag->nodes[node];
			nd.first_cluster = dag->clusters.size();
			nd.cluster_count = p_count;
			for (uint32_t i = 0; i < p_count; i++) {
				const Work &w = p_level[p_items[i].id];
				FellowClusterDag::Cluster c;
				c.first = out_indices.size();
				c.count = w.indices.size();
				for (uint32_t v : w.indices) {
					out_indices.push_back(v);
				}
				c.aabb = w.aabb;
				c.center = w.self.c;
				c.radius = w.self.r;
				c.error = w.error;
				c.parent_center = w.parent.c;
				c.parent_radius = w.parent.r;
				c.parent_error = w.parent_error;
				dag->clusters.push_back(c);
			}
		} else {
			AABB box(p_items[0].c, Vector3());
			for (uint32_t i = 1; i < p_count; i++) {
				box.expand_to(p_items[i].c);
			}
			SortArray<Item, ByAxis> sorter;
			sorter.compare.axis = box.get_longest_axis_index();
			const uint32_t mid = p_count / 2;
			sorter.nth_element(0, p_count, mid, p_items);
			build_node(p_items, mid, p_level);
			dag->nodes[node].right = dag->nodes.size();
			build_node(p_items + mid, p_count - mid, p_level);
		}
		return node;
	}

	// Fills a node's bounds from its clusters (a leaf) or its children (nodes after it in pre-order: done backwards).
	void bound_nodes(uint32_t p_first, uint32_t p_end) {
		for (int64_t n = int64_t(p_end) - 1; n >= int64_t(p_first); n--) {
			FellowClusterDag::Node &nd = dag->nodes[n];
			LocalVector<Sphere> own;
			LocalVector<Sphere> parents;
			bool root = false;
			auto take = [&](const AABB &p_aabb, const Sphere &p_own, float p_min_error, const Sphere &p_parent, float p_max_parent_error, bool p_init) {
				if (p_init) {
					nd.aabb = p_aabb;
					nd.min_error = p_min_error;
					nd.max_parent_error = p_max_parent_error;
				} else {
					nd.aabb.merge_with(p_aabb);
					nd.min_error = MIN(nd.min_error, p_min_error);
					nd.max_parent_error = MAX(nd.max_parent_error, p_max_parent_error);
				}
				own.push_back(p_own);
				if (p_max_parent_error == FLT_MAX) {
					root = true;
				} else {
					parents.push_back(p_parent);
				}
			};
			if (nd.right == 0) {
				for (uint32_t i = 0; i < nd.cluster_count; i++) {
					const FellowClusterDag::Cluster &c = dag->clusters[nd.first_cluster + i];
					take(c.aabb, Sphere{ c.center, c.radius }, c.error, Sphere{ c.parent_center, c.parent_radius }, c.parent_error, i == 0);
				}
			} else {
				const FellowClusterDag::Node &l = dag->nodes[n + 1];
				const FellowClusterDag::Node &r = dag->nodes[nd.right];
				take(l.aabb, Sphere{ l.center, l.radius }, l.min_error, Sphere{ l.parent_center, l.parent_radius }, l.max_parent_error, true);
				take(r.aabb, Sphere{ r.center, r.radius }, r.min_error, Sphere{ r.parent_center, r.parent_radius }, r.max_parent_error, false);
			}
			const Sphere so = enclose(own.ptr(), own.size());
			nd.center = so.c;
			nd.radius = so.r;
			if (root) {
				nd.max_parent_error = FLT_MAX;
			}
			if (!parents.is_empty()) {
				const Sphere sp = enclose(parents.ptr(), parents.size());
				nd.parent_center = sp.c;
				nd.parent_radius = sp.r;
			} else {
				nd.parent_center = so.c;
				nd.parent_radius = so.r;
			}
		}
	}

	void finalize_level(const LocalVector<Work> &p_level) {
		FellowClusterDag::Level lv;
		lv.first_node = dag->nodes.size();
		lv.first_cluster = dag->clusters.size();
		const uint32_t first_index = out_indices.size();
		LocalVector<Item> items;
		items.resize(p_level.size());
		for (uint32_t i = 0; i < p_level.size(); i++) {
			items[i].c = p_level[i].aabb.get_center();
			items[i].id = i;
		}
		build_node(items.ptr(), items.size(), p_level);
		lv.node_count = dag->nodes.size() - lv.first_node;
		lv.cluster_count = dag->clusters.size() - lv.first_cluster;
		lv.index_count = out_indices.size() - first_index;
		bound_nodes(lv.first_node, dag->nodes.size());
		dag->levels.push_back(lv);
	}
};

// A level's groups simplified and split again on worker threads, each into its own result.
struct GroupResult {
	bool ok = false;
	Sphere sphere;
	float error = 0.0f;
	LocalVector<Work> clusters;
};

struct GroupJob {
	Builder *b = nullptr;
	const LocalVector<Work> *current = nullptr;
	const LocalVector<LocalVector<uint32_t>> *members = nullptr;
	LocalVector<GroupResult> *results = nullptr;
	const float *normals = nullptr;
	float normal_weight = 0.0f;
	uint32_t group_count = 0;
	SafeNumeric<uint32_t> next;

	void help(uint32_t, void *) {
		for (uint32_t g = next.postincrement(); g < group_count; g = next.postincrement()) {
			run(g);
		}
	}

	void run(uint32_t p_group) {
		const LocalVector<uint32_t> &g = (*members)[p_group];
		GroupResult &r = (*results)[p_group];
		if (g.is_empty()) {
			return;
		}
		LocalVector<uint32_t> merged;
		for (uint32_t c : g) {
			for (uint32_t v : (*current)[c].indices) {
				merged.push_back(v);
			}
		}
		const size_t target = (merged.size() / 6) * 3;
		LocalVector<uint32_t> simplified;
		simplified.resize(merged.size());
		float err = 0.0f;
		const unsigned int options = FellowMeshopt::SIMPLIFY_LOCK_BORDER | FellowMeshopt::SIMPLIFY_SPARSE | FellowMeshopt::SIMPLIFY_ERROR_ABSOLUTE;
		size_t n;
		if (normals) {
			const float weights[3] = { normal_weight, normal_weight, normal_weight };
			n = FellowMeshopt::simplify_with_attributes(simplified.ptr(), merged.ptr(), merged.size(), b->pos, b->vertex_count, sizeof(float) * 3, normals, sizeof(float) * 3, weights, 3, nullptr, target, FLT_MAX, options, &err);
		} else {
			n = FellowMeshopt::simplify(simplified.ptr(), merged.ptr(), merged.size(), b->pos, b->vertex_count, sizeof(float) * 3, target, FLT_MAX, options, &err);
		}
		if (n == 0 || float(n) > float(merged.size()) * DAG_MIN_REDUCTION) {
			return;
		}
		LocalVector<Sphere> spheres;
		float group_error = 0.0f;
		for (uint32_t c : g) {
			spheres.push_back((*current)[c].self);
			group_error = MAX(group_error, (*current)[c].error);
		}
		r.sphere = enclose(spheres.ptr(), spheres.size());
		r.error = group_error + err;
		b->clusterize(simplified.ptr(), n, r.clusters);
		r.ok = true;
	}
};

} // namespace

// The hierarchies kept in user://fellow_dag_cache (geometry/cluster_dag_cache), named by a SHA-256 of
// what they are built from; the file: a header (the structures' sizes, so another build's layout is a miss), the
// clusters, nodes and levels as they are in memory, then the indices.
static constexpr uint32_t DAG_CACHE_MAGIC = 0x47414446; // "FDAG"
static constexpr uint32_t DAG_CACHE_VERSION = 1; // (bump when the build changes what it makes)

static String _dag_cache_path(uint64_t p_format, const Vector<uint8_t> &p_vertex_data, uint32_t p_vertex_count, const AABB &p_aabb, const Vector<uint8_t> &p_index_data, uint32_t p_index_count, float p_normal_weight) {
	CryptoCore::SHA256Context ctx;
	ctx.start();
	const uint32_t head[4] = { DAG_CACHE_VERSION, p_vertex_count, p_index_count, uint32_t(sizeof(real_t)) };
	ctx.update(reinterpret_cast<const uint8_t *>(head), sizeof(head));
	ctx.update(reinterpret_cast<const uint8_t *>(&p_format), sizeof(p_format));
	ctx.update(reinterpret_cast<const uint8_t *>(&p_normal_weight), sizeof(p_normal_weight));
	const float box[6] = { float(p_aabb.position.x), float(p_aabb.position.y), float(p_aabb.position.z), float(p_aabb.size.x), float(p_aabb.size.y), float(p_aabb.size.z) };
	ctx.update(reinterpret_cast<const uint8_t *>(box), sizeof(box));
	ctx.update(p_vertex_data.ptr(), p_vertex_data.size());
	ctx.update(p_index_data.ptr(), p_index_data.size());
	unsigned char hash[32];
	ctx.finish(hash);
	return OS::get_singleton()->get_user_data_dir().path_join("fellow_dag_cache").path_join(String::hex_encode_buffer(hash, 32) + ".fdag");
}

struct DagCacheHeader {
	uint32_t magic = DAG_CACHE_MAGIC;
	uint32_t version = DAG_CACHE_VERSION;
	uint32_t cluster_size = sizeof(FellowClusterDag::Cluster);
	uint32_t node_size = sizeof(FellowClusterDag::Node);
	uint32_t level_size = sizeof(FellowClusterDag::Level);
	uint32_t is_16 = 0;
	uint32_t base_index_count = 0;
	uint32_t index_count = 0;
	uint32_t clusters = 0;
	uint32_t nodes = 0;
	uint32_t levels = 0;
};

static FellowClusterDag *_dag_cache_load(const String &p_path) {
	Ref<FileAccess> f = FileAccess::open(p_path, FileAccess::READ);
	if (f.is_null()) {
		return nullptr;
	}
	DagCacheHeader h;
	const DagCacheHeader want;
	if (f->get_buffer(reinterpret_cast<uint8_t *>(&h), sizeof(h)) != sizeof(h) || h.magic != want.magic || h.version != want.version || h.cluster_size != want.cluster_size || h.node_size != want.node_size || h.level_size != want.level_size) {
		return nullptr;
	}
	const uint64_t index_bytes = uint64_t(h.index_count) * (h.is_16 ? 2 : 4);
	if (f->get_length() != sizeof(h) + uint64_t(h.clusters) * h.cluster_size + uint64_t(h.nodes) * h.node_size + uint64_t(h.levels) * h.level_size + index_bytes) {
		return nullptr;
	}
	FellowClusterDag *dag = memnew(FellowClusterDag);
	dag->is_16 = h.is_16 != 0;
	dag->base_index_count = h.base_index_count;
	dag->index_count = h.index_count;
	dag->clusters.resize(h.clusters);
	dag->nodes.resize(h.nodes);
	dag->levels.resize(h.levels);
	dag->pending_indices.resize(index_bytes);
	f->get_buffer(reinterpret_cast<uint8_t *>(dag->clusters.ptr()), uint64_t(h.clusters) * h.cluster_size);
	f->get_buffer(reinterpret_cast<uint8_t *>(dag->nodes.ptr()), uint64_t(h.nodes) * h.node_size);
	f->get_buffer(reinterpret_cast<uint8_t *>(dag->levels.ptr()), uint64_t(h.levels) * h.level_size);
	if (f->get_buffer(dag->pending_indices.ptrw(), index_bytes) != index_bytes) {
		memdelete(dag);
		return nullptr;
	}
	return dag;
}

static void _dag_cache_save(const String &p_path, const FellowClusterDag *p_dag) {
	DirAccess::make_dir_recursive_absolute(p_path.get_base_dir());
	// (written beside and renamed: a reader never sees half a file; two builds of the same surface write the same bytes)
	const String tmp = p_path + vformat(".%d.tmp", OS::get_singleton()->get_ticks_usec() % 1000000);
	{
		Ref<FileAccess> f = FileAccess::open(tmp, FileAccess::WRITE);
		if (f.is_null()) {
			return;
		}
		DagCacheHeader h;
		h.is_16 = p_dag->is_16 ? 1 : 0;
		h.base_index_count = p_dag->base_index_count;
		h.index_count = p_dag->index_count;
		h.clusters = p_dag->clusters.size();
		h.nodes = p_dag->nodes.size();
		h.levels = p_dag->levels.size();
		f->store_buffer(reinterpret_cast<const uint8_t *>(&h), sizeof(h));
		f->store_buffer(reinterpret_cast<const uint8_t *>(p_dag->clusters.ptr()), uint64_t(h.clusters) * h.cluster_size);
		f->store_buffer(reinterpret_cast<const uint8_t *>(p_dag->nodes.ptr()), uint64_t(h.nodes) * h.node_size);
		f->store_buffer(reinterpret_cast<const uint8_t *>(p_dag->levels.ptr()), uint64_t(h.levels) * h.level_size);
		f->store_buffer(p_dag->pending_indices.ptr(), p_dag->pending_indices.size());
	}
	Ref<DirAccess> d = DirAccess::create(DirAccess::ACCESS_FILESYSTEM);
	if (d->rename(tmp, p_path) != OK) {
		d->remove(tmp);
	}
}

FellowClusterDag *fellow_cluster_dag_build(uint64_t p_format, const Vector<uint8_t> &p_vertex_data, uint32_t p_vertex_count, const AABB &p_aabb, const Vector<uint8_t> &p_index_data, uint32_t p_index_count) {
	if (!FellowMeshopt::available() || !(p_format & RSE::ARRAY_FORMAT_VERTEX) || (p_format & RSE::ARRAY_FLAG_USE_2D_VERTICES) || p_vertex_count == 0 || p_index_count < 3 * DAG_MAX_TRIANGLES * 2 || p_index_count % 3 != 0) {
		return nullptr;
	}
	const bool compressed = p_format & RSE::ARRAY_FLAG_COMPRESS_ATTRIBUTES;
	const uint32_t stride = compressed ? sizeof(uint16_t) * 4 : sizeof(float) * 3;
	ERR_FAIL_COND_V(uint64_t(p_vertex_data.size()) < uint64_t(p_vertex_count) * stride, nullptr);
	const bool is_16 = p_vertex_count <= 65536;
	const uint32_t index_size = is_16 ? 2 : 4;
	ERR_FAIL_COND_V(uint64_t(p_index_data.size()) < uint64_t(p_index_count) * index_size, nullptr);

	// Positions (local space, as the mesh clusters read them) and indices.
	LocalVector<float> pos;
	pos.resize(p_vertex_count * 3);
	{
		const uint8_t *r = p_vertex_data.ptr();
		for (uint32_t i = 0; i < p_vertex_count; i++) {
			Vector3 v;
			if (compressed) {
				const uint16_t *q = reinterpret_cast<const uint16_t *>(r + i * stride);
				v = p_aabb.position + p_aabb.size * Vector3(q[0] / 65535.0f, q[1] / 65535.0f, q[2] / 65535.0f);
			} else {
				const float *f = reinterpret_cast<const float *>(r + i * stride);
				v = Vector3(f[0], f[1], f[2]);
			}
			pos[i * 3] = v.x;
			pos[i * 3 + 1] = v.y;
			pos[i * 3 + 2] = v.z;
		}
	}
	// Normals (uncompressed formats: octahedral, after the positions), for the simplification's error.
	LocalVector<float> normals;
	float normal_weight = DAG_NORMAL_WEIGHT;
	if (OS::get_singleton()->has_environment("FELLOW_DAG_NORMAL_WEIGHT")) { // (experiments)
		normal_weight = OS::get_singleton()->get_environment("FELLOW_DAG_NORMAL_WEIGHT").to_float();
	}
	if ((p_format & RSE::ARRAY_FORMAT_NORMAL) && !compressed && normal_weight > 0.0f) {
		uint32_t offsets[RSE::ARRAY_MAX];
		uint32_t vertex_element_size = 0;
		uint32_t normal_element_size = 0;
		uint32_t attrib_element_size = 0;
		uint32_t skin_element_size = 0;
		RenderingServer::get_singleton()->mesh_surface_make_offsets_from_format(p_format, p_vertex_count, p_index_count, offsets, vertex_element_size, normal_element_size, attrib_element_size, skin_element_size);
		if (normal_element_size > 0 && uint64_t(offsets[RSE::ARRAY_NORMAL]) + uint64_t(p_vertex_count - 1) * normal_element_size + 4 <= uint64_t(p_vertex_data.size())) {
			normals.resize(p_vertex_count * 3);
			for (uint32_t i = 0; i < p_vertex_count; i++) {
				uint32_t e;
				memcpy(&e, p_vertex_data.ptr() + offsets[RSE::ARRAY_NORMAL] + i * normal_element_size, 4);
				const Vector3 n = Vector3::octahedron_decode(Vector2((e & 0xFFFF) / 65535.0, ((e >> 16) & 0xFFFF) / 65535.0));
				normals[i * 3] = n.x;
				normals[i * 3 + 1] = n.y;
				normals[i * 3 + 2] = n.z;
			}
		}
	}
	String cache_path;
	if (Fellow::cluster_dag_cache()) {
		cache_path = _dag_cache_path(p_format, p_vertex_data, p_vertex_count, p_aabb, p_index_data, p_index_count, normals.is_empty() ? 0.0f : normal_weight);
		FellowClusterDag *cached = _dag_cache_load(cache_path);
		if (cached != nullptr) {
			Fellow::cluster_dag_stats().cache_hits.increment();
			return cached;
		}
	}
	LocalVector<uint32_t> indices;
	indices.resize(p_index_count);
	for (uint32_t i = 0; i < p_index_count; i++) {
		indices[i] = is_16 ? uint32_t(reinterpret_cast<const uint16_t *>(p_index_data.ptr())[i]) : reinterpret_cast<const uint32_t *>(p_index_data.ptr())[i];
		ERR_FAIL_COND_V(indices[i] >= p_vertex_count, nullptr);
	}

	FellowClusterDag *dag = memnew(FellowClusterDag);
	dag->is_16 = is_16;
	dag->base_index_count = p_index_count;
	Builder b;
	b.pos = pos.ptr();
	b.vertex_count = p_vertex_count;
	b.dag = dag;

	// Level 0: the surface's triangles in clusters, each its own bounds and no error.
	const uint64_t t_start = OS::get_singleton()->get_ticks_usec();
	LocalVector<Work> current;
	b.clusterize(indices.ptr(), indices.size(), current);
	print_verbose(vformat("FellowEngine cluster DAG: %d triangles, level 0: %d clusters in %d ms", p_index_count / 3, current.size(), (OS::get_singleton()->get_ticks_usec() - t_start) / 1000));
	for (Work &w : current) {
		w.self.c = w.aabb.get_center();
		w.self.r = 0.0f;
		for (uint32_t v : w.indices) {
			w.self.r = MAX(w.self.r, float(w.self.c.distance_to(b.p(v))));
		}
		w.error = 0.0f;
	}

	LocalVector<uint32_t> concat;
	LocalVector<uint32_t> counts;
	LocalVector<uint32_t> partition;
	for (uint32_t level = 0;; level++) {
		if (current.size() <= 1 || level + 1 >= DAG_MAX_LEVELS) {
			b.finalize_level(current); // the last level: roots
			break;
		}
		// Neighbouring clusters grouped.
		concat.clear();
		counts.resize(current.size());
		for (uint32_t i = 0; i < current.size(); i++) {
			counts[i] = current[i].indices.size();
			for (uint32_t v : current[i].indices) {
				concat.push_back(v);
			}
		}
		partition.resize(current.size());
		const size_t groups = FellowMeshopt::partition_clusters(partition.ptr(), concat.ptr(), concat.size(), counts.ptr(), current.size(), pos.ptr(), p_vertex_count, sizeof(float) * 3, DAG_GROUP_SIZE);
		LocalVector<LocalVector<uint32_t>> members;
		members.resize(groups);
		for (uint32_t i = 0; i < current.size(); i++) {
			members[partition[i]].push_back(i);
		}
		// Each group simplified to half its triangles, its border locked, and split into clusters again — the groups in
		// parallel, their results taken in their order (the same hierarchy whatever the threads).
		LocalVector<GroupResult> results;
		results.resize(groups);
		GroupJob job;
		job.b = &b;
		job.current = &current;
		job.members = &members;
		job.results = &results;
		job.normals = normals.is_empty() ? nullptr : normals.ptr();
		job.normal_weight = normal_weight;
		job.group_count = groups;
		job.next.set(0);
		// Helpers (high priority: the pool keeps a thread from low-priority tasks such as this build) take groups from
		// a shared counter, and so does this thread: the work is done even if no helper starts before it is over.
		const int helpers = MIN(int(groups) - 1, WorkerThreadPool::get_singleton()->get_thread_count() - 1);
		WorkerThreadPool::GroupID gid = -1;
		if (helpers > 0) {
			gid = WorkerThreadPool::get_singleton()->add_template_group_task(&job, &GroupJob::help, (void *)nullptr, helpers, helpers, true, "FellowEngine cluster DAG");
		}
		job.help(0, nullptr);
		if (gid >= 0) {
			WorkerThreadPool::get_singleton()->wait_for_group_task_completion(gid);
		}
		LocalVector<Work> next;
		for (uint32_t g = 0; g < groups; g++) {
			GroupResult &r = results[g];
			if (!r.ok) {
				continue; // (its clusters stay roots)
			}
			for (uint32_t c : members[g]) {
				current[c].parent = r.sphere;
				current[c].parent_error = r.error;
			}
			for (Work &w : r.clusters) {
				w.self = r.sphere;
				w.error = r.error;
				next.push_back(std::move(w));
			}
		}
		b.finalize_level(current);
		print_verbose(vformat("FellowEngine cluster DAG: level %d: %d groups, %d clusters above, %d ms", level + 1, groups, next.size(), (OS::get_singleton()->get_ticks_usec() - t_start) / 1000));
		if (next.is_empty()) {
			break;
		}
		current = std::move(next);
	}

	dag->index_count = b.out_indices.size();
	dag->pending_indices.resize(uint64_t(dag->index_count) * index_size);
	uint8_t *w = dag->pending_indices.ptrw();
	for (uint32_t i = 0; i < dag->index_count; i++) {
		if (is_16) {
			reinterpret_cast<uint16_t *>(w)[i] = uint16_t(b.out_indices[i]);
		} else {
			reinterpret_cast<uint32_t *>(w)[i] = b.out_indices[i];
		}
	}
	if (!cache_path.is_empty()) {
		_dag_cache_save(cache_path, dag);
		Fellow::cluster_dag_stats().cache_writes.increment();
	}
	return dag;
}

uint32_t FellowClusterDag::select(const Plane *p_planes, int p_plane_count, const Params &p_params, LocalVector<uint32_t> &r_ranges, uint32_t *r_clusters) const {
	// Godot's test: an error (local units) is fine enough at a local distance when its size on screen is at most the
	// threshold (error 0 always; distance 0 never otherwise).
	const real_t min_scale = p_params.min_scale;
	auto fine = [&](float p_error, real_t p_local_distance) -> bool {
		if (p_error <= 0.0f) {
			return true;
		}
		if (p_error == FLT_MAX) {
			return false;
		}
		real_t d = p_local_distance * min_scale;
		if (p_params.distance_bias > 0.0) {
			d = MAX(0.0, d - p_params.distance_bias);
		}
		d *= p_params.distance_multiplier;
		if (d <= 0.0) {
			return false;
		}
		return p_error * p_params.model_scale / float(d) <= p_params.threshold;
	};
	auto nearest = [&](const Vector3 &p_center, float p_radius) -> real_t {
		return MAX(real_t(0.0), p_params.eye.distance_to(p_center) - p_radius);
	};
	auto in_volume = [&](const AABB &p_aabb, uint32_t &r_mask) -> bool {
		const Vector3 c = p_aabb.get_center();
		const Vector3 e = p_aabb.size * 0.5;
		for (int i = 0; i < p_plane_count; i++) {
			if (!(r_mask & (1u << i))) {
				continue;
			}
			const Plane &pl = p_planes[i];
			const real_t dist = pl.normal.dot(c) - pl.d;
			const real_t radius = Math::abs(pl.normal.x) * e.x + Math::abs(pl.normal.y) * e.y + Math::abs(pl.normal.z) * e.z;
			if (dist - radius > 0) {
				return false;
			}
			if (dist + radius <= 0) {
				r_mask &= ~(1u << i);
			}
		}
		return true;
	};

	uint32_t emitted = 0;
	uint32_t drawn_clusters = 0;
	uint32_t stack_node[64];
	uint32_t stack_mask[64];
	for (const Level &lv : levels) {
		int sp = 0;
		stack_node[sp] = lv.first_node;
		stack_mask[sp] = (1u << p_plane_count) - 1;
		sp++;
		while (sp > 0) {
			sp--;
			const uint32_t n = stack_node[sp];
			uint32_t mask = stack_mask[sp];
			const Node &nd = nodes[n];
			if (!in_volume(nd.aabb, mask)) {
				continue;
			}
			// Every parent here fine enough: their clusters are drawn, not these. No own error fine enough even at the
			// farthest: coarser than what is drawn here.
			if (fine(nd.max_parent_error, nearest(nd.parent_center, nd.parent_radius))) {
				continue;
			}
			if (!fine(nd.min_error, p_params.eye.distance_to(nd.center) + nd.radius)) {
				continue;
			}
			if (nd.right != 0 && sp + 2 <= 64) {
				stack_node[sp] = nd.right;
				stack_mask[sp] = mask;
				sp++;
				stack_node[sp] = n + 1;
				stack_mask[sp] = mask;
				sp++;
				continue;
			}
			for (uint32_t i = 0; i < nd.cluster_count; i++) {
				const Cluster &c = clusters[nd.first_cluster + i];
				uint32_t cmask = mask;
				if (!fine(c.error, nearest(c.center, c.radius)) || fine(c.parent_error, nearest(c.parent_center, c.parent_radius)) || (cmask != 0 && !in_volume(c.aabb, cmask))) {
					continue;
				}
				const uint32_t rs = r_ranges.size();
				if (rs >= 2 && r_ranges[rs - 2] + r_ranges[rs - 1] == c.first) {
					r_ranges[rs - 1] += c.count;
				} else {
					r_ranges.push_back(c.first);
					r_ranges.push_back(c.count);
				}
				emitted += c.count;
				drawn_clusters++;
			}
		}
	}
	if (r_clusters) {
		*r_clusters = drawn_clusters;
	}
	return emitted;
}
