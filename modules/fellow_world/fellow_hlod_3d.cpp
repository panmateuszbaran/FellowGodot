/**************************************************************************/
/*  fellow_hlod_3d.cpp                                                    */
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

#include "fellow_hlod_3d.h"

#include "fellow_impostor_baker.h"

#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "core/os/os.h"
#include "scene/3d/mesh_instance_3d.h"
#include "scene/3d/multimesh_instance_3d.h"
#include "scene/resources/3d/importer_mesh.h"
#include "scene/resources/material.h"
#include "scene/resources/mesh.h"
#include "scene/resources/multimesh.h"
#include "servers/rendering/fellow_settings.h"
#include "servers/rendering/rendering_server.h"

// The vertex attributes a merged surface carries (a surface is merged only with surfaces of the same set).
enum {
	HLOD_NORMAL = 1,
	HLOD_TANGENT = 2,
	HLOD_COLOR = 4,
	HLOD_UV = 8,
	HLOD_UV2 = 16,
};

namespace {

struct SourceSurface {
	PackedVector3Array vertices;
	PackedVector3Array normals;
	PackedFloat32Array tangents;
	PackedColorArray colors;
	PackedVector2Array uvs;
	PackedVector2Array uv2s;
	PackedInt32Array indices;
	uint32_t attributes = 0;
	bool valid = false;
};

struct Accum {
	Ref<Material> material;
	uint32_t attributes = 0;
	PackedVector3Array vertices;
	PackedVector3Array normals;
	PackedFloat32Array tangents;
	PackedColorArray colors;
	PackedVector2Array uvs;
	PackedVector2Array uv2s;
	PackedInt32Array indices;
};

// A proxy per cell and per set of instance settings (they are per instance, not per surface).
struct ProxyKey {
	Vector3i cell;
	int cast_shadows = 0;
	uint32_t layers = 0;
	int gi_mode = 0;
	bool operator==(const ProxyKey &p_other) const {
		return cell == p_other.cell && cast_shadows == p_other.cast_shadows && layers == p_other.layers && gi_mode == p_other.gi_mode;
	}
};

struct ProxyKeyHasher {
	static _FORCE_INLINE_ uint32_t hash(const ProxyKey &p_key) {
		uint32_t h = hash_murmur3_one_32(uint32_t(p_key.cell.x));
		h = hash_murmur3_one_32(uint32_t(p_key.cell.y), h);
		h = hash_murmur3_one_32(uint32_t(p_key.cell.z), h);
		h = hash_murmur3_one_32(uint32_t(p_key.cast_shadows), h);
		h = hash_murmur3_one_32(p_key.layers, h);
		h = hash_murmur3_one_32(uint32_t(p_key.gi_mode), h);
		return hash_fmix32(h);
	}
};

struct Proxy {
	ProxyKey key;
	LocalVector<MeshInstance3D *> members;
	LocalVector<Accum> surfaces;
	HashMap<uint64_t, uint32_t> surface_of; // material id and attributes -> surface
};

SourceSurface read_surface(const Ref<Mesh> &p_mesh, int p_surface) {
	SourceSurface s;
	if (p_mesh->surface_get_primitive_type(p_surface) != Mesh::PRIMITIVE_TRIANGLES) {
		return s;
	}
	const Array arrays = p_mesh->surface_get_arrays(p_surface);
	if (arrays.size() != Mesh::ARRAY_MAX) {
		return s;
	}
	// Skinned, custom or blend data: not merged (the merged surface could not carry it).
	if (arrays[Mesh::ARRAY_BONES].get_type() != Variant::NIL || arrays[Mesh::ARRAY_WEIGHTS].get_type() != Variant::NIL) {
		return s;
	}
	for (int c = Mesh::ARRAY_CUSTOM0; c <= Mesh::ARRAY_CUSTOM3; c++) {
		if (arrays[c].get_type() != Variant::NIL) {
			return s;
		}
	}
	s.vertices = arrays[Mesh::ARRAY_VERTEX];
	const int n = s.vertices.size();
	if (n == 0) {
		return s;
	}
	if (arrays[Mesh::ARRAY_NORMAL].get_type() != Variant::NIL) {
		s.normals = arrays[Mesh::ARRAY_NORMAL];
		ERR_FAIL_COND_V(s.normals.size() != n, SourceSurface());
		s.attributes |= HLOD_NORMAL;
	}
	if (arrays[Mesh::ARRAY_TANGENT].get_type() != Variant::NIL) {
		s.tangents = arrays[Mesh::ARRAY_TANGENT];
		ERR_FAIL_COND_V(s.tangents.size() != n * 4, SourceSurface());
		s.attributes |= HLOD_TANGENT;
	}
	if (arrays[Mesh::ARRAY_COLOR].get_type() != Variant::NIL) {
		s.colors = arrays[Mesh::ARRAY_COLOR];
		ERR_FAIL_COND_V(s.colors.size() != n, SourceSurface());
		s.attributes |= HLOD_COLOR;
	}
	if (arrays[Mesh::ARRAY_TEX_UV].get_type() != Variant::NIL) {
		s.uvs = arrays[Mesh::ARRAY_TEX_UV];
		ERR_FAIL_COND_V(s.uvs.size() != n, SourceSurface());
		s.attributes |= HLOD_UV;
	}
	if (arrays[Mesh::ARRAY_TEX_UV2].get_type() != Variant::NIL) {
		s.uv2s = arrays[Mesh::ARRAY_TEX_UV2];
		ERR_FAIL_COND_V(s.uv2s.size() != n, SourceSurface());
		s.attributes |= HLOD_UV2;
	}
	if (arrays[Mesh::ARRAY_INDEX].get_type() != Variant::NIL) {
		s.indices = arrays[Mesh::ARRAY_INDEX];
	} else {
		s.indices.resize(n);
		int32_t *w = s.indices.ptrw();
		for (int i = 0; i < n; i++) {
			w[i] = i;
		}
	}
	if (s.indices.size() % 3 != 0) {
		return SourceSurface();
	}
	s.valid = true;
	return s;
}

void append_surface(Accum &r_accum, const SourceSurface &p_src, const Transform3D &p_xform) {
	const int base = r_accum.vertices.size();
	const int n = p_src.vertices.size();
	const Basis normal_basis = p_xform.basis.inverse().transposed();
	const bool mirrored = p_xform.basis.determinant() < 0.0;
	r_accum.vertices.resize(base + n);
	Vector3 *v = r_accum.vertices.ptrw() + base;
	const Vector3 *sv = p_src.vertices.ptr();
	for (int i = 0; i < n; i++) {
		v[i] = p_xform.xform(sv[i]);
	}
	if (r_accum.attributes & HLOD_NORMAL) {
		r_accum.normals.resize(base + n);
		Vector3 *w = r_accum.normals.ptrw() + base;
		const Vector3 *s = p_src.normals.ptr();
		for (int i = 0; i < n; i++) {
			w[i] = normal_basis.xform(s[i]).normalized();
		}
	}
	if (r_accum.attributes & HLOD_TANGENT) {
		r_accum.tangents.resize((base + n) * 4);
		float *w = r_accum.tangents.ptrw() + base * 4;
		const float *s = p_src.tangents.ptr();
		for (int i = 0; i < n; i++) {
			const Vector3 t = p_xform.basis.xform(Vector3(s[i * 4 + 0], s[i * 4 + 1], s[i * 4 + 2])).normalized();
			w[i * 4 + 0] = t.x;
			w[i * 4 + 1] = t.y;
			w[i * 4 + 2] = t.z;
			w[i * 4 + 3] = mirrored ? -s[i * 4 + 3] : s[i * 4 + 3];
		}
	}
	if (r_accum.attributes & HLOD_COLOR) {
		r_accum.colors.append_array(p_src.colors);
	}
	if (r_accum.attributes & HLOD_UV) {
		r_accum.uvs.append_array(p_src.uvs);
	}
	if (r_accum.attributes & HLOD_UV2) {
		r_accum.uv2s.append_array(p_src.uv2s);
	}
	const int ibase = r_accum.indices.size();
	const int ni = p_src.indices.size();
	r_accum.indices.resize(ibase + ni);
	int32_t *w = r_accum.indices.ptrw() + ibase;
	const int32_t *s = p_src.indices.ptr();
	for (int i = 0; i < ni; i += 3) {
		// A mirroring transform turns the triangles over: keep their front faces outwards.
		w[i + 0] = base + s[i + 0];
		w[i + 1] = base + s[mirrored ? i + 2 : i + 1];
		w[i + 2] = base + s[mirrored ? i + 1 : i + 2];
	}
}

Array accum_arrays(const Accum &p_accum) {
	Array arrays;
	arrays.resize(Mesh::ARRAY_MAX);
	arrays[Mesh::ARRAY_VERTEX] = p_accum.vertices;
	if (p_accum.attributes & HLOD_NORMAL) {
		arrays[Mesh::ARRAY_NORMAL] = p_accum.normals;
	}
	if (p_accum.attributes & HLOD_TANGENT) {
		arrays[Mesh::ARRAY_TANGENT] = p_accum.tangents;
	}
	if (p_accum.attributes & HLOD_COLOR) {
		arrays[Mesh::ARRAY_COLOR] = p_accum.colors;
	}
	if (p_accum.attributes & HLOD_UV) {
		arrays[Mesh::ARRAY_TEX_UV] = p_accum.uvs;
	}
	if (p_accum.attributes & HLOD_UV2) {
		arrays[Mesh::ARRAY_TEX_UV2] = p_accum.uv2s;
	}
	arrays[Mesh::ARRAY_INDEX] = p_accum.indices;
	return arrays;
}

// A mesh can be merged when nothing about it is per instance beyond what a proxy keeps (its cast shadow setting, layers
// and GI mode): no skin, blend shapes, overlay, transparency, visibility range or parent of its own.
bool mesh_instance_mergeable(MeshInstance3D *p_mi) {
	if (!p_mi->is_visible_in_tree() || p_mi->is_in_group(SNAME("fellow_hlod_exclude"))) {
		return false;
	}
	const Ref<Mesh> mesh = p_mi->get_mesh();
	if (mesh.is_null() || mesh->get_surface_count() == 0 || mesh->get_blend_shape_count() > 0) {
		return false;
	}
	if (p_mi->get_skin().is_valid()) {
		return false; // (skinned surfaces carry bone weights: not merged either)
	}
	if (p_mi->get_material_overlay().is_valid() || p_mi->get_transparency() > 0.0f || p_mi->get_visibility_range_begin() > 0.0f || p_mi->get_visibility_range_end() > 0.0f || !p_mi->get_visibility_parent().is_empty()) {
		return false;
	}
	if (p_mi->get_cast_shadows_setting() == GeometryInstance3D::SHADOW_CASTING_SETTING_SHADOWS_ONLY) {
		return false; // its proxy would be visible
	}
	return !p_mi->is_set_as_top_level();
}

// Post-order: whether every visual instance in the subtree can follow a proxy (a mesh's visibility parent passes to its
// children that have none of their own: a light or particles under a merged mesh would vanish with it).
bool collect(Node *p_node, FellowHLOD3D *p_root, LocalVector<MeshInstance3D *> &r_out) {
	bool subtree_ok = true;
	const int count = p_node->get_child_count(false);
	LocalVector<MeshInstance3D *> below;
	for (int i = 0; i < count; i++) {
		Node *c = p_node->get_child(i, false);
		if (!collect(c, p_root, below)) {
			subtree_ok = false;
		}
	}
	if (p_node == p_root) {
		for (MeshInstance3D *m : below) {
			r_out.push_back(m);
		}
		return subtree_ok;
	}
	MeshInstance3D *mi = Object::cast_to<MeshInstance3D>(p_node);
	bool self_ok;
	if (mi) {
		self_ok = mesh_instance_mergeable(mi);
	} else {
		VisualInstance3D *vi = Object::cast_to<VisualInstance3D>(p_node);
		Node3D *n3d = Object::cast_to<Node3D>(p_node);
		// Other visual instances (lights, particles, decals, ...) and nodes that set a visibility parent keep their
		// ancestors out.
		self_ok = vi == nullptr && (n3d == nullptr || n3d->get_visibility_parent().is_empty());
	}
	if (mi && self_ok && subtree_ok) {
		r_out.push_back(mi);
	}
	if (!self_ok || !subtree_ok) {
		// The meshes below this node that can merge still do (their own visibility parent is explicit).
		for (MeshInstance3D *m : below) {
			r_out.push_back(m);
		}
		return false;
	}
	for (MeshInstance3D *m : below) {
		r_out.push_back(m);
	}
	return true;
}

// A material that draws every vertex where the mesh has it (no billboard, grow, point or fixed size): its instances can
// be culled one by one on the GPU (MultiMesh culling only culls such materials; an instanced proxy must be culled).
bool material_instancing_safe(const Ref<Material> &p_material, int p_depth = 0) {
	if (p_material.is_null()) {
		return true;
	}
	const Ref<BaseMaterial3D> bm = p_material;
	if (bm.is_null() || p_depth > 8) {
		return false; // shader materials may move vertices or read INSTANCE_ID
	}
	if (bm->get_billboard_mode() != BaseMaterial3D::BILLBOARD_DISABLED || bm->is_grow_enabled() || bm->get_flag(BaseMaterial3D::FLAG_USE_POINT_SIZE) || bm->get_flag(BaseMaterial3D::FLAG_FIXED_SIZE) || bm->get_flag(BaseMaterial3D::FLAG_PARTICLE_TRAILS_MODE)) {
		return false;
	}
	return material_instancing_safe(bm->get_next_pass(), p_depth + 1);
}

// The node's box is its mesh's under its transform (no margin, no custom box): the culler and the GPU then judge the
// same point (the box's center) for its distance.
bool mesh_instance_instancing_safe(MeshInstance3D *p_mi) {
	if (p_mi->get_extra_cull_margin() > 0.0f || p_mi->get_custom_aabb().has_surface() || p_mi->get_custom_aabb().has_volume()) {
		return false;
	}
	const Ref<Mesh> mesh = p_mi->get_mesh();
	if (!material_instancing_safe(p_mi->get_material_override())) {
		return false;
	}
	for (int s = 0; s < mesh->get_surface_count(); s++) {
		if (p_mi->get_surface_override_material(s).is_valid()) {
			return false; // (a MultiMesh has no per-surface materials of its own)
		}
		if (p_mi->get_material_override().is_null() && !material_instancing_safe(mesh->surface_get_material(s))) {
			return false;
		}
	}
	return true;
}

int mesh_triangles(const Ref<Mesh> &p_mesh) {
	int64_t n = 0;
	for (int s = 0; s < p_mesh->get_surface_count(); s++) {
		const int idx = p_mesh->surface_get_array_index_len(s);
		n += (idx > 0 ? idx : p_mesh->surface_get_array_len(s)) / 3;
	}
	return (int)MIN(n, (int64_t)INT32_MAX);
}

// A MultiMeshInstance3D below the node that can hand over to impostors: its instances must be culled one by one (its
// box the culler's, its materials not moving vertices), nothing per instance the impostor would lose but colour.
bool multimesh_instance_impostor_safe(MultiMeshInstance3D *p_mmi, int p_min_triangles) {
	if (!p_mmi->is_visible_in_tree() || p_mmi->is_in_group(SNAME("fellow_hlod_exclude")) || p_mmi->is_set_as_top_level()) {
		return false;
	}
	const Ref<MultiMesh> mm = p_mmi->get_multimesh();
	if (mm.is_null() || mm->get_transform_format() != MultiMesh::TRANSFORM_3D || mm->get_instance_count() <= 0 || mm->get_mesh().is_null()) {
		return false;
	}
	if (p_mmi->get_visibility_range_begin() > 0.0f || p_mmi->get_visibility_range_end() > 0.0f || !p_mmi->get_visibility_parent().is_empty() || p_mmi->get_extra_cull_margin() > 0.0f || p_mmi->get_custom_aabb().has_volume() || p_mmi->get_material_overlay().is_valid() || p_mmi->get_transparency() > 0.0f) {
		return false;
	}
	const Ref<Mesh> mesh = mm->get_mesh();
	if (mesh_triangles(mesh) < p_min_triangles || mesh->get_blend_shape_count() > 0) {
		return false;
	}
	if (!material_instancing_safe(p_mmi->get_material_override())) {
		return false;
	}
	if (p_mmi->get_material_override().is_null()) {
		for (int s = 0; s < mesh->get_surface_count(); s++) {
			if (!material_instancing_safe(mesh->surface_get_material(s))) {
				return false;
			}
		}
	}
	return true;
}

void collect_multimeshes(Node *p_node, LocalVector<MultiMeshInstance3D *> &r_out) {
	for (int i = 0; i < p_node->get_child_count(false); i++) {
		Node *c = p_node->get_child(i, false);
		MultiMeshInstance3D *mmi = Object::cast_to<MultiMeshInstance3D>(c);
		if (mmi) {
			r_out.push_back(mmi);
		}
		collect_multimeshes(c, r_out);
	}
}

} // namespace

void FellowHLOD3D::build() {
	ERR_FAIL_COND_MSG(!is_inside_tree(), "FellowHLOD3D: build() needs the node in the scene tree.");
	clear();
	generation++;
	const uint64_t t0 = OS::get_singleton()->get_ticks_usec();
	// Impostors (decision 0122): baked one view per frame by an internal baker; applied when done.
	const bool impostors = impostor_distance > 0.0 && Fellow::multimesh_culling();
	int impostor_bakes = 0;
	auto queue_impostor = [&](const Ref<Mesh> &p_mesh, const Ref<Material> &p_override, uint32_t p_index) {
		if (!baker) {
			baker = memnew(FellowImpostorBaker);
			baker->set_name("ImpostorBaker");
			add_child(baker, false, INTERNAL_MODE_BACK);
		}
		baker->queue_bake(p_mesh, p_override, impostor_frames, impostor_resolution, callable_mp(this, &FellowHLOD3D::_impostor_baked).bind(generation, p_index));
		impostor_bakes++;
	};

	LocalVector<MeshInstance3D *> candidates;
	collect(this, this, candidates);
	const Transform3D to_local = get_global_transform().affine_inverse();

	// 1. Instanced proxies: meshes repeated at least instance_threshold times (same mesh, material override, per-instance
	// settings), with materials the GPU can cull one by one, while MultiMesh culling is on.
	LocalVector<MeshInstance3D *> rest;
	int instanced_members = 0, instanced_groups = 0;
	if (instancing && Fellow::multimesh_culling()) {
		HashMap<String, LocalVector<MeshInstance3D *>> groups;
		LocalVector<String> order;
		for (MeshInstance3D *mi : candidates) {
			// (A mirroring transform would need the instance's triangles turned over: merged instead.)
			if (!mesh_instance_instancing_safe(mi) || (to_local * mi->get_global_transform()).basis.determinant() <= 0.0) {
				rest.push_back(mi);
				continue;
			}
			const Ref<Material> mo = mi->get_material_override();
			const String key = vformat("%d/%d/%d/%d/%d", (int64_t)(uint64_t)mi->get_mesh()->get_instance_id(), mo.is_valid() ? (int64_t)(uint64_t)mo->get_instance_id() : (int64_t)0, (int)mi->get_cast_shadows_setting(), (int64_t)mi->get_layer_mask(), (int)mi->get_gi_mode());
			HashMap<String, LocalVector<MeshInstance3D *>>::Iterator it = groups.find(key);
			if (it == groups.end()) {
				it = groups.insert(key, LocalVector<MeshInstance3D *>());
				order.push_back(key);
			}
			it->value.push_back(mi);
		}
		for (const String &key : order) {
			LocalVector<MeshInstance3D *> &g = groups[key];
			if ((int)g.size() < MAX(1, instance_threshold)) {
				for (MeshInstance3D *mi : g) {
					rest.push_back(mi);
				}
				continue;
			}
			MeshInstance3D *first = g[0];
			Ref<MultiMesh> mm;
			mm.instantiate();
			mm->set_transform_format(MultiMesh::TRANSFORM_3D);
			mm->set_mesh(first->get_mesh());
			mm->set_instance_count(g.size());
			for (uint32_t i = 0; i < g.size(); i++) {
				mm->set_instance_transform(i, to_local * g[i]->get_global_transform());
			}
			MultiMeshInstance3D *proxy = memnew(MultiMeshInstance3D);
			proxy->set_name(vformat("HLOD_instanced_%d", instanced_groups));
			proxy->set_multimesh(mm);
			proxy->set_material_override(first->get_material_override());
			proxy->set_cast_shadows_setting(first->get_cast_shadows_setting());
			proxy->set_layer_mask(first->get_layer_mask());
			proxy->set_gi_mode(first->get_gi_mode());
			add_child(proxy, false, INTERNAL_MODE_BACK);
			Instanced in;
			in.proxy = proxy;
			instanced.push_back(in);
			if (impostors && impostor_distance > distance && mesh_triangles(first->get_mesh()) >= impostor_min_triangles) {
				queue_impostor(first->get_mesh(), first->get_material_override(), instanced.size() - 1);
			}
			instanced_groups++;
			for (MeshInstance3D *mi : g) {
				// Drawn by the camera up to the distance, its instance from it on (no hysteresis: both draw the same
				// mesh, material and transform, so the switch does not show).
				Member m;
				m.id = mi->get_instance_id();
				m.instanced = true;
				m.old_range_end = mi->get_visibility_range_end();
				m.old_range_end_margin = mi->get_visibility_range_end_margin();
				members.push_back(m);
				mi->set_visibility_range_end(distance);
				mi->set_visibility_range_end_margin(0.0);
				instanced_members++;
			}
		}
	} else {
		rest = candidates;
	}

	// 2. Merged proxies: the other meshes, per cell and per set of instance settings.
	const real_t cell = MAX((real_t)0.01, cell_size);
	HashMap<ProxyKey, uint32_t, ProxyKeyHasher> proxy_of;
	LocalVector<Proxy> cells;
	for (MeshInstance3D *mi : rest) {
		const Transform3D xf = to_local * mi->get_global_transform();
		const AABB box = xf.xform(mi->get_mesh()->get_aabb());
		const Vector3 c = box.get_center() / cell;
		ProxyKey key;
		key.cell = Vector3i((int)Math::floor(c.x), (int)Math::floor(c.y), (int)Math::floor(c.z));
		key.cast_shadows = (int)mi->get_cast_shadows_setting();
		key.layers = mi->get_layer_mask();
		key.gi_mode = (int)mi->get_gi_mode();
		HashMap<ProxyKey, uint32_t, ProxyKeyHasher>::Iterator it = proxy_of.find(key);
		uint32_t p;
		if (it == proxy_of.end()) {
			p = cells.size();
			cells.push_back(Proxy());
			cells[p].key = key;
			proxy_of.insert(key, p);
		} else {
			p = it->value;
		}
		cells[p].members.push_back(mi);
	}

	// Meshes shared by many nodes are read once.
	HashMap<uint64_t, SourceSurface> source_cache; // mesh id * 64 + surface
	int merged_members = 0, merged_count = 0, surface_count = 0, skipped_cells = 0;
	int64_t triangles = 0, lod_triangles = 0;
	for (Proxy &px : cells) {
		if ((int)px.members.size() < MAX(1, min_meshes)) {
			skipped_cells++;
			continue;
		}
		LocalVector<MeshInstance3D *> merged;
		for (MeshInstance3D *mi : px.members) {
			const Ref<Mesh> mesh = mi->get_mesh();
			const Transform3D xf = to_local * mi->get_global_transform();
			const Ref<Material> override = mi->get_material_override();
			bool all_ok = true;
			LocalVector<const SourceSurface *> srcs;
			for (int s = 0; s < mesh->get_surface_count(); s++) {
				const uint64_t key = (uint64_t)mesh->get_instance_id() * 64 + (uint64_t)MIN(s, 63);
				HashMap<uint64_t, SourceSurface>::Iterator it = source_cache.find(key);
				if (it == source_cache.end()) {
					it = source_cache.insert(key, read_surface(mesh, s));
				}
				if (!it->value.valid) {
					all_ok = false;
					break;
				}
				srcs.push_back(&it->value);
			}
			if (!all_ok) {
				continue; // left as it is (no visibility parent)
			}
			for (int s = 0; s < mesh->get_surface_count(); s++) {
				Ref<Material> mat = override;
				if (mat.is_null()) {
					mat = mi->get_surface_override_material(s);
				}
				if (mat.is_null()) {
					mat = mesh->surface_get_material(s);
				}
				const SourceSurface &src = *srcs[s];
				const uint64_t skey = (mat.is_valid() ? (uint64_t)mat->get_instance_id() : (uint64_t)0) * 32 + src.attributes;
				HashMap<uint64_t, uint32_t>::Iterator st = px.surface_of.find(skey);
				uint32_t si;
				if (st == px.surface_of.end()) {
					si = px.surfaces.size();
					px.surfaces.push_back(Accum());
					px.surfaces[si].material = mat;
					px.surfaces[si].attributes = src.attributes;
					px.surface_of.insert(skey, si);
				} else {
					si = st->value;
				}
				append_surface(px.surfaces[si], src, xf);
			}
			merged.push_back(mi);
		}
		if ((int)merged.size() < MAX(1, min_meshes) || px.surfaces.is_empty()) {
			skipped_cells++;
			continue;
		}

		Ref<ArrayMesh> mesh;
		if (lods) {
			Ref<ImporterMesh> im;
			im.instantiate();
			for (const Accum &a : px.surfaces) {
				im->add_surface(Mesh::PRIMITIVE_TRIANGLES, accum_arrays(a), TypedArray<Array>(), Dictionary(), a.material);
			}
			im->generate_lods(Math::deg_to_rad(60.0), Array());
			mesh = im->get_mesh();
			for (int s = 0; s < im->get_surface_count(); s++) {
				for (int l = 0; l < im->get_surface_lod_count(s); l++) {
					lod_triangles += im->get_surface_lod_indices(s, l).size() / 3;
				}
			}
		} else {
			mesh.instantiate();
			for (const Accum &a : px.surfaces) {
				mesh->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, accum_arrays(a));
				mesh->surface_set_material(mesh->get_surface_count() - 1, a.material);
			}
		}
		for (const Accum &a : px.surfaces) {
			triangles += a.indices.size() / 3;
		}
		surface_count += px.surfaces.size();

		MeshInstance3D *proxy = memnew(MeshInstance3D);
		proxy->set_name(vformat("HLOD_%d_%d_%d", px.key.cell.x, px.key.cell.y, px.key.cell.z));
		proxy->set_mesh(mesh);
		proxy->set_cast_shadows_setting((GeometryInstance3D::ShadowCastingSetting)px.key.cast_shadows);
		proxy->set_layer_mask(px.key.layers);
		proxy->set_gi_mode((GeometryInstance3D::GIMode)px.key.gi_mode);
		proxy->set_visibility_range_begin(distance);
		proxy->set_visibility_range_begin_margin(margin);
		proxy->set_visibility_range_fade_mode(GeometryInstance3D::VISIBILITY_RANGE_FADE_DISABLED);
		add_child(proxy, false, INTERNAL_MODE_BACK);
		merged_proxies.push_back(proxy);
		merged_count++;
		for (MeshInstance3D *mi : merged) {
			Member m;
			m.id = mi->get_instance_id();
			m.old_parent = mi->get_visibility_parent();
			members.push_back(m);
			mi->set_visibility_parent(mi->get_path_to(proxy));
			merged_members++;
		}
	}
	// MultiMeshInstance3D nodes below this node: their far instances handed over to impostors.
	int impostor_multimeshes = 0;
	if (impostors) {
		LocalVector<MultiMeshInstance3D *> mmis;
		collect_multimeshes(this, mmis);
		for (MultiMeshInstance3D *mmi : mmis) {
			bool own = false;
			for (const Instanced &in : instanced) {
				own = own || in.proxy == mmi;
			}
			if (own || !multimesh_instance_impostor_safe(mmi, impostor_min_triangles)) {
				continue;
			}
			Instanced in;
			in.original = mmi->get_instance_id();
			instanced.push_back(in);
			queue_impostor(mmi->get_multimesh()->get_mesh(), mmi->get_material_override(), instanced.size() - 1);
			impostor_multimeshes++;
		}
	}
	built = true;
	_apply_roles();

	build_info.clear();
	build_info["candidates"] = (int)candidates.size();
	build_info["instanced_members"] = instanced_members;
	build_info["instanced_proxies"] = instanced_groups;
	build_info["merged_members"] = merged_members;
	build_info["merged_proxies"] = merged_count;
	build_info["merged_surfaces"] = surface_count;
	build_info["cells_skipped"] = skipped_cells;
	build_info["merged_triangles"] = triangles;
	build_info["lod_triangles"] = lod_triangles;
	build_info["impostor_bakes"] = impostor_bakes;
	build_info["impostor_multimeshes"] = impostor_multimeshes;
	build_info["build_msec"] = double(OS::get_singleton()->get_ticks_usec() - t0) / 1000.0;
}

void FellowHLOD3D::_impostor_baked(const Ref<ArrayMesh> &p_quad, uint32_t p_generation, uint32_t p_index) {
	if (p_generation != generation || !built || p_index >= instanced.size() || p_quad.is_null()) {
		return;
	}
	Instanced &in = instanced[p_index];
	MultiMeshInstance3D *source = in.proxy ? in.proxy : ObjectDB::get_instance<MultiMeshInstance3D>(in.original);
	if (!source || source->get_multimesh().is_null()) {
		return;
	}
	const Ref<MultiMesh> src = source->get_multimesh();
	Ref<MultiMesh> mm;
	mm.instantiate();
	mm->set_transform_format(MultiMesh::TRANSFORM_3D);
	mm->set_mesh(p_quad);
	mm->set_instance_count(src->get_instance_count());
	for (int i = 0; i < src->get_instance_count(); i++) {
		mm->set_instance_transform(i, src->get_instance_transform(i));
	}
	MultiMeshInstance3D *imp = memnew(MultiMeshInstance3D);
	imp->set_name("HLOD_impostors");
	imp->set_multimesh(mm);
	imp->set_cast_shadows_setting(source->get_cast_shadows_setting());
	imp->set_layer_mask(source->get_layer_mask());
	imp->set_gi_mode(source->get_gi_mode());
	// Under its source, so its instances sit where the source's do (the node's own proxies are at its origin).
	source->add_child(imp, false, INTERNAL_MODE_BACK);
	in.impostor = imp;
	_apply_roles();
}

bool FellowHLOD3D::is_baking() const {
	return baker && baker->is_busy();
}

void FellowHLOD3D::clear() {
	RenderingServer *rs = RenderingServer::get_singleton();
	for (const Member &m : members) {
		MeshInstance3D *mi = ObjectDB::get_instance<MeshInstance3D>(m.id);
		if (!mi) {
			continue;
		}
		rs->fellow_instance_set_hlod(mi->get_instance(), RenderingServer::FELLOW_HLOD_NONE, 0.0f, 0.0f);
		if (m.instanced) {
			mi->set_visibility_range_end(m.old_range_end);
			mi->set_visibility_range_end_margin(m.old_range_end_margin);
		} else {
			mi->set_visibility_parent(m.old_parent);
		}
	}
	members.clear();
	for (GeometryInstance3D *p : merged_proxies) {
		if (p->get_parent() == this) {
			remove_child(p);
		}
		memdelete(p);
	}
	merged_proxies.clear();
	for (Instanced &in : instanced) {
		if (in.impostor) {
			if (in.impostor->get_parent()) {
				in.impostor->get_parent()->remove_child(in.impostor);
			}
			memdelete(in.impostor);
		}
		MultiMeshInstance3D *original = ObjectDB::get_instance<MultiMeshInstance3D>(in.original);
		if (original) {
			rs->fellow_instance_set_hlod(original->get_instance(), RenderingServer::FELLOW_HLOD_NONE, 0.0f, 0.0f);
			rs->fellow_instance_set_instance_range(original->get_instance(), 0.0f, 0.0f, false);
		}
		if (in.proxy) {
			if (in.proxy->get_parent() == this) {
				remove_child(in.proxy);
			}
			memdelete(in.proxy);
		}
	}
	instanced.clear();
	generation++;
	built = false;
	build_info.clear();
}

void FellowHLOD3D::_queue_build() {
	if (build_queued) {
		return;
	}
	build_queued = true;
	callable_mp(this, &FellowHLOD3D::_deferred_build).call_deferred();
}

void FellowHLOD3D::_deferred_build() {
	build_queued = false;
	if (is_inside_tree()) {
		build();
	}
}

void FellowHLOD3D::_apply_roles() {
	// The renderer is told which instances are proxies and which follow one, with the distance. Instanced proxies always
	// (the GPU draws their instances from the distance on; their members' own range ends there), with the per-cascade
	// shadows; merged proxies with SHADOW_MODE_PER_CASCADE: a directional cascade starting beyond the distance draws the
	// proxies, the others the meshes (camera-independent, so the shadow cache can keep them). SHADOW_MODE_FOLLOW_CAMERA:
	// stock's visibility dependencies in every pass.
	// Impostors (once baked) take over from impostor_distance on: the instanced proxy (or the MultiMesh below the node)
	// draws its instances up to it, the impostors from it, per instance on the GPU and per cascade in the shadows.
	RenderingServer *rs = RenderingServer::get_singleton();
	const bool per_cascade = shadow_mode == SHADOW_MODE_PER_CASCADE;
	for (const Instanced &in : instanced) {
		const float hand_over = in.impostor ? (float)impostor_distance : 0.0f;
		if (in.proxy) {
			rs->fellow_instance_set_hlod(in.proxy->get_instance(), RenderingServer::FELLOW_HLOD_PROXY, distance, hand_over);
			rs->fellow_instance_set_instance_range(in.proxy->get_instance(), distance, hand_over, false);
		}
		MultiMeshInstance3D *original = ObjectDB::get_instance<MultiMeshInstance3D>(in.original);
		if (original) {
			rs->fellow_instance_set_hlod(original->get_instance(), in.impostor ? RenderingServer::FELLOW_HLOD_MEMBER : RenderingServer::FELLOW_HLOD_NONE, hand_over, 0.0f);
			rs->fellow_instance_set_instance_range(original->get_instance(), 0.0f, hand_over, false);
		}
		if (in.impostor) {
			rs->fellow_instance_set_hlod(in.impostor->get_instance(), RenderingServer::FELLOW_HLOD_PROXY, impostor_distance, 0.0f);
			rs->fellow_instance_set_instance_range(in.impostor->get_instance(), impostor_distance, 0.0f, true);
		}
	}
	for (GeometryInstance3D *p : merged_proxies) {
		rs->fellow_instance_set_hlod(p->get_instance(), per_cascade ? RenderingServer::FELLOW_HLOD_PROXY : RenderingServer::FELLOW_HLOD_NONE, distance, 0.0f);
	}
	for (const Member &m : members) {
		MeshInstance3D *mi = ObjectDB::get_instance<MeshInstance3D>(m.id);
		if (mi) {
			rs->fellow_instance_set_hlod(mi->get_instance(), (per_cascade || m.instanced) ? RenderingServer::FELLOW_HLOD_MEMBER : RenderingServer::FELLOW_HLOD_NONE, distance, 0.0f);
		}
	}
}

void FellowHLOD3D::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_READY: {
			if (build_on_ready && !built) {
				_queue_build();
			}
		} break;
	}
}

void FellowHLOD3D::set_distance(real_t p_distance) {
	distance = MAX((real_t)0.0, p_distance);
	for (GeometryInstance3D *p : merged_proxies) {
		p->set_visibility_range_begin(distance);
	}
	for (const Member &m : members) {
		MeshInstance3D *mi = m.instanced ? ObjectDB::get_instance<MeshInstance3D>(m.id) : nullptr;
		if (mi) {
			mi->set_visibility_range_end(distance);
		}
	}
	if (built) {
		_apply_roles();
	}
}

real_t FellowHLOD3D::get_distance() const {
	return distance;
}

void FellowHLOD3D::set_margin(real_t p_margin) {
	margin = MAX((real_t)0.0, p_margin);
	for (GeometryInstance3D *p : merged_proxies) {
		p->set_visibility_range_begin_margin(margin);
	}
}

real_t FellowHLOD3D::get_margin() const {
	return margin;
}

void FellowHLOD3D::set_cell_size(real_t p_size) {
	cell_size = MAX((real_t)0.01, p_size);
}

real_t FellowHLOD3D::get_cell_size() const {
	return cell_size;
}

void FellowHLOD3D::set_min_meshes(int p_count) {
	min_meshes = MAX(1, p_count);
}

int FellowHLOD3D::get_min_meshes() const {
	return min_meshes;
}

void FellowHLOD3D::set_instancing(bool p_enable) {
	instancing = p_enable;
}

bool FellowHLOD3D::get_instancing() const {
	return instancing;
}

void FellowHLOD3D::set_instance_threshold(int p_count) {
	instance_threshold = MAX(1, p_count);
}

int FellowHLOD3D::get_instance_threshold() const {
	return instance_threshold;
}

void FellowHLOD3D::set_generate_lods(bool p_enable) {
	lods = p_enable;
}

bool FellowHLOD3D::get_generate_lods() const {
	return lods;
}

void FellowHLOD3D::set_build_on_ready(bool p_enable) {
	build_on_ready = p_enable;
}

bool FellowHLOD3D::get_build_on_ready() const {
	return build_on_ready;
}

void FellowHLOD3D::set_shadow_mode(ShadowMode p_mode) {
	shadow_mode = p_mode;
	if (built) {
		_apply_roles();
	}
}

FellowHLOD3D::ShadowMode FellowHLOD3D::get_shadow_mode() const {
	return shadow_mode;
}

void FellowHLOD3D::set_impostor_distance(real_t p_distance) {
	impostor_distance = MAX((real_t)0.0, p_distance);
	if (built) {
		_apply_roles();
	}
}

real_t FellowHLOD3D::get_impostor_distance() const {
	return impostor_distance;
}

void FellowHLOD3D::set_impostor_frames(int p_frames) {
	impostor_frames = CLAMP(p_frames, 2, 32);
}

int FellowHLOD3D::get_impostor_frames() const {
	return impostor_frames;
}

void FellowHLOD3D::set_impostor_resolution(int p_resolution) {
	impostor_resolution = CLAMP(p_resolution, 16, 1024);
}

int FellowHLOD3D::get_impostor_resolution() const {
	return impostor_resolution;
}

void FellowHLOD3D::set_impostor_min_triangles(int p_count) {
	impostor_min_triangles = MAX(0, p_count);
}

int FellowHLOD3D::get_impostor_min_triangles() const {
	return impostor_min_triangles;
}

FellowHLOD3D::~FellowHLOD3D() {
	members.clear(); // the proxies are children: freed with the node
	merged_proxies.clear();
	instanced.clear();
}

void FellowHLOD3D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_distance", "distance"), &FellowHLOD3D::set_distance);
	ClassDB::bind_method(D_METHOD("get_distance"), &FellowHLOD3D::get_distance);
	ClassDB::bind_method(D_METHOD("set_margin", "margin"), &FellowHLOD3D::set_margin);
	ClassDB::bind_method(D_METHOD("get_margin"), &FellowHLOD3D::get_margin);
	ClassDB::bind_method(D_METHOD("set_cell_size", "size"), &FellowHLOD3D::set_cell_size);
	ClassDB::bind_method(D_METHOD("get_cell_size"), &FellowHLOD3D::get_cell_size);
	ClassDB::bind_method(D_METHOD("set_min_meshes", "count"), &FellowHLOD3D::set_min_meshes);
	ClassDB::bind_method(D_METHOD("get_min_meshes"), &FellowHLOD3D::get_min_meshes);
	ClassDB::bind_method(D_METHOD("set_instancing", "enable"), &FellowHLOD3D::set_instancing);
	ClassDB::bind_method(D_METHOD("get_instancing"), &FellowHLOD3D::get_instancing);
	ClassDB::bind_method(D_METHOD("set_instance_threshold", "count"), &FellowHLOD3D::set_instance_threshold);
	ClassDB::bind_method(D_METHOD("get_instance_threshold"), &FellowHLOD3D::get_instance_threshold);
	ClassDB::bind_method(D_METHOD("set_impostor_distance", "distance"), &FellowHLOD3D::set_impostor_distance);
	ClassDB::bind_method(D_METHOD("get_impostor_distance"), &FellowHLOD3D::get_impostor_distance);
	ClassDB::bind_method(D_METHOD("set_impostor_frames", "frames"), &FellowHLOD3D::set_impostor_frames);
	ClassDB::bind_method(D_METHOD("get_impostor_frames"), &FellowHLOD3D::get_impostor_frames);
	ClassDB::bind_method(D_METHOD("set_impostor_resolution", "resolution"), &FellowHLOD3D::set_impostor_resolution);
	ClassDB::bind_method(D_METHOD("get_impostor_resolution"), &FellowHLOD3D::get_impostor_resolution);
	ClassDB::bind_method(D_METHOD("set_impostor_min_triangles", "count"), &FellowHLOD3D::set_impostor_min_triangles);
	ClassDB::bind_method(D_METHOD("get_impostor_min_triangles"), &FellowHLOD3D::get_impostor_min_triangles);
	ClassDB::bind_method(D_METHOD("is_baking"), &FellowHLOD3D::is_baking);
	ClassDB::bind_method(D_METHOD("set_generate_lods", "enable"), &FellowHLOD3D::set_generate_lods);
	ClassDB::bind_method(D_METHOD("get_generate_lods"), &FellowHLOD3D::get_generate_lods);
	ClassDB::bind_method(D_METHOD("set_build_on_ready", "enable"), &FellowHLOD3D::set_build_on_ready);
	ClassDB::bind_method(D_METHOD("get_build_on_ready"), &FellowHLOD3D::get_build_on_ready);
	ClassDB::bind_method(D_METHOD("set_shadow_mode", "mode"), &FellowHLOD3D::set_shadow_mode);
	ClassDB::bind_method(D_METHOD("get_shadow_mode"), &FellowHLOD3D::get_shadow_mode);
	ClassDB::bind_method(D_METHOD("build"), &FellowHLOD3D::build);
	ClassDB::bind_method(D_METHOD("clear"), &FellowHLOD3D::clear);
	ClassDB::bind_method(D_METHOD("is_built"), &FellowHLOD3D::is_built);
	ClassDB::bind_method(D_METHOD("get_build_info"), &FellowHLOD3D::get_build_info);

	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "distance", PROPERTY_HINT_RANGE, "0,10000,0.1,or_greater,suffix:m"), "set_distance", "get_distance");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "margin", PROPERTY_HINT_RANGE, "0,100,0.1,or_greater,suffix:m"), "set_margin", "get_margin");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "instancing"), "set_instancing", "get_instancing");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "instance_threshold", PROPERTY_HINT_RANGE, "1,100000,1,or_greater"), "set_instance_threshold", "get_instance_threshold");
	ADD_GROUP("Impostors", "impostor_");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "impostor_distance", PROPERTY_HINT_RANGE, "0,10000,0.1,or_greater,suffix:m"), "set_impostor_distance", "get_impostor_distance");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "impostor_frames", PROPERTY_HINT_RANGE, "2,32,1"), "set_impostor_frames", "get_impostor_frames");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "impostor_resolution", PROPERTY_HINT_RANGE, "16,1024,1,suffix:px"), "set_impostor_resolution", "get_impostor_resolution");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "impostor_min_triangles", PROPERTY_HINT_RANGE, "0,100000,1,or_greater"), "set_impostor_min_triangles", "get_impostor_min_triangles");
	ADD_GROUP("", "");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "cell_size", PROPERTY_HINT_RANGE, "1,1000,0.1,or_greater,suffix:m"), "set_cell_size", "get_cell_size");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "min_meshes", PROPERTY_HINT_RANGE, "1,1000,1,or_greater"), "set_min_meshes", "get_min_meshes");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "generate_lods"), "set_generate_lods", "get_generate_lods");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "build_on_ready"), "set_build_on_ready", "get_build_on_ready");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "shadow_mode", PROPERTY_HINT_ENUM, "Follow Camera,Per Cascade"), "set_shadow_mode", "get_shadow_mode");

	BIND_ENUM_CONSTANT(SHADOW_MODE_FOLLOW_CAMERA);
	BIND_ENUM_CONSTANT(SHADOW_MODE_PER_CASCADE);
}
