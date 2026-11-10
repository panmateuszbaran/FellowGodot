/**************************************************************************/
/*  fellow_cell_proxies.cpp                                               */
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

#include "fellow_cell_proxies.h"

#include "fellow_terrain_3d.h"

#include "core/os/mutex.h"
#include "scene/3d/mesh_instance_3d.h"
#include "scene/3d/multimesh_instance_3d.h"
#include "scene/resources/3d/importer_mesh.h"
#include "scene/resources/image_texture.h"
#include "scene/resources/multimesh.h"

#include "modules/fellow_fluid/fellow_shared_shaders.h"

namespace FellowCellProxies {

static Mutex average_mutex;
static HashMap<ObjectID, Color> average_cache;

Color texture_average(const Ref<Texture2D> &p_texture) {
	if (p_texture.is_null()) {
		return Color(1, 1, 1);
	}
	const ObjectID key = p_texture->get_instance_id();
	{
		MutexLock lock(average_mutex);
		if (const Color *c = average_cache.getptr(key)) {
			return *c;
		}
	}
	Color average(1, 1, 1);
	Ref<Image> img = p_texture->get_image();
	if (img.is_valid() && !img->is_empty()) {
		// A small mip (a few texels wide, a compressed format's blocks whole), decompressed.
		Ref<Image> small = img;
		if (img->has_mipmaps()) {
			int level = img->get_mipmap_count();
			while (level > 0) {
				int64_t ofs, size;
				int w, h;
				img->get_mipmap_offset_size_and_dimensions(level, ofs, size, w, h);
				if (w >= 8 && h >= 8) {
					break;
				}
				level--;
			}
			if (level > 0) {
				small = img->get_image_from_mipmap(level);
			}
		}
		small = small->duplicate();
		if (small->is_compressed()) {
			small->decompress();
		}
		if (!small->is_compressed() && !small->is_empty()) {
			small->clear_mipmaps();
			if (small->get_width() > 32 || small->get_height() > 32) {
				small->resize(32, 32, Image::INTERPOLATE_BILINEAR);
			}
			Color sum(0, 0, 0, 0);
			const int n = small->get_width() * small->get_height();
			for (int y = 0; y < small->get_height(); y++) {
				for (int x = 0; x < small->get_width(); x++) {
					sum += small->get_pixel(x, y).srgb_to_linear(); // an albedo texture is sRGB
				}
			}
			if (n > 0) {
				average = Color(sum.r / n, sum.g / n, sum.b / n, 1.0f);
			}
		}
	}
	MutexLock lock(average_mutex);
	average_cache.insert(key, average);
	return average;
}

// The linear albedo a material draws, on average.
static Color _material_albedo(const Ref<Material> &p_material) {
	Ref<BaseMaterial3D> bm = p_material;
	if (bm.is_valid()) {
		Color c = bm->get_albedo().srgb_to_linear();
		const Ref<Texture2D> tex = bm->get_texture(BaseMaterial3D::TEXTURE_ALBEDO);
		if (tex.is_valid()) {
			c *= texture_average(tex);
		}
		return Color(c.r, c.g, c.b, 1.0f);
	}
	return Color(0.5f, 0.5f, 0.5f, 1.0f);
}

int mesh_triangles(const Ref<Mesh> &p_mesh) {
	if (p_mesh.is_null()) {
		return 0;
	}
	int n = 0;
	for (int s = 0; s < p_mesh->get_surface_count(); s++) {
		if (p_mesh->surface_get_primitive_type(s) != Mesh::PRIMITIVE_TRIANGLES) {
			continue;
		}
		const int indices = p_mesh->surface_get_array_index_len(s);
		n += (indices > 0 ? indices : p_mesh->surface_get_array_len(s)) / 3;
	}
	return n;
}

namespace {

struct Merged {
	PackedVector3Array vertices;
	PackedVector3Array normals;
	PackedColorArray colors;
	PackedInt32Array indices;
};

// A surface's triangles into p_merged, under p_xform, in one colour.
void _append(Merged &r_merged, const Array &p_arrays, const Transform3D &p_xform, const Color &p_color) {
	if (p_arrays.size() != Mesh::ARRAY_MAX || p_arrays[Mesh::ARRAY_BONES].get_type() != Variant::NIL) {
		return;
	}
	const PackedVector3Array verts = p_arrays[Mesh::ARRAY_VERTEX];
	if (verts.is_empty()) {
		return;
	}
	const PackedVector3Array normals = p_arrays[Mesh::ARRAY_NORMAL];
	const PackedInt32Array indices = p_arrays[Mesh::ARRAY_INDEX];
	const int base = r_merged.vertices.size();
	const Basis nb = p_xform.basis.inverse().transposed();
	const bool mirrored = p_xform.basis.determinant() < 0.0f;
	for (int i = 0; i < verts.size(); i++) {
		r_merged.vertices.push_back(p_xform.xform(verts[i]));
		r_merged.normals.push_back(normals.size() == verts.size() ? nb.xform(normals[i]).normalized() : Vector3(0, 1, 0));
		r_merged.colors.push_back(p_color);
	}
	const int n = indices.is_empty() ? verts.size() : indices.size();
	for (int i = 0; i + 2 < n; i += 3) {
		const int a = indices.is_empty() ? i : indices[i];
		const int b = indices.is_empty() ? i + 1 : indices[i + 1];
		const int c = indices.is_empty() ? i + 2 : indices[i + 2];
		r_merged.indices.push_back(base + a);
		r_merged.indices.push_back(base + (mirrored ? c : b));
		r_merged.indices.push_back(base + (mirrored ? b : c));
	}
}

void _append_mesh(Merged &r_merged, const Ref<Mesh> &p_mesh, const Transform3D &p_xform, const Ref<Material> &p_override, const MeshInstance3D *p_mi) {
	for (int s = 0; s < p_mesh->get_surface_count(); s++) {
		if (p_mesh->surface_get_primitive_type(s) != Mesh::PRIMITIVE_TRIANGLES) {
			continue;
		}
		Ref<Material> m = p_override;
		if (m.is_null() && p_mi && s < p_mi->get_surface_override_material_count()) {
			m = p_mi->get_surface_override_material(s);
		}
		if (m.is_null()) {
			m = p_mesh->surface_get_material(s);
		}
		_append(r_merged, p_mesh->surface_get_arrays(s), p_xform, _material_albedo(m));
	}
}

// The indices of the LOD with at most p_ratio of the triangles (else the coarsest), its vertices compacted: a map from
// the old vertices to the new ones (-1: unused).
PackedInt32Array _choose_lod(const Ref<ImporterMesh> &p_mesh, int p_surface, const PackedInt32Array &p_full, float p_ratio) {
	PackedInt32Array best = p_full;
	const int lods = p_mesh->get_surface_lod_count(p_surface);
	for (int l = 0; l < lods; l++) {
		const Vector<int> idx = p_mesh->get_surface_lod_indices(p_surface, l);
		if (idx.is_empty()) {
			continue;
		}
		best = PackedInt32Array(idx);
		if (idx.size() <= int(p_full.size() * p_ratio)) {
			break;
		}
	}
	return best;
}

void _compact(const PackedInt32Array &p_indices, int p_vertex_count, LocalVector<int> &r_remap, LocalVector<int> &r_used, PackedInt32Array &r_indices) {
	r_remap.resize(p_vertex_count);
	for (int i = 0; i < p_vertex_count; i++) {
		r_remap[i] = -1;
	}
	r_used.clear();
	r_indices.resize(p_indices.size());
	int32_t *w = r_indices.ptrw();
	for (int i = 0; i < p_indices.size(); i++) {
		const int v = p_indices[i];
		if (r_remap[v] < 0) {
			r_remap[v] = r_used.size();
			r_used.push_back(v);
		}
		w[i] = r_remap[v];
	}
}

template <typename T>
T _gather(const T &p_src, const LocalVector<int> &p_used, int p_per_vertex = 1) {
	T out;
	out.resize(p_used.size() * p_per_vertex);
	auto *w = out.ptrw();
	for (uint32_t i = 0; i < p_used.size(); i++) {
		for (int k = 0; k < p_per_vertex; k++) {
			w[i * p_per_vertex + k] = p_src[p_used[i] * p_per_vertex + k];
		}
	}
	return out;
}

} // namespace

Ref<StandardMaterial3D> proxy_material() {
	// Shared for the session, released with FellowEngine's shared shaders while the RenderingServer lives.
	Ref<StandardMaterial3D> material = FellowSharedShaders::resource("fellow_cell_proxy_material");
	if (material.is_null()) {
		material.instantiate();
		material->set_flag(BaseMaterial3D::FLAG_ALBEDO_FROM_VERTEX_COLOR, true);
		material->set_roughness(0.9f);
		material->set_name("FellowCellProxy");
		FellowSharedShaders::store("fellow_cell_proxy_material", material);
	}
	return material;
}

Ref<ArrayMesh> bake_proxy(Node *p_content, float p_ratio, int p_terrain_quads, Dictionary &r_info) {
	Merged merged;
	int source_triangles = 0;
	int skipped_instances = 0;
	struct Item {
		Node *node;
		Transform3D xform;
	};
	LocalVector<Item> stack;
	Node3D *root3d = Object::cast_to<Node3D>(p_content);
	stack.push_back({ p_content, root3d ? root3d->get_transform() : Transform3D() });
	while (!stack.is_empty()) {
		const Item it = stack[stack.size() - 1];
		stack.resize(stack.size() - 1);
		Node3D *n3 = Object::cast_to<Node3D>(it.node);
		if (n3 && !n3->is_visible()) {
			continue;
		}
		if (FellowTerrain3D *t = Object::cast_to<FellowTerrain3D>(it.node)) {
			t->prepare_data();
			const Array arrays = t->make_proxy_arrays(p_terrain_quads);
			if (arrays.size() == Mesh::ARRAY_MAX) {
				const PackedColorArray colors = arrays[Mesh::ARRAY_COLOR];
				Merged part;
				_append(part, arrays, it.xform, Color(1, 1, 1));
				// Its own colours per vertex.
				const int base = merged.vertices.size();
				merged.vertices.append_array(part.vertices);
				merged.normals.append_array(part.normals);
				merged.colors.append_array(colors.size() == part.vertices.size() ? colors : part.colors);
				for (int i = 0; i < part.indices.size(); i++) {
					merged.indices.push_back(base + part.indices[i]);
				}
				source_triangles += part.indices.size() / 3;
			}
		} else if (MeshInstance3D *mi = Object::cast_to<MeshInstance3D>(it.node)) {
			const Ref<Mesh> mesh = mi->get_mesh();
			if (mesh.is_valid() && mi->get_skin().is_null()) {
				_append_mesh(merged, mesh, it.xform, mi->get_material_override(), mi);
				source_triangles += mesh_triangles(mesh);
			}
		} else if (MultiMeshInstance3D *mmi = Object::cast_to<MultiMeshInstance3D>(it.node)) {
			const Ref<MultiMesh> mm = mmi->get_multimesh();
			if (mm.is_valid() && mm->get_mesh().is_valid() && mm->get_transform_format() == MultiMesh::TRANSFORM_3D) {
				// Up to 256 instances (a sample of a larger MultiMesh: far away, its density is what shows).
				const int count = mm->get_visible_instance_count() >= 0 ? mm->get_visible_instance_count() : mm->get_instance_count();
				const int stride = MAX(1, (count + 255) / 256);
				for (int i = 0; i < count; i += stride) {
					_append_mesh(merged, mm->get_mesh(), it.xform * mm->get_instance_transform(i), mmi->get_material_override(), nullptr);
				}
				skipped_instances += count - (count + stride - 1) / stride;
				source_triangles += mesh_triangles(mm->get_mesh()) * count;
			}
		}
		for (int i = 0; i < it.node->get_child_count(); i++) {
			Node *c = it.node->get_child(i);
			Node3D *c3 = Object::cast_to<Node3D>(c);
			stack.push_back({ c, c3 ? it.xform * c3->get_transform() : it.xform });
		}
	}
	r_info["source_triangles"] = source_triangles;
	r_info["sampled_out_instances"] = skipped_instances;
	if (merged.indices.is_empty()) {
		r_info["proxy_triangles"] = 0;
		return Ref<ArrayMesh>();
	}
	// Simplified (meshoptimizer through Godot's LOD generation: vertices welded where their normals are within 60°).
	Array arrays;
	arrays.resize(Mesh::ARRAY_MAX);
	arrays[Mesh::ARRAY_VERTEX] = merged.vertices;
	arrays[Mesh::ARRAY_NORMAL] = merged.normals;
	arrays[Mesh::ARRAY_COLOR] = merged.colors;
	arrays[Mesh::ARRAY_INDEX] = merged.indices;
	Ref<ImporterMesh> im;
	im.instantiate();
	im->add_surface(Mesh::PRIMITIVE_TRIANGLES, arrays);
	im->generate_lods(60.0f, Array());
	const PackedInt32Array chosen = _choose_lod(im, 0, merged.indices, p_ratio);
	LocalVector<int> remap;
	LocalVector<int> used;
	PackedInt32Array indices;
	_compact(chosen, merged.vertices.size(), remap, used, indices);
	Array out;
	out.resize(Mesh::ARRAY_MAX);
	out[Mesh::ARRAY_VERTEX] = _gather(merged.vertices, used);
	out[Mesh::ARRAY_NORMAL] = _gather(merged.normals, used);
	out[Mesh::ARRAY_COLOR] = _gather(merged.colors, used);
	out[Mesh::ARRAY_INDEX] = indices;
	Ref<ArrayMesh> proxy;
	proxy.instantiate();
	proxy->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, out);
	proxy->surface_set_material(0, proxy_material());
	r_info["proxy_triangles"] = indices.size() / 3;
	r_info["proxy_vertices"] = int(used.size());
	return proxy;
}

Ref<ArrayMesh> simplified_mesh(const Ref<Mesh> &p_mesh, float p_ratio) {
	if (p_mesh.is_null() || p_mesh->get_surface_count() == 0) {
		return Ref<ArrayMesh>();
	}
	Ref<ImporterMesh> im;
	im.instantiate();
	LocalVector<Array> sources;
	for (int s = 0; s < p_mesh->get_surface_count(); s++) {
		Array a = p_mesh->surface_get_arrays(s);
		if (p_mesh->surface_get_primitive_type(s) != Mesh::PRIMITIVE_TRIANGLES || a.size() != Mesh::ARRAY_MAX || a[Mesh::ARRAY_BONES].get_type() != Variant::NIL || p_mesh->get_blend_shape_count() > 0) {
			return Ref<ArrayMesh>(); // skinned, blended or not triangles: kept as it is
		}
		for (int c = Mesh::ARRAY_CUSTOM0; c <= Mesh::ARRAY_CUSTOM3; c++) {
			if (a[c].get_type() != Variant::NIL) {
				return Ref<ArrayMesh>();
			}
		}
		if (a[Mesh::ARRAY_INDEX].get_type() == Variant::NIL) {
			const PackedVector3Array v = a[Mesh::ARRAY_VERTEX];
			PackedInt32Array idx;
			idx.resize(v.size());
			for (int i = 0; i < v.size(); i++) {
				idx.write[i] = i;
			}
			a[Mesh::ARRAY_INDEX] = idx;
		}
		sources.push_back(a);
		Ref<ArrayMesh> am = p_mesh;
		im->add_surface(Mesh::PRIMITIVE_TRIANGLES, a, TypedArray<Array>(), Dictionary(), p_mesh->surface_get_material(s), am.is_valid() ? am->surface_get_name(s) : String());
	}
	im->generate_lods(60.0f, Array());
	Ref<ArrayMesh> out;
	out.instantiate();
	int before = 0;
	int after = 0;
	for (int s = 0; s < int(sources.size()); s++) {
		const Array &a = sources[s];
		const PackedInt32Array full = a[Mesh::ARRAY_INDEX];
		const PackedVector3Array verts = a[Mesh::ARRAY_VERTEX];
		const PackedInt32Array chosen = _choose_lod(im, s, full, p_ratio);
		LocalVector<int> remap;
		LocalVector<int> used;
		PackedInt32Array indices;
		_compact(chosen, verts.size(), remap, used, indices);
		Array o;
		o.resize(Mesh::ARRAY_MAX);
		o[Mesh::ARRAY_VERTEX] = _gather(verts, used);
		if (a[Mesh::ARRAY_NORMAL].get_type() != Variant::NIL) {
			o[Mesh::ARRAY_NORMAL] = _gather(PackedVector3Array(a[Mesh::ARRAY_NORMAL]), used);
		}
		if (a[Mesh::ARRAY_TANGENT].get_type() != Variant::NIL) {
			o[Mesh::ARRAY_TANGENT] = _gather(PackedFloat32Array(a[Mesh::ARRAY_TANGENT]), used, 4);
		}
		if (a[Mesh::ARRAY_COLOR].get_type() != Variant::NIL) {
			o[Mesh::ARRAY_COLOR] = _gather(PackedColorArray(a[Mesh::ARRAY_COLOR]), used);
		}
		if (a[Mesh::ARRAY_TEX_UV].get_type() != Variant::NIL) {
			o[Mesh::ARRAY_TEX_UV] = _gather(PackedVector2Array(a[Mesh::ARRAY_TEX_UV]), used);
		}
		if (a[Mesh::ARRAY_TEX_UV2].get_type() != Variant::NIL) {
			o[Mesh::ARRAY_TEX_UV2] = _gather(PackedVector2Array(a[Mesh::ARRAY_TEX_UV2]), used);
		}
		o[Mesh::ARRAY_INDEX] = indices;
		out->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, o);
		out->surface_set_material(s, p_mesh->surface_get_material(s));
		Ref<ArrayMesh> am = p_mesh;
		if (am.is_valid()) {
			out->surface_set_name(s, am->surface_get_name(s));
		}
		before += full.size() / 3;
		after += indices.size() / 3;
	}
	if (after * 2 > before) {
		return Ref<ArrayMesh>();
	}
	out->set_name(p_mesh->get_name());
	return out;
}

} // namespace FellowCellProxies
