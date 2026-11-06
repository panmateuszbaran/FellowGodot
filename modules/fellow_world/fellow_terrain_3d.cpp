/**************************************************************************/
/*  fellow_terrain_3d.cpp                                                 */
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

#include "fellow_terrain_3d.h"

#include "core/config/engine.h"
#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "core/object/worker_thread_pool.h"
#include "core/os/os.h"
#include "scene/3d/camera_3d.h"
#include "scene/main/viewport.h"
#include "scene/resources/3d/world_3d.h"
#include "servers/physics_3d/physics_server_3d.h"
#include "servers/rendering/fellow_settings.h"
#include "servers/rendering/rendering_server.h"

#include "modules/fellow_fluid/fellow_shared_shaders.h"

#ifdef TOOLS_ENABLED
#include "editor/scene/3d/node_3d_editor_plugin.h"
#endif

// ---------------------------------------------------------------------------------------------------------------------
// FellowTerrainLayer

void FellowTerrainLayer::set_albedo_texture(const Ref<Texture2D> &p_texture) {
	albedo_texture = p_texture;
	emit_changed();
}

void FellowTerrainLayer::set_albedo_color(const Color &p_color) {
	albedo_color = p_color;
	emit_changed();
}

void FellowTerrainLayer::set_height_in_albedo_alpha(bool p_enable) {
	height_in_albedo_alpha = p_enable;
	emit_changed();
}

void FellowTerrainLayer::set_normal_texture(const Ref<Texture2D> &p_texture) {
	normal_texture = p_texture;
	emit_changed();
}

void FellowTerrainLayer::set_normal_strength(float p_strength) {
	normal_strength = CLAMP(p_strength, 0.0f, 4.0f);
	emit_changed();
}

void FellowTerrainLayer::set_roughness(float p_roughness) {
	roughness = CLAMP(p_roughness, 0.0f, 1.0f);
	emit_changed();
}

void FellowTerrainLayer::set_texture_size(float p_metres) {
	texture_size = MAX(0.01f, p_metres);
	emit_changed();
}

void FellowTerrainLayer::set_triplanar(bool p_enable) {
	triplanar = p_enable;
	emit_changed();
}

void FellowTerrainLayer::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_albedo_texture", "texture"), &FellowTerrainLayer::set_albedo_texture);
	ClassDB::bind_method(D_METHOD("get_albedo_texture"), &FellowTerrainLayer::get_albedo_texture);
	ClassDB::bind_method(D_METHOD("set_albedo_color", "color"), &FellowTerrainLayer::set_albedo_color);
	ClassDB::bind_method(D_METHOD("get_albedo_color"), &FellowTerrainLayer::get_albedo_color);
	ClassDB::bind_method(D_METHOD("set_height_in_albedo_alpha", "enable"), &FellowTerrainLayer::set_height_in_albedo_alpha);
	ClassDB::bind_method(D_METHOD("is_height_in_albedo_alpha"), &FellowTerrainLayer::is_height_in_albedo_alpha);
	ClassDB::bind_method(D_METHOD("set_normal_texture", "texture"), &FellowTerrainLayer::set_normal_texture);
	ClassDB::bind_method(D_METHOD("get_normal_texture"), &FellowTerrainLayer::get_normal_texture);
	ClassDB::bind_method(D_METHOD("set_normal_strength", "strength"), &FellowTerrainLayer::set_normal_strength);
	ClassDB::bind_method(D_METHOD("get_normal_strength"), &FellowTerrainLayer::get_normal_strength);
	ClassDB::bind_method(D_METHOD("set_roughness", "roughness"), &FellowTerrainLayer::set_roughness);
	ClassDB::bind_method(D_METHOD("get_roughness"), &FellowTerrainLayer::get_roughness);
	ClassDB::bind_method(D_METHOD("set_texture_size", "metres"), &FellowTerrainLayer::set_texture_size);
	ClassDB::bind_method(D_METHOD("get_texture_size"), &FellowTerrainLayer::get_texture_size);
	ClassDB::bind_method(D_METHOD("set_triplanar", "enable"), &FellowTerrainLayer::set_triplanar);
	ClassDB::bind_method(D_METHOD("is_triplanar"), &FellowTerrainLayer::is_triplanar);

	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "albedo_texture", PROPERTY_HINT_RESOURCE_TYPE, "Texture2D"), "set_albedo_texture", "get_albedo_texture");
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "albedo_color", PROPERTY_HINT_COLOR_NO_ALPHA), "set_albedo_color", "get_albedo_color");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "height_in_albedo_alpha"), "set_height_in_albedo_alpha", "is_height_in_albedo_alpha");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "normal_texture", PROPERTY_HINT_RESOURCE_TYPE, "Texture2D"), "set_normal_texture", "get_normal_texture");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "normal_strength", PROPERTY_HINT_RANGE, "0,4,0.01"), "set_normal_strength", "get_normal_strength");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "roughness", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_roughness", "get_roughness");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "texture_size", PROPERTY_HINT_RANGE, "0.01,100,0.01,or_greater,suffix:m"), "set_texture_size", "get_texture_size");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "triplanar"), "set_triplanar", "is_triplanar");
}

// ---------------------------------------------------------------------------------------------------------------------
// FellowTerrainFoliage

#define FOLIAGE_SETTER(m_setter, m_type, m_member, m_value) \
	void FellowTerrainFoliage::m_setter(m_type p_value) { \
		m_member = m_value; \
		emit_changed(); \
	}

FOLIAGE_SETTER(set_mesh, const Ref<Mesh> &, mesh, p_value)
FOLIAGE_SETTER(set_layer, int, layer, CLAMP(p_value, 0, FellowTerrain3D::MAX_LAYERS - 1))
FOLIAGE_SETTER(set_density, float, density, CLAMP(p_value, 0.0f, 100.0f))
FOLIAGE_SETTER(set_min_weight, float, min_weight, CLAMP(p_value, 0.0f, 1.0f))
FOLIAGE_SETTER(set_range, float, range, MAX(1.0f, p_value))
FOLIAGE_SETTER(set_cell_size, float, cell_size, MAX(0.0f, p_value))
FOLIAGE_SETTER(set_slope_min, float, slope_min, CLAMP(p_value, 0.0f, 90.0f))
FOLIAGE_SETTER(set_slope_max, float, slope_max, CLAMP(p_value, 0.0f, 90.0f))
FOLIAGE_SETTER(set_height_min, float, height_min, p_value)
FOLIAGE_SETTER(set_height_max, float, height_max, p_value)
FOLIAGE_SETTER(set_scale_min, float, scale_min, MAX(0.001f, p_value))
FOLIAGE_SETTER(set_scale_max, float, scale_max, MAX(0.001f, p_value))
FOLIAGE_SETTER(set_random_yaw, bool, random_yaw, p_value)
FOLIAGE_SETTER(set_align_to_normal, float, align_to_normal, CLAMP(p_value, 0.0f, 1.0f))
FOLIAGE_SETTER(set_sink, float, sink, p_value)
FOLIAGE_SETTER(set_cast_shadows, bool, cast_shadows, p_value)
FOLIAGE_SETTER(set_seed, int, seed, p_value)

#undef FOLIAGE_SETTER

void FellowTerrainFoliage::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_mesh", "mesh"), &FellowTerrainFoliage::set_mesh);
	ClassDB::bind_method(D_METHOD("get_mesh"), &FellowTerrainFoliage::get_mesh);
	ClassDB::bind_method(D_METHOD("set_layer", "layer"), &FellowTerrainFoliage::set_layer);
	ClassDB::bind_method(D_METHOD("get_layer"), &FellowTerrainFoliage::get_layer);
	ClassDB::bind_method(D_METHOD("set_density", "density"), &FellowTerrainFoliage::set_density);
	ClassDB::bind_method(D_METHOD("get_density"), &FellowTerrainFoliage::get_density);
	ClassDB::bind_method(D_METHOD("set_min_weight", "weight"), &FellowTerrainFoliage::set_min_weight);
	ClassDB::bind_method(D_METHOD("get_min_weight"), &FellowTerrainFoliage::get_min_weight);
	ClassDB::bind_method(D_METHOD("set_range", "metres"), &FellowTerrainFoliage::set_range);
	ClassDB::bind_method(D_METHOD("get_range"), &FellowTerrainFoliage::get_range);
	ClassDB::bind_method(D_METHOD("set_cell_size", "metres"), &FellowTerrainFoliage::set_cell_size);
	ClassDB::bind_method(D_METHOD("get_cell_size"), &FellowTerrainFoliage::get_cell_size);
	ClassDB::bind_method(D_METHOD("set_slope_min", "degrees"), &FellowTerrainFoliage::set_slope_min);
	ClassDB::bind_method(D_METHOD("get_slope_min"), &FellowTerrainFoliage::get_slope_min);
	ClassDB::bind_method(D_METHOD("set_slope_max", "degrees"), &FellowTerrainFoliage::set_slope_max);
	ClassDB::bind_method(D_METHOD("get_slope_max"), &FellowTerrainFoliage::get_slope_max);
	ClassDB::bind_method(D_METHOD("set_height_min", "metres"), &FellowTerrainFoliage::set_height_min);
	ClassDB::bind_method(D_METHOD("get_height_min"), &FellowTerrainFoliage::get_height_min);
	ClassDB::bind_method(D_METHOD("set_height_max", "metres"), &FellowTerrainFoliage::set_height_max);
	ClassDB::bind_method(D_METHOD("get_height_max"), &FellowTerrainFoliage::get_height_max);
	ClassDB::bind_method(D_METHOD("set_scale_min", "scale"), &FellowTerrainFoliage::set_scale_min);
	ClassDB::bind_method(D_METHOD("get_scale_min"), &FellowTerrainFoliage::get_scale_min);
	ClassDB::bind_method(D_METHOD("set_scale_max", "scale"), &FellowTerrainFoliage::set_scale_max);
	ClassDB::bind_method(D_METHOD("get_scale_max"), &FellowTerrainFoliage::get_scale_max);
	ClassDB::bind_method(D_METHOD("set_random_yaw", "enable"), &FellowTerrainFoliage::set_random_yaw);
	ClassDB::bind_method(D_METHOD("is_random_yaw"), &FellowTerrainFoliage::is_random_yaw);
	ClassDB::bind_method(D_METHOD("set_align_to_normal", "amount"), &FellowTerrainFoliage::set_align_to_normal);
	ClassDB::bind_method(D_METHOD("get_align_to_normal"), &FellowTerrainFoliage::get_align_to_normal);
	ClassDB::bind_method(D_METHOD("set_sink", "metres"), &FellowTerrainFoliage::set_sink);
	ClassDB::bind_method(D_METHOD("get_sink"), &FellowTerrainFoliage::get_sink);
	ClassDB::bind_method(D_METHOD("set_cast_shadows", "enable"), &FellowTerrainFoliage::set_cast_shadows);
	ClassDB::bind_method(D_METHOD("is_casting_shadows"), &FellowTerrainFoliage::is_casting_shadows);
	ClassDB::bind_method(D_METHOD("set_seed", "seed"), &FellowTerrainFoliage::set_seed);
	ClassDB::bind_method(D_METHOD("get_seed"), &FellowTerrainFoliage::get_seed);

	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "mesh", PROPERTY_HINT_RESOURCE_TYPE, "Mesh"), "set_mesh", "get_mesh");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "layer", PROPERTY_HINT_RANGE, "0,8,1"), "set_layer", "get_layer");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "density", PROPERTY_HINT_RANGE, "0,100,0.001,or_greater,suffix:/m\u00b2"), "set_density", "get_density");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "min_weight", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_min_weight", "get_min_weight");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "range", PROPERTY_HINT_RANGE, "1,2000,0.1,or_greater,suffix:m"), "set_range", "get_range");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "cell_size", PROPERTY_HINT_RANGE, "0,512,0.1,or_greater,suffix:m"), "set_cell_size", "get_cell_size");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "slope_min", PROPERTY_HINT_RANGE, "0,90,0.1,degrees"), "set_slope_min", "get_slope_min");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "slope_max", PROPERTY_HINT_RANGE, "0,90,0.1,degrees"), "set_slope_max", "get_slope_max");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "height_min", PROPERTY_HINT_NONE, "suffix:m"), "set_height_min", "get_height_min");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "height_max", PROPERTY_HINT_NONE, "suffix:m"), "set_height_max", "get_height_max");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "scale_min", PROPERTY_HINT_RANGE, "0.001,10,0.001,or_greater"), "set_scale_min", "get_scale_min");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "scale_max", PROPERTY_HINT_RANGE, "0.001,10,0.001,or_greater"), "set_scale_max", "get_scale_max");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "random_yaw"), "set_random_yaw", "is_random_yaw");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "align_to_normal", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_align_to_normal", "get_align_to_normal");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "sink", PROPERTY_HINT_RANGE, "-10,10,0.001,or_greater,or_less,suffix:m"), "set_sink", "get_sink");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "cast_shadows"), "set_cast_shadows", "is_casting_shadows");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "seed"), "set_seed", "get_seed");
}

// ---------------------------------------------------------------------------------------------------------------------
// The shader. One code for every terrain (FellowSharedShaders: compiled once a session). The vertex stage places a
// patch's grid (VERTEX.xz: grid units, 0..patch_size) at INSTANCE_CUSTOM.xy (terrain-local metres) with its level's
// cell (INSTANCE_CUSTOM.z: the level), morphs its odd vertices onto the next coarser grid between the level's morph
// distances from `lod_camera` (CDLOD), and reads the height at the level plus the morph (a coarser mip as it morphs: the
// distant ground smoothed like its grid, and the same height on both sides of a level's edge).

static String _terrain_shader_code() {
	String code = R"(shader_type spatial;
render_mode cull_back;

uniform sampler2D heightmap : filter_linear_mipmap, repeat_disable;
uniform sampler2D normalmap : filter_linear_mipmap_anisotropic, repeat_disable;
uniform sampler2D splat_0 : filter_linear_mipmap, repeat_disable;
uniform sampler2D splat_1 : filter_linear_mipmap, repeat_disable;
uniform vec2 map_texels = vec2(1.0);
uniform vec2 map_extent = vec2(0.0);
uniform float spacing = 1.0;
uniform vec4 morph[16];
uniform mat3 terrain_basis = mat3(1.0);
uniform float height_blend = 0.3;
uniform float macro_variation = 0.15;
uniform int debug_view = 0;
uniform vec4 layer_color[9];
uniform vec4 layer_params[9];
uniform float layer_height_alpha[9];
instance uniform vec3 lod_camera;
)";
	for (int i = 0; i < FellowTerrain3D::MAX_LAYERS; i++) {
		code += vformat("uniform sampler2D albedo_%d : source_color, hint_default_white, filter_linear_mipmap_anisotropic, repeat_enable;\n", i);
		code += vformat("uniform sampler2D normal_%d : hint_normal, filter_linear_mipmap_anisotropic, repeat_enable;\n", i);
	}
	code += R"(
varying vec3 v_local;
varying float v_lod;

vec2 map_uv(vec2 p) {
	return (p / spacing + 0.5) / map_texels;
}

void vertex() {
	float level = INSTANCE_CUSTOM.z;
	float cell = spacing * exp2(level);
	vec2 base = INSTANCE_CUSTOM.xy;
	vec2 g = VERTEX.xz;
	vec2 p = base + g * cell;
	float h0 = textureLod(heightmap, map_uv(p), level).r;
	vec4 m = morph[int(level)];
	float k = clamp((distance(vec3(p.x, h0, p.y), lod_camera) - m.x) * m.y, 0.0, 1.0);
	g -= fract(g * 0.5) * 2.0 * k;
	g = min(g, (map_extent - base) / cell);
	p = base + g * cell;
	vec2 uv = map_uv(p);
	float lod = level + k;
	float h = textureLod(heightmap, uv, lod).r;
	VERTEX = vec3(g.x, h, g.y);
	vec2 n2 = textureLod(normalmap, uv, lod).rg * 2.0 - 1.0;
	vec3 n = vec3(n2.x, sqrt(max(0.0, 1.0 - dot(n2, n2))), n2.y);
	// The instance's basis scales x and z by the cell (the MultiMesh's normals are multiplied by it): divided here.
	NORMAL = vec3(n.x / cell, n.y, n.z / cell);
	v_local = vec3(p.x, h, p.y);
	v_lod = lod;
}

float hash12(vec2 p) {
	vec3 p3 = fract(vec3(p.xyx) * 0.1031);
	p3 += dot(p3, p3.yzx + 33.33);
	return fract((p3.x + p3.y) * p3.z);
}

float value_noise(vec2 p) {
	vec2 i = floor(p);
	vec2 f = fract(p);
	vec2 u = f * f * (3.0 - 2.0 * f);
	return mix(mix(hash12(i), hash12(i + vec2(1.0, 0.0)), u.x), mix(hash12(i + vec2(0.0, 1.0)), hash12(i + vec2(1.0, 1.0)), u.x), u.y);
}

vec2 unpack_normal(vec4 t, float strength) {
	return (t.xy * 2.0 - 1.0) * strength;
}
)";
	// A layer: its albedo (alpha: its height, or 0.5) and its normal in terrain space, from the top (x, -z) or three
	// projections (UDN blend), with explicit gradients (sampled inside branches).
	for (int i = 0; i < FellowTerrain3D::MAX_LAYERS; i++) {
		code += vformat(R"(
void layer_%d(vec3 pl, vec3 dpdx, vec3 dpdy, vec3 nw, vec3 blend, out vec4 r_albedo, out vec3 r_normal) {
	float s = layer_params[%d].x;
	float strength = layer_params[%d].z;
	if (layer_params[%d].w > 0.5) {
		vec3 sgn = sign(nw + vec3(1e-6));
		vec2 ux = vec2(pl.z * sgn.x, -pl.y) * s;
		vec2 uy = vec2(pl.x, -pl.z) * s;
		vec2 uz = vec2(-pl.x * sgn.z, -pl.y) * s;
		vec4 ax = textureGrad(albedo_%d, ux, vec2(dpdx.z * sgn.x, -dpdx.y) * s, vec2(dpdy.z * sgn.x, -dpdy.y) * s);
		vec4 ay = textureGrad(albedo_%d, uy, vec2(dpdx.x, -dpdx.z) * s, vec2(dpdy.x, -dpdy.z) * s);
		vec4 az = textureGrad(albedo_%d, uz, vec2(-dpdx.x * sgn.z, -dpdx.y) * s, vec2(-dpdy.x * sgn.z, -dpdy.y) * s);
		vec2 tx = unpack_normal(textureGrad(normal_%d, ux, vec2(dpdx.z * sgn.x, -dpdx.y) * s, vec2(dpdy.z * sgn.x, -dpdy.y) * s), strength);
		vec2 ty = unpack_normal(textureGrad(normal_%d, uy, vec2(dpdx.x, -dpdx.z) * s, vec2(dpdy.x, -dpdy.z) * s), strength);
		vec2 tz = unpack_normal(textureGrad(normal_%d, uz, vec2(-dpdx.x * sgn.z, -dpdx.y) * s, vec2(-dpdy.x * sgn.z, -dpdy.y) * s), strength);
		tx.x *= sgn.x;
		tz.x *= -sgn.z;
		vec3 nx = vec3(nw.x, tx.y + nw.y, tx.x + nw.z);
		vec3 ny = vec3(ty.x + nw.x, nw.y, ty.y + nw.z);
		vec3 nz = vec3(tz.x + nw.x, tz.y + nw.y, nw.z);
		r_albedo = ax * blend.x + ay * blend.y + az * blend.z;
		r_normal = normalize(nx * blend.x + ny * blend.y + nz * blend.z);
	} else {
		vec2 uy = vec2(pl.x, -pl.z) * s;
		vec2 gx = vec2(dpdx.x, -dpdx.z) * s;
		vec2 gy = vec2(dpdy.x, -dpdy.z) * s;
		r_albedo = textureGrad(albedo_%d, uy, gx, gy);
		vec2 ty = unpack_normal(textureGrad(normal_%d, uy, gx, gy), strength);
		r_normal = normalize(vec3(ty.x + nw.x, nw.y, ty.y + nw.z));
	}
	if (layer_height_alpha[%d] < 0.5) {
		r_albedo.a = 0.5;
	}
}
)",
				i, i, i, i, i, i, i, i, i, i, i, i, i);
	}
	code += R"(
void fragment() {
	vec2 uv = map_uv(v_local.xz);
	vec2 n2 = texture(normalmap, uv).rg * 2.0 - 1.0;
	vec3 nw = normalize(vec3(n2.x, sqrt(max(0.0, 1.0 - dot(n2, n2))), n2.y));
	vec4 s0 = texture(splat_0, uv);
	vec4 s1 = texture(splat_1, uv);
	float w[9];
	w[1] = s0.r;
	w[2] = s0.g;
	w[3] = s0.b;
	w[4] = s0.a;
	w[5] = s1.r;
	w[6] = s1.g;
	w[7] = s1.b;
	w[8] = s1.a;
	w[0] = max(0.0, 1.0 - (s0.r + s0.g + s0.b + s0.a + s1.r + s1.g + s1.b + s1.a));
	vec3 dpdx = dFdx(v_local);
	vec3 dpdy = dFdy(v_local);
	vec3 blend = pow(abs(nw), vec3(4.0));
	blend /= max(dot(blend, vec3(1.0)), 1e-5);
	float top = -1e9;
)";
	// Each layer's albedo, normal and blend value in its own variables (no array indexed at run time: kept in
	// registers), sampled only where its weight is above 0.002.
	for (int i = 0; i < FellowTerrain3D::MAX_LAYERS; i++) {
		code += vformat(R"(	vec4 alb_%d = vec4(0.0);
	vec3 nrm_%d = nw;
	float v_%d = -1e9;
	if (w[%d] > 0.002) {
		layer_%d(v_local, dpdx, dpdy, nw, blend, alb_%d, nrm_%d);
		v_%d = w[%d] + alb_%d.a * height_blend;
		top = max(top, v_%d);
	}
)",
				i, i, i, i, i, i, i, i, i, i, i);
	}
	code += R"(	vec3 albedo = vec3(0.0);
	vec3 n = vec3(0.0);
	float rough = 0.0;
	float total = 0.0;
	float b;
)";
	for (int i = 0; i < FellowTerrain3D::MAX_LAYERS; i++) {
		code += vformat(R"(	b = v_%d > -1e8 ? (height_blend > 0.0 ? max(v_%d - (top - 0.15), 0.0) * w[%d] : w[%d]) : 0.0;
	albedo += alb_%d.rgb * layer_color[%d].rgb * b;
	n += nrm_%d * b;
	rough += layer_params[%d].y * b;
	total += b;
)",
				i, i, i, i, i, i, i, i);
	}
	code += R"(	total = max(total, 1e-5);
	albedo /= total;
	rough /= total;
	float mv = value_noise(v_local.xz * 0.013) * 0.6 + value_noise(v_local.xz * 0.071) * 0.4;
	albedo *= mix(1.0, 0.7 + 0.6 * mv, macro_variation);
	vec3 nf = normalize(terrain_basis * normalize(n + vec3(0.0, 1e-5, 0.0)));
	if (debug_view == 1) {
		// The level of detail (and the morph to the next) as colours.
		vec3 pal[6] = vec3[](vec3(0.9, 0.2, 0.2), vec3(0.95, 0.75, 0.2), vec3(0.3, 0.8, 0.3), vec3(0.2, 0.6, 0.95), vec3(0.6, 0.35, 0.9), vec3(0.85, 0.85, 0.85));
		int l0 = int(floor(v_lod));
		albedo = mix(pal[l0 % 6], pal[(l0 + 1) % 6], fract(v_lod));
	}
	ALBEDO = albedo;
	ROUGHNESS = rough;
	NORMAL = normalize((VIEW_MATRIX * vec4(nf, 0.0)).xyz);
}
)";
	return code;
}

// ---------------------------------------------------------------------------------------------------------------------
// FellowTerrain3D: data

Ref<Image> FellowTerrain3D::heightmap_from_image(const Ref<Image> &p_image, float p_min_height, float p_max_height) {
	ERR_FAIL_COND_V(p_image.is_null() || p_image->is_empty(), Ref<Image>());
	Ref<Image> img = p_image->duplicate();
	if (img->is_compressed()) {
		img->decompress();
	}
	if (img->has_mipmaps()) {
		img->clear_mipmaps();
	}
	const bool already_metres = img->get_format() == Image::FORMAT_RF || img->get_format() == Image::FORMAT_RH;
	img->convert(Image::FORMAT_RF);
	if (!already_metres) {
		// 0..1 (an 8- or 16-bit grey image) to metres.
		Vector<uint8_t> data = img->get_data();
		float *f = reinterpret_cast<float *>(data.ptrw());
		const int n = img->get_width() * img->get_height();
		for (int i = 0; i < n; i++) {
			f[i] = p_min_height + f[i] * (p_max_height - p_min_height);
		}
		img->set_data(img->get_width(), img->get_height(), false, Image::FORMAT_RF, data);
	}
	return img;
}

void FellowTerrain3D::_build_minmax() {
	minmax.clear();
	minmax_size.clear();
	if (map_w < 2 || map_h < 2) {
		return;
	}
	// Level 0: blocks of patch_size cells (patch_size + 1 texels, the edges shared).
	const int p = patch_size;
	Vector2i size((map_w - 1 + p - 1) / p, (map_h - 1 + p - 1) / p);
	LocalVector<Vector2> level;
	level.resize(size.x * size.y);
	for (int bz = 0; bz < size.y; bz++) {
		for (int bx = 0; bx < size.x; bx++) {
			float lo = 1e30f;
			float hi = -1e30f;
			const int x1 = MIN(map_w - 1, (bx + 1) * p);
			const int z1 = MIN(map_h - 1, (bz + 1) * p);
			for (int z = bz * p; z <= z1; z++) {
				const float *row = heights.ptr() + z * map_w;
				for (int x = bx * p; x <= x1; x++) {
					lo = MIN(lo, row[x]);
					hi = MAX(hi, row[x]);
				}
			}
			level[bz * size.x + bx] = Vector2(lo, hi);
		}
	}
	minmax.push_back(std::move(level));
	minmax_size.push_back(size);
	while (size.x > 1 || size.y > 1) {
		Vector2i up((size.x + 1) / 2, (size.y + 1) / 2);
		LocalVector<Vector2> next;
		next.resize(up.x * up.y);
		const LocalVector<Vector2> &prev = minmax[minmax.size() - 1];
		for (int z = 0; z < up.y; z++) {
			for (int x = 0; x < up.x; x++) {
				Vector2 r(1e30f, -1e30f);
				for (int dz = 0; dz < 2; dz++) {
					for (int dx = 0; dx < 2; dx++) {
						const int sx = x * 2 + dx;
						const int sz = z * 2 + dz;
						if (sx < size.x && sz < size.y) {
							const Vector2 &v = prev[sz * size.x + sx];
							r.x = MIN(r.x, v.x);
							r.y = MAX(r.y, v.y);
						}
					}
				}
				next[z * up.x + x] = r;
			}
		}
		minmax.push_back(std::move(next));
		minmax_size.push_back(up);
		size = up;
	}
}

namespace {
struct NormalsJob {
	const float *h = nullptr;
	uint8_t *out = nullptr;
	int w = 0;
	int d = 0;
	float inv = 1.0f;
};

void _normals_row(void *p_job, uint32_t p_z) {
	const NormalsJob &j = *static_cast<const NormalsJob *>(p_job);
	const int z = int(p_z);
	for (int x = 0; x < j.w; x++) {
		const float hl = j.h[z * j.w + MAX(0, x - 1)];
		const float hr = j.h[z * j.w + MIN(j.w - 1, x + 1)];
		const float hd = j.h[MAX(0, z - 1) * j.w + x];
		const float hu = j.h[MIN(j.d - 1, z + 1) * j.w + x];
		Vector3 n((hl - hr) * j.inv, 1.0f, (hd - hu) * j.inv);
		n.normalize();
		j.out[(z * j.w + x) * 2 + 0] = uint8_t(CLAMP(Math::round((n.x * 0.5f + 0.5f) * 255.0f), 0.0f, 255.0f));
		j.out[(z * j.w + x) * 2 + 1] = uint8_t(CLAMP(Math::round((n.z * 0.5f + 0.5f) * 255.0f), 0.0f, 255.0f));
	}
}
} // namespace

void FellowTerrain3D::_build_normals(Ref<Image> &r_normals) const {
	// RG8: the normal's x and z (y is the rest of the unit length), from central differences of the heights.
	Vector<uint8_t> data;
	data.resize(map_w * map_h * 2);
	NormalsJob job;
	job.h = heights.ptr();
	job.out = data.ptrw();
	job.w = map_w;
	job.d = map_h;
	job.inv = 1.0f / (2.0f * vertex_spacing);
	if (map_h >= 256) {
		WorkerThreadPool::GroupID group = WorkerThreadPool::get_singleton()->add_native_group_task(&_normals_row, &job, map_h, -1, true, SNAME("FellowTerrain3D normals"));
		WorkerThreadPool::get_singleton()->wait_for_group_task_completion(group);
	} else {
		for (int z = 0; z < map_h; z++) {
			_normals_row(&job, z);
		}
	}
	r_normals = Image::create_from_data(map_w, map_h, false, Image::FORMAT_RG8, data);
	r_normals->generate_mipmaps();
}

void FellowTerrain3D::_build_mesh() {
	const int p = patch_size;
	PackedVector3Array verts;
	verts.resize((p + 1) * (p + 1));
	Vector3 *vw = verts.ptrw();
	for (int z = 0; z <= p; z++) {
		for (int x = 0; x <= p; x++) {
			vw[z * (p + 1) + x] = Vector3(x, 0, z);
		}
	}
	PackedInt32Array indices;
	indices.resize(p * p * 6);
	int32_t *iw = indices.ptrw();
	int k = 0;
	for (int z = 0; z < p; z++) {
		for (int x = 0; x < p; x++) {
			const int a = z * (p + 1) + x;
			const int b = a + 1;
			const int c = a + (p + 1);
			const int d = c + 1;
			// Clockwise from above (Godot's front faces); every cell's diagonal from b to c, so that a morphed patch is
			// the next coarser grid exactly.
			iw[k++] = a;
			iw[k++] = b;
			iw[k++] = c;
			iw[k++] = b;
			iw[k++] = d;
			iw[k++] = c;
		}
	}
	Array arrays;
	arrays.resize(Mesh::ARRAY_MAX);
	arrays[Mesh::ARRAY_VERTEX] = verts;
	arrays[Mesh::ARRAY_INDEX] = indices;
	patch_mesh.instantiate();
	patch_mesh->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, arrays);
	// The grid in grid units; heights in metres anywhere on the terrain (the instances scale x and z only).
	patch_mesh->set_custom_aabb(AABB(Vector3(0, height_min - 1.0f, 0), Vector3(p, height_max - height_min + 2.0f, p)));
	if (material.is_valid()) {
		patch_mesh->surface_set_material(0, material);
	}
}

void FellowTerrain3D::_rebuild_data() {
	data_dirty = false;
	_clear_foliage(); // its tasks read the data
	foliage_dirty = true;
	heights.clear();
	map_w = 0;
	map_h = 0;
	if (heightmap.is_valid() && !heightmap->is_empty()) {
		Ref<Image> img = heightmap;
		if (img->get_format() != Image::FORMAT_RF || img->has_mipmaps()) {
			img = heightmap_from_image(heightmap, 0.0f, 1.0f);
		}
		map_w = img->get_width();
		map_h = img->get_height();
		heights.resize(map_w * map_h);
		const Vector<uint8_t> data = img->get_data();
		memcpy(heights.ptr(), data.ptr(), sizeof(float) * map_w * map_h);
	}
	height_min = 0.0f;
	height_max = 0.0f;
	if (map_w >= 2 && map_h >= 2) {
		height_min = 1e30f;
		height_max = -1e30f;
		for (uint32_t i = 0; i < heights.size(); i++) {
			height_min = MIN(height_min, heights[i]);
			height_max = MAX(height_max, heights[i]);
		}
	}
	_build_minmax();
	top_level = 0;
	while (top_level < MAX_LEVELS - 1 && float(patch_size << (top_level + 1)) < float(MAX(map_w, map_h) - 1)) {
		top_level++;
	}

	if (map_w >= 2 && map_h >= 2) {
		Vector<uint8_t> hdata;
		hdata.resize(sizeof(float) * map_w * map_h);
		memcpy(hdata.ptrw(), heights.ptr(), hdata.size());
		Ref<Image> himg = Image::create_from_data(map_w, map_h, false, Image::FORMAT_RF, hdata);
		himg->generate_mipmaps();
		height_texture = ImageTexture::create_from_image(himg);
		Ref<Image> nimg;
		_build_normals(nimg);
		normal_texture = ImageTexture::create_from_image(nimg);
	} else {
		height_texture.unref();
		normal_texture.unref();
	}
	for (int i = 0; i < 2; i++) {
		Ref<Image> s = i == 0 ? splat_map : splat_map_2;
		Ref<ImageTexture> &t = i == 0 ? splat_texture : splat_texture_2;
		if (s.is_valid() && !s->is_empty()) {
			Ref<Image> c = s->duplicate();
			if (c->is_compressed()) {
				c->decompress();
			}
			c->convert(Image::FORMAT_RGBA8);
			// The foliage's copy (the first mip): read by its tasks.
			splat_cpu_size[i] = Vector2i(c->get_width(), c->get_height());
			splat_cpu[i].resize(c->get_width() * c->get_height() * 4);
			memcpy(splat_cpu[i].ptr(), c->get_data().ptr(), splat_cpu[i].size());
			if (!c->has_mipmaps()) {
				c->generate_mipmaps();
			}
			t = ImageTexture::create_from_image(c);
		} else {
			Ref<Image> c = Image::create_empty(1, 1, false, Image::FORMAT_RGBA8);
			c->fill(Color(0, 0, 0, 0));
			t = ImageTexture::create_from_image(c);
			splat_cpu[i].clear();
			splat_cpu_size[i] = Vector2i();
		}
	}
	_build_mesh();
	material_dirty = true;
	selection_valid = false;
	if (is_inside_tree()) {
		_update_collision();
	}
	update_configuration_warnings();
}

void FellowTerrain3D::_update_material() {
	material_dirty = false;
	if (material.is_null()) {
		material.instantiate();
		material->set_shader(FellowSharedShaders::shader(_terrain_shader_code()));
		if (patch_mesh.is_valid()) {
			patch_mesh->surface_set_material(0, material);
		}
	}
	material->set_shader_parameter("heightmap", height_texture);
	material->set_shader_parameter("normalmap", normal_texture);
	material->set_shader_parameter("splat_0", splat_texture);
	material->set_shader_parameter("splat_1", splat_texture_2);
	material->set_shader_parameter("map_texels", Vector2(MAX(1, map_w), MAX(1, map_h)));
	material->set_shader_parameter("map_extent", Vector2(MAX(0, map_w - 1), MAX(0, map_h - 1)) * vertex_spacing);
	material->set_shader_parameter("spacing", vertex_spacing);
	material->set_shader_parameter("height_blend", height_blend);
	material->set_shader_parameter("macro_variation", macro_variation);
	material->set_shader_parameter("debug_view", int(debug_view));

	// The levels' ranges (CDLOD): a level's vertices morph onto the next coarser grid from morph_ratio of the way
	// between the finer level's range and its own to its own range, where the coarser level takes over.
	const float node0 = _node_size(0);
	const float r0 = MAX(lod_distance > 0.0f ? lod_distance : 3.0f * node0, 2.5f * node0);
	PackedVector4Array morph;
	morph.resize(MAX_LEVELS);
	for (int l = 0; l < MAX_LEVELS; l++) {
		ranges[l] = r0 * float(1 << l);
		const float prev = l == 0 ? 0.0f : ranges[l - 1];
		const float start = prev + (ranges[l] - prev) * morph_ratio;
		const float end = ranges[l];
		const bool morphs = l < top_level;
		morph.write[l] = Vector4(start, morphs ? 1.0f / MAX(end - start, 1e-3f) : 0.0f, 0, 0);
	}
	material->set_shader_parameter("morph", morph);

	PackedColorArray colors;
	PackedVector4Array params;
	PackedFloat32Array height_alpha;
	colors.resize(MAX_LAYERS);
	params.resize(MAX_LAYERS);
	height_alpha.resize(MAX_LAYERS);
	for (int i = 0; i < MAX_LAYERS; i++) {
		Ref<FellowTerrainLayer> l = i < layers.size() ? Ref<FellowTerrainLayer>(layers[i]) : Ref<FellowTerrainLayer>();
		if (l.is_valid()) {
			colors.write[i] = l->get_albedo_color();
			params.write[i] = Vector4(1.0f / l->get_texture_size(), l->get_roughness(), l->get_normal_strength(), l->is_triplanar() ? 1.0f : 0.0f);
			height_alpha.write[i] = l->is_height_in_albedo_alpha() ? 1.0f : 0.0f;
			material->set_shader_parameter(vformat("albedo_%d", i), l->get_albedo_texture());
			material->set_shader_parameter(vformat("normal_%d", i), l->get_normal_texture());
		} else {
			colors.write[i] = Color(1, 1, 1);
			params.write[i] = Vector4(0.25f, 0.9f, 1.0f, 0.0f);
			height_alpha.write[i] = 0.0f;
			material->set_shader_parameter(vformat("albedo_%d", i), Variant());
			material->set_shader_parameter(vformat("normal_%d", i), Variant());
		}
	}
	material->set_shader_parameter("layer_color", colors);
	material->set_shader_parameter("layer_params", params);
	material->set_shader_parameter("layer_height_alpha", height_alpha);
	_update_transforms();
}

void FellowTerrain3D::_layer_changed() {
	material_dirty = true;
	if (is_inside_tree()) {
		_update_material();
	}
}

// ---------------------------------------------------------------------------------------------------------------------
// Rendering

void FellowTerrain3D::_create_instances() {
	if (instance[0].is_valid() || !is_inside_tree()) {
		return;
	}
	RenderingServer *rs = RenderingServer::get_singleton();
	const RID scenario = get_world_3d()->get_scenario();
	for (int i = 0; i < 2; i++) {
		multimesh[i] = rs->multimesh_create();
		rs->multimesh_set_mesh(multimesh[i], patch_mesh.is_valid() ? patch_mesh->get_rid() : RID());
		capacity[i] = 0;
		instance[i] = rs->instance_create2(multimesh[i], scenario);
		rs->instance_attach_object_instance_id(instance[i], get_instance_id());
		rs->instance_set_visible(instance[i], false);
		// The terrain's shader moves its vertices, but within the mesh's box (the heights' range): the GPU may cull
		// its patches one by one (decision 0034).
		rs->fellow_instance_set_instance_range(instance[i], 0.0f, 0.0f, true);
		patches[i].clear();
	}
	shown = -1;
	pending = -1;
	selection_valid = false;
	_update_instance_settings();
	_update_transforms();
}

void FellowTerrain3D::_free_instances() {
	RenderingServer *rs = RenderingServer::get_singleton();
	for (int i = 0; i < 2; i++) {
		if (instance[i].is_valid()) {
			rs->free_rid(instance[i]);
			instance[i] = RID();
		}
		if (multimesh[i].is_valid()) {
			rs->free_rid(multimesh[i]);
			multimesh[i] = RID();
		}
		capacity[i] = 0;
		patches[i].clear();
	}
	shown = -1;
	pending = -1;
}

void FellowTerrain3D::_update_instance_settings() {
	RenderingServer *rs = RenderingServer::get_singleton();
	for (int i = 0; i < 2; i++) {
		if (instance[i].is_null()) {
			continue;
		}
		rs->instance_geometry_set_cast_shadows_setting(instance[i], cast_shadows ? RSE::SHADOW_CASTING_SETTING_ON : RSE::SHADOW_CASTING_SETTING_OFF);
		rs->instance_set_layer_mask(instance[i], render_layer_mask);
		rs->instance_set_visible(instance[i], i == shown && is_visible_in_tree());
	}
	for (KeyValue<uint64_t, FoliageCell *> &kv : foliage_cells) {
		if (kv.value->instance.is_valid()) {
			rs->instance_set_layer_mask(kv.value->instance, render_layer_mask);
			rs->instance_set_visible(kv.value->instance, is_visible_in_tree());
		}
	}
}

void FellowTerrain3D::_update_transforms() {
	if (!is_inside_tree()) {
		return;
	}
	const Transform3D xf = get_global_transform();
	RenderingServer *rs = RenderingServer::get_singleton();
	for (int i = 0; i < 2; i++) {
		if (instance[i].is_valid()) {
			rs->instance_set_transform(instance[i], xf);
		}
	}
	if (material.is_valid()) {
		material->set_shader_parameter("terrain_basis", xf.basis.orthonormalized());
	}
	if (body.is_valid()) {
		PhysicsServer3D::get_singleton()->body_set_state(body, PhysicsServer3D::BODY_STATE_TRANSFORM, xf);
	}
	for (KeyValue<uint64_t, FoliageCell *> &kv : foliage_cells) {
		if (kv.value->instance.is_valid()) {
			rs->instance_set_transform(kv.value->instance, xf);
		}
	}
	selection_valid = false;
}

Camera3D *FellowTerrain3D::_find_camera() const {
#ifdef TOOLS_ENABLED
	if (Engine::get_singleton()->is_editor_hint()) {
		Node3DEditor *editor = Node3DEditor::get_singleton();
		if (editor && editor->get_last_used_viewport()) {
			return editor->get_last_used_viewport()->get_camera_3d();
		}
		return nullptr;
	}
#endif
	Viewport *vp = get_viewport();
	return vp ? vp->get_camera_3d() : nullptr;
}

AABB FellowTerrain3D::_node_box(int p_level, int p_x, int p_z) const {
	// A node at level L spans blocks of level L + 1 (patch_size << (L + 1) texels).
	const int k = p_level + 1;
	const float size = _node_size(p_level);
	const float x0 = p_x * size;
	const float z0 = p_z * size;
	const float ex = float(map_w - 1) * vertex_spacing;
	const float ez = float(map_h - 1) * vertex_spacing;
	Vector2 mm(height_min, height_max);
	if (k < int(minmax.size())) {
		const Vector2i s = minmax_size[k];
		if (p_x < s.x && p_z < s.y) {
			mm = minmax[k][p_z * s.x + p_x];
		}
	}
	return AABB(Vector3(x0, mm.x, z0), Vector3(MIN(size, ex - x0), mm.y - mm.x, MIN(size, ez - z0)));
}

static inline float _box_distance_squared(const AABB &p_box, const Vector3 &p_point) {
	const Vector3 lo = p_box.position;
	const Vector3 hi = p_box.position + p_box.size;
	const Vector3 c(CLAMP(p_point.x, lo.x, hi.x), CLAMP(p_point.y, lo.y, hi.y), CLAMP(p_point.z, lo.z, hi.z));
	return c.distance_squared_to(p_point);
}

bool FellowTerrain3D::_select_node(int p_level, int p_x, int p_z, const Vector3 &p_camera, LocalVector<Patch> &r_out) const {
	const AABB box = _node_box(p_level, p_x, p_z);
	if (box.size.x <= 0.0f || box.size.z <= 0.0f) {
		return true; // outside the map: nothing to draw, nothing for the parent to draw either
	}
	const float d2 = _box_distance_squared(box, p_camera);
	if (d2 > ranges[p_level] * ranges[p_level]) {
		return false; // beyond this level's range: the parent draws the area
	}
	if (p_level == 0 || d2 > ranges[p_level - 1] * ranges[p_level - 1]) {
		for (int i = 0; i < 4; i++) {
			const int qx = p_x * 2 + (i & 1);
			const int qz = p_z * 2 + (i >> 1);
			if (float(qx) * _node_size(p_level) * 0.5f < float(map_w - 1) * vertex_spacing && float(qz) * _node_size(p_level) * 0.5f < float(map_h - 1) * vertex_spacing) {
				r_out.push_back({ qx, qz, p_level });
			}
		}
		return true;
	}
	for (int i = 0; i < 4; i++) {
		const int cx = p_x * 2 + (i & 1);
		const int cz = p_z * 2 + (i >> 1);
		if (!_select_node(p_level - 1, cx, cz, p_camera, r_out)) {
			r_out.push_back({ cx, cz, p_level }); // the child's area, at this level
		}
	}
	return true;
}

void FellowTerrain3D::_select(const Vector3 &p_camera, LocalVector<Patch> &r_out) const {
	r_out.clear();
	if (map_w < 2 || map_h < 2) {
		return;
	}
	if (!_select_node(top_level, 0, 0, p_camera, r_out)) {
		for (int i = 0; i < 4; i++) {
			const int qx = i & 1;
			const int qz = i >> 1;
			if (float(qx) * _node_size(top_level) * 0.5f < float(map_w - 1) * vertex_spacing && float(qz) * _node_size(top_level) * 0.5f < float(map_h - 1) * vertex_spacing) {
				r_out.push_back({ qx, qz, top_level });
			}
		}
	}
}

void FellowTerrain3D::_write_patches(int p_which, const LocalVector<Patch> &p_patches, const Vector3 &p_camera) {
	RenderingServer *rs = RenderingServer::get_singleton();
	const int n = p_patches.size();
	if (n > capacity[p_which]) {
		capacity[p_which] = MAX(256, int(Math::next_power_of_2(uint32_t(n))));
		rs->multimesh_allocate_data(multimesh[p_which], capacity[p_which], RSE::MULTIMESH_TRANSFORM_3D, false, true);
	}
	buffer.resize(capacity[p_which] * 16);
	float *w = buffer.ptr();
	for (int i = 0; i < n; i++) {
		const Patch &pt = p_patches[i];
		const float cell = vertex_spacing * float(1 << pt.level);
		const float qsize = cell * float(patch_size);
		const float x0 = pt.x * qsize;
		const float z0 = pt.z * qsize;
		float *d = w + i * 16;
		// The transform's rows (basis row, origin): x and z scaled by the cell, y unscaled.
		d[0] = cell;
		d[1] = 0.0f;
		d[2] = 0.0f;
		d[3] = x0;
		d[4] = 0.0f;
		d[5] = 1.0f;
		d[6] = 0.0f;
		d[7] = 0.0f;
		d[8] = 0.0f;
		d[9] = 0.0f;
		d[10] = cell;
		d[11] = z0;
		d[12] = x0;
		d[13] = z0;
		d[14] = float(pt.level);
		d[15] = 0.0f;
	}
	for (int i = n; i < capacity[p_which]; i++) {
		float *d = w + i * 16;
		for (int j = 0; j < 16; j++) {
			d[j] = 0.0f;
		}
	}
	Vector<float> data;
	data.resize(capacity[p_which] * 16);
	memcpy(data.ptrw(), w, sizeof(float) * capacity[p_which] * 16);
	rs->multimesh_set_buffer(multimesh[p_which], data);
	rs->multimesh_set_visible_instances(multimesh[p_which], n);
	rs->instance_geometry_set_shader_parameter(instance[p_which], "lod_camera", p_camera);
	patches[p_which] = p_patches;
}

void FellowTerrain3D::_process_lod() {
	if (instance[0].is_null() || map_w < 2) {
		return;
	}
	if (material_dirty) {
		_update_material();
	}
	RenderingServer *rs = RenderingServer::get_singleton();
	const uint64_t frame = Engine::get_singleton()->get_frames_drawn();
	if (pending >= 0) {
		// Shown once its buffer is three drawn frames old: its previous transforms are its current ones by then (the
		// MultiMesh's motion vectors), and the GPU culls it again (decision 0034 skips a MultiMesh written this frame).
		if (frame >= pending_frame + 3 || shown < 0) {
			const bool visible = is_visible_in_tree();
			rs->instance_set_visible(instance[pending], visible);
			if (shown >= 0) {
				rs->instance_set_visible(instance[shown], false);
			}
			shown = pending;
			pending = -1;
			swaps++;
		} else {
			Fellow::request_redraw();
			return;
		}
	}
	Camera3D *cam = _find_camera();
	if (!cam) {
		return;
	}
	const Vector3 c = get_global_transform().affine_inverse().xform(cam->get_global_position());
	if (selection_valid && c.distance_squared_to(selected_camera) < (0.05f * vertex_spacing) * (0.05f * vertex_spacing)) {
		return;
	}
	const uint64_t t0 = OS::get_singleton()->get_ticks_usec();
	_select(c, scratch);
	last_select_usec = OS::get_singleton()->get_ticks_usec() - t0;
	selections++;
	selected_camera = c;
	selection_valid = true;
	if (shown >= 0 && scratch.size() == patches[shown].size()) {
		bool same = true;
		for (uint32_t i = 0; i < scratch.size() && same; i++) {
			same = scratch[i] == patches[shown][i];
		}
		if (same) {
			// The same patches: only the morph's camera moves (the same in both of a frame's vertex passes).
			rs->instance_geometry_set_shader_parameter(instance[shown], "lod_camera", c);
			return;
		}
	}
	const int target = shown < 0 ? 0 : 1 - shown;
	_write_patches(target, scratch, c);
	pending = target;
	pending_frame = frame;
	if (shown < 0) {
		_process_lod(); // nothing shown yet: show it now
	} else {
		Fellow::request_redraw();
	}
}

// ---------------------------------------------------------------------------------------------------------------------
// Collision

void FellowTerrain3D::_free_collision() {
	PhysicsServer3D *ps = PhysicsServer3D::get_singleton();
	if (body.is_valid()) {
		ps->free_rid(body);
		body = RID();
	}
	for (const RID &s : shapes) {
		ps->free_rid(s);
	}
	shapes.clear();
}

void FellowTerrain3D::_update_collision() {
	_free_collision();
	if (!collision_enabled || map_w < 2 || map_h < 2 || !is_inside_tree()) {
		return;
	}
	PhysicsServer3D *ps = PhysicsServer3D::get_singleton();
	body = ps->body_create();
	ps->body_set_mode(body, PhysicsServer3D::BODY_MODE_STATIC);
	ps->body_attach_object_instance_id(body, get_instance_id());
	ps->body_set_collision_layer(body, collision_layer);
	ps->body_set_collision_mask(body, collision_mask);
	ps->body_set_space(body, get_world_3d()->get_space());
	ps->body_set_state(body, PhysicsServer3D::BODY_STATE_TRANSFORM, get_global_transform());
	// Regions of collision_region_size cells (their edges shared), each a height map shape: scaled by the spacing in x
	// and z, centred on the region.
	const int r = collision_region_size;
	for (int z0 = 0; z0 < map_h - 1; z0 += r) {
		for (int x0 = 0; x0 < map_w - 1; x0 += r) {
			const int w = MIN(r, map_w - 1 - x0) + 1;
			const int d = MIN(r, map_h - 1 - z0) + 1;
			PackedFloat32Array h;
			h.resize(w * d);
			float *hw = h.ptrw();
			float lo = 1e30f;
			float hi = -1e30f;
			for (int z = 0; z < d; z++) {
				for (int x = 0; x < w; x++) {
					const float v = heights[(z0 + z) * map_w + x0 + x];
					hw[z * w + x] = v;
					lo = MIN(lo, v);
					hi = MAX(hi, v);
				}
			}
			Dictionary data;
			data["width"] = w;
			data["depth"] = d;
			data["heights"] = h;
			data["min_height"] = lo;
			data["max_height"] = hi;
			RID shape = ps->heightmap_shape_create();
			ps->shape_set_data(shape, data);
			Transform3D xf;
			xf.basis.scale(Vector3(vertex_spacing, 1.0f, vertex_spacing));
			xf.origin = Vector3((x0 + (w - 1) * 0.5f) * vertex_spacing, 0.0f, (z0 + (d - 1) * 0.5f) * vertex_spacing);
			ps->body_add_shape(body, shape, xf);
			shapes.push_back(shape);
		}
	}
}

// ---------------------------------------------------------------------------------------------------------------------
// Foliage (decision 0159): each kind scattered in square cells around the camera. A cell's instances are made once, on a
// worker thread (jittered points on a grid of the kind's density, kept where the layer's weight, the slope and the
// height allow), into a MultiMesh of its own: a cell's buffer is never rewritten (no motion vectors from another
// cell's instances), and the GPU culls its instances one by one beyond the kind's range.

static inline uint32_t _foliage_hash(uint32_t p_a, uint32_t p_b, uint32_t p_c) {
	uint32_t h = p_a * 0x9E3779B1u ^ (p_b + 0x7F4A7C15u) * 0x85EBCA77u ^ (p_c + 0x165667B1u) * 0xC2B2AE3Du;
	h ^= h >> 15;
	h *= 0x2C1B3C6Du;
	h ^= h >> 12;
	h *= 0x297A2D39u;
	h ^= h >> 15;
	return h;
}

static inline float _foliage_u01(uint32_t p_h) {
	return float(p_h >> 8) * (1.0f / 16777216.0f);
}

static inline uint64_t _foliage_key(int p_kind, int p_cx, int p_cz) {
	return (uint64_t(p_kind) << 56) | (uint64_t(uint32_t(p_cx) & 0xFFFFFFFu) << 28) | uint64_t(uint32_t(p_cz) & 0xFFFFFFFu);
}

float FellowTerrain3D::_layer_weight_local(int p_layer, float p_x, float p_z) const {
	const float u = (p_x / vertex_spacing + 0.5f) / float(MAX(1, map_w));
	const float v = (p_z / vertex_spacing + 0.5f) / float(MAX(1, map_h));
	auto sample = [&](int p_map, int p_channel) -> float {
		if (splat_cpu[p_map].is_empty()) {
			return 0.0f;
		}
		const Vector2i sz = splat_cpu_size[p_map];
		const float fx = CLAMP(u * sz.x - 0.5f, 0.0f, float(sz.x - 1));
		const float fz = CLAMP(v * sz.y - 0.5f, 0.0f, float(sz.y - 1));
		const int x0 = int(fx);
		const int z0 = int(fz);
		const int x1 = MIN(x0 + 1, sz.x - 1);
		const int z1 = MIN(z0 + 1, sz.y - 1);
		const float tx = fx - x0;
		const float tz = fz - z0;
		const uint8_t *d = splat_cpu[p_map].ptr();
		const float a = d[(z0 * sz.x + x0) * 4 + p_channel];
		const float b = d[(z0 * sz.x + x1) * 4 + p_channel];
		const float c = d[(z1 * sz.x + x0) * 4 + p_channel];
		const float e = d[(z1 * sz.x + x1) * 4 + p_channel];
		return Math::lerp(Math::lerp(a, b, tx), Math::lerp(c, e, tx), tz) * (1.0f / 255.0f);
	};
	if (p_layer == 0) {
		float sum = 0.0f;
		for (int m = 0; m < 2; m++) {
			for (int ch = 0; ch < 4; ch++) {
				sum += sample(m, ch);
			}
		}
		return MAX(0.0f, 1.0f - sum);
	}
	return sample((p_layer - 1) / 4, (p_layer - 1) % 4);
}

void FellowTerrain3D::_generate_foliage_cell(void *p_cell) {
	FoliageCell *cell = static_cast<FoliageCell *>(p_cell);
	const FellowTerrain3D *t = cell->terrain;
	const FoliageKind &k = t->foliage_kinds[cell->kind];
	const float ex = float(t->map_w - 1) * t->vertex_spacing;
	const float ez = float(t->map_h - 1) * t->vertex_spacing;
	const int side = MAX(1, int(Math::ceil(k.cell * Math::sqrt(k.density))));
	const float step = k.cell / float(side);
	const float e = t->vertex_spacing;
	LocalVector<float> out;
	out.reserve(side * side * 6);
	int count = 0;
	for (int j = 0; j < side; j++) {
		for (int i = 0; i < side; i++) {
			const uint32_t gx = uint32_t(cell->cx * side + i);
			const uint32_t gz = uint32_t(cell->cz * side + j);
			const uint32_t h0 = _foliage_hash(k.seed, gx, gz);
			const uint32_t h1 = _foliage_hash(h0, 1, 0);
			const uint32_t h2 = _foliage_hash(h0, 2, 0);
			const float x = cell->cx * k.cell + (float(i) + _foliage_u01(h0)) * step;
			const float z = cell->cz * k.cell + (float(j) + _foliage_u01(h1)) * step;
			if (x < 0.0f || z < 0.0f || x > ex || z > ez) {
				continue;
			}
			const float w = t->_layer_weight_local(k.layer, x, z);
			if (w < k.min_weight) {
				continue;
			}
			// Fewer where the layer's weight is lower: all at weight 1, none at min_weight.
			if (_foliage_u01(h2) > (w - k.min_weight) / MAX(1e-4f, 1.0f - k.min_weight) && w < 0.9999f) {
				continue;
			}
			const float y = t->_height_local(x, z);
			const float hl = t->_height_local(MAX(0.0f, x - e), z);
			const float hr = t->_height_local(MIN(ex, x + e), z);
			const float hd = t->_height_local(x, MAX(0.0f, z - e));
			const float hu = t->_height_local(x, MIN(ez, z + e));
			const Vector3 n = Vector3((hl - hr) / (2.0f * e), 1.0f, (hd - hu) / (2.0f * e)).normalized();
			if (n.y > k.cos_slope_min + 1e-6f || n.y < k.cos_slope_max - 1e-6f || y < k.height_min || y > k.height_max) {
				continue;
			}
			const uint32_t h3 = _foliage_hash(h0, 3, 0);
			const uint32_t h4 = _foliage_hash(h0, 4, 0);
			const float sc = Math::lerp(k.scale_min, k.scale_max, _foliage_u01(h3));
			Basis b;
			if (k.random_yaw) {
				b = Basis(Vector3(0, 1, 0), _foliage_u01(h4) * Math::TAU);
			}
			if (k.align > 0.0f) {
				const Vector3 up = Vector3(0, 1, 0).lerp(n, k.align).normalized();
				b = Basis(Quaternion(Vector3(0, 1, 0), up)) * b;
			}
			b = b * Basis::from_scale(Vector3(sc, sc, sc));
			const Vector3 o(x, y - k.sink * sc, z);
			const float row[12] = { b.rows[0].x, b.rows[0].y, b.rows[0].z, o.x, b.rows[1].x, b.rows[1].y, b.rows[1].z, o.y, b.rows[2].x, b.rows[2].y, b.rows[2].z, o.z };
			for (int r = 0; r < 12; r++) {
				out.push_back(row[r]);
			}
			count++;
		}
	}
	cell->buffer.resize(out.size());
	if (!out.is_empty()) {
		memcpy(cell->buffer.ptrw(), out.ptr(), out.size() * sizeof(float));
	}
	cell->count = count;
}

void FellowTerrain3D::_foliage_changed() {
	foliage_dirty = true;
}

void FellowTerrain3D::_update_foliage_kinds() {
	foliage_kinds.clear();
	for (int i = 0; i < foliage.size(); i++) {
		Ref<FellowTerrainFoliage> f = foliage[i];
		FoliageKind k;
		if (f.is_valid() && f->get_mesh().is_valid()) {
			k.mesh = f->get_mesh()->get_rid();
			k.layer = f->get_layer();
			k.density = f->get_density();
			k.min_weight = f->get_min_weight();
			k.range = f->get_range();
			k.cell = f->get_effective_cell_size();
			k.cos_slope_min = Math::cos(Math::deg_to_rad(MIN(f->get_slope_min(), f->get_slope_max())));
			k.cos_slope_max = Math::cos(Math::deg_to_rad(MAX(f->get_slope_min(), f->get_slope_max())));
			k.height_min = f->get_height_min();
			k.height_max = f->get_height_max();
			k.scale_min = f->get_scale_min();
			k.scale_max = f->get_scale_max();
			k.random_yaw = f->is_random_yaw();
			k.align = f->get_align_to_normal();
			k.sink = f->get_sink();
			k.shadows = f->is_casting_shadows();
			k.seed = uint32_t(f->get_seed()) * 2654435761u + uint32_t(i) * 40503u + 17u;
		} else {
			k.density = 0.0f; // no mesh: nothing to scatter
		}
		foliage_kinds.push_back(k);
	}
}

void FellowTerrain3D::_free_foliage_cell(FoliageCell *p_cell) {
	if (p_cell->task != WorkerThreadPool::INVALID_TASK_ID) {
		WorkerThreadPool::get_singleton()->wait_for_task_completion(p_cell->task);
		p_cell->task = WorkerThreadPool::INVALID_TASK_ID;
	}
	RenderingServer *rs = RenderingServer::get_singleton();
	if (p_cell->instance.is_valid()) {
		rs->free_rid(p_cell->instance);
		foliage_instances -= p_cell->count;
	}
	if (p_cell->multimesh.is_valid()) {
		rs->free_rid(p_cell->multimesh);
	}
	memdelete(p_cell);
}

void FellowTerrain3D::_clear_foliage() {
	for (KeyValue<uint64_t, FoliageCell *> &kv : foliage_cells) {
		_free_foliage_cell(kv.value);
	}
	foliage_cells.clear();
	foliage_instances = 0;
}

void FellowTerrain3D::_make_foliage_cell_visible(FoliageCell *p_cell) {
	p_cell->ready = true;
	if (p_cell->count == 0 || !is_inside_tree()) {
		p_cell->buffer.clear();
		return;
	}
	const FoliageKind &k = foliage_kinds[p_cell->kind];
	RenderingServer *rs = RenderingServer::get_singleton();
	p_cell->multimesh = rs->multimesh_create();
	rs->multimesh_set_mesh(p_cell->multimesh, k.mesh);
	rs->multimesh_allocate_data(p_cell->multimesh, p_cell->count, RSE::MULTIMESH_TRANSFORM_3D, false, false);
	rs->multimesh_set_buffer(p_cell->multimesh, p_cell->buffer);
	p_cell->buffer.clear();
	p_cell->instance = rs->instance_create2(p_cell->multimesh, get_world_3d()->get_scenario());
	rs->instance_set_transform(p_cell->instance, get_global_transform());
	rs->instance_attach_object_instance_id(p_cell->instance, get_instance_id());
	rs->instance_geometry_set_cast_shadows_setting(p_cell->instance, k.shadows ? RSE::SHADOW_CASTING_SETTING_ON : RSE::SHADOW_CASTING_SETTING_OFF);
	rs->instance_set_layer_mask(p_cell->instance, render_layer_mask);
	rs->instance_set_visible(p_cell->instance, is_visible_in_tree());
	// Each instance drawn by the camera up to the kind's range (culled one by one on the GPU, decision 0122's range).
	rs->fellow_instance_set_instance_range(p_cell->instance, 0.0f, k.range, false);
	foliage_instances += p_cell->count;
	Fellow::request_redraw();
}

void FellowTerrain3D::_process_foliage(const Vector3 &p_camera) {
	if (foliage_dirty) {
		_clear_foliage();
		_update_foliage_kinds();
		foliage_dirty = false;
	}
	if (foliage_kinds.is_empty() || map_w < 2) {
		return;
	}
	foliage_frame++;
	const float ex = float(map_w - 1) * vertex_spacing;
	const float ez = float(map_h - 1) * vertex_spacing;
	int launched = 0;
	for (uint32_t ki = 0; ki < foliage_kinds.size(); ki++) {
		const FoliageKind &k = foliage_kinds[ki];
		if (k.density <= 0.0f) {
			continue;
		}
		const float cs = k.cell;
		const float r = k.range;
		const int x0 = MAX(0, int(Math::floor((p_camera.x - r) / cs)));
		const int x1 = MIN(int(Math::ceil(ex / cs)) - 1, int(Math::floor((p_camera.x + r) / cs)));
		const int z0 = MAX(0, int(Math::floor((p_camera.z - r) / cs)));
		const int z1 = MIN(int(Math::ceil(ez / cs)) - 1, int(Math::floor((p_camera.z + r) / cs)));
		for (int cz = z0; cz <= z1; cz++) {
			for (int cx = x0; cx <= x1; cx++) {
				const float dx = MAX(0.0f, MAX(cx * cs - p_camera.x, p_camera.x - (cx + 1) * cs));
				const float dz = MAX(0.0f, MAX(cz * cs - p_camera.z, p_camera.z - (cz + 1) * cs));
				if (dx * dx + dz * dz > r * r) {
					continue;
				}
				const uint64_t key = _foliage_key(ki, cx, cz);
				FoliageCell **found = foliage_cells.getptr(key);
				FoliageCell *cell = found ? *found : nullptr;
				if (!cell) {
					if (launched >= foliage_cells_per_frame) {
						continue; // next frame
					}
					cell = memnew(FoliageCell);
					cell->terrain = this;
					cell->kind = ki;
					cell->cx = cx;
					cell->cz = cz;
					cell->task = WorkerThreadPool::get_singleton()->add_native_task(&FellowTerrain3D::_generate_foliage_cell, cell, false, SNAME("FellowTerrain3D foliage"));
					foliage_cells.insert(key, cell);
					launched++;
				}
				cell->seen = foliage_frame;
			}
		}
	}
	LocalVector<uint64_t> dropped;
	for (KeyValue<uint64_t, FoliageCell *> &kv : foliage_cells) {
		FoliageCell *cell = kv.value;
		if (!cell->ready && cell->task != WorkerThreadPool::INVALID_TASK_ID && WorkerThreadPool::get_singleton()->is_task_completed(cell->task)) {
			WorkerThreadPool::get_singleton()->wait_for_task_completion(cell->task);
			cell->task = WorkerThreadPool::INVALID_TASK_ID;
			_make_foliage_cell_visible(cell);
		}
		if (cell->seen != foliage_frame) {
			// Out of range: dropped 15 % beyond it (so that a camera on a cell's edge does not make and free it in turn).
			const FoliageKind &k = foliage_kinds[cell->kind];
			const float cs = k.cell;
			const float dx = MAX(0.0f, MAX(cell->cx * cs - p_camera.x, p_camera.x - (cell->cx + 1) * cs));
			const float dz = MAX(0.0f, MAX(cell->cz * cs - p_camera.z, p_camera.z - (cell->cz + 1) * cs));
			if (dx * dx + dz * dz > k.range * k.range * 1.3225f) {
				dropped.push_back(kv.key);
			}
		}
	}
	for (const uint64_t key : dropped) {
		_free_foliage_cell(foliage_cells[key]);
		foliage_cells.erase(key);
	}
	if (launched > 0) {
		Fellow::request_redraw(); // the editor draws again to see the new cells finished
	}
}

void FellowTerrain3D::set_foliage(const TypedArray<FellowTerrainFoliage> &p_foliage) {
	const Callable cb = callable_mp(this, &FellowTerrain3D::_foliage_changed);
	for (int i = 0; i < foliage.size(); i++) {
		Ref<FellowTerrainFoliage> f = foliage[i];
		if (f.is_valid() && f->is_connected(CoreStringName(changed), cb)) {
			f->disconnect(CoreStringName(changed), cb);
		}
	}
	foliage = p_foliage;
	for (int i = 0; i < foliage.size(); i++) {
		Ref<FellowTerrainFoliage> f = foliage[i];
		if (f.is_valid() && !f->is_connected(CoreStringName(changed), cb)) {
			f->connect(CoreStringName(changed), cb);
		}
	}
	foliage_dirty = true;
}

void FellowTerrain3D::set_foliage_cells_per_frame(int p_cells) {
	foliage_cells_per_frame = CLAMP(p_cells, 1, 256);
}

// ---------------------------------------------------------------------------------------------------------------------
// Queries

float FellowTerrain3D::_height_local(float p_x, float p_z) const {
	const float fx = p_x / vertex_spacing;
	const float fz = p_z / vertex_spacing;
	if (map_w < 2 || fx < 0.0f || fz < 0.0f || fx > float(map_w - 1) || fz > float(map_h - 1)) {
		return Math::NaN;
	}
	const int x = MIN(int(fx), map_w - 2);
	const int z = MIN(int(fz), map_h - 2);
	const float tx = fx - x;
	const float tz = fz - z;
	const float *h = heights.ptr();
	const float a = h[z * map_w + x];
	const float b = h[z * map_w + x + 1];
	const float c = h[(z + 1) * map_w + x];
	const float d = h[(z + 1) * map_w + x + 1];
	// The mesh's triangles (diagonal from (x + 1, z) to (x, z + 1)), as the GPU draws the finest level.
	if (tx + tz <= 1.0f) {
		return a + (b - a) * tx + (c - a) * tz;
	}
	return d + (c - d) * (1.0f - tx) + (b - d) * (1.0f - tz);
}

void FellowTerrain3D::_ensure_data() const {
	if (data_dirty) {
		const_cast<FellowTerrain3D *>(this)->_rebuild_data();
	}
}

float FellowTerrain3D::get_height(const Vector3 &p_global) const {
	_ensure_data();
	const Transform3D xf = is_inside_tree() ? get_global_transform() : get_transform();
	const Vector3 l = xf.affine_inverse().xform(p_global);
	const float h = _height_local(l.x, l.z);
	if (Math::is_nan(h)) {
		return h;
	}
	return xf.xform(Vector3(l.x, h, l.z)).y;
}

Vector3 FellowTerrain3D::get_normal(const Vector3 &p_global) const {
	_ensure_data();
	const Transform3D xf = is_inside_tree() ? get_global_transform() : get_transform();
	const Vector3 l = xf.affine_inverse().xform(p_global);
	const float e = vertex_spacing;
	const float hl = _height_local(MAX(0.0f, l.x - e), l.z);
	const float hr = _height_local(MIN(float(map_w - 1) * e, l.x + e), l.z);
	const float hd = _height_local(l.x, MAX(0.0f, l.z - e));
	const float hu = _height_local(l.x, MIN(float(map_h - 1) * e, l.z + e));
	if (Math::is_nan(hl) || Math::is_nan(hr) || Math::is_nan(hd) || Math::is_nan(hu)) {
		return Vector3();
	}
	const Vector3 n((hl - hr) / (2.0f * e), 1.0f, (hd - hu) / (2.0f * e));
	return xf.basis.orthonormalized().xform(n.normalized()).normalized();
}

PackedFloat32Array FellowTerrain3D::get_layer_weights(const Vector3 &p_global) const {
	_ensure_data();
	PackedFloat32Array r;
	r.resize(MAX_LAYERS);
	r.fill(0.0f);
	const Transform3D xf = is_inside_tree() ? get_global_transform() : get_transform();
	const Vector3 l = xf.affine_inverse().xform(p_global);
	if (map_w < 2) {
		return r;
	}
	const float u = CLAMP((l.x / vertex_spacing + 0.5f) / map_w, 0.0f, 1.0f);
	const float v = CLAMP((l.z / vertex_spacing + 0.5f) / map_h, 0.0f, 1.0f);
	float sum = 0.0f;
	for (int s = 0; s < 2; s++) {
		const Ref<Image> img = s == 0 ? splat_map : splat_map_2;
		if (img.is_null() || img->is_empty() || img->is_compressed()) {
			continue;
		}
		const int px = CLAMP(int(u * img->get_width()), 0, img->get_width() - 1);
		const int pz = CLAMP(int(v * img->get_height()), 0, img->get_height() - 1);
		const Color c = img->get_pixel(px, pz);
		const float w[4] = { c.r, c.g, c.b, c.a };
		for (int i = 0; i < 4; i++) {
			r.write[1 + s * 4 + i] = w[i];
			sum += w[i];
		}
	}
	r.write[0] = MAX(0.0f, 1.0f - sum);
	return r;
}

AABB FellowTerrain3D::get_aabb() const {
	_ensure_data();
	if (map_w < 2) {
		return AABB();
	}
	return AABB(Vector3(0, height_min, 0), Vector3(float(map_w - 1) * vertex_spacing, height_max - height_min, float(map_h - 1) * vertex_spacing));
}

Dictionary FellowTerrain3D::get_stats() const {
	Dictionary d;
	const int n = shown >= 0 ? int(patches[shown].size()) : 0;
	d["patches"] = n;
	d["triangles"] = n * patch_size * patch_size * 2;
	d["levels"] = top_level + 1;
	d["lod_distance"] = ranges[0];
	d["selections"] = int64_t(selections);
	d["swaps"] = int64_t(swaps);
	d["last_select_usec"] = int64_t(last_select_usec);
	d["collision_shapes"] = int(shapes.size());
	d["foliage_cells"] = int(foliage_cells.size());
	d["foliage_instances"] = foliage_instances;
	d["map_size"] = Vector2i(map_w, map_h);
	PackedInt32Array per_level;
	per_level.resize(top_level + 1);
	per_level.fill(0);
	if (shown >= 0) {
		for (const Patch &p : patches[shown]) {
			per_level.write[p.level]++;
		}
	}
	d["patches_per_level"] = per_level;
	return d;
}

void FellowTerrain3D::update_heightmap() {
	data_dirty = true;
	if (is_inside_tree()) {
		_rebuild_data();
		if (instance[0].is_valid()) {
			RenderingServer *rs = RenderingServer::get_singleton();
			for (int i = 0; i < 2; i++) {
				rs->multimesh_set_mesh(multimesh[i], patch_mesh->get_rid());
			}
		}
		_update_material();
	}
}

// ---------------------------------------------------------------------------------------------------------------------
// Properties

void FellowTerrain3D::set_heightmap(const Ref<Image> &p_image) {
	heightmap = p_image;
	update_heightmap();
	update_configuration_warnings();
}

void FellowTerrain3D::set_splat_map(const Ref<Image> &p_image) {
	splat_map = p_image;
	update_heightmap();
}

void FellowTerrain3D::set_splat_map_2(const Ref<Image> &p_image) {
	splat_map_2 = p_image;
	update_heightmap();
}

void FellowTerrain3D::set_layers(const TypedArray<FellowTerrainLayer> &p_layers) {
	const Callable cb = callable_mp(this, &FellowTerrain3D::_layer_changed);
	for (int i = 0; i < layers.size(); i++) {
		Ref<FellowTerrainLayer> l = layers[i];
		if (l.is_valid() && l->is_connected(CoreStringName(changed), cb)) {
			l->disconnect(CoreStringName(changed), cb);
		}
	}
	layers = p_layers;
	if (layers.size() > MAX_LAYERS) {
		WARN_PRINT(vformat("FellowTerrain3D: %d layers, the first %d are used.", layers.size(), MAX_LAYERS));
	}
	for (int i = 0; i < layers.size(); i++) {
		Ref<FellowTerrainLayer> l = layers[i];
		if (l.is_valid() && !l->is_connected(CoreStringName(changed), cb)) {
			l->connect(CoreStringName(changed), cb);
		}
	}
	_layer_changed();
}

void FellowTerrain3D::set_vertex_spacing(float p_spacing) {
	vertex_spacing = MAX(0.01f, p_spacing);
	update_heightmap();
}

void FellowTerrain3D::set_patch_size(int p_size) {
	int s = 8;
	while (s < p_size && s < 64) {
		s *= 2;
	}
	patch_size = s;
	update_heightmap();
}

void FellowTerrain3D::set_lod_distance(float p_distance) {
	lod_distance = MAX(0.0f, p_distance);
	material_dirty = true;
	selection_valid = false;
}

void FellowTerrain3D::set_morph_ratio(float p_ratio) {
	morph_ratio = CLAMP(p_ratio, 0.05f, 0.95f);
	material_dirty = true;
	selection_valid = false;
}

void FellowTerrain3D::set_height_blend(float p_blend) {
	height_blend = MAX(0.0f, p_blend);
	if (material.is_valid()) {
		material->set_shader_parameter("height_blend", height_blend);
	}
}

void FellowTerrain3D::set_macro_variation(float p_variation) {
	macro_variation = CLAMP(p_variation, 0.0f, 1.0f);
	if (material.is_valid()) {
		material->set_shader_parameter("macro_variation", macro_variation);
	}
}

void FellowTerrain3D::set_debug_view(DebugView p_view) {
	debug_view = p_view;
	if (material.is_valid()) {
		material->set_shader_parameter("debug_view", int(debug_view));
	}
}

void FellowTerrain3D::set_cast_shadows(bool p_enable) {
	cast_shadows = p_enable;
	_update_instance_settings();
}

void FellowTerrain3D::set_render_layer_mask(uint32_t p_mask) {
	render_layer_mask = p_mask;
	_update_instance_settings();
}

void FellowTerrain3D::set_collision_enabled(bool p_enable) {
	collision_enabled = p_enable;
	if (is_inside_tree()) {
		_update_collision();
	}
}

void FellowTerrain3D::set_collision_layer(uint32_t p_layer) {
	collision_layer = p_layer;
	if (body.is_valid()) {
		PhysicsServer3D::get_singleton()->body_set_collision_layer(body, collision_layer);
	}
}

void FellowTerrain3D::set_collision_mask(uint32_t p_mask) {
	collision_mask = p_mask;
	if (body.is_valid()) {
		PhysicsServer3D::get_singleton()->body_set_collision_mask(body, collision_mask);
	}
}

void FellowTerrain3D::set_collision_region_size(int p_size) {
	collision_region_size = CLAMP(p_size, 16, 4096);
	if (is_inside_tree()) {
		_update_collision();
	}
}

PackedStringArray FellowTerrain3D::get_configuration_warnings() const {
	PackedStringArray warnings = Node3D::get_configuration_warnings();
	if (heightmap.is_null() || heightmap->is_empty()) {
		warnings.push_back(RTR("A heightmap (an Image of heights in metres, FORMAT_RF) is required for the terrain to be drawn. FellowTerrain3D.heightmap_from_image() converts a grey image."));
	} else if (heightmap->get_width() < 2 || heightmap->get_height() < 2) {
		warnings.push_back(RTR("The heightmap needs at least 2 x 2 texels."));
	}
	if (layers.size() > MAX_LAYERS) {
		warnings.push_back(vformat(RTR("Only the first %d layers are used."), MAX_LAYERS));
	}
	return warnings;
}

void FellowTerrain3D::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_ENTER_WORLD: {
			if (data_dirty) {
				_rebuild_data();
			} else {
				_update_collision();
			}
			_update_material();
			_create_instances();
			set_process_internal(true);
		} break;
		case NOTIFICATION_EXIT_WORLD: {
			set_process_internal(false);
			_clear_foliage();
			foliage_dirty = true;
			_free_instances();
			_free_collision();
		} break;
		case NOTIFICATION_TRANSFORM_CHANGED: {
			_update_transforms();
		} break;
		case NOTIFICATION_VISIBILITY_CHANGED: {
			_update_instance_settings();
		} break;
		case NOTIFICATION_INTERNAL_PROCESS: {
			_process_lod();
			if (!foliage.is_empty() || !foliage_cells.is_empty()) {
				Camera3D *cam = _find_camera();
				if (cam && map_w >= 2) {
					_process_foliage(get_global_transform().affine_inverse().xform(cam->get_global_position()));
				}
			}
		} break;
	}
}

void FellowTerrain3D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_heightmap", "image"), &FellowTerrain3D::set_heightmap);
	ClassDB::bind_method(D_METHOD("get_heightmap"), &FellowTerrain3D::get_heightmap);
	ClassDB::bind_method(D_METHOD("set_splat_map", "image"), &FellowTerrain3D::set_splat_map);
	ClassDB::bind_method(D_METHOD("get_splat_map"), &FellowTerrain3D::get_splat_map);
	ClassDB::bind_method(D_METHOD("set_splat_map_2", "image"), &FellowTerrain3D::set_splat_map_2);
	ClassDB::bind_method(D_METHOD("get_splat_map_2"), &FellowTerrain3D::get_splat_map_2);
	ClassDB::bind_method(D_METHOD("set_layers", "layers"), &FellowTerrain3D::set_layers);
	ClassDB::bind_method(D_METHOD("get_layers"), &FellowTerrain3D::get_layers);
	ClassDB::bind_method(D_METHOD("set_vertex_spacing", "metres"), &FellowTerrain3D::set_vertex_spacing);
	ClassDB::bind_method(D_METHOD("get_vertex_spacing"), &FellowTerrain3D::get_vertex_spacing);
	ClassDB::bind_method(D_METHOD("set_patch_size", "quads"), &FellowTerrain3D::set_patch_size);
	ClassDB::bind_method(D_METHOD("get_patch_size"), &FellowTerrain3D::get_patch_size);
	ClassDB::bind_method(D_METHOD("set_lod_distance", "metres"), &FellowTerrain3D::set_lod_distance);
	ClassDB::bind_method(D_METHOD("get_lod_distance"), &FellowTerrain3D::get_lod_distance);
	ClassDB::bind_method(D_METHOD("set_morph_ratio", "ratio"), &FellowTerrain3D::set_morph_ratio);
	ClassDB::bind_method(D_METHOD("get_morph_ratio"), &FellowTerrain3D::get_morph_ratio);
	ClassDB::bind_method(D_METHOD("set_height_blend", "blend"), &FellowTerrain3D::set_height_blend);
	ClassDB::bind_method(D_METHOD("get_height_blend"), &FellowTerrain3D::get_height_blend);
	ClassDB::bind_method(D_METHOD("set_macro_variation", "variation"), &FellowTerrain3D::set_macro_variation);
	ClassDB::bind_method(D_METHOD("get_macro_variation"), &FellowTerrain3D::get_macro_variation);
	ClassDB::bind_method(D_METHOD("set_debug_view", "view"), &FellowTerrain3D::set_debug_view);
	ClassDB::bind_method(D_METHOD("get_debug_view"), &FellowTerrain3D::get_debug_view);
	ClassDB::bind_method(D_METHOD("set_cast_shadows", "enable"), &FellowTerrain3D::set_cast_shadows);
	ClassDB::bind_method(D_METHOD("is_casting_shadows"), &FellowTerrain3D::is_casting_shadows);
	ClassDB::bind_method(D_METHOD("set_render_layer_mask", "mask"), &FellowTerrain3D::set_render_layer_mask);
	ClassDB::bind_method(D_METHOD("get_render_layer_mask"), &FellowTerrain3D::get_render_layer_mask);
	ClassDB::bind_method(D_METHOD("set_collision_enabled", "enable"), &FellowTerrain3D::set_collision_enabled);
	ClassDB::bind_method(D_METHOD("is_collision_enabled"), &FellowTerrain3D::is_collision_enabled);
	ClassDB::bind_method(D_METHOD("set_collision_layer", "layer"), &FellowTerrain3D::set_collision_layer);
	ClassDB::bind_method(D_METHOD("get_collision_layer"), &FellowTerrain3D::get_collision_layer);
	ClassDB::bind_method(D_METHOD("set_collision_mask", "mask"), &FellowTerrain3D::set_collision_mask);
	ClassDB::bind_method(D_METHOD("get_collision_mask"), &FellowTerrain3D::get_collision_mask);
	ClassDB::bind_method(D_METHOD("set_collision_region_size", "cells"), &FellowTerrain3D::set_collision_region_size);
	ClassDB::bind_method(D_METHOD("get_collision_region_size"), &FellowTerrain3D::get_collision_region_size);

	ClassDB::bind_method(D_METHOD("set_foliage", "foliage"), &FellowTerrain3D::set_foliage);
	ClassDB::bind_method(D_METHOD("get_foliage"), &FellowTerrain3D::get_foliage);
	ClassDB::bind_method(D_METHOD("set_foliage_cells_per_frame", "cells"), &FellowTerrain3D::set_foliage_cells_per_frame);
	ClassDB::bind_method(D_METHOD("get_foliage_cells_per_frame"), &FellowTerrain3D::get_foliage_cells_per_frame);

	ClassDB::bind_method(D_METHOD("update_heightmap"), &FellowTerrain3D::update_heightmap);
	ClassDB::bind_method(D_METHOD("get_height", "global_position"), &FellowTerrain3D::get_height);
	ClassDB::bind_method(D_METHOD("get_normal", "global_position"), &FellowTerrain3D::get_normal);
	ClassDB::bind_method(D_METHOD("get_layer_weights", "global_position"), &FellowTerrain3D::get_layer_weights);
	ClassDB::bind_method(D_METHOD("get_aabb"), &FellowTerrain3D::get_aabb);
	ClassDB::bind_method(D_METHOD("get_material"), &FellowTerrain3D::get_material);
	ClassDB::bind_method(D_METHOD("get_stats"), &FellowTerrain3D::get_stats);
	ClassDB::bind_static_method("FellowTerrain3D", D_METHOD("heightmap_from_image", "image", "min_height", "max_height"), &FellowTerrain3D::heightmap_from_image);

	ADD_GROUP("Data", "");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "heightmap", PROPERTY_HINT_RESOURCE_TYPE, "Image"), "set_heightmap", "get_heightmap");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "vertex_spacing", PROPERTY_HINT_RANGE, "0.01,16,0.01,or_greater,suffix:m"), "set_vertex_spacing", "get_vertex_spacing");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "splat_map", PROPERTY_HINT_RESOURCE_TYPE, "Image"), "set_splat_map", "get_splat_map");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "splat_map_2", PROPERTY_HINT_RESOURCE_TYPE, "Image"), "set_splat_map_2", "get_splat_map_2");
	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "layers", PROPERTY_HINT_ARRAY_TYPE, MAKE_RESOURCE_TYPE_HINT("FellowTerrainLayer")), "set_layers", "get_layers");
	ADD_GROUP("Material", "");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "height_blend", PROPERTY_HINT_RANGE, "0,2,0.01"), "set_height_blend", "get_height_blend");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "macro_variation", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_macro_variation", "get_macro_variation");
	ADD_GROUP("Foliage", "foliage");
	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "foliage", PROPERTY_HINT_ARRAY_TYPE, MAKE_RESOURCE_TYPE_HINT("FellowTerrainFoliage")), "set_foliage", "get_foliage");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "foliage_cells_per_frame", PROPERTY_HINT_RANGE, "1,256,1"), "set_foliage_cells_per_frame", "get_foliage_cells_per_frame");
	ADD_GROUP("Level of Detail", "");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "patch_size", PROPERTY_HINT_ENUM, "8:8,16:16,32:32,64:64"), "set_patch_size", "get_patch_size");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "lod_distance", PROPERTY_HINT_RANGE, "0,1000,0.1,or_greater,suffix:m"), "set_lod_distance", "get_lod_distance");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "morph_ratio", PROPERTY_HINT_RANGE, "0.05,0.95,0.01"), "set_morph_ratio", "get_morph_ratio");
	ADD_GROUP("Rendering", "");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "cast_shadows"), "set_cast_shadows", "is_casting_shadows");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "render_layer_mask", PROPERTY_HINT_LAYERS_3D_RENDER), "set_render_layer_mask", "get_render_layer_mask");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "debug_view", PROPERTY_HINT_ENUM, "Off,Levels of Detail"), "set_debug_view", "get_debug_view");
	ADD_GROUP("Collision", "collision_");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "collision_enabled"), "set_collision_enabled", "is_collision_enabled");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "collision_layer", PROPERTY_HINT_LAYERS_3D_PHYSICS), "set_collision_layer", "get_collision_layer");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "collision_mask", PROPERTY_HINT_LAYERS_3D_PHYSICS), "set_collision_mask", "get_collision_mask");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "collision_region_size", PROPERTY_HINT_RANGE, "16,4096,1,suffix:cells"), "set_collision_region_size", "get_collision_region_size");

	BIND_ENUM_CONSTANT(DEBUG_VIEW_OFF);
	BIND_ENUM_CONSTANT(DEBUG_VIEW_LEVELS);
}

FellowTerrain3D::FellowTerrain3D() {
	set_notify_transform(true);
}

FellowTerrain3D::~FellowTerrain3D() {
	_clear_foliage();
	_free_instances();
	_free_collision();
}
