/**************************************************************************/
/*  fellow_mesh_clusters.cpp                                              */
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

#include "fellow_mesh_clusters.h"

#include "core/templates/sort_array.h"
#include "servers/rendering/rendering_server_enums.h"

namespace {

struct CentroidLess {
	const Vector3 *centroids = nullptr;
	int axis = 0;
	_FORCE_INLINE_ bool operator()(uint32_t p_a, uint32_t p_b) const {
		return centroids[p_a][axis] < centroids[p_b][axis];
	}
};

struct Builder {
	const Vector3 *centroids = nullptr;
	uint32_t *tris = nullptr; // Triangle ids, partitioned in place (leaf order once the build is done).
	uint32_t *tri_leaf = nullptr; // Leaf node of each triangle id.
	uint32_t max_leaf = 0;
	LocalVector<FellowMeshClusters::Node> *nodes = nullptr;
	uint32_t leaf_count = 0;

	void build(uint32_t p_begin, uint32_t p_end) {
		const uint32_t node = nodes->size();
		nodes->push_back(FellowMeshClusters::Node());
		if (p_end - p_begin <= max_leaf) {
			for (uint32_t i = p_begin; i < p_end; i++) {
				tri_leaf[tris[i]] = node;
			}
			leaf_count++;
			return;
		}
		Vector3 lo = centroids[tris[p_begin]];
		Vector3 hi = lo;
		for (uint32_t i = p_begin + 1; i < p_end; i++) {
			const Vector3 &c = centroids[tris[i]];
			lo = lo.min(c);
			hi = hi.max(c);
		}
		const Vector3 ext = hi - lo;
		const int axis = ext.x >= ext.y ? (ext.x >= ext.z ? 0 : 2) : (ext.y >= ext.z ? 1 : 2);
		const uint32_t mid = p_begin + (p_end - p_begin) / 2;
		SortArray<uint32_t, CentroidLess, false> sorter;
		sorter.compare.centroids = centroids;
		sorter.compare.axis = axis;
		sorter.nth_element(p_begin, p_end, mid, tris);
		(*nodes)[node].axis = axis;
		(*nodes)[node].split = centroids[tris[mid]][axis];
		build(p_begin, mid);
		(*nodes)[node].right = nodes->size();
		build(mid, p_end);
	}
};

_FORCE_INLINE_ uint32_t read_index(const uint8_t *p_data, bool p_16, uint32_t p_i) {
	return p_16 ? uint32_t(reinterpret_cast<const uint16_t *>(p_data)[p_i]) : reinterpret_cast<const uint32_t *>(p_data)[p_i];
}

// Orders the triangles of one index buffer by leaf (p_tri_leaf: leaf node of each triangle), fills the level's node
// ranges and bounds, and returns the leaf-ordered index data.
Vector<uint8_t> make_level(const LocalVector<FellowMeshClusters::Node> &p_nodes, const Vector3 *p_positions, uint32_t p_vertex_count, const uint8_t *p_indices, bool p_16, uint32_t p_tri_count, const uint32_t *p_tri_leaf, LocalVector<FellowMeshClusters::Range> &r_ranges) {
	const uint32_t node_count = p_nodes.size();
	r_ranges.resize(node_count);
	LocalVector<uint32_t> start;
	start.resize(node_count + 1);
	for (uint32_t i = 0; i <= node_count; i++) {
		start[i] = 0;
	}
	for (uint32_t t = 0; t < p_tri_count; t++) {
		start[p_tri_leaf[t] + 1]++;
	}
	// Leaves appear in pre-order with increasing node index, so a prefix sum over node indices is leaf order.
	for (uint32_t i = 0; i < node_count; i++) {
		start[i + 1] += start[i];
	}
	LocalVector<uint32_t> cursor;
	cursor.resize(node_count);
	for (uint32_t i = 0; i < node_count; i++) {
		cursor[i] = start[i];
		r_ranges[i].first = start[i] * 3;
		r_ranges[i].count = (start[i + 1] - start[i]) * 3;
		r_ranges[i].aabb = AABB();
	}
	const uint32_t index_size = p_16 ? 2 : 4;
	Vector<uint8_t> out;
	out.resize(p_tri_count * 3 * index_size);
	uint8_t *w = out.ptrw();
	LocalVector<bool> has_bounds;
	has_bounds.resize(node_count);
	for (uint32_t i = 0; i < node_count; i++) {
		has_bounds[i] = false;
	}
	for (uint32_t t = 0; t < p_tri_count; t++) {
		const uint32_t leaf = p_tri_leaf[t];
		const uint32_t dst = cursor[leaf]++;
		for (uint32_t k = 0; k < 3; k++) {
			const uint32_t v = read_index(p_indices, p_16, t * 3 + k);
			if (p_16) {
				reinterpret_cast<uint16_t *>(w)[dst * 3 + k] = uint16_t(v);
			} else {
				reinterpret_cast<uint32_t *>(w)[dst * 3 + k] = v;
			}
			if (v < p_vertex_count) {
				if (has_bounds[leaf]) {
					r_ranges[leaf].aabb.expand_to(p_positions[v]);
				} else {
					r_ranges[leaf].aabb = AABB(p_positions[v], Vector3());
					has_bounds[leaf] = true;
				}
			}
		}
	}
	// Internal nodes: union of the children (children have larger indices than their parent in pre-order).
	for (int64_t n = int64_t(node_count) - 1; n >= 0; n--) {
		const FellowMeshClusters::Node &node = p_nodes[n];
		if (node.right == 0) {
			continue;
		}
		const uint32_t l = uint32_t(n) + 1;
		const uint32_t r = node.right;
		// The right subtree directly follows the left one in leaf order: the node's range is their concatenation.
		r_ranges[n].first = r_ranges[l].first;
		r_ranges[n].count = r_ranges[l].count + r_ranges[r].count;
		if (has_bounds[l] && has_bounds[r]) {
			r_ranges[n].aabb = r_ranges[l].aabb.merge(r_ranges[r].aabb);
		} else if (has_bounds[l] || has_bounds[r]) {
			r_ranges[n].aabb = has_bounds[l] ? r_ranges[l].aabb : r_ranges[r].aabb;
		}
		has_bounds[n] = has_bounds[l] || has_bounds[r];
	}
	// Inflate the bounds slightly: the cull must never reject a node the GPU would rasterise a pixel of.
	for (uint32_t i = 0; i < node_count; i++) {
		AABB &b = r_ranges[i].aabb;
		const real_t margin = b.get_longest_axis_size() * 0.001 + 0.0001;
		b.grow_by(margin);
	}
	return out;
}

} // namespace

uint32_t FellowMeshClusters::cull(uint32_t p_level, const Plane *p_planes, int p_plane_count, LocalVector<uint32_t> &r_ranges) const {
	ERR_FAIL_UNSIGNED_INDEX_V(p_level, levels.size(), 0);
	const LocalVector<Range> &ranges = levels[p_level].nodes;
	uint32_t emitted = 0;
	// Depth-first, left before right, so ranges come out in ascending order and adjacent ones can be merged.
	uint32_t stack_node[64];
	uint32_t stack_mask[64];
	int sp = 0;
	stack_node[sp] = 0;
	stack_mask[sp] = (1u << p_plane_count) - 1;
	sp++;
	while (sp > 0) {
		sp--;
		const uint32_t n = stack_node[sp];
		uint32_t mask = stack_mask[sp];
		const Range &r = ranges[n];
		if (r.count == 0) {
			continue;
		}
		const Vector3 c = r.aabb.get_center();
		const Vector3 e = r.aabb.size * 0.5;
		bool outside = false;
		for (int i = 0; i < p_plane_count; i++) {
			if (!(mask & (1u << i))) {
				continue;
			}
			const Plane &p = p_planes[i];
			const real_t dist = p.normal.dot(c) - p.d;
			const real_t radius = Math::abs(p.normal.x) * e.x + Math::abs(p.normal.y) * e.y + Math::abs(p.normal.z) * e.z;
			if (dist - radius > 0) {
				outside = true;
				break;
			}
			if (dist + radius <= 0) {
				mask &= ~(1u << i); // Entirely inside this plane: no need to test the subtree against it.
			}
		}
		if (outside) {
			continue;
		}
		const Node &node = nodes[n];
		if (mask == 0 || node.right == 0 || sp + 2 > 64) {
			const uint32_t rs = r_ranges.size();
			if (rs >= 2 && r_ranges[rs - 2] + r_ranges[rs - 1] == r.first) {
				r_ranges[rs - 1] += r.count;
			} else {
				r_ranges.push_back(r.first);
				r_ranges.push_back(r.count);
			}
			emitted += r.count;
			continue;
		}
		stack_node[sp] = node.right;
		stack_mask[sp] = mask;
		sp++;
		stack_node[sp] = n + 1;
		stack_mask[sp] = mask;
		sp++;
	}
	return emitted;
}

void FellowMeshClusters::free_rids() {
	for (Level &l : levels) {
		if (l.index_array.is_valid()) {
			RD::get_singleton()->free_rid(l.index_array);
			l.index_array = RID();
		}
		if (l.index_buffer.is_valid()) {
			RD::get_singleton()->free_rid(l.index_buffer);
			l.index_buffer = RID();
		}
	}
}

FellowMeshClusters *fellow_mesh_clusters_build(uint64_t p_format, const Vector<uint8_t> &p_vertex_data, uint32_t p_vertex_count, const AABB &p_aabb, const Vector<uint8_t> &p_index_data, uint32_t p_index_count, const Vector<Vector<uint8_t>> &p_lods, uint32_t p_max_leaf_triangles) {
	if (!(p_format & RSE::ARRAY_FORMAT_VERTEX) || (p_format & RSE::ARRAY_FLAG_USE_2D_VERTICES) || p_vertex_count == 0 || p_index_count < 6 || p_index_count % 3 != 0) {
		return nullptr;
	}
	const bool compressed = p_format & RSE::ARRAY_FLAG_COMPRESS_ATTRIBUTES;
	const uint32_t stride = compressed ? sizeof(uint16_t) * 4 : sizeof(float) * 3;
	ERR_FAIL_COND_V(uint64_t(p_vertex_data.size()) < uint64_t(p_vertex_count) * stride, nullptr);
	const bool is_16 = p_vertex_count <= 65536;
	const uint32_t index_size = is_16 ? 2 : 4;
	ERR_FAIL_COND_V(uint64_t(p_index_data.size()) < uint64_t(p_index_count) * index_size, nullptr);

	// Positions (local space).
	LocalVector<Vector3> positions;
	positions.resize(p_vertex_count);
	{
		const uint8_t *r = p_vertex_data.ptr();
		for (uint32_t i = 0; i < p_vertex_count; i++) {
			if (compressed) {
				const uint16_t *v = reinterpret_cast<const uint16_t *>(r + i * stride);
				positions[i] = p_aabb.position + p_aabb.size * Vector3(v[0] / 65535.0f, v[1] / 65535.0f, v[2] / 65535.0f);
			} else {
				const float *v = reinterpret_cast<const float *>(r + i * stride);
				positions[i] = Vector3(v[0], v[1], v[2]);
			}
		}
	}

	const uint8_t *base_indices = p_index_data.ptr();
	const uint32_t tri_count = p_index_count / 3;
	LocalVector<Vector3> centroids;
	centroids.resize(tri_count);
	for (uint32_t t = 0; t < tri_count; t++) {
		Vector3 c;
		for (uint32_t k = 0; k < 3; k++) {
			const uint32_t v = read_index(base_indices, is_16, t * 3 + k);
			c += v < p_vertex_count ? positions[v] : Vector3();
		}
		centroids[t] = c / 3.0;
	}

	FellowMeshClusters *clusters = memnew(FellowMeshClusters);
	LocalVector<uint32_t> tris;
	tris.resize(tri_count);
	for (uint32_t t = 0; t < tri_count; t++) {
		tris[t] = t;
	}
	LocalVector<uint32_t> tri_leaf;
	tri_leaf.resize(tri_count);
	{
		Builder b;
		b.centroids = centroids.ptr();
		b.tris = tris.ptr();
		b.tri_leaf = tri_leaf.ptr();
		b.max_leaf = MAX(1u, p_max_leaf_triangles);
		b.nodes = &clusters->nodes;
		b.build(0, tri_count);
		clusters->leaf_count = b.leaf_count;
	}

	const uint32_t level_count = 1 + p_lods.size();
	clusters->levels.resize(level_count);
	for (uint32_t l = 0; l < level_count; l++) {
		FellowMeshClusters::Level &level = clusters->levels[l];
		Vector<uint8_t> ordered;
		uint32_t index_count = 0;
		if (l == 0) {
			index_count = p_index_count;
			ordered = make_level(clusters->nodes, positions.ptr(), p_vertex_count, base_indices, is_16, tri_count, tri_leaf.ptr(), level.nodes);
		} else {
			const Vector<uint8_t> &lod = p_lods[l - 1];
			index_count = lod.size() / index_size;
			const uint32_t lod_tris = index_count / 3;
			const uint8_t *lod_indices = lod.ptr();
			LocalVector<uint32_t> lod_leaf;
			lod_leaf.resize(lod_tris);
			for (uint32_t t = 0; t < lod_tris; t++) {
				Vector3 c;
				for (uint32_t k = 0; k < 3; k++) {
					const uint32_t v = read_index(lod_indices, is_16, t * 3 + k);
					c += v < p_vertex_count ? positions[v] : Vector3();
				}
				c /= 3.0;
				uint32_t n = 0;
				while (clusters->nodes[n].right != 0) {
					const FellowMeshClusters::Node &node = clusters->nodes[n];
					n = c[node.axis] < node.split ? n + 1 : node.right;
				}
				lod_leaf[t] = n;
			}
			ordered = make_level(clusters->nodes, positions.ptr(), p_vertex_count, lod_indices, is_16, lod_tris, lod_leaf.ptr(), level.nodes);
			index_count = lod_tris * 3;
		}
		level.index_count = index_count;
		if (index_count == 0) {
			continue;
		}
		level.index_buffer = RD::get_singleton()->index_buffer_create(index_count, is_16 ? RD::INDEX_BUFFER_FORMAT_UINT16 : RD::INDEX_BUFFER_FORMAT_UINT32, ordered, false);
		level.index_array = RD::get_singleton()->index_array_create(level.index_buffer, 0, index_count);
		clusters->memory_bytes += uint64_t(index_count) * index_size;
	}
	return clusters;
}
