/**************************************************************************/
/*  fellow_hair_3d.cpp                                                    */
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

#include "fellow_hair_3d.h"

#include "fellow_node_warnings.h"
#include "fellow_shared_shaders.h"
#include "fellow_wind_3d.h"

#include "core/config/engine.h"
#include "core/config/project_settings.h"
#include "core/math/random_pcg.h"
#include "core/object/class_db.h"
#include "core/object/worker_thread_pool.h"
#include "core/os/os.h"
#include "scene/3d/camera_3d.h"
#include "scene/3d/physics/collision_shape_3d.h"
#include "scene/main/viewport.h"
#include "scene/resources/3d/capsule_shape_3d.h"
#include "scene/resources/3d/sphere_shape_3d.h"
#include "servers/rendering/rendering_server.h"

FellowHair3D::GlobalStats FellowHair3D::global_stats;
LocalVector<FellowHair3D *> FellowHair3D::registry;
uint64_t FellowHair3D::group_frame = UINT64_MAX;

namespace {

constexpr int FLOATS_PER_STRAND = 20; // a 3D transform (12), a colour (4), custom data (4)
constexpr int STRIP_SEGMENTS = 24; // a drawn strand's segments (Catmull-Rom between the guides' points)

} // namespace

// The strands' material (decision 0248). Each strand is an instance of a strip of STRIP_SEGMENTS quads (UV.y along it,
// UV.x -1 / +1 across); its instance colour holds two of its three guides and their weights (r, g: indices; b, a:
// weights), its custom data the first guide (x), a random number (y), its length's share of the guides' (z) and which
// half of the guides' texture is this frame's (w). The guides' points: the texture's rows [half × guides + guide].
String FellowHair3D::shader_code() {
	return R"(// FellowEngine FellowHair3D (decision 0248)
shader_type spatial;
render_mode skip_vertex_transform, cull_disabled;

uniform highp sampler2D guides : filter_nearest, repeat_disable;
uniform int guide_count = 1;
uniform int points = 2;
uniform float root_width = 0.0004;
uniform float tip_width = 0.00012;
uniform vec4 root_color : source_color = vec4(0.07, 0.045, 0.03, 1.0);
uniform vec4 tip_color : source_color = vec4(0.22, 0.14, 0.08, 1.0);
uniform float color_variation = 0.2;
uniform float specular_shift = 0.08;
uniform float specular_strength = 0.5;
uniform float specular_sharpness = 120.0;
uniform float transmission = 0.35;
uniform float root_occlusion = 0.6;
uniform float layer_occlusion = 0.45;
uniform float width_scale = 1.0;

varying vec3 hair_tangent;
varying float hair_t;
varying float hair_coverage;
varying float hair_seed;

vec3 guide_fetch(int g, int row_base, int i) {
	return texelFetch(guides, ivec2(clamp(i, 0, points - 1), row_base + g), 0).xyz;
}

// Catmull-Rom along a guide at u (0 root, 1 tip); its derivative in d.
vec3 guide_at(int g, int row_base, float u, out vec3 d) {
	float f = clamp(u, 0.0, 1.0) * float(points - 1);
	int i = min(int(floor(f)), points - 2);
	float s = f - float(i);
	vec3 p1 = guide_fetch(g, row_base, i);
	vec3 p2 = guide_fetch(g, row_base, i + 1);
	vec3 p0 = i > 0 ? guide_fetch(g, row_base, i - 1) : 2.0 * p1 - p2;
	vec3 p3 = i + 2 < points ? guide_fetch(g, row_base, i + 2) : 2.0 * p2 - p1;
	vec3 b = p2 - p0;
	vec3 c = 2.0 * p0 - 5.0 * p1 + 4.0 * p2 - p3;
	vec3 e = -p0 + 3.0 * p1 - 3.0 * p2 + p3;
	d = 0.5 * (b + 2.0 * c * s + 3.0 * e * s * s);
	return p1 + 0.5 * (b * s + c * s * s + e * s * s * s);
}

void vertex() {
	int row_base = int(INSTANCE_CUSTOM.w + 0.5) * guide_count;
	int g0 = int(INSTANCE_CUSTOM.x + 0.5);
	int g1 = int(COLOR.r + 0.5);
	int g2 = int(COLOR.g + 0.5);
	float w1 = COLOR.b;
	float w2 = COLOR.a;
	float w0 = 1.0 - w1 - w2;
	hair_seed = INSTANCE_CUSTOM.y;
	float t = UV.y;
	hair_t = t;
	float u = t * INSTANCE_CUSTOM.z;
	vec3 d0;
	vec3 d1;
	vec3 d2;
	vec3 p = w0 * guide_at(g0, row_base, u, d0) + w1 * guide_at(g1, row_base, u, d1) + w2 * guide_at(g2, row_base, u, d2);
	vec3 tw = w0 * d0 + w1 * d1 + w2 * d2;
	if (dot(tw, tw) < 1e-14) {
		tw = vec3(0.0, -1.0, 0.0);
	}
	vec3 pv = (VIEW_MATRIX * vec4(p, 1.0)).xyz;
	vec3 tv = normalize(mat3(VIEW_MATRIX) * tw);
	bool ortho = PROJECTION_MATRIX[3][3] != 0.0;
	vec3 to_eye = ortho ? vec3(0.0, 0.0, 1.0) : normalize(-pv);
	vec3 side = cross(tv, to_eye);
	float sl = length(side);
	side = sl > 1e-5 ? side / sl : vec3(1.0, 0.0, 0.0);
	// At least a pixel wide: a thinner strand's coverage goes to its alpha, dithered (TAA resolves it).
	float width = mix(root_width, tip_width, t) * width_scale;
	float pixel = (ortho ? 2.0 : 2.0 * max(-pv.z, 1e-4)) / (PROJECTION_MATRIX[1][1] * VIEWPORT_SIZE.y);
	float drawn = max(width, pixel);
	hair_coverage = width / drawn;
	VERTEX = pv + side * (UV.x * 0.5 * drawn);
	NORMAL = normalize(to_eye - tv * dot(to_eye, tv) + vec3(1e-6));
	TANGENT = tv;
	BINORMAL = side;
	hair_tangent = tv;
}

void fragment() {
	float v = (fract(sin(hair_seed * 91.3 + 7.1) * 43758.5453) - 0.5) * 2.0 * color_variation;
	vec3 col = mix(root_color.rgb, tip_color.rgb, smoothstep(0.0, 0.85, hair_t)) * (1.0 + v);
	ALBEDO = clamp(col, vec3(0.0), vec3(1.0));
	vec3 t = normalize(hair_tangent);
	// Facing the eye across the strand, whichever side the ribbon shows.
	NORMAL = normalize(VIEW - t * dot(VIEW, t) + vec3(1e-6));
	ALPHA = hair_coverage * (1.0 - 0.7 * smoothstep(0.8, 1.0, hair_t));
	ALPHA_HASH_SCALE = 1.0;
	ROUGHNESS = 0.6;
	SPECULAR = 0.0;
	// The light the hair keeps from itself, roughly: darker at the roots and on the strands under others (a strand's
	// random layer; the light's shadow maps add the rest).
	float layer = fract(hair_seed * 0.6180339 + 0.31);
	AO = mix(1.0 - root_occlusion, 1.0, smoothstep(0.0, 0.5, hair_t)) * mix(1.0 - layer_occlusion, 1.0, sqrt(layer));
	AO_LIGHT_AFFECT = 0.4;
}

// Kajiya-Kay's diffuse (the strand's cross-section lit from any side), two specular lobes about cones shifted along the
// strand (Marschner's R, the light's colour, and TRT, the hair's colour, broader), and light through it from behind.
void light() {
	vec3 t = normalize(hair_tangent);
	float tl = dot(t, LIGHT);
	float diffuse = sqrt(max(0.0, 1.0 - tl * tl));
	float wrap = clamp(dot(NORMAL, LIGHT) * 0.5 + 0.5, 0.0, 1.0);
	vec3 lc = LIGHT_COLOR * ATTENUATION;
	DIFFUSE_LIGHT += lc * diffuse * mix(0.25, 1.0, wrap) / PI;
	vec3 h = normalize(LIGHT + VIEW);
	vec3 t1 = normalize(t + NORMAL * specular_shift);
	vec3 t2 = normalize(t - NORMAL * specular_shift * 2.0);
	float h1 = dot(t1, h);
	float h2 = dot(t2, h);
	float r = pow(sqrt(max(0.0, 1.0 - h1 * h1)), specular_sharpness);
	float trt = pow(sqrt(max(0.0, 1.0 - h2 * h2)), specular_sharpness * 0.25);
	float facing = clamp(dot(NORMAL, LIGHT) * 2.0 + 0.6, 0.0, 1.0);
	SPECULAR_LIGHT += lc * facing * specular_strength * (vec3(r) * 0.12 + ALBEDO * trt * 1.2);
	float back = pow(clamp(dot(-LIGHT, VIEW), 0.0, 1.0), 4.0) * transmission;
	SPECULAR_LIGHT += ALBEDO * LIGHT_COLOR * mix(ATTENUATION, 1.0, 0.5) * back;
}
)";
}

FellowHair3D::FellowHair3D() {
	multimesh.instantiate();
	multimesh->set_transform_format(MultiMesh::TRANSFORM_3D);
	multimesh->set_use_colors(true);
	multimesh->set_use_custom_data(true);
	set_base(multimesh->get_rid());
}

FellowHair3D::~FellowHair3D() {
	set_base(RID());
}

void FellowHair3D::_clear() {
	built = false;
	positions_valid = false;
	guide_root.clear();
	guide_normal.clear();
	rest.clear();
	x.clear();
	prev.clear();
	seg_rest.clear();
	instance_data.clear();
	if (multimesh.is_valid()) {
		multimesh->set_instance_count(0);
	}
}

void FellowHair3D::_rebuild() {
	if (!is_inside_tree()) {
		return;
	}
	_clear();
	_build();
	update_gizmos();
}

// The guides' roots on the scalp's cap (a Fibonacci spiral over the sphere, kept where the cap and hairline allow), their
// styled shape (combed: out along the scalp's normal, then falling, kept outside the sphere — let fall under gravity
// in the node's space at build), and the strands: random roots on the cap, each between its three nearest guides.
void FellowHair3D::_build() {
	RandomPCG rng(uint64_t(int64_t(seed)) * 0x9E3779B97F4A7C15ull + 1442695040888963407ull);
	const float cap = Math::cos(Math::deg_to_rad(CLAMP(coverage, 1.0f, 180.0f)));
	const float face = Math::cos(Math::deg_to_rad(CLAMP(forehead_angle, 0.0f, 180.0f)));
	auto on_scalp = [&](const Vector3 &p_dir) {
		return p_dir.y >= cap && !(forehead_angle > 0.0f && p_dir.dot(Vector3(0, 0, -1)) > face);
	};
	const bool mesh_scalp = _scalp_mesh_triangles();
	// The guides' roots: on the mesh's painted faces, spread by farthest-point sampling over random points on them
	// (milestone 125); or on a Fibonacci spiral over the sphere's cap.
	if (mesh_scalp) {
		LocalVector<Vector3> cand_p;
		LocalVector<Vector3> cand_n;
		const int candidates = MAX(64, guide_count * 16);
		for (int i = 0; i < candidates; i++) {
			Vector3 p, n;
			_scalp_mesh_sample(rng, p, n);
			cand_p.push_back(p);
			cand_n.push_back(n);
		}
		LocalVector<float> nearest;
		nearest.resize(cand_p.size());
		for (float &f : nearest) {
			f = 1e30f;
		}
		uint32_t pick = 0;
		for (int i = 0; i < guide_count; i++) {
			guide_root.push_back(cand_p[pick]);
			guide_normal.push_back(cand_n[pick]);
			uint32_t next = 0;
			float far = -1.0f;
			for (uint32_t c = 0; c < cand_p.size(); c++) {
				nearest[c] = MIN(nearest[c], cand_p[c].distance_squared_to(cand_p[pick]));
				if (nearest[c] > far) {
					far = nearest[c];
					next = c;
				}
			}
			if (far <= 0.0f) {
				break; // fewer distinct points than guides
			}
			pick = next;
		}
	}
	LocalVector<Vector3> dirs;
	const int tries = MAX(16, guide_count * 8);
	for (int i = 0; !mesh_scalp && i < tries && int(dirs.size()) < guide_count * 4; i++) {
		const float y = 1.0f - 2.0f * (float(i) + 0.5f) / float(tries);
		const float r = Math::sqrt(MAX(0.0f, 1.0f - y * y));
		const float phi = float(i) * 2.39996323f;
		const Vector3 d(r * Math::cos(phi), y, r * Math::sin(phi));
		if (on_scalp(d)) {
			dirs.push_back(d);
		}
	}
	if (!mesh_scalp) {
		if (dirs.is_empty()) {
			return;
		}
		// Thin the spiral's points evenly down to guide_count.
		const int count = MIN(guide_count, int(dirs.size()));
		for (int i = 0; i < count; i++) {
			const Vector3 d = dirs[uint32_t(int64_t(i) * dirs.size() / count)];
			guide_root.push_back(scalp_center + d * scalp_radius);
			guide_normal.push_back(d);
		}
	}
	const int g = int(guide_root.size());
	if (g == 0) {
		return;
	}
	points = segments + 1;
	const float seg = length / float(segments);
	seg_rest.resize(g);
	for (int i = 0; i < g; i++) {
		seg_rest[i] = seg;
	}
	// The styled shape: each guide combed along the scalp — down and back (+Z), a little out from it — then falling;
	// relaxed with gravity, the sphere and the lengths.
	rest.resize(g * points);
	for (int i = 0; i < g; i++) {
		const Vector3 n = guide_normal[i];
		Vector3 comb = Vector3(0, -1, 0.7f);
		comb = (comb - n * comb.dot(n));
		comb = comb.length_squared() > 1e-6f ? comb.normalized() : Vector3(0, 0, 1);
		const Vector3 start = (comb + n * 0.25f).normalized();
		Vector3 p = guide_root[i];
		for (int k = 0; k < points; k++) {
			const float f = float(k) / float(segments);
			Vector3 dir = start.lerp(Vector3(0, -1, 0.15f).normalized(), CLAMP(f * 1.2f, 0.0f, 1.0f)).normalized();
			if (curl > 0.0f) {
				const float a = float(k) * seg * curl_frequency * Math::TAU + float(i) * 1.7f;
				const Vector3 side = dir.cross(Vector3(0, 0, 1)).normalized();
				dir = (dir + (side * Math::cos(a) + dir.cross(side) * Math::sin(a)) * (curl * Math::TAU * curl_frequency)).normalized();
			}
			rest[i * points + k] = p;
			p += dir * seg;
		}
	}
	const float keep_out = scalp_radius + collision_margin;
	for (int it = 0; it < 120; it++) {
		for (int i = 0; i < g; i++) {
			for (int k = 1; k < points; k++) {
				Vector3 &q = rest[i * points + k];
				q.y -= 0.002f * (1.0f - float(it) / 120.0f); // a little gravity
				const Vector3 rel = q - scalp_center;
				const float d = rel.length();
				if (d < keep_out && d > 1e-6f) {
					q = scalp_center + rel / d * keep_out;
				}
			}
			for (int k = 1; k < points; k++) {
				const Vector3 a = rest[i * points + k - 1];
				Vector3 &b = rest[i * points + k];
				const Vector3 ab = b - a;
				const float l = ab.length();
				if (l > 1e-7f) {
					b = a + ab * (seg / l);
				}
			}
		}
	}
	// The strands: roots on the cap, each between its three nearest guides.
	const int s_count = MAX(0, strand_count);
	instance_data.resize(s_count * FLOATS_PER_STRAND);
	float *w = instance_data.ptrw();
	int made = 0;
	for (int attempt = 0; made < s_count && attempt < s_count * 20; attempt++) {
		Vector3 root;
		if (mesh_scalp) {
			Vector3 n;
			_scalp_mesh_sample(rng, root, n);
		} else {
			const float y = rng.randf() * 2.0f - 1.0f;
			const float phi = rng.randf() * Math::TAU;
			const float r = Math::sqrt(MAX(0.0f, 1.0f - y * y));
			const Vector3 d(r * Math::cos(phi), y, r * Math::sin(phi));
			if (!on_scalp(d)) {
				continue;
			}
			root = scalp_center + d * scalp_radius;
		}
		int best[3] = { 0, 0, 0 };
		float best_d[3] = { 1e30f, 1e30f, 1e30f };
		for (int i = 0; i < g; i++) {
			const float dd = guide_root[i].distance_squared_to(root);
			for (int s = 0; s < 3; s++) {
				if (dd < best_d[s]) {
					for (int m = 2; m > s; m--) {
						best_d[m] = best_d[m - 1];
						best[m] = best[m - 1];
					}
					best_d[s] = dd;
					best[s] = i;
					break;
				}
			}
		}
		// Inverse-distance weights, pulled towards the nearest guide by (1 - spread): clumps at 0.
		float wt[3];
		float sum = 0.0f;
		for (int s = 0; s < 3; s++) {
			wt[s] = 1.0f / (Math::sqrt(best_d[s]) + 1e-4f);
			sum += wt[s];
		}
		for (int s = 0; s < 3; s++) {
			wt[s] /= sum;
		}
		const float sp = CLAMP(spread, 0.0f, 1.0f);
		const float w1 = wt[1] * sp;
		const float w2 = (g >= 3 ? wt[2] : 0.0f) * sp;
		float *o = w + made * FLOATS_PER_STRAND;
		o[0] = 1.0f, o[1] = 0.0f, o[2] = 0.0f, o[3] = 0.0f; // identity (the shader places the strand)
		o[4] = 0.0f, o[5] = 1.0f, o[6] = 0.0f, o[7] = 0.0f;
		o[8] = 0.0f, o[9] = 0.0f, o[10] = 1.0f, o[11] = 0.0f;
		o[12] = float(g >= 2 ? best[1] : best[0]);
		o[13] = float(g >= 3 ? best[2] : best[0]);
		o[14] = g >= 2 ? w1 : 0.0f;
		o[15] = w2;
		o[16] = float(best[0]);
		o[17] = rng.randf() * 100.0f;
		o[18] = CLAMP(1.0f - length_variation * rng.randf(), 0.05f, 1.0f);
		o[19] = 0.0f;
		made++;
	}
	instance_data.resize(made * FLOATS_PER_STRAND);
	// The strip every strand is drawn with.
	if (strip.is_null()) {
		strip.instantiate();
	} else {
		strip->clear_surfaces();
	}
	PackedVector3Array verts;
	PackedVector2Array uvs;
	PackedColorArray colors;
	PackedInt32Array indices;
	for (int k = 0; k <= STRIP_SEGMENTS; k++) {
		const float t = float(k) / float(STRIP_SEGMENTS);
		for (int side = -1; side <= 1; side += 2) {
			verts.push_back(Vector3(float(side) * 0.001f, -t * length, 0.0f)); // replaced in the shader
			uvs.push_back(Vector2(float(side), t));
			colors.push_back(Color(1, 1, 1, 1));
		}
		if (k < STRIP_SEGMENTS) {
			const int a = k * 2;
			indices.push_back(a);
			indices.push_back(a + 1);
			indices.push_back(a + 2);
			indices.push_back(a + 1);
			indices.push_back(a + 3);
			indices.push_back(a + 2);
		}
	}
	Array arrays;
	arrays.resize(Mesh::ARRAY_MAX);
	arrays[Mesh::ARRAY_VERTEX] = verts;
	arrays[Mesh::ARRAY_TEX_UV] = uvs;
	arrays[Mesh::ARRAY_COLOR] = colors;
	arrays[Mesh::ARRAY_INDEX] = indices;
	strip->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, arrays);
	if (material.is_null()) {
		material.instantiate();
		material->set_shader(FellowSharedShaders::shader(shader_code()));
	}
	strip->surface_set_material(0, material);
	multimesh->set_mesh(strip);
	multimesh->set_instance_count(made);
	visible_strands = -1; // the level of detail set again
	width_scale = 1.0f;
	// The guides' texture: two halves of the guides' points.
	guide_image = Image::create_empty(points, g * 2, false, Image::FORMAT_RGBAF);
	guide_texture = ImageTexture::create_from_image(guide_image);
	_update_material();
	// The multimesh's culling box: the scalp and the hair's length about it, in the node's space.
	multimesh->set_custom_aabb(AABB(scalp_center - Vector3(1, 1, 1) * (scalp_radius + length * 1.2f), Vector3(2, 2, 2) * (scalp_radius + length * 1.2f)));
	built = true;
	global_stats.guides = g;
	global_stats.strands = made;
	_reset_positions();
	_upload();
}

void FellowHair3D::_update_material() {
	if (material.is_null()) {
		return;
	}
	material->set_shader_parameter("guides", guide_texture);
	material->set_shader_parameter("guide_count", int(guide_root.size()));
	material->set_shader_parameter("points", points);
	material->set_shader_parameter("root_width", root_width * 0.001f);
	material->set_shader_parameter("tip_width", tip_width * 0.001f);
	material->set_shader_parameter("root_color", root_color);
	material->set_shader_parameter("tip_color", tip_color);
	material->set_shader_parameter("color_variation", color_variation);
	material->set_shader_parameter("specular_shift", specular_shift);
	material->set_shader_parameter("specular_strength", specular_strength);
	material->set_shader_parameter("specular_sharpness", specular_sharpness);
	material->set_shader_parameter("transmission", transmission);
	material->set_shader_parameter("root_occlusion", root_occlusion);
	material->set_shader_parameter("width_scale", width_scale);
}

Vector3 FellowHair3D::_rest_global(int p_guide, int p_point, const Transform3D &p_xform) const {
	return p_xform.xform(rest[p_guide * points + p_point]);
}

void FellowHair3D::_reset_positions() {
	const uint32_t n = rest.size();
	x.resize(n);
	prev.resize(n);
	const Transform3D xf = is_inside_tree() ? get_global_transform() : Transform3D();
	for (uint32_t i = 0; i < n; i++) {
		x[i] = xf.xform(rest[i]);
		prev[i] = x[i];
	}
	last_xform = xf;
	positions_valid = true;
}

void FellowHair3D::reset() {
	if (built) {
		_reset_positions();
		_upload();
	}
}

void FellowHair3D::_gather_colliders(LocalVector<Vector4> &r_spheres, LocalVector<Vector3> &r_capsule_a, LocalVector<Vector3> &r_capsule_b, LocalVector<float> &r_capsule_r) const {
	const Transform3D xf = get_global_transform();
	const Vector3 scale = xf.basis.get_scale();
	const float s = (Math::abs(scale.x) + Math::abs(scale.y) + Math::abs(scale.z)) / 3.0f;
	const Vector3 c = xf.xform(scalp_center);
	r_spheres.push_back(Vector4(c.x, c.y, c.z, (scalp_radius + collision_margin) * s));
	for (int i = 0; i < colliders.size(); i++) {
		const CollisionShape3D *cs = Object::cast_to<CollisionShape3D>(get_node_or_null(colliders[i]));
		if (!cs || cs->is_disabled() || cs->get_shape().is_null()) {
			continue;
		}
		const Transform3D sx = cs->get_global_transform();
		const Vector3 ss = sx.basis.get_scale();
		const float sm = MAX(Math::abs(ss.x), MAX(Math::abs(ss.y), Math::abs(ss.z)));
		if (const SphereShape3D *sphere = Object::cast_to<SphereShape3D>(cs->get_shape().ptr())) {
			r_spheres.push_back(Vector4(sx.origin.x, sx.origin.y, sx.origin.z, sphere->get_radius() * sm + collision_margin));
		} else if (const CapsuleShape3D *capsule = Object::cast_to<CapsuleShape3D>(cs->get_shape().ptr())) {
			const float half = MAX(0.0f, capsule->get_height() * 0.5f - capsule->get_radius());
			r_capsule_a.push_back(sx.xform(Vector3(0, half, 0)));
			r_capsule_b.push_back(sx.xform(Vector3(0, -half, 0)));
			r_capsule_r.push_back(capsule->get_radius() * sm + collision_margin);
		}
	}
}

// Position-based dynamics in substeps: Verlet with damping, gravity and the air's drag; the roots on the scalp; the
// segments' lengths (from the root out, the root's side immovable); the pull to the styled shape in the head's space,
// strongest at the root; out of the spheres and capsules.
// Milestone 126 (decision 0250): what the simulation needs from the scene, read on the main thread — the node's
// transform, gravity, the wind at each guide's middle, the colliders — so that the step itself can run on a worker.
// Returns whether it will run.
bool FellowHair3D::_prepare(double p_delta) {
	prep_run = false;
	if (!built) {
		return false;
	}
	_update_lod();
	bool near = true;
	if (simulation_distance > 0.0f) {
		const Camera3D *camera = get_viewport() ? get_viewport()->get_camera_3d() : nullptr;
		near = !camera || camera->get_global_position().distance_to(get_global_position()) <= simulation_distance;
	}
	simulated = simulating && near;
	if (!simulated) {
		_reset_positions(); // the styled shape, following the head
		return false;
	}
	prep_xf = get_global_transform();
	const int g = int(guide_root.size());
	if (!positions_valid || x.size() != rest.size() || prep_xf.origin.distance_to(last_xform.origin) > 1.0f) {
		_reset_positions(); // the first frame, or a teleport
	}
	prep_dt = float(MIN(p_delta, 1.0 / 20.0));
	if (prep_dt <= 0.0f) {
		return false;
	}
	ProjectSettings *settings = ProjectSettings::get_singleton();
	prep_gravity = Vector3(settings->get_setting("physics/3d/default_gravity_vector", Vector3(0, -1, 0))) * float(settings->get_setting("physics/3d/default_gravity", 9.8)) * gravity_scale;
	prep_wind.resize(g);
	FellowWind3D *fw = FellowWind3D::get_active();
	for (int i = 0; i < g; i++) {
		prep_wind[i] = (fw && wind_scale != 0.0f) ? fw->get_velocity_at(x[i * points + points / 2]) * wind_scale : Vector3();
	}
	prep_spheres.clear();
	prep_cap_a.clear();
	prep_cap_b.clear();
	prep_cap_r.clear();
	_gather_colliders(prep_spheres, prep_cap_a, prep_cap_b, prep_cap_r);
	prep_run = true;
	return true;
}

void FellowHair3D::_simulate() {
	if (!prep_run) {
		return;
	}
	const uint64_t t0 = OS::get_singleton()->get_ticks_usec();
	const Transform3D xf = prep_xf;
	const int g = int(guide_root.size());
	const int n_sub = MAX(1, substeps);
	const float dt = prep_dt / float(n_sub);
	const Vector3 gravity = prep_gravity;
	const LocalVector<Vector3> &wind = prep_wind;
	const LocalVector<Vector4> &spheres = prep_spheres;
	const LocalVector<Vector3> &cap_a = prep_cap_a;
	const LocalVector<Vector3> &cap_b = prep_cap_b;
	const LocalVector<float> &cap_r = prep_cap_r;
	const float decay = Math::exp(-damping * dt);
	for (int sub = 0; sub < n_sub; sub++) {
		// The head's transform within the frame (the roots move smoothly from the last frame's to this one's).
		const float f = float(sub + 1) / float(n_sub);
		Transform3D hx = last_xform.interpolate_with(xf, f);
		for (int i = 0; i < g; i++) {
			const float seg = seg_rest[i] * hx.basis.get_scale().y;
			Vector3 *p = &x[i * points];
			Vector3 *q = &prev[i * points];
			p[0] = hx.xform(rest[i * points]);
			q[0] = p[0];
			for (int k = 1; k < points; k++) {
				const Vector3 v = (p[k] - q[k]) * decay;
				const Vector3 air = (wind[i] - (p[k] - q[k]) / dt) * (drag * dt);
				q[k] = p[k];
				p[k] += v + gravity * (dt * dt) + air * dt;
			}
			// The styled shape, in the head's space: strongest at the root, none at the tip.
			if (stiffness > 0.0f) {
				for (int k = 1; k < points; k++) {
					const float along = float(k) / float(segments);
					const float pull = stiffness * (1.0f - along) * (1.0f - along);
					p[k] = p[k].lerp(hx.xform(rest[i * points + k]), pull);
				}
			}
			// From the root out (one pass: following the root keeps every length exact): each point at its segment's
			// length from the one before (the root's side immovable: hair does not stretch), out of the colliders (tested by
			// squared distance), and at its length again if one moved it — a collider's surface left by at most a little.
			for (int k = 1; k < points; k++) {
				Vector3 d = p[k] - p[k - 1];
				float l2 = d.length_squared();
				if (l2 > 1e-14f) {
					p[k] = p[k - 1] + d * (seg / Math::sqrt(l2));
				}
				bool pushed = false;
				for (const Vector4 &s : spheres) {
					const Vector3 rel(p[k].x - s.x, p[k].y - s.y, p[k].z - s.z);
					const float r2 = rel.length_squared();
					if (r2 < s.w * s.w && r2 > 1e-14f) {
						p[k] = Vector3(s.x, s.y, s.z) + rel * (s.w / Math::sqrt(r2));
						pushed = true;
					}
				}
				for (uint32_t c = 0; c < cap_a.size(); c++) {
					const Vector3 ab = cap_b[c] - cap_a[c];
					const float t = CLAMP((p[k] - cap_a[c]).dot(ab) / MAX(ab.length_squared(), 1e-8f), 0.0f, 1.0f);
					const Vector3 on = cap_a[c] + ab * t;
					const Vector3 rel = p[k] - on;
					const float r2 = rel.length_squared();
					if (r2 < cap_r[c] * cap_r[c] && r2 > 1e-14f) {
						p[k] = on + rel * (cap_r[c] / Math::sqrt(r2));
						pushed = true;
					}
				}
				if (pushed) {
					d = p[k] - p[k - 1];
					l2 = d.length_squared();
					if (l2 > 1e-14f) {
						p[k] = p[k - 1] + d * (seg / Math::sqrt(l2));
					}
				}
			}
		}
	}
	last_xform = xf;
	last_simulate_usec = OS::get_singleton()->get_ticks_usec() - t0; // (global_stats summed on the main thread)
}

// Milestone 126: every hair node of the frame — prepared on the main thread, stepped on the worker threads at once,
// sent to the GPU on the main thread — by the first of them processed in the frame.
void FellowHair3D::_group_task(uint32_t p_index, FellowHair3D *const *p_list) {
	p_list[p_index]->_simulate();
}

void FellowHair3D::_group_process(double p_delta) {
	const uint64_t t0 = OS::get_singleton()->get_ticks_usec();
	LocalVector<FellowHair3D *> all;
	LocalVector<FellowHair3D *> run;
	for (FellowHair3D *h : registry) {
		if (h->is_inside_tree() && h->built) {
			all.push_back(h);
			if (h->_prepare(p_delta)) {
				run.push_back(h);
			}
		}
	}
	if (run.size() == 1) {
		run[0]->_simulate();
	} else if (run.size() > 1) {
		WorkerThreadPool::GroupID group = WorkerThreadPool::get_singleton()->add_template_group_task(this, &FellowHair3D::_group_task, (FellowHair3D *const *)run.ptr(), run.size(), -1, true, SNAME("FellowHair3D"));
		WorkerThreadPool::get_singleton()->wait_for_group_task_completion(group);
	}
	for (FellowHair3D *h : all) {
		if (h->prep_run) {
			global_stats.simulate_usec += h->last_simulate_usec;
		}
		h->_upload();
	}
	global_stats.frames++;
	global_stats.group_usec += OS::get_singleton()->get_ticks_usec() - t0;
	global_stats.simulated += run.size();
}

// This frame's points into its half of the guides' texture (the other half keeps the last frame's), and the strands'
// instance data with that half: the previous frame's instance data (motion vectors) names the other.
void FellowHair3D::_upload() {
	if (!built || guide_image.is_null() || guide_texture.is_null()) {
		return;
	}
	const uint64_t t0 = OS::get_singleton()->get_ticks_usec();
	parity ^= 1;
	const int g = int(guide_root.size());
	Vector<uint8_t> bytes = guide_image->get_data();
	float *px = reinterpret_cast<float *>(bytes.ptrw());
	const Transform3D xf = get_global_transform();
	for (int i = 0; i < g; i++) {
		for (int k = 0; k < points; k++) {
			const Vector3 p = positions_valid ? x[i * points + k] : xf.xform(rest[i * points + k]);
			float *o = px + ((parity * g + i) * points + k) * 4;
			o[0] = p.x;
			o[1] = p.y;
			o[2] = p.z;
			o[3] = 1.0f;
		}
	}
	if (!positions_valid) {
		// Both halves the same: no motion.
		memcpy(px + ((1 - parity) * g * points) * 4, px + (parity * g * points) * 4, sizeof(float) * 4 * g * points);
	}
	guide_image->set_data(points, g * 2, false, Image::FORMAT_RGBAF, bytes);
	guide_texture->update(guide_image);
	float *w = instance_data.ptrw();
	const int strands = instance_data.size() / FLOATS_PER_STRAND;
	for (int s = 0; s < strands; s++) {
		w[s * FLOATS_PER_STRAND + 19] = float(parity);
	}
	RenderingServer::get_singleton()->multimesh_set_buffer(multimesh->get_rid(), instance_data);
	last_upload_usec = OS::get_singleton()->get_ticks_usec() - t0;
	global_stats.upload_usec += last_upload_usec;
}

void FellowHair3D::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_ENTER_TREE: {
			registry.push_back(this);
			_rebuild();
			set_process_internal(!Engine::get_singleton()->is_editor_hint());
			set_notify_transform(Engine::get_singleton()->is_editor_hint());
		} break;
		case NOTIFICATION_EXIT_TREE: {
			registry.erase(this);
			set_process_internal(false);
		} break;
		case NOTIFICATION_INTERNAL_PROCESS: {
			const uint64_t frame = Engine::get_singleton()->get_process_frames();
			if (frame != group_frame) {
				group_frame = frame;
				_group_process(get_process_delta_time());
			}
		} break;
		case NOTIFICATION_TRANSFORM_CHANGED: {
			if (Engine::get_singleton()->is_editor_hint() && built) {
				_reset_positions(); // the editor shows the styled shape where the node is
				_upload();
			}
		} break;
	}
}

AABB FellowHair3D::get_aabb() const {
	const float r = scalp_radius + length * 1.2f;
	return AABB(scalp_center - Vector3(r, r, r), Vector3(r, r, r) * 2.0f);
}

PackedStringArray FellowHair3D::get_configuration_warnings() const {
	PackedStringArray warnings = GeometryInstance3D::get_configuration_warnings();
	if (is_inside_tree() && built && guide_root.size() < 3) {
		warnings.push_back(RTR("Fewer than three guides fit on the scalp: widen Coverage or narrow Forehead Angle."));
	}
	return warnings;
}

Dictionary FellowHair3D::get_stats() const {
	Dictionary d;
	d["guides"] = int(guide_root.size());
	d["points"] = points;
	d["strands"] = int(instance_data.size() / FLOATS_PER_STRAND);
	d["simulate_usec"] = int64_t(last_simulate_usec);
	d["upload_usec"] = int64_t(last_upload_usec);
	d["visible_strands"] = visible_strands < 0 ? int(instance_data.size() / FLOATS_PER_STRAND) : visible_strands; // milestone 125
	d["width_scale"] = width_scale;
	d["simulated"] = simulated; // the last frame (not beyond simulation_distance, simulating on)
	return d;
}

// Every hair node's work since the last reset (milestone 126).
Dictionary FellowHair3D::get_global_stats() {
	Dictionary d;
	d["frames"] = int64_t(global_stats.frames);
	d["group_usec"] = int64_t(global_stats.group_usec);
	d["simulate_usec"] = int64_t(global_stats.simulate_usec);
	d["upload_usec"] = int64_t(global_stats.upload_usec);
	d["simulated"] = int64_t(global_stats.simulated);
	return d;
}

void FellowHair3D::reset_global_stats() {
	global_stats = GlobalStats();
}

PackedVector3Array FellowHair3D::get_guide_points(int p_guide) const {
	PackedVector3Array out;
	if (p_guide < 0 || p_guide >= int(guide_root.size())) {
		return out;
	}
	const Transform3D xf = is_inside_tree() ? get_global_transform() : Transform3D();
	for (int k = 0; k < points; k++) {
		out.push_back(positions_valid && x.size() == rest.size() ? x[p_guide * points + k] : xf.xform(rest[p_guide * points + k]));
	}
	return out;
}

// Milestone 125: the scalp mesh's faces the roots go on (its painted ones), with their areas' running sum.
bool FellowHair3D::_scalp_mesh_triangles() {
	scalp_tris.clear();
	scalp_cdf.clear();
	if (scalp_mesh.is_null()) {
		return false;
	}
	float total = 0.0f;
	for (int s = 0; s < scalp_mesh->get_surface_count(); s++) {
		if (scalp_mesh->surface_get_primitive_type(s) != Mesh::PRIMITIVE_TRIANGLES) {
			continue;
		}
		const Array arrays = scalp_mesh->surface_get_arrays(s);
		const PackedVector3Array verts = arrays[Mesh::ARRAY_VERTEX];
		const PackedVector3Array normals = arrays[Mesh::ARRAY_NORMAL];
		const PackedColorArray colors = arrays[Mesh::ARRAY_COLOR];
		const PackedInt32Array indices = arrays[Mesh::ARRAY_INDEX];
		const int count = indices.is_empty() ? verts.size() : indices.size();
		for (int i = 0; i + 2 < count; i += 3) {
			const int ia = indices.is_empty() ? i : indices[i];
			const int ib = indices.is_empty() ? i + 1 : indices[i + 1];
			const int ic = indices.is_empty() ? i + 2 : indices[i + 2];
			if (ia >= verts.size() || ib >= verts.size() || ic >= verts.size()) {
				continue;
			}
			if (colors.size() == verts.size() && (colors[ia].r < 0.5f || colors[ib].r < 0.5f || colors[ic].r < 0.5f)) {
				continue; // not painted (all three of its vertices)
			}
			ScalpTri tri;
			tri.a = verts[ia];
			tri.b = verts[ib];
			tri.c = verts[ic];
			// Godot's front faces are clockwise: the outward normal is (c - a) × (b - a).
			const Vector3 face = (tri.c - tri.a).cross(tri.b - tri.a);
			tri.area = face.length() * 0.5f;
			if (tri.area <= 1e-12f) {
				continue;
			}
			const Vector3 fn = face.normalized();
			const bool has_normals = normals.size() == verts.size();
			tri.na = has_normals ? normals[ia] : fn;
			tri.nb = has_normals ? normals[ib] : fn;
			tri.nc = has_normals ? normals[ic] : fn;
			total += tri.area;
			scalp_tris.push_back(tri);
			scalp_cdf.push_back(total);
		}
	}
	return !scalp_tris.is_empty();
}

// A random point on the painted faces (by area), and the mesh's normal there.
void FellowHair3D::_scalp_mesh_sample(RandomPCG &p_rng, Vector3 &r_position, Vector3 &r_normal) const {
	const float u = p_rng.randf() * scalp_cdf[scalp_cdf.size() - 1];
	uint32_t lo = 0;
	uint32_t hi = scalp_cdf.size() - 1;
	while (lo < hi) {
		const uint32_t mid = (lo + hi) / 2;
		if (scalp_cdf[mid] < u) {
			lo = mid + 1;
		} else {
			hi = mid;
		}
	}
	const ScalpTri &tri = scalp_tris[lo];
	const float r1 = Math::sqrt(p_rng.randf());
	const float r2 = p_rng.randf();
	const float wa = 1.0f - r1;
	const float wb = r1 * (1.0f - r2);
	const float wc = r1 * r2;
	r_position = tri.a * wa + tri.b * wb + tri.c * wc;
	r_normal = (tri.na * wa + tri.nb * wb + tri.nc * wc).normalized();
}

// Milestone 125: the strands drawn by the camera's distance — all within lod_distance, then a share falling with the
// square of the distance (the strands' screen size), at least min_strand_share; the drawn ones wider in proportion so
// that the hair's coverage stays (the roots are in random order: any first share covers the scalp).
void FellowHair3D::_update_lod() {
	const int total = int(instance_data.size() / FLOATS_PER_STRAND);
	int visible = total;
	if (lod_distance > 0.0f && total > 0) {
		const Camera3D *camera = get_viewport() ? get_viewport()->get_camera_3d() : nullptr;
		if (camera && camera->get_projection() == Camera3D::PROJECTION_PERSPECTIVE) {
			// The distance at which the hair would look this size through Godot's default 75-degree view: a zoomed
			// camera's far hair keeps its strands.
			const float zoom = Math::tan(Math::deg_to_rad(camera->get_fov()) * 0.5f) / Math::tan(Math::deg_to_rad(75.0f) * 0.5f);
			const float d = camera->get_global_position().distance_to(get_global_position()) * zoom;
			const float share = d > lod_distance ? MAX(min_strand_share, (lod_distance * lod_distance) / (d * d)) : 1.0f;
			visible = CLAMP(int(Math::round(float(total) * share)), 1, total);
		}
	}
	if (visible == visible_strands) {
		return;
	}
	visible_strands = visible;
	width_scale = total > 0 ? float(total) / float(visible) : 1.0f;
	multimesh->set_visible_instance_count(visible == total ? -1 : visible);
	if (material.is_valid()) {
		material->set_shader_parameter("width_scale", width_scale);
	}
}

void FellowHair3D::set_scalp_mesh(const Ref<Mesh> &p_mesh) {
	scalp_mesh = p_mesh;
	_rebuild();
}

// A guide's styled shape where the node is now (global): the simulated points' targets.
PackedVector3Array FellowHair3D::get_guide_rest_points(int p_guide) const {
	PackedVector3Array out;
	if (p_guide < 0 || p_guide >= int(guide_root.size())) {
		return out;
	}
	const Transform3D xf = is_inside_tree() ? get_global_transform() : Transform3D();
	for (int k = 0; k < points; k++) {
		out.push_back(xf.xform(rest[p_guide * points + k]));
	}
	return out;
}

#define FELLOW_HAIR_REBUILD_SETTER(m_name, m_type, m_value) \
	void FellowHair3D::set_##m_name(m_type p_value) { \
		m_name = m_value; \
		_rebuild(); \
	}
#define FELLOW_HAIR_LOOK_SETTER(m_name, m_type, m_value) \
	void FellowHair3D::set_##m_name(m_type p_value) { \
		m_name = m_value; \
		_update_material(); \
	}

FELLOW_HAIR_REBUILD_SETTER(scalp_radius, float, MAX(0.001f, p_value))
FELLOW_HAIR_REBUILD_SETTER(scalp_center, const Vector3 &, p_value)
FELLOW_HAIR_REBUILD_SETTER(coverage, float, CLAMP(p_value, 1.0f, 180.0f))
FELLOW_HAIR_REBUILD_SETTER(forehead_angle, float, CLAMP(p_value, 0.0f, 180.0f))
FELLOW_HAIR_REBUILD_SETTER(guide_count, int, CLAMP(p_value, 3, 4096))
FELLOW_HAIR_REBUILD_SETTER(strand_count, int, CLAMP(p_value, 0, 200000))
FELLOW_HAIR_REBUILD_SETTER(segments, int, CLAMP(p_value, 2, 64))
FELLOW_HAIR_REBUILD_SETTER(length, float, MAX(0.001f, p_value))
FELLOW_HAIR_REBUILD_SETTER(length_variation, float, CLAMP(p_value, 0.0f, 0.95f))
FELLOW_HAIR_REBUILD_SETTER(curl, float, MAX(0.0f, p_value))
FELLOW_HAIR_REBUILD_SETTER(curl_frequency, float, MAX(0.0f, p_value))
FELLOW_HAIR_REBUILD_SETTER(spread, float, CLAMP(p_value, 0.0f, 1.0f))
FELLOW_HAIR_REBUILD_SETTER(seed, int, p_value)
FELLOW_HAIR_LOOK_SETTER(root_width, float, MAX(0.001f, p_value))
FELLOW_HAIR_LOOK_SETTER(tip_width, float, MAX(0.001f, p_value))
FELLOW_HAIR_LOOK_SETTER(root_color, const Color &, p_value)
FELLOW_HAIR_LOOK_SETTER(tip_color, const Color &, p_value)
FELLOW_HAIR_LOOK_SETTER(color_variation, float, CLAMP(p_value, 0.0f, 1.0f))
FELLOW_HAIR_LOOK_SETTER(specular_shift, float, CLAMP(p_value, -1.0f, 1.0f))
FELLOW_HAIR_LOOK_SETTER(specular_strength, float, MAX(0.0f, p_value))
FELLOW_HAIR_LOOK_SETTER(specular_sharpness, float, MAX(1.0f, p_value))
FELLOW_HAIR_LOOK_SETTER(transmission, float, MAX(0.0f, p_value))
FELLOW_HAIR_LOOK_SETTER(root_occlusion, float, CLAMP(p_value, 0.0f, 1.0f))

void FellowHair3D::set_simulating(bool p_simulating) {
	simulating = p_simulating;
	if (!simulating) {
		reset();
	}
}

void FellowHair3D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("reset"), &FellowHair3D::reset);
	ClassDB::bind_method(D_METHOD("get_stats"), &FellowHair3D::get_stats);
	ClassDB::bind_method(D_METHOD("get_guide_points", "guide"), &FellowHair3D::get_guide_points);
	ClassDB::bind_method(D_METHOD("get_guide_rest_points", "guide"), &FellowHair3D::get_guide_rest_points);
	ClassDB::bind_method(D_METHOD("get_point_count"), &FellowHair3D::get_point_count);
	ClassDB::bind_static_method("FellowHair3D", D_METHOD("get_global_stats"), &FellowHair3D::get_global_stats);
	ClassDB::bind_static_method("FellowHair3D", D_METHOD("reset_global_stats"), &FellowHair3D::reset_global_stats);
	ClassDB::bind_method(D_METHOD("set_scalp_mesh", "mesh"), &FellowHair3D::set_scalp_mesh);
	ClassDB::bind_method(D_METHOD("get_scalp_mesh"), &FellowHair3D::get_scalp_mesh);
	ClassDB::bind_method(D_METHOD("set_lod_distance", "distance"), &FellowHair3D::set_lod_distance);
	ClassDB::bind_method(D_METHOD("get_lod_distance"), &FellowHair3D::get_lod_distance);
	ClassDB::bind_method(D_METHOD("set_min_strand_share", "share"), &FellowHair3D::set_min_strand_share);
	ClassDB::bind_method(D_METHOD("get_min_strand_share"), &FellowHair3D::get_min_strand_share);
	ClassDB::bind_method(D_METHOD("set_simulation_distance", "distance"), &FellowHair3D::set_simulation_distance);
	ClassDB::bind_method(D_METHOD("get_simulation_distance"), &FellowHair3D::get_simulation_distance);

#define FELLOW_HAIR_BIND(m_name, m_getter) \
	ClassDB::bind_method(D_METHOD("set_" #m_name, "value"), &FellowHair3D::set_##m_name); \
	ClassDB::bind_method(D_METHOD(#m_getter), &FellowHair3D::m_getter);
	FELLOW_HAIR_BIND(scalp_radius, get_scalp_radius)
	FELLOW_HAIR_BIND(scalp_center, get_scalp_center)
	FELLOW_HAIR_BIND(coverage, get_coverage)
	FELLOW_HAIR_BIND(forehead_angle, get_forehead_angle)
	FELLOW_HAIR_BIND(guide_count, get_guide_count)
	FELLOW_HAIR_BIND(strand_count, get_strand_count)
	FELLOW_HAIR_BIND(segments, get_segments)
	FELLOW_HAIR_BIND(length, get_length)
	FELLOW_HAIR_BIND(length_variation, get_length_variation)
	FELLOW_HAIR_BIND(curl, get_curl)
	FELLOW_HAIR_BIND(curl_frequency, get_curl_frequency)
	FELLOW_HAIR_BIND(spread, get_spread)
	FELLOW_HAIR_BIND(seed, get_seed)
	FELLOW_HAIR_BIND(simulating, is_simulating)
	FELLOW_HAIR_BIND(stiffness, get_stiffness)
	FELLOW_HAIR_BIND(damping, get_damping)
	FELLOW_HAIR_BIND(gravity_scale, get_gravity_scale)
	FELLOW_HAIR_BIND(wind_scale, get_wind_scale)
	FELLOW_HAIR_BIND(drag, get_drag)
	FELLOW_HAIR_BIND(collision_margin, get_collision_margin)
	FELLOW_HAIR_BIND(colliders, get_colliders)
	FELLOW_HAIR_BIND(substeps, get_substeps)
	FELLOW_HAIR_BIND(root_width, get_root_width)
	FELLOW_HAIR_BIND(tip_width, get_tip_width)
	FELLOW_HAIR_BIND(root_color, get_root_color)
	FELLOW_HAIR_BIND(tip_color, get_tip_color)
	FELLOW_HAIR_BIND(color_variation, get_color_variation)
	FELLOW_HAIR_BIND(specular_shift, get_specular_shift)
	FELLOW_HAIR_BIND(specular_strength, get_specular_strength)
	FELLOW_HAIR_BIND(specular_sharpness, get_specular_sharpness)
	FELLOW_HAIR_BIND(transmission, get_transmission)
	FELLOW_HAIR_BIND(root_occlusion, get_root_occlusion)
#undef FELLOW_HAIR_BIND

	ADD_GROUP("Scalp", "");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "scalp_radius", PROPERTY_HINT_RANGE, "0.01,1,0.001,or_greater,suffix:m"), "set_scalp_radius", "get_scalp_radius");
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR3, "scalp_center", PROPERTY_HINT_NONE, "suffix:m"), "set_scalp_center", "get_scalp_center");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "coverage", PROPERTY_HINT_RANGE, "1,180,0.1,degrees"), "set_coverage", "get_coverage");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "forehead_angle", PROPERTY_HINT_RANGE, "0,180,0.1,degrees"), "set_forehead_angle", "get_forehead_angle");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "scalp_mesh", PROPERTY_HINT_RESOURCE_TYPE, "Mesh"), "set_scalp_mesh", "get_scalp_mesh");
	ADD_GROUP("Strands", "");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "guide_count", PROPERTY_HINT_RANGE, "3,1024,1,or_greater"), "set_guide_count", "get_guide_count");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "strand_count", PROPERTY_HINT_RANGE, "0,50000,1,or_greater"), "set_strand_count", "get_strand_count");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "segments", PROPERTY_HINT_RANGE, "2,64,1"), "set_segments", "get_segments");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "length", PROPERTY_HINT_RANGE, "0.01,2,0.001,or_greater,suffix:m"), "set_length", "get_length");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "length_variation", PROPERTY_HINT_RANGE, "0,0.95,0.01"), "set_length_variation", "get_length_variation");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "curl", PROPERTY_HINT_RANGE, "0,0.05,0.0005,or_greater,suffix:m"), "set_curl", "get_curl");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "curl_frequency", PROPERTY_HINT_RANGE, "0,100,0.1,or_greater"), "set_curl_frequency", "get_curl_frequency");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "spread", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_spread", "get_spread");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "seed"), "set_seed", "get_seed");
	ADD_GROUP("Motion", "");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "simulating"), "set_simulating", "is_simulating");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "stiffness", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_stiffness", "get_stiffness");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "damping", PROPERTY_HINT_RANGE, "0,20,0.01,or_greater"), "set_damping", "get_damping");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "gravity_scale", PROPERTY_HINT_RANGE, "-2,4,0.01"), "set_gravity_scale", "get_gravity_scale");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "wind_scale", PROPERTY_HINT_RANGE, "0,4,0.01"), "set_wind_scale", "get_wind_scale");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "drag", PROPERTY_HINT_RANGE, "0,10,0.01,or_greater"), "set_drag", "get_drag");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "collision_margin", PROPERTY_HINT_RANGE, "0,0.1,0.001,or_greater,suffix:m"), "set_collision_margin", "get_collision_margin");
	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "colliders", PROPERTY_HINT_ARRAY_TYPE, MAKE_RESOURCE_TYPE_HINT("NodePath")), "set_colliders", "get_colliders");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "substeps", PROPERTY_HINT_RANGE, "1,16,1"), "set_substeps", "get_substeps");
	ADD_GROUP("Level of Detail", "");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "lod_distance", PROPERTY_HINT_RANGE, "0,50,0.1,or_greater,suffix:m"), "set_lod_distance", "get_lod_distance");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "min_strand_share", PROPERTY_HINT_RANGE, "0.001,1,0.001"), "set_min_strand_share", "get_min_strand_share");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "simulation_distance", PROPERTY_HINT_RANGE, "0,500,0.1,or_greater,suffix:m"), "set_simulation_distance", "get_simulation_distance");
	ADD_GROUP("Look", "");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "root_width", PROPERTY_HINT_RANGE, "0.001,5,0.001,or_greater,suffix:mm"), "set_root_width", "get_root_width");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "tip_width", PROPERTY_HINT_RANGE, "0.001,5,0.001,or_greater,suffix:mm"), "set_tip_width", "get_tip_width");
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "root_color", PROPERTY_HINT_COLOR_NO_ALPHA), "set_root_color", "get_root_color");
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "tip_color", PROPERTY_HINT_COLOR_NO_ALPHA), "set_tip_color", "get_tip_color");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "color_variation", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_color_variation", "get_color_variation");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "specular_shift", PROPERTY_HINT_RANGE, "-1,1,0.01"), "set_specular_shift", "get_specular_shift");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "specular_strength", PROPERTY_HINT_RANGE, "0,4,0.01,or_greater"), "set_specular_strength", "get_specular_strength");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "specular_sharpness", PROPERTY_HINT_RANGE, "1,1000,1"), "set_specular_sharpness", "get_specular_sharpness");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "transmission", PROPERTY_HINT_RANGE, "0,4,0.01"), "set_transmission", "get_transmission");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "root_occlusion", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_root_occlusion", "get_root_occlusion");
}
