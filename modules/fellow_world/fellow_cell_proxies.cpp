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

#include "core/math/geometry_2d.h"
#include "core/math/triangle_mesh.h"
#include "core/object/worker_thread_pool.h"
#include "core/os/mutex.h"
#include "core/os/os.h"
#include "core/templates/safe_refcount.h"
#include "scene/3d/mesh_instance_3d.h"
#include "scene/3d/multimesh_instance_3d.h"
#include "scene/resources/3d/importer_mesh.h"
#include "scene/resources/image_texture.h"
#include "scene/resources/multimesh.h"
#include "scene/resources/surface_tool.h"

#include "modules/fellow_fluid/fellow_shared_shaders.h"

// The UV unwrapper of ArrayMesh::lightmap_unwrap() (xatlas; editor builds).
extern bool (*array_mesh_lightmap_unwrap_callback)(float p_texel_size, const float *p_vertices, const float *p_normals, int p_vertex_count, const int *p_indices, int p_index_count, const uint8_t *p_cache_data, bool *r_use_cache, uint8_t **r_mesh_cache, int *r_mesh_cache_size, float **r_uv, int **r_vertex, int *r_vertex_count, int **r_index, int *r_index_count, int *r_size_hint_x, int *r_size_hint_y);

namespace FellowCellProxies {

static Mutex average_mutex;
static HashMap<String, Color> average_cache;

// Keyed by the texture's file when it has one: a bake loads the cells one by one, their textures again each time (the
// texture's data read back from the GPU is most of the cost).
static String _texture_key(const Ref<Texture2D> &p_texture) {
	return p_texture->get_path().is_empty() ? "#" + itos(uint64_t(p_texture->get_instance_id())) : p_texture->get_path();
}

Color texture_average(const Ref<Texture2D> &p_texture) {
	if (p_texture.is_null()) {
		return Color(1, 1, 1);
	}
	const String key = _texture_key(p_texture);
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

// A texture's levels as a bake samples them: decompressed, 8-bit RGBA, the finest at most 512 texels wide (a proxy's
// texel is centimetres to metres: the levels below are what it averages), cached by texture while proxies bake.
struct TextureLevels {
	struct Level {
		int width = 0;
		int height = 0;
		LocalVector<uint8_t> rgba;
	};
	LocalVector<Level> levels;
};

Mutex levels_mutex;
HashMap<String, TextureLevels *> levels_cache;
float srgb_to_linear_table[256];

const TextureLevels *_texture_levels(const Ref<Texture2D> &p_texture) {
	const String key = _texture_key(p_texture);
	{
		MutexLock lock(levels_mutex);
		if (TextureLevels **l = levels_cache.getptr(key)) {
			return *l;
		}
	}
	TextureLevels *tl = memnew(TextureLevels);
	Ref<Image> img = p_texture->get_image();
	if (img.is_valid() && !img->is_empty()) {
		if (!img->has_mipmaps()) {
			img = img->duplicate();
			if (img->is_compressed()) {
				img->decompress();
			}
			if (!img->is_compressed()) {
				img->convert(Image::FORMAT_RGBA8);
				while (img->get_width() > 512 || img->get_height() > 512) {
					img->resize(MAX(1, img->get_width() / 2), MAX(1, img->get_height() / 2), Image::INTERPOLATE_BILINEAR);
				}
				img->generate_mipmaps();
			}
		}
		const bool compressed = img->is_compressed();
		for (int l = 0; l <= img->get_mipmap_count(); l++) {
			int64_t ofs, size;
			int w, h;
			img->get_mipmap_offset_size_and_dimensions(l, ofs, size, w, h);
			if (w > 512 || h > 512) {
				continue;
			}
			if (compressed && (w < 4 || h < 4)) {
				break; // (a compressed format's blocks whole: the 4x4 level is the coarsest kept)
			}
			Ref<Image> level = img->get_image_from_mipmap(l);
			if (level->is_compressed()) {
				level->decompress();
			}
			if (level->is_compressed() || level->is_empty()) {
				break;
			}
			level->convert(Image::FORMAT_RGBA8);
			TextureLevels::Level out;
			out.width = level->get_width();
			out.height = level->get_height();
			const Vector<uint8_t> data = level->get_data();
			out.rgba.resize(data.size());
			memcpy(out.rgba.ptr(), data.ptr(), data.size());
			tl->levels.push_back(std::move(out));
		}
	}
	MutexLock lock(levels_mutex);
	if (TextureLevels **l = levels_cache.getptr(key)) {
		memdelete(tl);
		return *l;
	}
	levels_cache.insert(key, tl);
	return tl;
}

inline Color _fetch(const TextureLevels::Level &p_level, int p_x, int p_y) {
	p_x %= p_level.width;
	p_y %= p_level.height;
	p_x += p_x < 0 ? p_level.width : 0;
	p_y += p_y < 0 ? p_level.height : 0;
	const uint8_t *t = &p_level.rgba[(p_y * p_level.width + p_x) * 4];
	return Color(srgb_to_linear_table[t[0]], srgb_to_linear_table[t[1]], srgb_to_linear_table[t[2]], t[3] / 255.0f);
}

// Bilinear, repeating (an albedo texture is sRGB: the texels in linear).
Color _bilinear(const TextureLevels::Level &p_level, const Vector2 &p_uv) {
	const float x = (p_uv.x - Math::floor(p_uv.x)) * p_level.width - 0.5f;
	const float y = (p_uv.y - Math::floor(p_uv.y)) * p_level.height - 0.5f;
	const float x0 = Math::floor(x);
	const float y0 = Math::floor(y);
	const float fx = x - x0;
	const float fy = y - y0;
	const int ix = int(x0);
	const int iy = int(y0);
	const Color top = _fetch(p_level, ix, iy).lerp(_fetch(p_level, ix + 1, iy), fx);
	const Color bottom = _fetch(p_level, ix, iy + 1).lerp(_fetch(p_level, ix + 1, iy + 1), fx);
	return top.lerp(bottom, fy);
}

// Trilinear: p_lod in levels below the finest kept.
Color _sample(const TextureLevels &p_texture, const Vector2 &p_uv, float p_lod) {
	const int last = int(p_texture.levels.size()) - 1;
	p_lod = CLAMP(p_lod, 0.0f, float(last));
	const int l0 = int(p_lod);
	const float f = p_lod - l0;
	Color c = _bilinear(p_texture.levels[l0], p_uv);
	if (f > 0.0f && l0 < last) {
		c = c.lerp(_bilinear(p_texture.levels[l0 + 1], p_uv), f);
	}
	return c;
}

// What a material draws, for a proxy: its mean colour, and for a texture its albedo, texture and UVs' transform.
struct Sampler {
	Color albedo = Color(0.5f, 0.5f, 0.5f); // linear, the texture apart
	Color texture_mean = Color(1, 1, 1);
	const TextureLevels *texture = nullptr; // sampled by a textured proxy (not with triplanar UVs: their mean colour)
	Vector2 uv_scale = Vector2(1, 1);
	Vector2 uv_offset;
	float alpha_cut = -1.0f; // texels below are not there (scissored, hashed and blended materials); -1: opaque
	bool vertex_tint = false; // the albedo times the mesh's vertex colours
	bool srgb_vertex = false;
	bool own_colors = false; // the arrays' colours as they are (a terrain's proxy arrays)
};

struct Merged {
	PackedVector3Array vertices;
	PackedVector3Array normals;
	PackedColorArray colors; // the mean colour drawn at each vertex (a vertex-coloured proxy's)
	PackedInt32Array indices;
	// For a textured proxy: the vertices' UVs and colours without their textures, each triangle's sampler.
	LocalVector<Vector2> uvs;
	LocalVector<Color> tints;
	LocalVector<int> triangle_sampler;
	LocalVector<uint8_t> locked; // vertices the simplifier keeps (a terrain region's edge, which its neighbours share)
	LocalVector<Sampler> samplers;
	HashMap<ObjectID, int> material_sampler;
	int terrain_sampler = -1;
	bool want_textures = false; // read the textures' levels
	bool textured = false; // a sampler has a texture
};

int _sampler(Merged &r_merged, const Ref<Material> &p_material) {
	const ObjectID id = p_material.is_valid() ? p_material->get_instance_id() : ObjectID();
	if (const int *found = r_merged.material_sampler.getptr(id)) {
		return *found;
	}
	Sampler s;
	const Ref<BaseMaterial3D> bm = p_material;
	if (bm.is_valid()) {
		const Color a = bm->get_albedo();
		const Color linear = a.srgb_to_linear();
		s.albedo = Color(linear.r, linear.g, linear.b, a.a);
		const Ref<Texture2D> tex = bm->get_texture(BaseMaterial3D::TEXTURE_ALBEDO);
		if (tex.is_valid()) {
			s.texture_mean = texture_average(tex);
			if (r_merged.want_textures && !bm->get_flag(BaseMaterial3D::FLAG_UV1_USE_TRIPLANAR)) {
				const TextureLevels *tl = _texture_levels(tex);
				if (!tl->levels.is_empty()) {
					s.texture = tl;
					r_merged.textured = true;
				}
			}
		}
		const Vector3 scale = bm->get_uv1_scale();
		const Vector3 offset = bm->get_uv1_offset();
		s.uv_scale = Vector2(scale.x, scale.y);
		s.uv_offset = Vector2(offset.x, offset.y);
		if (bm->get_transparency() == BaseMaterial3D::TRANSPARENCY_ALPHA_SCISSOR) {
			s.alpha_cut = bm->get_alpha_scissor_threshold();
		} else if (bm->get_transparency() != BaseMaterial3D::TRANSPARENCY_DISABLED) {
			s.alpha_cut = 0.5f;
		}
		s.vertex_tint = bm->get_flag(BaseMaterial3D::FLAG_ALBEDO_FROM_VERTEX_COLOR);
		s.srgb_vertex = bm->get_flag(BaseMaterial3D::FLAG_SRGB_VERTEX_COLOR);
	}
	const int index = r_merged.samplers.size();
	r_merged.samplers.push_back(s);
	r_merged.material_sampler.insert(id, index);
	return index;
}

// A surface's triangles into p_merged, under p_xform, drawn by a sampler.
void _append(Merged &r_merged, const Array &p_arrays, const Transform3D &p_xform, int p_sampler) {
	if (p_arrays.size() != Mesh::ARRAY_MAX || p_arrays[Mesh::ARRAY_BONES].get_type() != Variant::NIL) {
		return;
	}
	const PackedVector3Array verts = p_arrays[Mesh::ARRAY_VERTEX];
	if (verts.is_empty()) {
		return;
	}
	const Sampler &s = r_merged.samplers[p_sampler];
	const PackedVector3Array normals = p_arrays[Mesh::ARRAY_NORMAL];
	const PackedVector2Array uvs = p_arrays[Mesh::ARRAY_TEX_UV];
	const PackedColorArray colors = p_arrays[Mesh::ARRAY_COLOR];
	const PackedInt32Array indices = p_arrays[Mesh::ARRAY_INDEX];
	const bool has_colors = colors.size() == verts.size();
	const int base = r_merged.vertices.size();
	const Basis nb = p_xform.basis.inverse().transposed();
	const bool mirrored = p_xform.basis.determinant() < 0.0f;
	for (int i = 0; i < verts.size(); i++) {
		r_merged.vertices.push_back(p_xform.xform(verts[i]));
		r_merged.normals.push_back(normals.size() == verts.size() ? nb.xform(normals[i]).normalized() : Vector3(0, 1, 0));
		Color tint = s.albedo;
		if (has_colors && s.own_colors) {
			tint = colors[i];
		} else if (has_colors && s.vertex_tint) {
			const Color c = s.srgb_vertex ? colors[i].srgb_to_linear() : colors[i];
			tint = Color(tint.r * c.r, tint.g * c.g, tint.b * c.b, tint.a * c.a);
		}
		r_merged.tints.push_back(tint);
		r_merged.colors.push_back(Color(tint.r * s.texture_mean.r, tint.g * s.texture_mean.g, tint.b * s.texture_mean.b, 1.0f));
		r_merged.uvs.push_back(uvs.size() == verts.size() ? uvs[i] : Vector2());
		r_merged.locked.push_back(0);
	}
	const int n = indices.is_empty() ? verts.size() : indices.size();
	for (int i = 0; i + 2 < n; i += 3) {
		const int a = indices.is_empty() ? i : indices[i];
		const int b = indices.is_empty() ? i + 1 : indices[i + 1];
		const int c = indices.is_empty() ? i + 2 : indices[i + 2];
		r_merged.indices.push_back(base + a);
		r_merged.indices.push_back(base + (mirrored ? c : b));
		r_merged.indices.push_back(base + (mirrored ? b : c));
		r_merged.triangle_sampler.push_back(p_sampler);
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
		_append(r_merged, p_mesh->surface_get_arrays(s), p_xform, _sampler(r_merged, m));
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

// A proxy's shape (its colours come from its texture or the surfaces under its corners, so its vertices need not
// keep the materials apart): the merged triangles welded by position alone, simplified to about p_ratio of them
// (meshoptimizer, no border locked but a terrain region's and no part pruned: Godot's LOD generation locks open
// borders — a wall's quads not joined to the next wall at a corner — and then reaches its targets by deleting whole
// buildings), the normals rebuilt from the faces, split where they meet at more than 60 degrees; r_error the
// simplifier's estimate of how far it moved the surface (metres). False when the simplifier is missing or nothing is
// left.
bool _simplify_welded(const Merged &p_merged, float p_ratio, PackedVector3Array &r_vertices, PackedVector3Array &r_normals, PackedColorArray &r_colors, PackedInt32Array &r_indices, float &r_error) {
	if (!SurfaceTool::simplify_with_attrib_func || !SurfaceTool::generate_remap_func || !SurfaceTool::remap_index_func || !SurfaceTool::simplify_scale_func) {
		return false;
	}
	const int vertex_count = p_merged.vertices.size();
	const int index_count = p_merged.indices.size();
	LocalVector<float> positions;
	positions.resize(vertex_count * 3);
	for (int i = 0; i < vertex_count; i++) {
		const Vector3 &v = p_merged.vertices[i];
		positions[i * 3 + 0] = v.x;
		positions[i * 3 + 1] = v.y;
		positions[i * 3 + 2] = v.z;
	}
	LocalVector<unsigned int> remap;
	remap.resize(vertex_count);
	const size_t unique = SurfaceTool::generate_remap_func(remap.ptr(), (const unsigned int *)p_merged.indices.ptr(), index_count, positions.ptr(), vertex_count, sizeof(float) * 3);
	LocalVector<float> welded;
	welded.resize(unique * 3);
	LocalVector<int> representative; // an original vertex of each welded one (its colour)
	representative.resize(unique);
	LocalVector<uint8_t> lock;
	lock.resize(unique);
	memset(lock.ptr(), 0, unique);
	bool any_locked = false;
	for (int i = 0; i < vertex_count; i++) {
		if (remap[i] != ~0u) {
			representative[remap[i]] = i;
			if (p_merged.locked[i]) {
				lock[remap[i]] = 1; // (meshopt_SimplifyVertex_Lock)
				any_locked = true;
			}
			for (int k = 0; k < 3; k++) {
				welded[remap[i] * 3 + k] = positions[i * 3 + k];
			}
		}
	}
	LocalVector<unsigned int> indices;
	indices.resize(index_count);
	SurfaceTool::remap_index_func(indices.ptr(), (const unsigned int *)p_merged.indices.ptr(), index_count, remap.ptr());
	LocalVector<unsigned int> simplified;
	simplified.resize(index_count);
	const size_t target = MAX(size_t(index_count * p_ratio) / 3 * 3, size_t(36));
	float error = 0.0f;
	const size_t count = SurfaceTool::simplify_with_attrib_func(simplified.ptr(), indices.ptr(), index_count, welded.ptr(), unique, sizeof(float) * 3, nullptr, 0, nullptr, 0, any_locked ? lock.ptr() : nullptr, target, 1.0f, 0, &error);
	if (count == 0) {
		return false;
	}
	r_error = error * SurfaceTool::simplify_scale_func(welded.ptr(), unique, sizeof(float) * 3); // (relative to the extents)
	// The faces' normals (area-weighted), and the faces at each vertex.
	const int tris = int(count / 3);
	LocalVector<Vector3> face;
	face.resize(tris);
	LocalVector<int> start;
	start.resize(unique + 1);
	for (uint32_t i = 0; i <= unique; i++) {
		start[i] = 0;
	}
	for (int t = 0; t < tris; t++) {
		const Vector3 a(welded[simplified[t * 3] * 3], welded[simplified[t * 3] * 3 + 1], welded[simplified[t * 3] * 3 + 2]);
		const Vector3 b(welded[simplified[t * 3 + 1] * 3], welded[simplified[t * 3 + 1] * 3 + 1], welded[simplified[t * 3 + 1] * 3 + 2]);
		const Vector3 c(welded[simplified[t * 3 + 2] * 3], welded[simplified[t * 3 + 2] * 3 + 1], welded[simplified[t * 3 + 2] * 3 + 2]);
		// Facing as the source's normals at its corners (whichever way its triangles wind).
		face[t] = (b - a).cross(c - a);
		const Vector3 source_normal = p_merged.normals[representative[simplified[t * 3]]] + p_merged.normals[representative[simplified[t * 3 + 1]]] + p_merged.normals[representative[simplified[t * 3 + 2]]];
		if (face[t].dot(source_normal) < 0.0f) {
			face[t] = -face[t];
		}
		for (int k = 0; k < 3; k++) {
			start[simplified[t * 3 + k] + 1]++;
		}
	}
	for (uint32_t i = 0; i < unique; i++) {
		start[i + 1] += start[i];
	}
	LocalVector<int> fill;
	fill.resize(unique);
	for (uint32_t i = 0; i < unique; i++) {
		fill[i] = start[i];
	}
	LocalVector<int> incident;
	incident.resize(count);
	for (int t = 0; t < tris; t++) {
		for (int k = 0; k < 3; k++) {
			incident[fill[simplified[t * 3 + k]]++] = t;
		}
	}
	// Each corner: the faces at its vertex within 60 degrees of its own face; one vertex per position and normal.
	const float cos_split = Math::cos(Math::deg_to_rad(60.0f));
	HashMap<uint64_t, int> corner_vertex;
	r_vertices.clear();
	r_normals.clear();
	r_colors.clear();
	r_indices.clear();
	for (int t = 0; t < tris; t++) {
		const Vector3 fn = face[t].normalized();
		if (fn == Vector3()) {
			continue;
		}
		int corner[3];
		for (int k = 0; k < 3; k++) {
			const unsigned int v = simplified[t * 3 + k];
			Vector3 n;
			for (int j = start[v]; j < start[v + 1]; j++) {
				const Vector3 &other = face[incident[j]];
				if (other.normalized().dot(fn) >= cos_split) {
					n += other;
				}
			}
			n = n.normalized();
			// The same vertex with (nearly) the same normal: one vertex.
			const uint64_t key = (uint64_t(v) << 24) ^ (uint64_t(int(n.x * 127.0f) + 128) << 16) ^ (uint64_t(int(n.y * 127.0f) + 128) << 8) ^ uint64_t(int(n.z * 127.0f) + 128);
			if (const int *found = corner_vertex.getptr(key)) {
				corner[k] = *found;
			} else {
				corner[k] = r_vertices.size();
				corner_vertex.insert(key, corner[k]);
				r_vertices.push_back(Vector3(welded[v * 3], welded[v * 3 + 1], welded[v * 3 + 2]));
				r_normals.push_back(n);
				r_colors.push_back(p_merged.colors[representative[v]]);
			}
		}
		r_indices.push_back(corner[0]);
		r_indices.push_back(corner[1]);
		r_indices.push_back(corner[2]);
	}
	return !r_indices.is_empty();
}

void _init_srgb_table() {
	static bool ready = false;
	if (!ready) {
		for (int i = 0; i < 256; i++) {
			srgb_to_linear_table[i] = Color(i / 255.0f, 0, 0).srgb_to_linear().r;
		}
		ready = true;
	}
}

// A proxy texel: the unwrapped triangle over it and where (inside it, or the nearest point of one within a texel: a
// chart's border, which bilinear filtering reads).
struct BakeTexel {
	int triangle = -1;
	float b1 = 0.0f;
	float b2 = 0.0f;
	uint8_t priority = 0; // 2: inside, 1: a border, 0: none
};

struct ShadeContext {
	const Merged *merged = nullptr;
	const TriangleMesh *source = nullptr;
	const Vector3 *positions = nullptr; // the unwrapped proxy's
	const Vector3 *normals = nullptr;
	const Color *colors = nullptr;
	const int32_t *indices = nullptr;
	const BakeTexel *texels = nullptr;
	Color *out = nullptr;
	int width = 0;
	float texel_size = 0.0f;
	float reach = 0.0f;
	SafeNumeric<uint32_t> hits;
};

// A source triangle's colour at a point (its texture's level for a proxy texel's size); false where its texture is
// transparent.
bool _source_color(const Merged &p_merged, int p_face, const Vector3 &p_point, float p_texel_size, Color &r_color) {
	const int32_t *idx = p_merged.indices.ptr();
	const int ia = idx[p_face * 3 + 0];
	const int ib = idx[p_face * 3 + 1];
	const int ic = idx[p_face * 3 + 2];
	const Vector3 *v = p_merged.vertices.ptr();
	const Vector3 e0 = v[ib] - v[ia];
	const Vector3 e1 = v[ic] - v[ia];
	const Vector3 e2 = p_point - v[ia];
	const float d00 = e0.dot(e0);
	const float d01 = e0.dot(e1);
	const float d11 = e1.dot(e1);
	const float d20 = e2.dot(e0);
	const float d21 = e2.dot(e1);
	const float denom = d00 * d11 - d01 * d01;
	float bb = 0.0f;
	float bc = 0.0f;
	if (denom > 1e-12f) {
		bb = CLAMP((d11 * d20 - d01 * d21) / denom, 0.0f, 1.0f);
		bc = CLAMP((d00 * d21 - d01 * d20) / denom, 0.0f, 1.0f - bb);
	}
	const float ba = 1.0f - bb - bc;
	const Sampler &s = p_merged.samplers[p_merged.triangle_sampler[p_face]];
	if (!s.texture) {
		const Color *c = p_merged.colors.ptr();
		r_color = c[ia] * ba + c[ib] * bb + c[ic] * bc;
		return true;
	}
	const Color tint = p_merged.tints[ia] * ba + p_merged.tints[ib] * bb + p_merged.tints[ic] * bc;
	const Vector2 ua = p_merged.uvs[ia] * s.uv_scale;
	const Vector2 ub = p_merged.uvs[ib] * s.uv_scale;
	const Vector2 uc = p_merged.uvs[ic] * s.uv_scale;
	const Vector2 uv = ua * ba + ub * bb + uc * bc + s.uv_offset;
	// The level whose texels are the proxy's texel: the texture's texels per metre on this triangle.
	const float uv_area = Math::abs((ub - ua).cross(uc - ua));
	const float area = e0.cross(e1).length();
	const TextureLevels::Level &finest = s.texture->levels[0];
	const float footprint = area > 0.0f ? p_texel_size * Math::sqrt(uv_area / area * finest.width * finest.height) : 1.0f;
	const Color t = _sample(*s.texture, uv, Math::log2(MAX(footprint, 1.0f)));
	if (s.alpha_cut >= 0.0f && t.a * tint.a < s.alpha_cut) {
		return false;
	}
	r_color = Color(tint.r * t.r, tint.g * t.g, tint.b * t.b, 1.0f);
	return true;
}

// The source's triangles, to find the surface under a point of the proxy.
Ref<TriangleMesh> _source_mesh(const Merged &p_merged) {
	Vector<Vector3> faces;
	faces.resize(p_merged.indices.size());
	Vector3 *w = faces.ptrw();
	const Vector3 *v = p_merged.vertices.ptr();
	const int32_t *idx = p_merged.indices.ptr();
	for (int i = 0; i < p_merged.indices.size(); i++) {
		w[i] = v[idx[i]];
	}
	Ref<TriangleMesh> source;
	source.instantiate();
	source->create(faces);
	return source;
}

// The source's outermost surface along p_normal within p_reach of p_point (past transparent texels): its colour, the
// materials' means with p_texel_size 0. False where there is none.
bool _surface_color(const Merged &p_merged, const TriangleMesh &p_source, const Vector3 &p_point, const Vector3 &p_normal, float p_reach, float p_texel_size, Color &r_color) {
	Vector3 from = p_point + p_normal * p_reach;
	const Vector3 to = p_point - p_normal * p_reach;
	for (int step = 0; step < 4; step++) {
		Vector3 hit;
		Vector3 hit_normal;
		int32_t face = -1;
		if (!p_source.intersect_segment(from, to, hit, hit_normal, nullptr, &face) || face < 0) {
			return false;
		}
		if (p_texel_size <= 0.0f) {
			const int32_t *idx = p_merged.indices.ptr();
			const Vector3 *v = p_merged.vertices.ptr();
			const Color *c = p_merged.colors.ptr();
			// (The face's colour where it hits, by the nearest corner's weight: a material's mean is the same at all three.)
			const Vector3 &a = v[idx[face * 3]];
			const Vector3 &b = v[idx[face * 3 + 1]];
			const Vector3 &cc = v[idx[face * 3 + 2]];
			const float da = hit.distance_squared_to(a);
			const float db = hit.distance_squared_to(b);
			const float dc = hit.distance_squared_to(cc);
			r_color = da <= db && da <= dc ? c[idx[face * 3]] : (db <= dc ? c[idx[face * 3 + 1]] : c[idx[face * 3 + 2]]);
			return true;
		}
		if (_source_color(p_merged, face, hit, p_texel_size, r_color)) {
			return true;
		}
		from = hit - p_normal * 0.001f;
	}
	return false;
}

// A vertex-coloured proxy's colours: each corner the colour of the source's surface a little inside it (so that a
// roof and the wall under it, welded at the eaves, keep their own colours), vertices split where they differ.
void _corner_colors(const Merged &p_merged, const TriangleMesh &p_source, float p_reach, PackedVector3Array &r_vertices, PackedVector3Array &r_normals, PackedColorArray &r_colors, PackedInt32Array &r_indices, int &r_hits) {
	PackedVector3Array vertices;
	PackedVector3Array normals;
	PackedColorArray colors;
	PackedInt32Array indices;
	HashMap<uint64_t, int> corner_vertex;
	r_hits = 0;
	for (int t = 0; t + 2 < r_indices.size(); t += 3) {
		const int i[3] = { r_indices[t], r_indices[t + 1], r_indices[t + 2] };
		const Vector3 centroid = (r_vertices[i[0]] + r_vertices[i[1]] + r_vertices[i[2]]) / 3.0f;
		for (int k = 0; k < 3; k++) {
			const int v = i[k];
			Color c = r_colors[v];
			if (_surface_color(p_merged, p_source, r_vertices[v].lerp(centroid, 0.25f), r_normals[v], p_reach, 0.0f, c)) {
				r_hits++;
			}
			const Color q = c.clamp();
			const uint64_t key = (uint64_t(v) << 24) ^ (uint64_t(q.r * 255.0f) << 16) ^ (uint64_t(q.g * 255.0f) << 8) ^ uint64_t(q.b * 255.0f);
			if (const int *found = corner_vertex.getptr(key)) {
				indices.push_back(*found);
			} else {
				corner_vertex.insert(key, vertices.size());
				indices.push_back(vertices.size());
				vertices.push_back(r_vertices[v]);
				normals.push_back(r_normals[v]);
				colors.push_back(c);
			}
		}
	}
	r_vertices = vertices;
	r_normals = normals;
	r_colors = colors;
	r_indices = indices;
}

// A row of the proxy's texture: each texel's point of the proxy, the source's outermost surface along its normal
// within the reach (past transparent texels), that surface's colour (else the proxy's vertex colours).
void _shade_row(void *p_userdata, uint32_t p_row) {
	ShadeContext &ctx = *static_cast<ShadeContext *>(p_userdata);
	uint32_t hits = 0;
	for (int x = 0; x < ctx.width; x++) {
		const int k = int(p_row) * ctx.width + x;
		const BakeTexel &t = ctx.texels[k];
		if (t.priority == 0) {
			continue;
		}
		const int i0 = ctx.indices[t.triangle * 3 + 0];
		const int i1 = ctx.indices[t.triangle * 3 + 1];
		const int i2 = ctx.indices[t.triangle * 3 + 2];
		const float b0 = 1.0f - t.b1 - t.b2;
		const Vector3 p = ctx.positions[i0] * b0 + ctx.positions[i1] * t.b1 + ctx.positions[i2] * t.b2;
		Vector3 n = ctx.normals[i0] * b0 + ctx.normals[i1] * t.b1 + ctx.normals[i2] * t.b2;
		if (n.length_squared() < 1e-8f) {
			n = (ctx.positions[i1] - ctx.positions[i0]).cross(ctx.positions[i2] - ctx.positions[i0]);
		}
		n = n.normalized();
		Color c = ctx.colors[i0] * b0 + ctx.colors[i1] * t.b1 + ctx.colors[i2] * t.b2;
		if (_surface_color(*ctx.merged, *ctx.source, p, n, ctx.reach, ctx.texel_size, c)) {
			hits++;
		}
		ctx.out[k] = c;
	}
	ctx.hits.add(hits);
}

// The proxy unwrapped (xatlas, as for lightmaps) into a texture of texels of about p_texel_size (larger if it would
// exceed p_max_size), each coloured by the source's surface under it: r_arrays the unwrapped proxy, the material its
// texture's. Null where it cannot (no unwrapper outside editor builds; no area; an unwrapping that fails).
Ref<StandardMaterial3D> _bake_texture(const Merged &p_merged, const TriangleMesh &p_source, float p_reach, const PackedVector3Array &p_vertices, const PackedVector3Array &p_normals, const PackedColorArray &p_colors, const PackedInt32Array &p_indices, float p_texel_size, int p_max_size, Array &r_arrays, Dictionary &r_info) {
	const uint64_t t0 = OS::get_singleton()->get_ticks_usec();
	if (!array_mesh_lightmap_unwrap_callback) {
		r_info["texture_skipped"] = "no UV unwrapper (editor builds only)";
		return Ref<StandardMaterial3D>();
	}
	_init_srgb_table();
	LocalVector<float> positions;
	LocalVector<float> normals;
	LocalVector<int> indices;
	positions.resize(p_vertices.size() * 3);
	normals.resize(p_vertices.size() * 3);
	for (int i = 0; i < p_vertices.size(); i++) {
		for (int k = 0; k < 3; k++) {
			positions[i * 3 + k] = p_vertices[i][k];
			normals[i * 3 + k] = p_normals[i][k];
		}
	}
	for (int i = 0; i + 2 < p_indices.size(); i += 3) {
		const Vector3 &a = p_vertices[p_indices[i]];
		const Vector3 &b = p_vertices[p_indices[i + 1]];
		const Vector3 &c = p_vertices[p_indices[i + 2]];
		const float eps = 1.19209290e-7f; // (as ArrayMesh::lightmap_unwrap: xatlas's)
		if ((a - b).length_squared() < eps || (b - c).length_squared() < eps || (c - a).length_squared() < eps) {
			continue;
		}
		indices.push_back(p_indices[i]);
		indices.push_back(p_indices[i + 1]);
		indices.push_back(p_indices[i + 2]);
	}
	if (indices.is_empty()) {
		return Ref<StandardMaterial3D>();
	}
	float texel = p_texel_size;
	float *uv = nullptr;
	int *xref = nullptr;
	int *uv_index = nullptr;
	int uv_vertex_count = 0;
	int uv_index_count = 0;
	int size_x = 0;
	int size_y = 0;
	bool fits = false;
	for (int attempt = 0; attempt < 4 && !fits; attempt++) {
		bool use_cache = false;
		uint8_t *cache = nullptr;
		int cache_size = 0;
		if (!array_mesh_lightmap_unwrap_callback(texel, positions.ptr(), normals.ptr(), p_vertices.size(), indices.ptr(), indices.size(), nullptr, &use_cache, &cache, &cache_size, &uv, &xref, &uv_vertex_count, &uv_index, &uv_index_count, &size_x, &size_y)) {
			r_info["texture_skipped"] = "the UV unwrapping failed";
			return Ref<StandardMaterial3D>();
		}
		if (cache) {
			memfree(cache);
		}
		if (MAX(size_x, size_y) <= p_max_size) {
			fits = true;
		} else {
			memfree(uv);
			memfree(xref);
			memfree(uv_index);
			texel *= float(MAX(size_x, size_y)) / float(p_max_size) * 1.02f; // coarser texels, to fit
		}
	}
	if (!fits) {
		r_info["texture_skipped"] = "larger than the maximum size";
		return Ref<StandardMaterial3D>();
	}
	// Whole 4x4 blocks (a compressed texture's).
	const int width = (size_x + 3) & ~3;
	const int height = (size_y + 3) & ~3;
	PackedVector3Array out_vertices;
	PackedVector3Array out_normals;
	PackedColorArray out_colors;
	PackedVector2Array out_uvs;
	PackedInt32Array out_indices;
	out_vertices.resize(uv_vertex_count);
	out_normals.resize(uv_vertex_count);
	out_colors.resize(uv_vertex_count);
	out_uvs.resize(uv_vertex_count);
	for (int i = 0; i < uv_vertex_count; i++) {
		const int v = xref[i];
		out_vertices.write[i] = p_vertices[v];
		out_normals.write[i] = p_normals[v];
		out_colors.write[i] = p_colors[v];
		out_uvs.write[i] = Vector2(uv[i * 2] * size_x / width, uv[i * 2 + 1] * size_y / height);
	}
	out_indices.resize(uv_index_count);
	memcpy(out_indices.ptrw(), uv_index, sizeof(int32_t) * uv_index_count);
	memfree(uv);
	memfree(xref);
	memfree(uv_index);

	// The texels each triangle covers.
	LocalVector<BakeTexel> texels;
	texels.resize(width * height);
	const Vector2 px(width, height);
	const Vector2 *uvs = out_uvs.ptr();
	const int32_t *tri = out_indices.ptr();
	for (int t = 0; t < uv_index_count / 3; t++) {
		const Vector2 a = uvs[tri[t * 3]] * px;
		const Vector2 b = uvs[tri[t * 3 + 1]] * px;
		const Vector2 c = uvs[tri[t * 3 + 2]] * px;
		const float area2 = (b - a).cross(c - a);
		if (Math::abs(area2) < 1e-9f) {
			continue;
		}
		const int x0 = MAX(0, int(Math::floor(MIN(a.x, MIN(b.x, c.x)))) - 1);
		const int x1 = MIN(width - 1, int(Math::ceil(MAX(a.x, MAX(b.x, c.x)))) + 1);
		const int y0 = MAX(0, int(Math::floor(MIN(a.y, MIN(b.y, c.y)))) - 1);
		const int y1 = MIN(height - 1, int(Math::ceil(MAX(a.y, MAX(b.y, c.y)))) + 1);
		for (int y = y0; y <= y1; y++) {
			for (int x = x0; x <= x1; x++) {
				const Vector2 p(x + 0.5f, y + 0.5f);
				const float l1 = (p - a).cross(c - a) / area2;
				const float l2 = (b - a).cross(p - a) / area2;
				BakeTexel &bt = texels[y * width + x];
				if (l1 >= -1e-4f && l2 >= -1e-4f && 1.0f - l1 - l2 >= -1e-4f) {
					if (bt.priority < 2) {
						bt.triangle = t;
						bt.b1 = CLAMP(l1, 0.0f, 1.0f);
						bt.b2 = CLAMP(l2, 0.0f, 1.0f - bt.b1);
						bt.priority = 2;
					}
					continue;
				}
				if (bt.priority > 0) {
					continue;
				}
				Vector2 q = Geometry2D::get_closest_point_to_segment(p, a, b);
				const Vector2 qb = Geometry2D::get_closest_point_to_segment(p, b, c);
				const Vector2 qc = Geometry2D::get_closest_point_to_segment(p, c, a);
				if (qb.distance_squared_to(p) < q.distance_squared_to(p)) {
					q = qb;
				}
				if (qc.distance_squared_to(p) < q.distance_squared_to(p)) {
					q = qc;
				}
				if (q.distance_squared_to(p) > 1.0f) {
					continue;
				}
				bt.triangle = t;
				bt.b1 = CLAMP((q - a).cross(c - a) / area2, 0.0f, 1.0f);
				bt.b2 = CLAMP((b - a).cross(q - a) / area2, 0.0f, 1.0f - bt.b1);
				bt.priority = 1;
			}
		}
	}

	LocalVector<Color> colors;
	colors.resize(width * height);
	ShadeContext ctx;
	ctx.merged = &p_merged;
	ctx.source = &p_source;
	ctx.positions = out_vertices.ptr();
	ctx.normals = out_normals.ptr();
	ctx.colors = out_colors.ptr();
	ctx.indices = out_indices.ptr();
	ctx.texels = texels.ptr();
	ctx.out = colors.ptr();
	ctx.width = width;
	ctx.texel_size = texel;
	ctx.reach = MAX(p_reach, 2.0f * texel);
	const WorkerThreadPool::GroupID group = WorkerThreadPool::get_singleton()->add_native_group_task(&_shade_row, &ctx, height, -1, true, "FellowEngine proxy texture");
	WorkerThreadPool::get_singleton()->wait_for_group_task_completion(group);

	// The texels no triangle covers take their neighbours' colours (what filtering and the smaller mips read), the
	// rest the covered texels' mean.
	LocalVector<uint8_t> filled;
	filled.resize(width * height);
	Color sum(0, 0, 0, 0);
	int covered = 0;
	for (int k = 0; k < width * height; k++) {
		filled[k] = texels[k].priority > 0 ? 1 : 0;
		if (filled[k]) {
			sum += colors[k];
			covered++;
		}
	}
	const Color mean = covered > 0 ? sum / float(covered) : Color(0.5f, 0.5f, 0.5f);
	LocalVector<int> grown;
	for (int pass = 0; pass < 16; pass++) {
		grown.clear();
		for (int y = 0; y < height; y++) {
			for (int x = 0; x < width; x++) {
				const int k = y * width + x;
				if (filled[k]) {
					continue;
				}
				Color acc(0, 0, 0, 0);
				int n = 0;
				for (int dy = -1; dy <= 1; dy++) {
					for (int dx = -1; dx <= 1; dx++) {
						const int nx = x + dx;
						const int ny = y + dy;
						if (nx >= 0 && ny >= 0 && nx < width && ny < height && filled[ny * width + nx]) {
							acc += colors[ny * width + nx];
							n++;
						}
					}
				}
				if (n > 0) {
					colors[k] = acc / float(n);
					grown.push_back(k);
				}
			}
		}
		if (grown.is_empty()) {
			break;
		}
		for (int k : grown) {
			filled[k] = 1;
		}
	}
	Vector<uint8_t> data;
	data.resize(width * height * 3);
	uint8_t *w = data.ptrw();
	for (int k = 0; k < width * height; k++) {
		const Color s = (filled[k] ? colors[k] : mean).linear_to_srgb();
		w[k * 3 + 0] = uint8_t(CLAMP(int(s.r * 255.0f + 0.5f), 0, 255));
		w[k * 3 + 1] = uint8_t(CLAMP(int(s.g * 255.0f + 0.5f), 0, 255));
		w[k * 3 + 2] = uint8_t(CLAMP(int(s.b * 255.0f + 0.5f), 0, 255));
	}
	Ref<Image> img = Image::create_from_data(width, height, false, Image::FORMAT_RGB8, data);
	img->generate_mipmaps();
	// BC1 where the editor can compress (an eighth of the bytes; decompressed on load where the GPU lacks it).
	if (Image::_image_compress_bc_func) {
		img->compress(Image::COMPRESS_S3TC, Image::COMPRESS_SOURCE_SRGB);
	}
	Ref<StandardMaterial3D> material;
	material.instantiate();
	material->set_texture(BaseMaterial3D::TEXTURE_ALBEDO, ImageTexture::create_from_image(img));
	material->set_texture_filter(BaseMaterial3D::TEXTURE_FILTER_LINEAR_WITH_MIPMAPS_ANISOTROPIC);
	material->set_roughness(0.9f);
	material->set_name("FellowCellProxyTextured");

	r_arrays.resize(Mesh::ARRAY_MAX);
	r_arrays[Mesh::ARRAY_VERTEX] = out_vertices;
	r_arrays[Mesh::ARRAY_NORMAL] = out_normals;
	r_arrays[Mesh::ARRAY_TEX_UV] = out_uvs;
	r_arrays[Mesh::ARRAY_INDEX] = out_indices;
	r_info["texture_width"] = width;
	r_info["texture_height"] = height;
	r_info["texture_bytes"] = int(img->get_data().size());
	r_info["texture_compressed"] = img->is_compressed();
	r_info["texel_size"] = texel;
	r_info["texture_texels"] = covered;
	r_info["texture_hits"] = int(ctx.hits.get());
	r_info["texture_usec"] = int64_t(OS::get_singleton()->get_ticks_usec() - t0);
	return material;
}

} // namespace

void clear_bake_cache() {
	MutexLock lock(levels_mutex);
	for (KeyValue<String, TextureLevels *> &E : levels_cache) {
		memdelete(E.value);
	}
	levels_cache.clear();
}

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

Ref<ArrayMesh> bake_proxy(Node *p_content, float p_ratio, int p_terrain_quads, float p_texel_size, int p_max_texture_size, Dictionary &r_info) {
	Merged merged;
	merged.want_textures = p_texel_size > 0.0f;
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
				// Its own colours per vertex.
				if (merged.terrain_sampler < 0) {
					Sampler ts;
					ts.albedo = Color(1, 1, 1);
					ts.own_colors = true;
					merged.terrain_sampler = merged.samplers.size();
					merged.samplers.push_back(ts);
				}
				const int before = merged.indices.size();
				_append(merged, arrays, it.xform, merged.terrain_sampler);
				source_triangles += (merged.indices.size() - before) / 3;
				// Its edges kept where they are: the neighbouring regions' proxies meet them.
				HashMap<uint64_t, int> edges;
				for (int i = before; i < merged.indices.size(); i += 3) {
					for (int k = 0; k < 3; k++) {
						const uint32_t a = merged.indices[i + k];
						const uint32_t b = merged.indices[i + (k + 1) % 3];
						const uint64_t key = (uint64_t(MIN(a, b)) << 32) | uint64_t(MAX(a, b));
						edges[key] = edges.has(key) ? edges[key] + 1 : 1;
					}
				}
				for (const KeyValue<uint64_t, int> &E : edges) {
					if (E.value == 1) {
						merged.locked[uint32_t(E.key >> 32)] = 1;
						merged.locked[uint32_t(E.key & 0xffffffff)] = 1;
					}
				}
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
	// The shape: welded and simplified (meshoptimizer).
	PackedVector3Array vertices;
	PackedVector3Array normals;
	PackedColorArray colors;
	PackedInt32Array indices;
	float error = 0.0f;
	if (!_simplify_welded(merged, p_ratio, vertices, normals, colors, indices, error)) {
		r_info["proxy_triangles"] = 0;
		return Ref<ArrayMesh>();
	}
	// The source's surfaces are looked for within twice the simplifier's error (a centimetre at least) of the proxy's.
	const Ref<TriangleMesh> source = _source_mesh(merged);
	const float reach = MAX(2.0f * error, 0.01f);
	r_info["simplify_error"] = error;
	Ref<ArrayMesh> proxy;
	proxy.instantiate();
	// Where a material has a texture: the proxy unwrapped, and a texture of what the source draws under it.
	if (p_texel_size > 0.0f && merged.textured) {
		Array textured_arrays;
		const Ref<StandardMaterial3D> textured = _bake_texture(merged, *source.ptr(), reach, vertices, normals, colors, indices, p_texel_size, p_max_texture_size, textured_arrays, r_info);
		if (textured.is_valid()) {
			proxy->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, textured_arrays);
			proxy->surface_set_material(0, textured);
			r_info["proxy_vertices"] = PackedVector3Array(textured_arrays[Mesh::ARRAY_VERTEX]).size();
			r_info["proxy_triangles"] = PackedInt32Array(textured_arrays[Mesh::ARRAY_INDEX]).size() / 3;
			return proxy;
		}
	}
	// Else its materials' mean colours, at each corner the surface's under it.
	int hits = 0;
	_corner_colors(merged, *source.ptr(), reach, vertices, normals, colors, indices, hits);
	Array out;
	out.resize(Mesh::ARRAY_MAX);
	out[Mesh::ARRAY_VERTEX] = vertices;
	out[Mesh::ARRAY_NORMAL] = normals;
	out[Mesh::ARRAY_COLOR] = colors;
	out[Mesh::ARRAY_INDEX] = indices;
	proxy->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, out);
	proxy->surface_set_material(0, proxy_material());
	r_info["proxy_vertices"] = vertices.size();
	r_info["proxy_triangles"] = indices.size() / 3;
	r_info["corner_hits"] = hits;
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
