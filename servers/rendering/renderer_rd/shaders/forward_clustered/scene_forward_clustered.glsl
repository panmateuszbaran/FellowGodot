#[vertex]

#version 450

#VERSION_DEFINES

/* Include half precision types. */
#include "../half_inc.glsl"

#include "scene_forward_clustered_inc.glsl"

#define SHADER_IS_SRGB false
#define SHADER_SPACE_FAR 0.0

#ifdef USE_MULTIVIEW
#define OUTPUT_IS_MULTIVIEW true
#else
#define OUTPUT_IS_MULTIVIEW false
#endif

/* INPUT ATTRIBS */

// Always contains vertex position in XYZ, can contain tangent angle in W.
layout(location = 0) in vec4 vertex_angle_attrib;

//only for pure render depth when normal is not used

#if defined(NORMAL_USED) || defined(TANGENT_USED)
// Contains Normal/Axis in RG, can contain tangent in BA.
layout(location = 1) in vec4 axis_tangent_attrib;
#endif

// Location 2 is unused.

#if defined(COLOR_USED)
layout(location = 3) in vec4 color_attrib;
#endif

#ifdef UV_USED
layout(location = 4) in vec2 uv_attrib;
#endif

#if defined(UV2_USED) || defined(USE_LIGHTMAP) || defined(MODE_RENDER_MATERIAL)
layout(location = 5) in vec2 uv2_attrib;
#endif

#if defined(CUSTOM0_USED)
layout(location = 6) in vec4 custom0_attrib;
#endif

#if defined(CUSTOM1_USED)
layout(location = 7) in vec4 custom1_attrib;
#endif

#if defined(CUSTOM2_USED)
layout(location = 8) in vec4 custom2_attrib;
#endif

#if defined(CUSTOM3_USED)
layout(location = 9) in vec4 custom3_attrib;
#endif

#if defined(BONES_USED) || defined(USE_PARTICLE_TRAILS)
layout(location = 10) in uvec4 bone_attrib;
#endif

#if defined(WEIGHTS_USED) || defined(USE_PARTICLE_TRAILS)
layout(location = 11) in vec4 weight_attrib;
#endif

#ifdef MOTION_VECTORS
layout(location = 12) in vec4 previous_vertex_attrib;

#if defined(NORMAL_USED) || defined(TANGENT_USED)
layout(location = 13) in vec4 previous_normal_attrib;
#endif

#endif // MOTION_VECTORS

void axis_angle_to_tbn(vec3 axis, float angle, out vec3 tangent, out vec3 binormal, out vec3 normal) {
	float c = cos(angle);
	float s = sin(angle);
	vec3 omc_axis = (1.0 - c) * axis;
	vec3 s_axis = s * axis;
	tangent = omc_axis.xxx * axis + vec3(c, -s_axis.z, s_axis.y);
	binormal = omc_axis.yyy * axis + vec3(s_axis.z, c, -s_axis.x);
	normal = omc_axis.zzz * axis + vec3(-s_axis.y, s_axis.x, c);
}

/* Varyings */

layout(location = 0) out vec3 vertex_interp;

#ifdef NORMAL_USED
layout(location = 1) out vec3 normal_interp;
#endif

#if defined(COLOR_USED)
layout(location = 2) out vec4 color_interp;
#endif

#ifdef UV_USED
layout(location = 3) out vec2 uv_interp;
#endif

#if defined(UV2_USED) || defined(USE_LIGHTMAP)
layout(location = 4) out vec2 uv2_interp;
#endif

#ifdef TANGENT_USED
layout(location = 5) out vec3 tangent_interp;
layout(location = 6) out vec3 binormal_interp;
#endif

#ifdef MOTION_VECTORS
layout(location = 7) out vec4 screen_position;
layout(location = 8) out vec4 prev_screen_position;
#endif

#ifdef MATERIAL_UNIFORMS_USED
/* clang-format off */
layout(set = MATERIAL_UNIFORM_SET, binding = 0, std140) uniform MaterialUniforms {
#MATERIAL_UNIFORMS
} material;
/* clang-format on */
#endif

float global_time;

#ifdef MODE_DUAL_PARABOLOID

layout(location = 9) out float dp_clip;

#endif

layout(location = 10) out flat uint instance_index_interp;

#ifdef USE_MULTIVIEW
#extension GL_EXT_multiview : enable
#define ViewIndex gl_ViewIndex

vec3 multiview_uv(vec2 uv) {
	return vec3(uv, ViewIndex);
}
ivec3 multiview_uv(ivec2 uv) {
	return ivec3(uv, int(ViewIndex));
}
layout(location = 11) out vec4 combined_projected;
#else // USE_MULTIVIEW
#define ViewIndex 0
vec2 multiview_uv(vec2 uv) {
	return uv;
}
ivec2 multiview_uv(ivec2 uv) {
	return uv;
}
#endif //USE_MULTIVIEW

#if !defined(MODE_RENDER_DEPTH) && !defined(MODE_UNSHADED) && defined(USE_VERTEX_LIGHTING)
layout(location = 12) out vec4 diffuse_light_interp;
layout(location = 13) out vec4 specular_light_interp;

#include "../scene_forward_vertex_lights_inc.glsl"

void cluster_get_item_range(uint p_offset, out uint item_min, out uint item_max, out uint item_from, out uint item_to) {
	uint item_min_max = cluster_buffer.data[p_offset];
	item_min = item_min_max & 0xFFFFu;
	item_max = item_min_max >> 16;

	item_from = item_min >> 5;
	item_to = (item_max == 0) ? 0 : ((item_max - 1) >> 5) + 1; //side effect of how it is stored, as item_max 0 means no elements
}

uint cluster_get_range_clip_mask(uint i, uint z_min, uint z_max) {
	int local_min = clamp(int(z_min) - int(i) * 32, 0, 31);
	int mask_width = min(int(z_max) - int(z_min), 32 - local_min);
	return bitfieldInsert(uint(0), uint(0xFFFFFFFF), local_min, mask_width);
}
#endif // !defined(MODE_RENDER_DEPTH) && !defined(MODE_UNSHADED) && defined(USE_VERTEX_LIGHTING)

#if defined(POINT_SIZE_USED) && defined(POINT_COORD_USED)
layout(location = 14) out vec2 point_coord_interp;
#endif

#if defined(FELLOW_WIND_USED) && !defined(MODE_RENDER_SDF)
// The wind's sheen (decision 0125): how much brighter the bent plant looks (WIND_SHEEN x its bend).
layout(location = 15) out float fellow_wind_sheen_interp;
#endif

invariant gl_Position;

#GLOBALS

#ifdef USE_DOUBLE_PRECISION
// Helper functions for emulating double precision when adding floats.
vec3 quick_two_sum(vec3 a, vec3 b, out vec3 out_p) {
	vec3 s = a + b;
	out_p = b - (s - a);
	return s;
}

vec3 two_sum(vec3 a, vec3 b, out vec3 out_p) {
	vec3 s = a + b;
	vec3 v = s - a;
	out_p = (a - (s - v)) + (b - v);
	return s;
}

vec3 double_add_vec3(vec3 base_a, vec3 prec_a, vec3 base_b, vec3 prec_b, out vec3 out_precision) {
	vec3 s, t, se, te;
	s = two_sum(base_a, base_b, se);
	t = two_sum(prec_a, prec_b, te);
	se += t;
	s = quick_two_sum(s, se, se);
	se += te;
	s = quick_two_sum(s, se, out_precision);
	return s;
}
#endif

uint multimesh_stride() {
	uint stride = sc_multimesh_format_2d() ? 2 : 3;
	stride += sc_multimesh_has_color() ? 1 : 0;
	stride += sc_multimesh_has_custom_data() ? 1 : 0;
	return stride;
}

#if defined(FELLOW_WIND_USED) && !defined(MODE_RENDER_SDF)
// FellowEngine wind (FellowWind3D, decision 0125). A plant bends with the wind's force (the square of its speed), more
// towards its top (the bend grows with the height's square, as a cantilever's), and sways about that at a natural
// frequency of its size; gusts (a noise field laid along the wind and travelling with it) change the force from plant
// to plant and in time, so they roll over a meadow in waves. Vertices away from the stem (branches, a clump's blades)
// sway at their own phases; leaves flutter along their normals at a phase of their own. The material says how tall
// the plant is in its mesh (WIND_HEIGHT: from y = 0 to the top), how much it bends (WIND_BEND) and flutters
// (WIND_FLUTTER). The previous frame's wind and time give the motion vectors.
float fellow_wind_hash(vec2 p_p) {
	vec3 p3 = fract(vec3(p_p.xyx) * 0.1031);
	p3 += dot(p3, p3.yzx + 33.33);
	return fract((p3.x + p3.y) * p3.z);
}

// The displacement in world space of a vertex at p_local (the mesh's space) of a plant placed by p_model, and the stem's
// tilt there (world, horizontal: how far the stem leans per metre of height), which turns the normal with the bend.
vec3 fellow_wind_offset(vec3 p_local, vec3 p_normal_world, mat4 p_model, bool p_prev, float p_height, float p_bend, float p_flutter, out vec3 r_tilt, out float r_bend) {
	r_tilt = vec3(0.0);
	r_bend = 0.0;
	vec4 w0 = p_prev ? fellow_weather.data.wind_prev : fellow_weather.data.wind;
	vec4 w1 = p_prev ? fellow_weather.data.wind2_prev : fellow_weather.data.wind2;
	// The wind's own clock (FellowWind3D.time): deterministic, and the previous frame's for the motion vectors.
	float p_time = p_prev ? fellow_weather.data.wind_time.y : fellow_weather.data.wind_time.x;
	if (w1.w <= 0.0 || w0.z <= 0.0) {
		return vec3(0.0);
	}
	const float TWO_PI = 6.2831853;
	vec2 dir = w0.xy;
	vec2 side = vec2(-dir.y, dir.x);
	vec3 origin = p_model[3].xyz;
	float height = max(p_height, 1e-3);
	float height_w = height * length(p_model[1].xyz);
	float h = clamp(p_local.y / height, 0.0, 1.0);

	// The gust at the plant (cells of the gust size, twice as long along the wind).
	vec2 guv = vec2((dot(origin.xz, dir) - w1.x) * 0.5, dot(origin.xz, side)) * (w1.y * 0.125);
	float g = textureLod(sampler2D(fellow_wind_gusts, SAMPLER_LINEAR_REPEAT), guv, 0.0).r;
	float speed = w0.z * max(0.0, 1.0 + w0.w * clamp((g - 0.5) * 3.0, -1.0, 1.0));
	float force = speed * speed * 0.01; // 1 at 10 m/s

	// The bend at the top, a fraction of the height: steady with the force, softly limited.
	float b = p_bend * force;
	b = 0.75 * b / (0.75 + b);
	float phase = fellow_wind_hash(origin.xz) * TWO_PI;
	float freq = clamp(1.0 / sqrt(height_w), 0.15, 3.0); // Hz: a tall tree slower than a blade of grass
	float along = b * (0.75 + 0.25 * sin(TWO_PI * freq * p_time + phase));
	r_bend = along * h / 0.75; // 0..1: how far this part of the plant is bent
	float across = b * 0.15 * sin(TWO_PI * freq * 1.37 * p_time + phase * 1.7);
	vec2 top = (dir * along + side * across) * height_w;
	vec3 d = vec3(top.x, 0.0, top.y) * (h * h);
	r_tilt = vec3(top.x, 0.0, top.y) * (2.0 * h / height_w);
	d.y = -0.5 * dot(d.xz, d.xz) / max(h * height_w, 0.01); // the stem keeps its length

	// Branches: away from the stem, swaying at their own phases (coherent over a branch).
	float r = min(length(p_local.xz) / height, 1.0);
	float bp = phase + dot(p_local, vec3(0.7, 0.4, 0.6)) * 2.0;
	float branch = r * h * (0.03 + 0.08 * min(force, 2.0)) * height_w;
	d += vec3(dir.x, 0.0, dir.y) * (branch * sin(TWO_PI * freq * 2.7 * p_time + bp)) + vec3(0.0, branch * 0.4 * cos(TWO_PI * freq * 2.1 * p_time + bp), 0.0);

	// Leaves: a flutter along their normals, a few hertz, a phase per vertex.
	if (p_flutter > 0.0) {
		float lh = fellow_wind_hash(p_local.xz * 17.0 + p_local.y * 5.3);
		float fl = sin(TWO_PI * (4.0 + 3.0 * lh) * p_time + lh * 37.0);
		// At most 0.24 m (MultiMesh culling's bound for wind materials).
		d += p_normal_world * (fl * min(p_flutter * w1.z, 4.0) * 0.03 * sqrt(min(force, 4.0)) * min(height_w, 1.0));
	}
	return d;
}
#endif

void vertex_shader(vec3 vertex_input,
#ifdef NORMAL_USED
		in vec3 normal_input,
#endif
#ifdef TANGENT_USED
		in vec3 tangent_input,
		in vec3 binormal_input,
#endif
		in uint instance_index, in uint multimesh_offset, in SceneData scene_data, in mat3x4 in_model_matrix,
#ifdef USE_DOUBLE_PRECISION
		in vec3 model_precision,
#endif
		out vec4 screen_pos) {
	vec4 instance_custom = vec4(0.0);
#if defined(COLOR_USED)
	color_interp = color_attrib;
#endif

	mat4 inv_view_matrix = transpose(mat4(scene_data.inv_view_matrix[0],
			scene_data.inv_view_matrix[1],
			scene_data.inv_view_matrix[2],
			vec4(0.0, 0.0, 0.0, 1.0)));

	mat4 model_matrix = transpose(mat4(in_model_matrix[0],
			in_model_matrix[1],
			in_model_matrix[2],
			vec4(0.0, 0.0, 0.0, 1.0)));

#ifdef USE_DOUBLE_PRECISION
	vec3 view_precision = scene_data.inv_view_precision.xyz;
#endif

	mat3 model_normal_matrix;
	if (bool(fellow_flags & INSTANCE_FLAGS_NON_UNIFORM_SCALE)) {
		model_normal_matrix = transpose(inverse(mat3(model_matrix)));
	} else {
		model_normal_matrix = mat3(model_matrix);
	}

	mat4 matrix;
	mat4 read_model_matrix = model_matrix;

	if (sc_multimesh()) {
		//multimesh, instances are for it

#ifdef USE_PARTICLE_TRAILS
		uint trail_size = (fellow_flags >> INSTANCE_FLAGS_PARTICLE_TRAIL_SHIFT) & INSTANCE_FLAGS_PARTICLE_TRAIL_MASK;
		uint stride = 3 + 1 + 1; //particles always uses this format

		uint offset = trail_size * stride * INSTANCE_INDEX;

#ifdef COLOR_USED
		vec4 pcolor;
#endif
		{
			uint boffset = offset + bone_attrib.x * stride;
			matrix = mat4(transforms.data[boffset + 0], transforms.data[boffset + 1], transforms.data[boffset + 2], vec4(0.0, 0.0, 0.0, 1.0)) * weight_attrib.x;
#ifdef COLOR_USED
			pcolor = transforms.data[boffset + 3] * weight_attrib.x;
#endif
		}
		if (weight_attrib.y > 0.001) {
			uint boffset = offset + bone_attrib.y * stride;
			matrix += mat4(transforms.data[boffset + 0], transforms.data[boffset + 1], transforms.data[boffset + 2], vec4(0.0, 0.0, 0.0, 1.0)) * weight_attrib.y;
#ifdef COLOR_USED
			pcolor += transforms.data[boffset + 3] * weight_attrib.y;
#endif
		}
		if (weight_attrib.z > 0.001) {
			uint boffset = offset + bone_attrib.z * stride;
			matrix += mat4(transforms.data[boffset + 0], transforms.data[boffset + 1], transforms.data[boffset + 2], vec4(0.0, 0.0, 0.0, 1.0)) * weight_attrib.z;
#ifdef COLOR_USED
			pcolor += transforms.data[boffset + 3] * weight_attrib.z;
#endif
		}
		if (weight_attrib.w > 0.001) {
			uint boffset = offset + bone_attrib.w * stride;
			matrix += mat4(transforms.data[boffset + 0], transforms.data[boffset + 1], transforms.data[boffset + 2], vec4(0.0, 0.0, 0.0, 1.0)) * weight_attrib.w;
#ifdef COLOR_USED
			pcolor += transforms.data[boffset + 3] * weight_attrib.w;
#endif
		}

		instance_custom = transforms.data[offset + 4];

#ifdef COLOR_USED
		color_interp *= pcolor;
#endif

#else
		uint stride = multimesh_stride();
		uint offset = stride * (INSTANCE_INDEX + multimesh_offset);

		if (sc_multimesh_format_2d()) {
			matrix = mat4(transforms.data[offset + 0], transforms.data[offset + 1], vec4(0.0, 0.0, 1.0, 0.0), vec4(0.0, 0.0, 0.0, 1.0));
			offset += 2;
		} else {
			matrix = mat4(transforms.data[offset + 0], transforms.data[offset + 1], transforms.data[offset + 2], vec4(0.0, 0.0, 0.0, 1.0));
			offset += 3;
		}

		if (sc_multimesh_has_color()) {
#ifdef COLOR_USED
			color_interp *= transforms.data[offset];
#endif
			offset += 1;
		}

		if (sc_multimesh_has_custom_data()) {
			instance_custom = transforms.data[offset];
		}

#endif
		//transpose
		matrix = transpose(matrix);
#if !defined(USE_DOUBLE_PRECISION) || defined(SKIP_TRANSFORM_USED) || defined(VERTEX_WORLD_COORDS_USED) || defined(MODEL_MATRIX_USED) || defined(FELLOW_WIND_USED)
		// Normally we can bake the multimesh transform into the model matrix, but when using double precision
		// we avoid baking it in so we can emulate high precision.
		read_model_matrix = model_matrix * matrix;
#if !defined(USE_DOUBLE_PRECISION) || defined(SKIP_TRANSFORM_USED) || defined(VERTEX_WORLD_COORDS_USED)
		model_matrix = read_model_matrix;
#endif // !defined(USE_DOUBLE_PRECISION) || defined(SKIP_TRANSFORM_USED) || defined(VERTEX_WORLD_COORDS_USED)
#endif // !defined(USE_DOUBLE_PRECISION) || defined(SKIP_TRANSFORM_USED) || defined(VERTEX_WORLD_COORDS_USED) || defined(MODEL_MATRIX_USED) || defined(FELLOW_WIND_USED)
		if (bool(fellow_flags & INSTANCE_FLAGS_FELLOW_INSTANCE_NORMALS)) {
			model_normal_matrix = model_normal_matrix * transpose(inverse(mat3(matrix)));
		} else {
			model_normal_matrix = model_normal_matrix * mat3(matrix);
		}
	}

	vec3 vertex = vertex_input;
#ifdef NORMAL_USED
	vec3 normal_highp = normal_input;
#endif

#ifdef TANGENT_USED
	vec3 tangent = tangent_input;
	vec3 binormal = binormal_input;
#endif

#ifdef UV_USED
	uv_interp = uv_attrib;
#endif

#if defined(UV2_USED) || defined(USE_LIGHTMAP)
	uv2_interp = uv2_attrib;
#endif

	vec4 uv_scale = FELLOW_INSTANCE(uv_scale);

	if (uv_scale != vec4(0.0)) { // Compression enabled
#ifdef UV_USED
		uv_interp = (uv_interp - 0.5) * uv_scale.xy;
#endif
#if defined(UV2_USED) || defined(USE_LIGHTMAP)
		uv2_interp = (uv2_interp - 0.5) * uv_scale.zw;
#endif
	}

#ifdef OVERRIDE_POSITION
	vec4 position = vec4(1.0);
#endif

#ifdef USE_MULTIVIEW
	mat4 combined_projection = scene_data.projection_matrix;
	mat4 projection_matrix = scene_data.projection_matrix_view[ViewIndex];
	mat4 inv_projection_matrix = scene_data.inv_projection_matrix_view[ViewIndex];
	vec3 eye_offset = scene_data.eye_offset[ViewIndex].xyz;
#else
	mat4 projection_matrix = scene_data.projection_matrix;
	mat4 inv_projection_matrix = scene_data.inv_projection_matrix;
	vec3 eye_offset = vec3(0.0, 0.0, 0.0);
#endif //USE_MULTIVIEW

//using world coordinates
#if !defined(SKIP_TRANSFORM_USED) && defined(VERTEX_WORLD_COORDS_USED)

	vertex = (model_matrix * vec4(vertex, 1.0)).xyz;

#ifdef NORMAL_USED
	normal_highp = model_normal_matrix * normal_highp;
#endif

#ifdef TANGENT_USED

	tangent = model_normal_matrix * tangent;
	binormal = model_normal_matrix * binormal;

#endif
#endif

#ifdef Z_CLIP_SCALE_USED
	float z_clip_scale = 1.0;
#endif

	float roughness_highp = 1.0;

	mat4 read_view_matrix = transpose(mat4(scene_data.view_matrix[0],
			scene_data.view_matrix[1],
			scene_data.view_matrix[2],
			vec4(0.0, 0.0, 0.0, 1.0)));

#ifdef USE_DOUBLE_PRECISION
	mat4 modelview = read_view_matrix * model_matrix;

	// We separate the basis from the origin because the basis is fine with single point precision.
	// Then we combine the translations from the model matrix and the view matrix using emulated doubles.
	// We add the result to the vertex and ignore the final lost precision.
	vec3 model_origin = model_matrix[3].xyz;
	if (sc_multimesh()) {
		modelview = modelview * matrix;

		vec3 instance_origin = mat3(model_matrix) * matrix[3].xyz;
		model_origin = double_add_vec3(model_origin, model_precision, instance_origin, vec3(0.0), model_precision);
	}

	// Overwrite the translation part of modelview with improved precision.
	vec3 temp_precision; // Will be ignored.
	modelview[3].xyz = double_add_vec3(model_origin, model_precision, inv_view_matrix[3].xyz, view_precision, temp_precision);
	modelview[3].xyz = mat3(read_view_matrix) * modelview[3].xyz;
#else
	mat4 modelview = read_view_matrix * model_matrix;
#endif
	mat3 modelview_normal = mat3(read_view_matrix) * model_normal_matrix;
	vec2 read_viewport_size = scene_data.viewport_size;

#ifdef POINT_SIZE_USED
	float point_size = 1.0;
#endif
	// The wind's parameters for this material (decision 0125): WIND_HEIGHT, WIND_BEND, WIND_FLUTTER.
	float fellow_wind_height = 1.0;
	float fellow_wind_bend = 0.3;
	float fellow_wind_flutter = 0.0;
	float fellow_wind_sheen = 0.0;

	{
#CODE : VERTEX
	}

#if defined(FELLOW_WIND_USED) && !defined(MODE_RENDER_SDF) && !defined(SKIP_TRANSFORM_USED)
	// The wind (decision 0125): the displacement and the stem's tilt in world space, added after the transform below
	// (no inverse of the model matrix: in view space).
	vec3 wind_d;
	vec3 wind_tilt;
	float wind_bend_amount;
	{
#ifdef NORMAL_USED
#ifdef VERTEX_WORLD_COORDS_USED
		vec3 wind_normal = normalize(normal_highp);
#else
		vec3 wind_normal = normalize(model_normal_matrix * normal_highp);
#endif
#else
		vec3 wind_normal = vec3(0.0, 1.0, 0.0);
#endif
#ifdef VERTEX_WORLD_COORDS_USED
		vec3 wind_local = (inverse(read_model_matrix) * vec4(vertex, 1.0)).xyz;
#else
		vec3 wind_local = vertex;
#endif
		wind_d = fellow_wind_offset(wind_local, wind_normal, read_model_matrix, scene_data.time != scene_data_block.data.time, fellow_wind_height, fellow_wind_bend, fellow_wind_flutter, wind_tilt, wind_bend_amount);
		fellow_wind_sheen_interp = fellow_wind_sheen * wind_bend_amount;
	}
#endif

	float roughness = roughness_highp;
#ifdef NORMAL_USED
	vec3 normal = normal_highp;
#endif

// using local coordinates (default)
#if !defined(SKIP_TRANSFORM_USED) && !defined(VERTEX_WORLD_COORDS_USED)

	vertex = (modelview * vec4(vertex, 1.0)).xyz;

#ifdef NORMAL_USED
	normal = modelview_normal * normal;
#endif

#ifdef TANGENT_USED

	binormal = modelview_normal * binormal;
	tangent = modelview_normal * tangent;
#endif
#if defined(FELLOW_WIND_USED) && !defined(MODE_RENDER_SDF)
	{
		mat3 wind_view = mat3(read_view_matrix);
		vertex += wind_view * wind_d;
#ifdef NORMAL_USED
		// The normal turned with the stem (a small rotation about up x tilt): n + t (n . up) - up (t . n).
		vec3 wind_t = wind_view * wind_tilt;
		vec3 wind_up = wind_view[1];
		normal = normalize(normal) + wind_t * dot(normalize(normal), wind_up) - wind_up * dot(wind_t, normalize(normal));
#endif
	}
#endif
#endif // !defined(SKIP_TRANSFORM_USED) && !defined(VERTEX_WORLD_COORDS_USED)

//using world coordinates
#if !defined(SKIP_TRANSFORM_USED) && defined(VERTEX_WORLD_COORDS_USED)

#if defined(FELLOW_WIND_USED) && !defined(MODE_RENDER_SDF)
	vertex += wind_d;
#ifdef NORMAL_USED
	normal = normalize(normal) + wind_tilt * normalize(normal).y - vec3(0.0, 1.0, 0.0) * dot(wind_tilt, normalize(normal));
#endif
#endif
	vertex = (read_view_matrix * vec4(vertex, 1.0)).xyz;
#ifdef NORMAL_USED
	normal = (read_view_matrix * vec4(normal, 0.0)).xyz;
#endif

#ifdef TANGENT_USED
	binormal = (read_view_matrix * vec4(binormal, 0.0)).xyz;
	tangent = (read_view_matrix * vec4(tangent, 0.0)).xyz;
#endif
#endif

	vertex_interp = vertex;

	// Normalize TBN vectors before interpolation, per MikkTSpace.
	// See: http://www.mikktspace.com/
#ifdef NORMAL_USED
	normal_interp = normalize(normal);
#endif

#ifdef TANGENT_USED
	tangent_interp = normalize(tangent);
	binormal_interp = normalize(binormal);
#endif

#ifdef MODE_RENDER_DEPTH

#ifdef MODE_DUAL_PARABOLOID

	vertex_interp.z *= scene_data.dual_paraboloid_side;

	dp_clip = vertex_interp.z; //this attempts to avoid noise caused by objects sent to the other parabolloid side due to bias

	//for dual paraboloid shadow mapping, this is the fastest but least correct way, as it curves straight edges

	vec3 vtx = vertex_interp;
	float distance = length(vtx);
	vtx = normalize(vtx);
	vtx.xy /= 1.0 - vtx.z;
	vtx.z = (distance / scene_data.z_far);
	vtx.z = vtx.z * 2.0 - 1.0;
	vertex_interp = vtx;

#endif

#endif //MODE_RENDER_DEPTH

#ifdef OVERRIDE_POSITION
	gl_Position = position;
#else
	gl_Position = projection_matrix * vec4(vertex_interp, 1.0);
#endif

#ifdef USE_MULTIVIEW
	combined_projected = combined_projection * vec4(vertex_interp, 1.0);
#endif

#ifdef MOTION_VECTORS
	screen_pos = gl_Position;
#endif

#if !defined(MODE_RENDER_DEPTH) && !defined(MODE_UNSHADED) && defined(USE_VERTEX_LIGHTING)
	diffuse_light_interp = vec4(0.0);
	specular_light_interp = vec4(0.0);

#ifdef USE_MULTIVIEW
	vec3 view = -normalize(vertex_interp - eye_offset);
	vec2 clip_pos = clamp((combined_projected.xy / combined_projected.w) * 0.5 + 0.5, 0.0, 1.0);
#else
	vec3 view = -normalize(vertex_interp);
	vec2 clip_pos = clamp((gl_Position.xy / gl_Position.w) * 0.5 + 0.5, 0.0, 1.0);
#endif

	uvec2 screen_size = uvec2(1.0 / scene_data.screen_pixel_size);
	uvec2 screen_pixel = clamp(uvec2(clip_pos * vec2(screen_size)), uvec2(0), screen_size - uvec2(1));
	uvec2 cluster_pos = screen_pixel >> implementation_data.cluster_shift;
	uint cluster_offset = (implementation_data.cluster_width * cluster_pos.y + cluster_pos.x) * (implementation_data.max_cluster_element_count_div_32 + 32);
	uint cluster_z = fellow_cluster_z(-vertex_interp.z, scene_data.z_near, scene_data.z_far, scene_data.flags);

	{ //omni lights

		uint cluster_omni_offset = cluster_offset;

		uint item_min;
		uint item_max;
		uint item_from;
		uint item_to;

		cluster_get_item_range(cluster_omni_offset + implementation_data.max_cluster_element_count_div_32 + cluster_z, item_min, item_max, item_from, item_to);

		for (uint i = item_from; i < item_to; i++) {
			uint mask = cluster_buffer.data[cluster_omni_offset + i];
			mask &= cluster_get_range_clip_mask(i, item_min, item_max);
			uint merged_mask = mask;

			while (merged_mask != 0) {
				uint bit = findMSB(merged_mask);
				merged_mask &= ~(1u << bit);
				uint light_index = 32 * i + bit;

				if (!bool(omni_lights.data[light_index].mask & FELLOW_INSTANCE(layer_mask))) {
					continue; //not masked
				}

				if (omni_lights.data[light_index].bake_mode == LIGHT_BAKE_STATIC && bool(fellow_flags & INSTANCE_FLAGS_USE_LIGHTMAP)) {
					continue; // Statically baked light and object uses lightmap, skip
				}

				light_process_omni_vertex(light_index, vertex, view, normal, roughness,
						diffuse_light_interp.rgb, specular_light_interp.rgb);
			}
		}
	}

	{ //spot lights
		uint cluster_spot_offset = cluster_offset + implementation_data.cluster_type_size;

		uint item_min;
		uint item_max;
		uint item_from;
		uint item_to;

		cluster_get_item_range(cluster_spot_offset + implementation_data.max_cluster_element_count_div_32 + cluster_z, item_min, item_max, item_from, item_to);

		for (uint i = item_from; i < item_to; i++) {
			uint mask = cluster_buffer.data[cluster_spot_offset + i];
			mask &= cluster_get_range_clip_mask(i, item_min, item_max);
			uint merged_mask = mask;

			while (merged_mask != 0) {
				uint bit = findMSB(merged_mask);
				merged_mask &= ~(1u << bit);

				uint light_index = 32 * i + bit;

				if (!bool(spot_lights.data[light_index].mask & FELLOW_INSTANCE(layer_mask))) {
					continue; //not masked
				}

				if (spot_lights.data[light_index].bake_mode == LIGHT_BAKE_STATIC && bool(fellow_flags & INSTANCE_FLAGS_USE_LIGHTMAP)) {
					continue; // Statically baked light and object uses lightmap, skip
				}

				light_process_spot_vertex(light_index, vertex, view, normal, roughness,
						diffuse_light_interp.rgb, specular_light_interp.rgb);
			}
		}
	}

	{ // Directional light.

		// We process the first directional light separately as it may have shadows.
		vec3 directional_diffuse = vec3(0.0);
		vec3 directional_specular = vec3(0.0);

		for (uint i = 0; i < scene_data.directional_light_count; i++) {
			if (!bool(directional_lights.data[i].mask & FELLOW_INSTANCE(layer_mask))) {
				continue; // Not masked, skip.
			}

			if (directional_lights.data[i].bake_mode == LIGHT_BAKE_STATIC && bool(fellow_flags & INSTANCE_FLAGS_USE_LIGHTMAP)) {
				continue; // Statically baked light and object uses lightmap, skip.
			}
			if (i == 0) {
				light_compute_vertex(normal, directional_lights.data[0].direction, view,
						directional_lights.data[0].color * directional_lights.data[0].energy,
						true, roughness,
						directional_diffuse,
						directional_specular);
			} else {
				light_compute_vertex(normal, directional_lights.data[i].direction, view,
						directional_lights.data[i].color * directional_lights.data[i].energy,
						true, roughness,
						diffuse_light_interp.rgb,
						specular_light_interp.rgb);
			}
		}

		// Calculate the contribution from the shadowed light so we can scale the shadows accordingly.
		float diff_avg = dot(diffuse_light_interp.rgb, vec3(0.33333));
		float diff_dir_avg = dot(directional_diffuse, vec3(0.33333));
		if (diff_avg > 0.0) {
			diffuse_light_interp.a = diff_dir_avg / (diff_avg + diff_dir_avg);
		} else {
			diffuse_light_interp.a = 1.0;
		}

		diffuse_light_interp.rgb += directional_diffuse;

		float spec_avg = dot(specular_light_interp.rgb, vec3(0.33333));
		float spec_dir_avg = dot(directional_specular, vec3(0.33333));
		if (spec_avg > 0.0) {
			specular_light_interp.a = spec_dir_avg / (spec_avg + spec_dir_avg);
		} else {
			specular_light_interp.a = 1.0;
		}

		specular_light_interp.rgb += directional_specular;
	}

#endif //!defined(MODE_RENDER_DEPTH) && !defined(MODE_UNSHADED) && defined(USE_VERTEX_LIGHTING)

#ifdef MODE_RENDER_DEPTH
	if (bool(scene_data.flags & SCENE_DATA_FLAGS_USE_PANCAKE_SHADOWS)) {
		if (gl_Position.z >= 0.9999) {
			gl_Position.z = 0.9999;
		}
	}
#endif
#ifdef MODE_RENDER_MATERIAL
	if (bool(scene_data.flags & SCENE_DATA_FLAGS_USE_UV2_MATERIAL)) {
		vec2 uv_dest_attrib;
		if (uv_scale != vec4(0.0)) {
			uv_dest_attrib = (uv2_attrib.xy - 0.5) * uv_scale.zw;
		} else {
			uv_dest_attrib = uv2_attrib.xy;
		}

		vec2 uv_offset = unpackHalf2x16(draw_call.uv_offset);
		gl_Position.xy = (uv_dest_attrib + uv_offset) * 2.0 - 1.0;
		gl_Position.z = 0.00001;
		gl_Position.w = 1.0;
	}
#endif

#ifdef Z_CLIP_SCALE_USED
	if (!bool(scene_data.flags & SCENE_DATA_FLAGS_IN_SHADOW_PASS)) {
		gl_Position.z = mix(gl_Position.w, gl_Position.z, z_clip_scale);
	}
#endif

#ifdef POINT_SIZE_USED
	if (sc_emulate_point_size) {
		vec2 point_coords[6] = vec2[](
				vec2(0, 1),
				vec2(0, 0),
				vec2(1, 1),
				vec2(0, 0),
				vec2(1, 0),
				vec2(1, 1));

		vec2 point_coord = point_coords[gl_VertexIndex % 6];
		gl_Position.xy += (point_coord * 2.0 - 1.0) * (point_size / scene_data.viewport_size) * gl_Position.w;

#ifdef POINT_COORD_USED
		point_coord_interp = point_coord;
#endif
	} else {
		gl_PointSize = point_size;
	}
#endif
}

void _unpack_vertex_attributes(vec4 p_vertex_in, vec3 p_compressed_aabb_position, vec3 p_compressed_aabb_size,
#if defined(NORMAL_USED) || defined(TANGENT_USED)
		vec4 p_normal_in,
#ifdef NORMAL_USED
		out vec3 r_normal,
#endif
		out vec3 r_tangent,
		out vec3 r_binormal,
#endif
		out vec3 r_vertex) {

	r_vertex = p_vertex_in.xyz * p_compressed_aabb_size + p_compressed_aabb_position;
#ifdef NORMAL_USED
	r_normal = oct_to_vec3(p_normal_in.xy * 2.0 - 1.0);
#endif

#if defined(NORMAL_USED) || defined(TANGENT_USED)

	float binormal_sign;

	// This works because the oct value (0, 1) maps onto (0, 0, -1) which encodes to (1, 1).
	// Accordingly, if p_normal_in.z contains octahedral values, it won't equal (0, 1).
	if (p_normal_in.z > 0.0 || p_normal_in.w < 1.0) {
		// Uncompressed format.
		vec2 signed_tangent_attrib = p_normal_in.zw * 2.0 - 1.0;
		r_tangent = oct_to_vec3(vec2(signed_tangent_attrib.x, abs(signed_tangent_attrib.y) * 2.0 - 1.0));
		binormal_sign = sign(signed_tangent_attrib.y);
		r_binormal = normalize(cross(r_normal, r_tangent) * binormal_sign);
	} else {
		// Compressed format.
		float angle = p_vertex_in.w;
		binormal_sign = angle > 0.5 ? 1.0 : -1.0; // 0.5 does not exist in UNORM16, so values are either greater or smaller.
		angle = abs(angle * 2.0 - 1.0) * M_PI; // 0.5 is basically zero, allowing to encode both signs reliably.
		vec3 axis = r_normal;
		axis_angle_to_tbn(axis, angle, r_tangent, r_binormal, r_normal);
		r_binormal *= binormal_sign;
	}
#endif
}

void main() {
	uint instance_index = draw_call.instance_index;
	if (!sc_multimesh()) {
		instance_index += INSTANCE_INDEX;
	}

	instance_index_interp = instance_index;
	fellow_element_init(instance_index);

#ifdef MOTION_VECTORS
	// Previous vertex.
	vec3 prev_vertex;
#ifdef NORMAL_USED
	vec3 prev_normal;
#endif
#if defined(NORMAL_USED) || defined(TANGENT_USED)
	vec3 prev_tangent;
	vec3 prev_binormal;
#endif

	_unpack_vertex_attributes(
			previous_vertex_attrib,
			FELLOW_INSTANCE(compressed_aabb_position_pad).xyz,
			FELLOW_INSTANCE(compressed_aabb_size_pad).xyz,

#if defined(NORMAL_USED) || defined(TANGENT_USED)
			previous_normal_attrib,
#ifdef NORMAL_USED
			prev_normal,
#endif
			prev_tangent,
			prev_binormal,
#endif
			prev_vertex);

	global_time = scene_data_block.prev_data.time;
	vertex_shader(prev_vertex,
#ifdef NORMAL_USED
			prev_normal,
#endif
#ifdef TANGENT_USED
			prev_tangent,
			prev_binormal,
#endif
			instance_index, draw_call.multimesh_motion_vectors_previous_offset, scene_data_block.prev_data, FELLOW_INSTANCE(prev_transform),
#ifdef USE_DOUBLE_PRECISION
			FELLOW_INSTANCE(prev_model_precision).xyz,
#endif
			prev_screen_position);
#else
	// Unused output.
	vec4 screen_position;
#endif

	vec3 vertex;
#ifdef NORMAL_USED
	vec3 normal;
#endif
#if defined(NORMAL_USED) || defined(TANGENT_USED)
	vec3 tangent;
	vec3 binormal;
#endif

	_unpack_vertex_attributes(
			vertex_angle_attrib,
			FELLOW_INSTANCE(compressed_aabb_position_pad).xyz,
			FELLOW_INSTANCE(compressed_aabb_size_pad).xyz,
#if defined(NORMAL_USED) || defined(TANGENT_USED)
			axis_tangent_attrib,
#ifdef NORMAL_USED
			normal,
#endif
			tangent,
			binormal,
#endif
			vertex);

	// Current vertex.
	global_time = scene_data_block.data.time;
	vertex_shader(vertex,
#ifdef NORMAL_USED
			normal,
#endif
#ifdef TANGENT_USED
			tangent,
			binormal,
#endif
			instance_index, draw_call.multimesh_motion_vectors_current_offset, scene_data_block.data, FELLOW_INSTANCE(transform),
#ifdef USE_DOUBLE_PRECISION
			FELLOW_INSTANCE(model_precision).xyz,
#endif

			screen_position);
}

#[fragment]

#version 450

#VERSION_DEFINES

#ifdef FELLOW_EXP_DEMOTE
// Diagnostics (debug/scene_shader_experiments 4194304; for image tests on drivers that leave derivatives of quads with
// discarded invocations to chance): discard demotes the invocation to a helper, so that derivatives stay defined.
#extension GL_EXT_demote_to_helper_invocation : require
#define discard demote
#endif

#define SHADER_IS_SRGB false
#define SHADER_SPACE_FAR 0.0

#ifdef USE_MULTIVIEW
#define OUTPUT_IS_MULTIVIEW true
#else
#define OUTPUT_IS_MULTIVIEW false
#endif

/* Include half precision types. */
#include "../half_inc.glsl"

#include "scene_forward_clustered_inc.glsl"

/* Varyings */

layout(location = 0) in vec3 vertex_interp;

#ifdef NORMAL_USED
layout(location = 1) in vec3 normal_interp;
#endif

#if defined(COLOR_USED)
layout(location = 2) in vec4 color_interp;
#endif

#ifdef UV_USED
layout(location = 3) in vec2 uv_interp;
#endif

#if defined(UV2_USED) || defined(USE_LIGHTMAP)
layout(location = 4) in vec2 uv2_interp;
#endif

#ifdef TANGENT_USED
layout(location = 5) in vec3 tangent_interp;
layout(location = 6) in vec3 binormal_interp;
#endif

#ifdef MOTION_VECTORS
layout(location = 7) in vec4 screen_position;
layout(location = 8) in vec4 prev_screen_position;
#endif

#ifdef MODE_DUAL_PARABOLOID

layout(location = 9) in float dp_clip;

#endif

layout(location = 10) in flat uint instance_index_interp;

#ifdef USE_LIGHTMAP
// w0, w1, w2, and w3 are the four cubic B-spline basis functions
float w0(float a) {
	return (1.0 / 6.0) * (a * (a * (-a + 3.0) - 3.0) + 1.0);
}

float w1(float a) {
	return (1.0 / 6.0) * (a * a * (3.0 * a - 6.0) + 4.0);
}

float w2(float a) {
	return (1.0 / 6.0) * (a * (a * (-3.0 * a + 3.0) + 3.0) + 1.0);
}

float w3(float a) {
	return (1.0 / 6.0) * (a * a * a);
}

// g0 and g1 are the two amplitude functions
float g0(float a) {
	return w0(a) + w1(a);
}

float g1(float a) {
	return w2(a) + w3(a);
}

// h0 and h1 are the two offset functions
float h0(float a) {
	return -1.0 + w1(a) / (w0(a) + w1(a));
}

float h1(float a) {
	return 1.0 + w3(a) / (w2(a) + w3(a));
}

vec4 textureArray_bicubic(texture2DArray tex, vec3 uv, vec2 texture_size) {
	vec2 texel_size = vec2(1.0) / texture_size;

	uv.xy = uv.xy * texture_size + vec2(0.5);

	vec2 iuv = floor(uv.xy);
	vec2 fuv = fract(uv.xy);

	float g0x = g0(fuv.x);
	float g1x = g1(fuv.x);
	float h0x = h0(fuv.x);
	float h1x = h1(fuv.x);
	float h0y = h0(fuv.y);
	float h1y = h1(fuv.y);

	vec2 p0 = (vec2(iuv.x + h0x, iuv.y + h0y) - vec2(0.5)) * texel_size;
	vec2 p1 = (vec2(iuv.x + h1x, iuv.y + h0y) - vec2(0.5)) * texel_size;
	vec2 p2 = (vec2(iuv.x + h0x, iuv.y + h1y) - vec2(0.5)) * texel_size;
	vec2 p3 = (vec2(iuv.x + h1x, iuv.y + h1y) - vec2(0.5)) * texel_size;

	return (g0(fuv.y) * (g0x * texture(sampler2DArray(tex, SAMPLER_LINEAR_CLAMP), vec3(p0, uv.z)) + g1x * texture(sampler2DArray(tex, SAMPLER_LINEAR_CLAMP), vec3(p1, uv.z)))) +
			(g1(fuv.y) * (g0x * texture(sampler2DArray(tex, SAMPLER_LINEAR_CLAMP), vec3(p2, uv.z)) + g1x * texture(sampler2DArray(tex, SAMPLER_LINEAR_CLAMP), vec3(p3, uv.z))));
}
#endif //USE_LIGHTMAP

#ifdef USE_MULTIVIEW
#extension GL_EXT_multiview : enable
#define ViewIndex gl_ViewIndex
vec3 multiview_uv(vec2 uv) {
	return vec3(uv, ViewIndex);
}
ivec3 multiview_uv(ivec2 uv) {
	return ivec3(uv, int(ViewIndex));
}
layout(location = 11) in vec4 combined_projected;
#else // USE_MULTIVIEW
#define ViewIndex 0
vec2 multiview_uv(vec2 uv) {
	return uv;
}
ivec2 multiview_uv(ivec2 uv) {
	return uv;
}
#endif // !USE_MULTIVIEW
#if !defined(MODE_RENDER_DEPTH) && !defined(MODE_UNSHADED) && defined(USE_VERTEX_LIGHTING)
layout(location = 12) in vec4 diffuse_light_interp;
layout(location = 13) in vec4 specular_light_interp;
#endif

#if defined(POINT_SIZE_USED) && defined(POINT_COORD_USED)
layout(location = 14) in vec2 point_coord_interp;
#endif

#if defined(FELLOW_WIND_USED) && !defined(MODE_RENDER_SDF)
layout(location = 15) in float fellow_wind_sheen_interp;
#endif

//defines to keep compatibility with vertex

#ifdef USE_MULTIVIEW
#define projection_matrix scene_data.projection_matrix_view[ViewIndex]
#define inv_projection_matrix scene_data.inv_projection_matrix_view[ViewIndex]
#else
#define projection_matrix scene_data.projection_matrix
#define inv_projection_matrix scene_data.inv_projection_matrix
#endif

#define global_time scene_data_block.data.time

#if defined(ENABLE_SSS) && defined(ENABLE_TRANSMITTANCE)
//both required for transmittance to be enabled
#define LIGHT_TRANSMITTANCE_USED
#endif

#ifdef MATERIAL_UNIFORMS_USED
/* clang-format off */
layout(set = MATERIAL_UNIFORM_SET, binding = 0, std140) uniform MaterialUniforms {
#MATERIAL_UNIFORMS
} material;
/* clang-format on */
#endif

#GLOBALS

#ifdef MODE_RENDER_DEPTH

#ifdef MODE_RENDER_MATERIAL

layout(location = 0) out vec4 albedo_output_buffer;
layout(location = 1) out vec4 normal_output_buffer;
layout(location = 2) out vec4 orm_output_buffer;
layout(location = 3) out vec4 emission_output_buffer;
layout(location = 4) out float depth_output_buffer;

#endif // MODE_RENDER_MATERIAL

#ifdef MODE_RENDER_NORMAL_ROUGHNESS
layout(location = 0) out vec4 normal_roughness_output_buffer;

#ifdef MODE_RENDER_VOXEL_GI
layout(location = 1) out uvec2 voxel_gi_buffer;
#endif

#endif //MODE_RENDER_NORMAL
#else // RENDER DEPTH

#ifdef MODE_SEPARATE_SPECULAR

layout(location = 0) out vec4 diffuse_buffer; //diffuse (rgb) and roughness
layout(location = 1) out vec4 specular_buffer; //specular and SSS (subsurface scatter)
#else

layout(location = 0) out vec4 frag_color;
#endif // MODE_SEPARATE_SPECULAR

#endif // RENDER DEPTH

#ifdef MOTION_VECTORS
layout(location = 2) out vec2 motion_vector;
#endif

#include "../scene_forward_aa_inc.glsl"

#if !defined(MODE_RENDER_DEPTH) && !defined(MODE_UNSHADED)

// Default to SPECULAR_SCHLICK_GGX.
#if !defined(SPECULAR_DISABLED) && !defined(SPECULAR_SCHLICK_GGX) && !defined(SPECULAR_TOON)
#define SPECULAR_SCHLICK_GGX
#endif

#include "../scene_forward_lights_inc.glsl"

// FellowEngine light tile culling (rendering/fellow/lights/tile_cull, decision 0049): in the opaque colour pass, the
// light loops visit only the lights set in the fragment's 8 x 8 tile's masks: the others fail the range test of every
// fragment of the tile. Only for materials whose out-of-range lights are skipped at that test (the zero attenuation
// skip), that shade the position they were depth tested at (no LIGHT_VERTEX, no depth writes), in a single view. The
// renderer sets the specialization bit only with the zero attenuation skip on (its bit and its scene flag).
#if defined(FELLOW_HAS_LIGHT_TILE_CULL) && defined(FELLOW_BACKFACING_LIGHT_SKIP_OK) && !defined(LIGHT_VERTEX_USED) && !defined(FELLOW_WRITES_DEPTH) && !defined(USE_MULTIVIEW) && !defined(USE_VERTEX_LIGHTING)
#define FELLOW_TILE_CULL_OK
uint fellow_tile_mask(uint p_type, uint p_word) {
	uvec2 tile = uvec2(gl_FragCoord.xy) >> 3;
	uint words = implementation_data.max_cluster_element_count_div_32;
	return fellow_light_tile_masks.data[((tile.y * implementation_data.fellow_tile_cull_width + tile.x) * 2u + p_type) * words + p_word];
}
#endif

#ifndef SHADOWS_DISABLED
// FellowEngine (rendering/fellow/shadows/pcss_blend_band): PCSS lookup of cascade c of directional light i, the same
// math as the stock per-cascade blocks in fragment_shader().
float fellow_directional_soft_cascade(uint i, uint c, vec3 vertex, vec3 base_normal_bias, float taa_frame_count) {
	// Per-cascade values are selected with branches, not dynamic vector indexing (which the SPIR-V specializer rejects).
	mat4 shadow_matrix;
	vec2 uv_scale;
	float range_begin;
	float bias;
	float normal_bias_scale;
	if (c == 0u) {
		shadow_matrix = directional_lights.data[i].shadow_matrix1;
		uv_scale = directional_lights.data[i].uv_scale1;
		range_begin = directional_lights.data[i].shadow_range_begin.x;
		bias = directional_lights.data[i].shadow_bias.x;
		normal_bias_scale = directional_lights.data[i].shadow_normal_bias.x;
	} else if (c == 1u) {
		shadow_matrix = directional_lights.data[i].shadow_matrix2;
		uv_scale = directional_lights.data[i].uv_scale2;
		range_begin = directional_lights.data[i].shadow_range_begin.y;
		bias = directional_lights.data[i].shadow_bias.y;
		normal_bias_scale = directional_lights.data[i].shadow_normal_bias.y;
	} else if (c == 2u) {
		shadow_matrix = directional_lights.data[i].shadow_matrix3;
		uv_scale = directional_lights.data[i].uv_scale3;
		range_begin = directional_lights.data[i].shadow_range_begin.z;
		bias = directional_lights.data[i].shadow_bias.z;
		normal_bias_scale = directional_lights.data[i].shadow_normal_bias.z;
	} else {
		shadow_matrix = directional_lights.data[i].shadow_matrix4;
		uv_scale = directional_lights.data[i].uv_scale4;
		range_begin = directional_lights.data[i].shadow_range_begin.w;
		bias = directional_lights.data[i].shadow_bias.w;
		normal_bias_scale = directional_lights.data[i].shadow_normal_bias.w;
	}

	vec3 light_dir = directional_lights.data[i].direction;
	vec4 v = vec4(vertex, 1.0);
	v.xyz += light_dir * bias;
	vec3 normal_bias = base_normal_bias * normal_bias_scale;
	normal_bias -= light_dir * dot(light_dir, normal_bias);
	v.xyz += normal_bias;

	vec4 pssm_coord = shadow_matrix * v;
	pssm_coord /= pssm_coord.w;
	float range_pos = dot(directional_lights.data[i].direction, v.xyz);
	float test_radius = (range_pos - range_begin) * directional_lights.data[i].softshadow_angle;
	vec2 tex_scale = uv_scale * test_radius;
	return float(sample_directional_soft_shadow(directional_shadow_atlas, pssm_coord.xyz, tex_scale * directional_lights.data[i].soft_shadow_scale, taa_frame_count, c));
}
#endif // !SHADOWS_DISABLED

#include "../scene_forward_gi_inc.glsl"

#endif //!defined(MODE_RENDER_DEPTH) && !defined(MODE_UNSHADED)

#ifndef MODE_RENDER_DEPTH

vec4 volumetric_fog_process(vec2 screen_uv, float z) {
	vec3 fog_pos = vec3(screen_uv, z * implementation_data.volumetric_fog_inv_length);
	if (fog_pos.z < 0.0) {
		return vec4(0.0, 0.0, 0.0, 1.0);
	} else if (fog_pos.z < 1.0) {
		fog_pos.z = pow(fog_pos.z, implementation_data.volumetric_fog_detail_spread);
	}

	return texture(sampler3D(volumetric_fog_texture, SAMPLER_LINEAR_CLAMP), fog_pos);
}

vec4 fog_process(vec3 vertex) {
	vec3 fog_color = scene_data_block.data.fog_light_color;

	if (scene_data_block.data.fog_aerial_perspective > 0.0) {
		vec3 sky_fog_color = vec3(0.0);
		vec3 cube_view = scene_data_block.data.radiance_inverse_xform * vertex;
		// mip_level always reads from the second mipmap and higher so the fog is always slightly blurred
		float mip_level = mix(1.0 / MAX_ROUGHNESS_LOD, 1.0, 1.0 - (abs(vertex.z) - scene_data_block.data.z_near) / (scene_data_block.data.z_far - scene_data_block.data.z_near));
#ifdef USE_RADIANCE_OCTMAP_ARRAY
		float roughness_lod, blend;
		blend = modf(mip_level * MAX_ROUGHNESS_LOD, roughness_lod);
		float cube_lod = vec3_to_oct_lod(dFdx(cube_view), dFdy(cube_view), scene_data_block.data.radiance_pixel_size);
		vec2 cube_uv = vec3_to_oct_with_border(cube_view, vec2(scene_data_block.data.radiance_border_size, 1.0 - scene_data_block.data.radiance_border_size * 2.0));
		vec3 sky_sample_a = textureLod(sampler2DArray(radiance_octmap, DEFAULT_SAMPLER_LINEAR_WITH_MIPMAPS_CLAMP), vec3(cube_uv, roughness_lod), cube_lod).rgb;
		vec3 sky_sample_b = textureLod(sampler2DArray(radiance_octmap, DEFAULT_SAMPLER_LINEAR_WITH_MIPMAPS_CLAMP), vec3(cube_uv, roughness_lod + 1), cube_lod).rgb;
		sky_fog_color = mix(sky_sample_a, sky_sample_b, blend);
#else
		float roughness_lod = mip_level * MAX_ROUGHNESS_LOD;
		vec2 cube_uv = vec3_to_oct_with_border(cube_view, vec2(scene_data_block.data.radiance_border_size, 1.0 - scene_data_block.data.radiance_border_size * 2.0));
		sky_fog_color = textureLod(sampler2D(radiance_octmap, DEFAULT_SAMPLER_LINEAR_WITH_MIPMAPS_CLAMP), cube_uv, roughness_lod).rgb;
#endif //USE_RADIANCE_OCTMAP_ARRAY
		fog_color = mix(fog_color, sky_fog_color, scene_data_block.data.fog_aerial_perspective);
	}

	if (scene_data_block.data.fog_sun_scatter > 0.001) {
		vec4 sun_scatter = vec4(0.0);
		float sun_total = 0.0;
		vec3 view = normalize(vertex);

		for (uint i = 0; i < scene_data_block.data.directional_light_count; i++) {
			vec3 light_color = directional_lights.data[i].color * directional_lights.data[i].energy;
			float light_amount = pow(max(dot(view, directional_lights.data[i].direction), 0.0), 8.0);
			fog_color += light_color * light_amount * scene_data_block.data.fog_sun_scatter;
		}
	}

	float fog_amount = 0.0;

	if (sc_use_depth_fog()) {
		float fog_z = smoothstep(scene_data_block.data.fog_depth_begin, scene_data_block.data.fog_depth_end, length(vertex));
		float fog_quad_amount = pow(fog_z, scene_data_block.data.fog_depth_curve) * scene_data_block.data.fog_density;
		fog_amount = fog_quad_amount;
	} else {
		fog_amount = 1 - exp(min(0.0, -length(vertex) * scene_data_block.data.fog_density));
	}

	if (abs(scene_data_block.data.fog_height_density) >= 0.0001) {
		mat4 inv_view_matrix = transpose(mat4(scene_data_block.data.inv_view_matrix[0],
				scene_data_block.data.inv_view_matrix[1],
				scene_data_block.data.inv_view_matrix[2],
				vec4(0.0, 0.0, 0.0, 1.0)));

		float y = (inv_view_matrix * vec4(vertex, 1.0)).y;

		float y_dist = y - scene_data_block.data.fog_height;

		float vfog_amount = 1.0 - exp(min(0.0, y_dist * scene_data_block.data.fog_height_density));

		fog_amount = max(vfog_amount, fog_amount);
	}

	return vec4(fog_color, fog_amount);
}

void cluster_get_item_range(uint p_offset, out uint item_min, out uint item_max, out uint item_from, out uint item_to) {
	uint item_min_max = cluster_buffer.data[p_offset];
	item_min = item_min_max & 0xFFFFu;
	item_max = item_min_max >> 16;

	item_from = item_min >> 5;
	item_to = (item_max == 0) ? 0 : ((item_max - 1) >> 5) + 1; //side effect of how it is stored, as item_max 0 means no elements
}

uint cluster_get_range_clip_mask(uint i, uint z_min, uint z_max) {
	int local_min = clamp(int(z_min) - int(i) * 32, 0, 31);
	int mask_width = min(int(z_max) - int(z_min), 32 - local_min);
	return bitfieldInsert(uint(0), uint(0xFFFFFFFF), local_min, mask_width);
}

#endif //!MODE_RENDER DEPTH

#if defined(MODE_RENDER_NORMAL_ROUGHNESS) || defined(MODE_RENDER_MATERIAL)
// https://advances.realtimerendering.com/s2010/Kaplanyan-CryEngine3(SIGGRAPH%202010%20Advanced%20RealTime%20Rendering%20Course).pdf
vec3 encode24(vec3 v) {
	// Unsigned normal (handles most symmetry)
	vec3 vNormalUns = abs(v);
	// Get the major axis for our collapsed cubemap lookup
	float maxNAbs = max(vNormalUns.z, max(vNormalUns.x, vNormalUns.y));
	// Get the collapsed cubemap texture coordinates
	vec2 vTexCoord = vNormalUns.z < maxNAbs ? (vNormalUns.y < maxNAbs ? vNormalUns.yz : vNormalUns.xz) : vNormalUns.xy;
	vTexCoord /= maxNAbs;
	vTexCoord = vTexCoord.x < vTexCoord.y ? vTexCoord.yx : vTexCoord.xy;
	// Stretch:
	vTexCoord.y /= vTexCoord.x;
	float fFittingScale = texture(sampler2D(best_fit_normal_texture, SAMPLER_NEAREST_CLAMP), vTexCoord).r;
	// Make vector touch unit cube
	vec3 result = v / maxNAbs;
	// scale the normal to get the best fit
	result *= fFittingScale;
	return result;
}
#endif // MODE_RENDER_NORMAL_ROUGHNESS

#if defined(FELLOW_EXP_BIT3) && !defined(MODE_RENDER_DEPTH) && !defined(MODE_UNSHADED)
// Diagnostics: shares of the shadow filter calls (positional with bit 3 alone, directional with bit 4 as well): red, the
// max-mip early-outs; green, the full path with every tap the same; blue, the full path with a partial result.
vec3 fellow_dbg_color() {
#if defined(FELLOW_EXP_BIT18)
	// Diagnostics (bits 8 + 262144): red, the omni/spot loop's iterations / 128; green, lights in the pixel's own mask / 128;
	// blue, lights shaded / 32. (Lights within range: fellow_dbg_light_range, unused here.)
#ifdef FELLOW_EXP_BIT5
	// With bit 32 as well: green, lights within range / 128.
	return vec3(float(fellow_dbg_light_iter) / 128.0, float(fellow_dbg_light_range) / 128.0, float(fellow_dbg_light_brdf) / 32.0);
#else
	return vec3(float(fellow_dbg_light_iter) / 128.0, float(fellow_dbg_light_own) / 128.0, float(fellow_dbg_light_brdf) / 32.0);
#endif
#elif defined(FELLOW_EXP_BIT4)
	// Directional soft shadows (PCSS): red, decided before the blocker search; green, decided by the search alone (no
	// blocker) or after it without the filter loop; blue, the filter loop ran.
	float calls = float(max(fellow_dbg_dir_call, 1u));
	return vec3(float(fellow_dbg_dir_call - fellow_dbg_dir_full), float(fellow_dbg_dir_full - fellow_dbg_dir_pcf), float(fellow_dbg_dir_pcf)) / calls;
#else
	float calls = float(max(fellow_dbg_pos_call, 1u));
	return vec3(float(fellow_dbg_pos_call - fellow_dbg_pos_full), float(fellow_dbg_pos_uniform), float(fellow_dbg_pos_full - fellow_dbg_pos_uniform)) / calls;
#endif
}
#endif

#if defined(FELLOW_EXP_BIT15) && defined(FELLOW_BACKFACING_LIGHT_SKIP_OK) && !defined(MODE_RENDER_DEPTH) && !defined(MODE_UNSHADED) && !defined(USE_VERTEX_LIGHTING)
#define FELLOW_LIGHT_PREFILTER
// Experiment (debug/scene_shader_experiments 32768): before a cluster word's lights are processed, the ones that
// cannot add anything to this pixel are dropped from its mask, four lights at a time with their loads together: the
// tests of the light loop (layer mask, static bake with a lightmap) and the first zero-attenuation tests of
// light_process_omni() / light_process_spot() (out of range; spot: outside the cone), the same expressions. The loop
// then processes the remaining lights in the same order.
#define FELLOW_LIGHT_BATCH 4u
uint fellow_light_next(inout uint r_todo, uint p_first) {
	if (r_todo == 0u) {
		return p_first;
	}
	uint b = findMSB(r_todo);
	r_todo &= ~(1u << b);
	return b;
}

uint fellow_omni_reach(uint p_word, uint p_mask, vec3 p_vertex, uint p_layer_mask, bool p_lightmap) {
	const bool skip_zero = sc_fellow_zero_attenuation_skip() && bool(scene_data_block.data.flags & SCENE_DATA_FLAGS_FELLOW_LIGHT_ZERO_ATTENUATION_SKIP);
	uint todo = subgroupBroadcastFirst(subgroupOr(p_mask));
	uint reach = 0u;
	while (todo != 0u) {
		uint b[FELLOW_LIGHT_BATCH];
		b[0] = findMSB(todo);
		todo &= ~(1u << b[0]);
		[[unroll]] for (uint k = 1u; k < FELLOW_LIGHT_BATCH; k++) {
			b[k] = fellow_light_next(todo, b[0]);
		}
		vec4 pr[FELLOW_LIGHT_BATCH];
		uint lm[FELLOW_LIGHT_BATCH];
		uint bm[FELLOW_LIGHT_BATCH];
		[[unroll]] for (uint k = 0u; k < FELLOW_LIGHT_BATCH; k++) {
			uint li = 32u * p_word + b[k];
			pr[k] = vec4(omni_lights.data[li].position, omni_lights.data[li].inv_radius);
			lm[k] = omni_lights.data[li].mask;
			bm[k] = omni_lights.data[li].bake_mode;
		}
		[[unroll]] for (uint k = 0u; k < FELLOW_LIGHT_BATCH; k++) {
			bool ok = ((1u << b[k]) & p_mask) != 0u && bool(lm[k] & p_layer_mask) && !(bm[k] == LIGHT_BAKE_STATIC && p_lightmap);
			if (ok && skip_zero) {
				vec3 light_rel_vec = pr[k].xyz - p_vertex;
				float light_length = length(light_rel_vec);
				ok = !(light_length * pr[k].w >= 1.0);
			}
			if (ok) {
				reach |= 1u << b[k];
			}
		}
	}
	return reach;
}

uint fellow_spot_reach(uint p_word, uint p_mask, vec3 p_vertex, uint p_layer_mask, bool p_lightmap) {
	const bool skip_zero = sc_fellow_zero_attenuation_skip() && bool(scene_data_block.data.flags & SCENE_DATA_FLAGS_FELLOW_LIGHT_ZERO_ATTENUATION_SKIP);
	uint todo = subgroupBroadcastFirst(subgroupOr(p_mask));
	uint reach = 0u;
	while (todo != 0u) {
		uint b[FELLOW_LIGHT_BATCH];
		b[0] = findMSB(todo);
		todo &= ~(1u << b[0]);
		[[unroll]] for (uint k = 1u; k < FELLOW_LIGHT_BATCH; k++) {
			b[k] = fellow_light_next(todo, b[0]);
		}
		vec4 pr[FELLOW_LIGHT_BATCH];
		vec4 dc[FELLOW_LIGHT_BATCH];
		uint lm[FELLOW_LIGHT_BATCH];
		uint bm[FELLOW_LIGHT_BATCH];
		[[unroll]] for (uint k = 0u; k < FELLOW_LIGHT_BATCH; k++) {
			uint li = 32u * p_word + b[k];
			pr[k] = vec4(spot_lights.data[li].position, spot_lights.data[li].inv_radius);
			dc[k] = vec4(spot_lights.data[li].direction, spot_lights.data[li].cone_angle);
			lm[k] = spot_lights.data[li].mask;
			bm[k] = spot_lights.data[li].bake_mode;
		}
		[[unroll]] for (uint k = 0u; k < FELLOW_LIGHT_BATCH; k++) {
			bool ok = ((1u << b[k]) & p_mask) != 0u && bool(lm[k] & p_layer_mask) && !(bm[k] == LIGHT_BAKE_STATIC && p_lightmap);
			if (ok && skip_zero) {
				vec3 light_rel_vec = pr[k].xyz - p_vertex;
				float light_length = length(light_rel_vec);
				hvec3 light_rel_vec_norm = hvec3(light_rel_vec / light_length);
				ok = !(light_length * pr[k].w >= 1.0 || dot(-vec3(light_rel_vec_norm), dc[k].xyz) <= dc[k].w);
			}
			if (ok) {
				reach |= 1u << b[k];
			}
		}
	}
	return reach;
}
#endif

#if !defined(MODE_RENDER_SDF) && (!defined(MODE_RENDER_DEPTH) || defined(MODE_RENDER_NORMAL_ROUGHNESS))
// The rain's water on a surface (decision 0124).
void fellow_wet_surface(in SceneData p_scene, in FellowWeatherData w, float wetness, float exposed, vec3 wp, vec3 gn, float wet_lod, inout vec3 r_albedo, inout float r_roughness, inout float r_metallic, inout vec3 r_normal) {
	mat4 inv_view = transpose(mat4(p_scene.inv_view_matrix[0], p_scene.inv_view_matrix[1], p_scene.inv_view_matrix[2], vec4(0.0, 0.0, 0.0, 1.0)));
	// The surface's own water: undersides stay nearly dry; porous (rough, non-metal) materials darken as water fills
	// their pores; a film makes every wet surface glossier.
	// Drying in patches (decision 0138): between wet and dry the surface is wet in patches — the puddles' noise taken as
	// the ground's lows (where the puddles stand) and highs, with a finer one at their edges and a broad one: the highs
	// dry first and wet last, the lows last and first.
	const float CELLS = 16.0;
	const mat2 ROT1 = mat2(0.8, 0.6, -0.6, 0.8);
	const mat2 ROT2 = mat2(-0.28, 0.96, -0.96, -0.28);
	float patches = (w.frost.z > 0.0 && wetness < 1.0) ? w.frost.z : 0.0;
	float wet_here = wetness;
	if (patches > 0.0) {
		vec2 puv = abs(gn.y) > 0.7 ? wp.xz : (abs(gn.x) > abs(gn.z) ? wp.zy : wp.xy);
		float lows = textureLod(sampler2D(fellow_rain_ripples, SAMPLER_LINEAR_WITH_MIPMAPS_REPEAT), puv * (0.23 / CELLS), wet_lod + log2(0.23 / CELLS)).a * 0.6;
		lows += textureLod(sampler2D(fellow_rain_ripples, SAMPLER_LINEAR_WITH_MIPMAPS_REPEAT), ROT1 * puv * (0.91 / CELLS) + vec2(0.31, 0.17), wet_lod + log2(0.91 / CELLS)).a * 0.3;
		lows += textureLod(sampler2D(fellow_rain_ripples, SAMPLER_LINEAR_WITH_MIPMAPS_REPEAT), ROT2 * puv * (3.7 / CELLS) + vec2(0.53, 0.71), wet_lod + log2(3.7 / CELLS)).a * 0.1;
		float fine = textureLod(sampler2D(fellow_rain_ripples, SAMPLER_LINEAR_WITH_MIPMAPS_REPEAT), ROT1 * puv * (11.0 / CELLS) + vec2(0.13, 0.89), wet_lod + log2(11.0 / CELLS)).a;
		// And a broad one (cells of 20 m, turned): the noise's 70 m repeat is not seen from above.
		float broad = textureLod(sampler2D(fellow_rain_ripples, SAMPLER_LINEAR_WITH_MIPMAPS_REPEAT), ROT2 * puv * (0.05 / CELLS) + vec2(0.47, 0.19), wet_lod + log2(0.05 / CELLS)).a;
		float high = 1.0 - (0.6 * lows + 0.25 * broad + 0.15 * fine);
		wet_here = mix(wetness, smoothstep(high - 0.08, high + 0.08, wetness * 1.4 - 0.2), patches);
	}
	float facing = mix(0.1, 1.0, smoothstep(-0.3, 0.3, gn.y));
	if (w.slant.z > 0.0) {
		// Wind-driven rain (decision 0139): a wall facing the wind is as wet as the ground.
		vec2 back = -w.slant.xy / max(length(w.slant.xy), 1e-4);
		facing = max(facing, smoothstep(0.0, 0.6, dot(gn.xz, back)) * w.slant.z);
	}
	float wet = wet_here * exposed * facing;
	float rough0 = r_roughness;
	float porous = (1.0 - r_metallic) * smoothstep(0.2, 0.6, rough0);
	r_albedo *= mix(1.0, 0.5, wet * porous);
	r_roughness = mix(r_roughness, min(r_roughness, 0.2), wet * 0.9);

	// Puddles on flat, rough, non-metal ground (noise thresholded by `puddles`, appearing once the surface is wet).
	// Walls and slopes have neither puddles nor the film's ripples: done.
	float flatness = smoothstep(0.95, 0.99, gn.y);
	if (flatness <= 0.0) {
		return;
	}
	// The noise: the ripple texture's alpha, a tiling value noise of 16 x 16 cells, at three scales (cells of 4.3 m,
	// 1.1 m and 27 cm; the two finer turned: no visible repeat).
	// Only where a puddle can be (flat, exposed, wet enough, rough and not metallic). Drying in patches (0138), the
	// puddles shrink to their deepest instead of fading: the threshold rises as the wetness falls.
	float puddle_amount = w.params.y;
	float puddle_wet = smoothstep(0.4, 0.9, wetness);
	if (patches > 0.0) {
		puddle_amount *= mix(1.0, smoothstep(0.15, 1.0, wetness), patches);
		puddle_wet = mix(puddle_wet, smoothstep(0.1, 0.3, wetness), patches);
	}
	float puddle = flatness * exposed * puddle_wet * (1.0 - r_metallic) * smoothstep(0.25, 0.45, rough0);
	if (puddle > 0.0 && puddle_amount > 0.0) {
		float n = textureLod(sampler2D(fellow_rain_ripples, SAMPLER_LINEAR_WITH_MIPMAPS_REPEAT), wp.xz * (0.23 / CELLS), wet_lod + log2(0.23 / CELLS)).a * 0.6;
		n += textureLod(sampler2D(fellow_rain_ripples, SAMPLER_LINEAR_WITH_MIPMAPS_REPEAT), ROT1 * wp.xz * (0.91 / CELLS) + vec2(0.31, 0.17), wet_lod + log2(0.91 / CELLS)).a * 0.3;
		n += textureLod(sampler2D(fellow_rain_ripples, SAMPLER_LINEAR_WITH_MIPMAPS_REPEAT), ROT2 * wp.xz * (3.7 / CELLS) + vec2(0.53, 0.71), wet_lod + log2(3.7 / CELLS)).a * 0.1;
		puddle *= clamp((n - 1.0 + puddle_amount) * 6.0, 0.0, 1.0);
	} else {
		puddle = 0.0;
	}

	// The rain's ripples (two lookups of the tiling texture, offset: no visible repeat), where it falls.
	vec2 slope = vec2(0.0);
	if (w.params.z > 0.0) {
		vec2 ruv = wp.xz * w.ripples.x;
		float rlod = wet_lod + log2(w.ripples.x);
		vec2 ra = textureLod(sampler2D(fellow_rain_ripples, SAMPLER_LINEAR_WITH_MIPMAPS_REPEAT), ruv, rlod).rg;
		vec2 rb = textureLod(sampler2D(fellow_rain_ripples, SAMPLER_LINEAR_WITH_MIPMAPS_REPEAT), ruv * 0.77 + vec2(0.37, 0.61), rlod + log2(0.77)).rg;
		slope = (ra + rb) * (w.ripples.y * w.params.z * exposed);
	}
	// Ice (decision 0136): frozen water does not ripple.
	slope *= 1.0 - w.frost.x;

	vec3 nw = normalize(mat3(inv_view) * r_normal);
	nw = normalize(mix(nw, normalize(vec3(-slope.x, 1.0, -slope.y)), puddle)); // standing water: flat, rippled
	nw = normalize(nw + vec3(-slope.x, 0.0, -slope.y) * (0.35 * wet * flatness * (1.0 - puddle))); // a film
	mat4 view = transpose(mat4(p_scene.view_matrix[0], p_scene.view_matrix[1], p_scene.view_matrix[2], vec4(0.0, 0.0, 0.0, 1.0)));
	r_normal = normalize(mat3(view) * nw);
	r_albedo *= mix(1.0, 0.8, puddle);
	r_roughness = mix(r_roughness, 0.03, puddle);
	r_metallic = mix(r_metallic, 0.0, puddle);
	if (w.frost.x > 0.0 && puddle > 0.0) {
		// The puddles frozen (decision 0136): dark clear ice, glossy still, a white rough rim where the ice is thin.
		float ice = puddle * w.frost.x;
		float rim = smoothstep(0.1, 0.35, puddle) * (1.0 - smoothstep(0.35, 0.7, puddle));
		r_albedo = mix(r_albedo, vec3(0.75, 0.8, 0.85), ice * (0.05 + 0.5 * rim));
		r_roughness = mix(r_roughness, 0.05 + 0.35 * rim, ice);
	}
}

// Hoarfrost (decision 0136): on what faces the sky (it cools there on a clear cold night), less on walls, none
// underneath; feathery (the puddles' noise, fine), white-grey crystals, rough, glinting.
void fellow_frost(in SceneData p_scene, in FellowWeatherData w, float frost, float exposed, vec3 wp, vec3 gn, float wet_lod, inout vec3 r_albedo, inout float r_roughness, inout float r_metallic, inout vec3 r_normal) {
	const float CELLS = 16.0;
	const mat2 ROT1 = mat2(0.8, 0.6, -0.6, 0.8);
	vec2 puv = abs(gn.y) > 0.7 ? wp.xz : (abs(gn.x) > abs(gn.z) ? wp.zy : wp.xy);
	float n = textureLod(sampler2D(fellow_rain_ripples, SAMPLER_LINEAR_WITH_MIPMAPS_REPEAT), puv * (5.0 / CELLS) + vec2(0.17, 0.59), wet_lod + log2(5.0 / CELLS)).a * 0.35;
	n += textureLod(sampler2D(fellow_rain_ripples, SAMPLER_LINEAR_WITH_MIPMAPS_REPEAT), ROT1 * puv * (19.0 / CELLS) + vec2(0.83, 0.41), wet_lod + log2(19.0 / CELLS)).a * 0.65;
	float fine = textureLod(sampler2D(fellow_rain_ripples, SAMPLER_LINEAR_WITH_MIPMAPS_REPEAT), ROT1 * puv * (47.0 / CELLS) + vec2(0.29, 0.71), wet_lod + log2(47.0 / CELLS)).a;
	float face = smoothstep(-0.2, 0.6, gn.y);
	// It grows in fine grains (5 cm), sooner in places (20 cm), and stays a thin veil, thicker here and there (the
	// finest noise): the surface shows through it.
	float grow = smoothstep(0.0, 0.3, n - 1.0 + frost * 1.3);
	float f = exposed * face * grow * min(frost * 2.0, 1.0);
	if (f <= 0.0) {
		return;
	}
	float veil = f * mix(0.2, 0.5, fine);
	r_albedo = mix(r_albedo, vec3(0.76, 0.8, 0.85), veil);
	r_roughness = mix(r_roughness, max(r_roughness, 0.75), f); // matte crystals (a gloss would mirror the sky white)
	r_metallic = mix(r_metallic, 0.0, f);
	// Glints: a crystal facing the light, here and there (cells of 5 mm).
	ivec3 cell = ivec3(floor(wp * 200.0));
	uint h = uint(cell.x) * 73856093u ^ uint(cell.y) * 19349663u ^ uint(cell.z) * 83492791u;
	h ^= h >> 16u;
	h *= 0x7feb352du;
	h ^= h >> 15u;
	h *= 0x846ca68bu;
	h ^= h >> 16u;
	float px = exp2(wet_lod) * (1.0 / 256.0); // metres per pixel
	float k = f * smoothstep(0.01, 0.004, px) * step(0.93, float(h & 0xffffu) / 65535.0);
	if (k > 0.0) {
		mat4 inv_view = transpose(mat4(p_scene.inv_view_matrix[0], p_scene.inv_view_matrix[1], p_scene.inv_view_matrix[2], vec4(0.0, 0.0, 0.0, 1.0)));
		mat4 view = transpose(mat4(p_scene.view_matrix[0], p_scene.view_matrix[1], p_scene.view_matrix[2], vec4(0.0, 0.0, 0.0, 1.0)));
		vec3 nw = normalize(mat3(inv_view) * r_normal);
		vec3 t = vec3(float((h >> 16u) & 0xffu), float((h >> 24u) & 0xffu), float(h & 0xffu)) / 127.5 - 1.0;
		r_normal = normalize(mat3(view) * normalize(mix(nw, normalize(nw + t * 0.6), k)));
		r_roughness = mix(r_roughness, 0.1, k);
	}
}

// Snow on a surface (decision 0128): it lies on what faces up (the geometry, and the material's own bumps a little),
// patchy while thin (the puddles' noise), and covers everything facing up once deep. Fresh snow: white with a little
// blue, rough, its surface smooth (the snow fills the material's relief), glinting where a crystal faces the sun.
void fellow_snow_cover(in SceneData p_scene, in FellowWeatherData w, float snow, float exposed, vec3 wp, vec3 gn, float wet_lod, inout vec3 r_albedo, inout float r_roughness, inout float r_metallic, inout vec3 r_normal) {
	mat4 inv_view = transpose(mat4(p_scene.inv_view_matrix[0], p_scene.inv_view_matrix[1], p_scene.inv_view_matrix[2], vec4(0.0, 0.0, 0.0, 1.0)));
	vec3 nw = normalize(mat3(inv_view) * r_normal);
	float up = mix(gn.y, nw.y, 0.35);
	if (up <= 0.5) {
		return;
	}
	// The noise: the puddles' (the ripple texture's alpha), cells of 2.7 m, 43 cm and 7 cm (the finest gives thin
	// snow's patches their ragged edges).
	const float CELLS = 16.0;
	const mat2 ROT1 = mat2(0.8, 0.6, -0.6, 0.8);
	const mat2 ROT2 = mat2(-0.28, 0.96, -0.96, -0.28);
	const float FINE = 2.3 / CELLS;
	vec2 fuv = ROT1 * wp.xz * FINE + vec2(0.71, 0.29);
	float flod = wet_lod + log2(FINE);
	float nc = textureLod(sampler2D(fellow_rain_ripples, SAMPLER_LINEAR_WITH_MIPMAPS_REPEAT), wp.xz * (0.37 / CELLS) + vec2(0.11, 0.43), wet_lod + log2(0.37 / CELLS)).a;
	float nf = textureLod(sampler2D(fellow_rain_ripples, SAMPLER_LINEAR_WITH_MIPMAPS_REPEAT), fuv, flod).a;
	float nx = textureLod(sampler2D(fellow_rain_ripples, SAMPLER_LINEAR_WITH_MIPMAPS_REPEAT), ROT2 * wp.xz * (14.0 / CELLS) + vec2(0.37, 0.83), wet_lod + log2(14.0 / CELLS)).a;
	float n = nc * 0.6 + nf * 0.28 + nx * 0.12;
	// How deep the snow is here (0: none, 1: it covers): the cover, none on slopes steeper than 60 degrees, thinner
	// where the noise is low.
	float depth = snow * 1.5 * smoothstep(0.5, 0.85, up) - (1.0 - n) * 0.75;
	float cover = smoothstep(0.0, 0.15, depth) * exposed;
	if (cover <= 0.0) {
		return;
	}
	// Tracks (decision 0130): how deep they go here (m; negative: a rim of pushed-up snow), from a world-anchored
	// wrapping texture around the camera. Where they reach the snow's depth the ground shows; their walls turn the
	// normal; the packed snow at their bottom is greyer and smoother.
	float track = 0.0;
	vec2 track_slope = vec2(0.0);
	if (w.snow.w > 0.5 && all(lessThan(abs(wp.xz - w.tracks.xy), vec2(w.tracks.w)))) {
		vec2 tuv = wp.xz * w.tracks.z;
		vec2 tsize = vec2(textureSize(sampler2D(fellow_snow_tracks, SAMPLER_LINEAR_REPEAT), 0));
		track = textureLod(sampler2D(fellow_snow_tracks, SAMPLER_LINEAR_REPEAT), tuv, 0.0).r;
		if (track != 0.0) {
			float tx = textureLod(sampler2D(fellow_snow_tracks, SAMPLER_LINEAR_REPEAT), tuv + vec2(1.0 / tsize.x, 0.0), 0.0).r;
			float tz = textureLod(sampler2D(fellow_snow_tracks, SAMPLER_LINEAR_REPEAT), tuv + vec2(0.0, 1.0 / tsize.y), 0.0).r;
			track_slope = vec2(tx - track, tz - track) * (w.tracks.z * tsize.x); // per metre (one texel apart)
			float sl = length(track_slope);
			track_slope *= sl > 3.0 ? 3.0 / sl : 1.0;
		}
	}
	float tdepth = clamp(track / w.snow.z, -1.0, 1.0);
	cover *= 1.0 - smoothstep(0.85, 1.0, tdepth); // the ground at the bottom of the deepest
	if (cover <= 0.0) {
		return;
	}
	float packed = max(tdepth, 0.0);
	// Its surface: smooth (it fills the material's relief), with gentle undulations of a centimetre or two (the fine
	// noise's slopes, 8 cm differences; flat where its mip levels blur it).
	const float D = 0.08;
	float nfx = textureLod(sampler2D(fellow_rain_ripples, SAMPLER_LINEAR_WITH_MIPMAPS_REPEAT), fuv + ROT1 * vec2(D, 0.0) * FINE, flod).a;
	float nfz = textureLod(sampler2D(fellow_rain_ripples, SAMPLER_LINEAR_WITH_MIPMAPS_REPEAT), fuv + ROT1 * vec2(0.0, D) * FINE, flod).a;
	vec2 slope = vec2(nfx - nf, nfz - nf) * (0.015 / D);
	// The tracks' walls: the snow's surface is -track, so its normal leans along +grad(track).
	vec3 sn = normalize(normalize(mix(nw, gn, 0.8)) + vec3(track_slope.x - slope.x, 0.0, track_slope.y - slope.y));
	float rough = mix(0.62, 0.45, packed);
	// Glints: a crystal in a cell of 8 mm, one cell in 20, turned at random; faded where a cell is under a pixel.
	if (w.snow.y > 0.0) {
		ivec3 cell = ivec3(floor(wp * 125.0));
		uint h = uint(cell.x) * 73856093u ^ uint(cell.y) * 19349663u ^ uint(cell.z) * 83492791u;
		h ^= h >> 16u;
		h *= 0x7feb352du;
		h ^= h >> 15u;
		h *= 0x846ca68bu;
		h ^= h >> 16u;
		float r = float(h & 0xffffu) / 65535.0;
		float px = exp2(wet_lod) * (1.0 / 256.0); // metres per pixel
		float k = w.snow.y * cover * (1.0 - packed) * smoothstep(0.016, 0.006, px) * step(0.95, r);
		if (k > 0.0) {
			vec3 t = vec3(float((h >> 16u) & 0xffu), float((h >> 24u) & 0xffu), float(h & 0xffu)) / 127.5 - 1.0;
			sn = normalize(mix(sn, normalize(sn + t * 0.5), k));
			rough = mix(rough, 0.12, k);
		}
	}
	mat4 view = transpose(mat4(p_scene.view_matrix[0], p_scene.view_matrix[1], p_scene.view_matrix[2], vec4(0.0, 0.0, 0.0, 1.0)));
	r_normal = normalize(mat3(view) * normalize(mix(nw, sn, cover)));
	r_albedo = mix(r_albedo, vec3(0.86, 0.89, 0.93) * (1.0 - 0.3 * packed), cover);
	r_roughness = mix(r_roughness, rough, cover);
	r_metallic = mix(r_metallic, 0.0, cover);
}

// FellowEngine weather (decision 0124): a material wetted after its own code and its decals, where the rain reaches it;
// covered with snow where the snow falls (decision 0128).
void fellow_wetness_apply(in SceneData p_scene, inout vec3 r_albedo, inout float r_roughness, inout float r_metallic, inout vec3 r_normal, vec3 p_vertex, vec3 p_geometric_normal) {
	FellowWeatherData w = fellow_weather.data;
	float wetness = w.params.x;
	float snow = w.snow.x;
	float frost = w.frost.y;
	if (wetness <= 0.0 && snow <= 0.0 && frost <= 0.0) {
		return;
	}
	mat4 inv_view = transpose(mat4(p_scene.inv_view_matrix[0], p_scene.inv_view_matrix[1], p_scene.inv_view_matrix[2], vec4(0.0, 0.0, 0.0, 1.0)));
	vec3 wp = (inv_view * vec4(p_vertex, 1.0)).xyz;
	vec3 gn = normalize(mat3(inv_view) * p_geometric_normal);
	// The mip level of the ripple texture (256²) at one texel per metre, from the position's derivatives while every
	// pixel of the quad is here (the early returns below are not uniform); each lookup adds log2 of its texels per
	// metre. Explicit levels: lookups with explicit gradients run at a fraction of the rate (RTX: 0.05 ms of the
	// pre-pass for five of them).
	float wet_lod = log2(max(max(length(dFdx(wp.xz)), length(dFdy(wp.xz))), 1e-6) * 256.0);

	// Where the rain reaches: below the scene's top as seen from above it does not. A wall looks just outside itself
	// (the roof above its own footprint would keep it dry); the bilinear lookup and the 25 cm ramp soften the edges.
	float exposed = 1.0;
	// The snow's shelter: the heightfield eroded by 35 cm (the lowest of five points), so that thin things overhead —
	// wires, strings of lights, lamps — leave no bare spots below them; under a roof the snow reaches 35 cm in.
	float exposed_snow = 1.0;
	if (w.occlusion2.z > 0.5) {
		vec3 sp = wp + vec3(gn.x, 0.0, gn.z) * (w.occlusion2.w + 0.1);
		vec2 uv = (sp.xz - w.occlusion.xy) * w.occlusion.zw + 0.5;
		if (all(greaterThan(uv, vec2(0.0))) && all(lessThan(uv, vec2(1.0)))) {
			float h = textureLod(sampler2D(fellow_rain_occlusion, SAMPLER_LINEAR_CLAMP), uv, 0.0).r;
			float top = w.occlusion2.x + (h * 2.0 - 1.0) * w.occlusion2.y;
			exposed = smoothstep(top - w.occlusion2.w * 2.0, top - w.occlusion2.w, wp.y);
			if (snow > 0.0) {
				vec2 r = 0.35 * w.occlusion.zw;
				h = min(h, textureLod(sampler2D(fellow_rain_occlusion, SAMPLER_LINEAR_CLAMP), uv + vec2(r.x, 0.0), 0.0).r);
				h = min(h, textureLod(sampler2D(fellow_rain_occlusion, SAMPLER_LINEAR_CLAMP), uv - vec2(r.x, 0.0), 0.0).r);
				h = min(h, textureLod(sampler2D(fellow_rain_occlusion, SAMPLER_LINEAR_CLAMP), uv + vec2(0.0, r.y), 0.0).r);
				h = min(h, textureLod(sampler2D(fellow_rain_occlusion, SAMPLER_LINEAR_CLAMP), uv - vec2(0.0, r.y), 0.0).r);
				top = w.occlusion2.x + (h * 2.0 - 1.0) * w.occlusion2.y;
				exposed_snow = smoothstep(top - w.occlusion2.w * 2.0, top - w.occlusion2.w, wp.y);
			}
		}
	}
	// Wind-driven rain (decision 0139): the drops fall slanted, drifting w.slant.xy per metre of fall; the rain reaches
	// a point where its slanted path up into the rain is open — the heightfield back along the drift (up to 1 m, as
	// far as the path rises 3 m) below the height the drop had there. Walls facing the wind get it under their eaves,
	// the lee of a building stays drier.
	if (w.occlusion2.z > 0.5 && w.slant.z > 0.0) {
		float s = length(w.slant.xy);
		vec2 back = -w.slant.xy / max(s, 1e-4);
		float d = min(1.0, s * 3.0);
		vec3 sp = wp + vec3(gn.x, 0.0, gn.z) * (w.occlusion2.w + 0.1) + vec3(back.x, 0.0, back.y) * d;
		vec2 uv = (sp.xz - w.occlusion.xy) * w.occlusion.zw + 0.5;
		float exposed_slant = 1.0;
		if (all(greaterThan(uv, vec2(0.0))) && all(lessThan(uv, vec2(1.0)))) {
			float h = textureLod(sampler2D(fellow_rain_occlusion, SAMPLER_LINEAR_CLAMP), uv, 0.0).r;
			float top = w.occlusion2.x + (h * 2.0 - 1.0) * w.occlusion2.y;
			exposed_slant = smoothstep(top - w.occlusion2.w * 2.0, top - w.occlusion2.w, wp.y + d / max(s, 1e-4));
		}
		exposed = mix(exposed, exposed_slant, w.slant.z);
	}
	if (exposed <= 0.0 && (snow <= 0.0 || exposed_snow <= 0.0)) {
		return; // sheltered: dry
	}
	if (wetness > 0.0 && exposed > 0.0) {
		fellow_wet_surface(p_scene, w, wetness, exposed, wp, gn, wet_lod, r_albedo, r_roughness, r_metallic, r_normal);
	}
	if (frost > 0.0 && exposed > 0.0) {
		fellow_frost(p_scene, w, frost, exposed, wp, gn, wet_lod, r_albedo, r_roughness, r_metallic, r_normal);
	}
	if (snow > 0.0 && exposed_snow > 0.0) {
		fellow_snow_cover(p_scene, w, snow, exposed_snow, wp, gn, wet_lod, r_albedo, r_roughness, r_metallic, r_normal);
	}
}

#endif

void fragment_shader(in SceneData scene_data) {
	uint instance_index = instance_index_interp;
	fellow_element_init(instance_index);

#ifdef PREMUL_ALPHA_USED
	float premul_alpha = 1.0;
#endif // PREMUL_ALPHA_USED
	//lay out everything, whatever is unused is optimized away anyway
	vec3 vertex = vertex_interp;
#ifdef USE_MULTIVIEW
	vec3 eye_offset = scene_data.eye_offset[ViewIndex].xyz;
	vec3 view_highp = -normalize(vertex_interp - eye_offset);

	// UV in our combined frustum space is used for certain screen uv processes where it's
	// overkill to render separate left and right eye views
	vec2 combined_uv = (combined_projected.xy / combined_projected.w) * 0.5 + 0.5;
#else
	vec3 eye_offset = vec3(0.0, 0.0, 0.0);
	vec3 view_highp = -normalize(vertex_interp);
#endif
	vec3 albedo_highp = vec3(1.0);
	vec3 backlight = vec3(0.0);
	vec4 transmittance_color = vec4(0.0, 0.0, 0.0, 1.0);
	float transmittance_depth = 0.0;
	float transmittance_boost = 0.0;
	float metallic_highp = 0.0;
	float specular = 0.5;
	vec3 emission = vec3(0.0);
	float roughness_highp = 1.0;
	float rim = 0.0;
	float rim_tint = 0.0;
	float clearcoat = 0.0;
	float clearcoat_roughness = 0.0;
	float anisotropy = 0.0;
	vec2 anisotropy_flow = vec2(1.0, 0.0);
	vec3 energy_compensation = vec3(1.0);
	vec4 fog = vec4(0.0, 0.0, 0.0, 1.0);
#if defined(CUSTOM_RADIANCE_USED)
	vec4 custom_radiance = vec4(0.0);
#endif
#if defined(CUSTOM_IRRADIANCE_USED)
	vec4 custom_irradiance = vec4(0.0);
#endif

	float ao = 1.0;
	float ao_light_affect = 0.0;

	float alpha_highp = float(fellow_flags >> INSTANCE_FLAGS_FADE_SHIFT) / float(255.0);

#ifdef TANGENT_USED
	vec3 binormal = binormal_interp;
	vec3 tangent = tangent_interp;
#else
	vec3 binormal = vec3(0.0);
	vec3 tangent = vec3(0.0);
#endif

#ifdef NORMAL_USED
	vec3 normal_highp = normal_interp;
#if defined(DO_SIDE_CHECK)
	if (!gl_FrontFacing) {
		normal_highp = -normal_highp;
	}
#endif // DO_SIDE_CHECK
#endif // NORMAL_USED

#ifdef UV_USED
	vec2 uv = uv_interp;
#endif

#if defined(UV2_USED) || defined(USE_LIGHTMAP)
	vec2 uv2 = uv2_interp;
#endif

#if defined(COLOR_USED)
	vec4 color = color_interp;
#endif

#if defined(NORMAL_MAP_USED)

	vec3 normal_map = vec3(0.5);
#endif

#if defined(BENT_NORMAL_MAP_USED)
	vec3 bent_normal_vector = vec3(0.5);
	vec3 bent_normal_map = vec3(0.5);
#endif

	float normal_map_depth = 1.0;

	vec2 screen_uv = gl_FragCoord.xy * scene_data.screen_pixel_size;

	float sss_strength = 0.0;

#ifdef ALPHA_SCISSOR_USED
	float alpha_scissor_threshold = 1.0;
#endif // ALPHA_SCISSOR_USED

#ifdef ALPHA_HASH_USED
	float alpha_hash_scale = 1.0;
#endif // ALPHA_HASH_USED

#ifdef ALPHA_ANTIALIASING_EDGE_USED
	float alpha_antialiasing_edge = 0.0;
	vec2 alpha_texture_coordinate = vec2(0.0, 0.0);
#endif // ALPHA_ANTIALIASING_EDGE_USED

	mat4 inv_view_matrix = transpose(mat4(scene_data.inv_view_matrix[0],
			scene_data.inv_view_matrix[1],
			scene_data.inv_view_matrix[2],
			vec4(0.0, 0.0, 0.0, 1.0)));
	mat4 read_model_matrix = transpose(mat4(FELLOW_INSTANCE(transform)[0],
			FELLOW_INSTANCE(transform)[1],
			FELLOW_INSTANCE(transform)[2],
			vec4(0.0, 0.0, 0.0, 1.0)));

#ifdef LIGHT_VERTEX_USED
	vec3 light_vertex = vertex;
#endif //LIGHT_VERTEX_USED

	mat3 model_normal_matrix;
	if (bool(fellow_flags & INSTANCE_FLAGS_NON_UNIFORM_SCALE)) {
		model_normal_matrix = transpose(inverse(mat3(read_model_matrix)));
	} else {
		model_normal_matrix = mat3(read_model_matrix);
	}

	mat4 read_view_matrix = transpose(mat4(scene_data.view_matrix[0],
			scene_data.view_matrix[1],
			scene_data.view_matrix[2],
			vec4(0.0, 0.0, 0.0, 1.0)));
	vec2 read_viewport_size = scene_data.viewport_size;

#ifdef POINT_COORD_USED
#ifdef POINT_SIZE_USED
	vec2 point_coord;
	if (sc_emulate_point_size) {
		point_coord = point_coord_interp;
	} else {
		point_coord = gl_PointCoord;
	}
#else // !POINT_SIZE_USED
	vec2 point_coord = vec2(0.5);
#endif
#endif

	{
#CODE : FRAGMENT
	}

	float roughness = roughness_highp;
	float metallic = metallic_highp;
	vec3 albedo = albedo_highp;
	float alpha = alpha_highp;
#ifdef NORMAL_USED
	vec3 normal = normal_highp;
#endif

#ifdef LIGHT_TRANSMITTANCE_USED
	transmittance_color.a *= sss_strength;
#endif

#ifdef LIGHT_VERTEX_USED
	vertex = light_vertex;
#ifdef USE_MULTIVIEW
	vec3 view = -normalize(vertex - eye_offset);
#else
	vec3 view = -normalize(vertex);
#endif //USE_MULTIVIEW
#else
	vec3 view = view_highp;
#endif //LIGHT_VERTEX_USED

#ifdef NORMAL_USED
	vec3 geo_normal = normalize(normal);
#endif // NORMAL_USED

#ifndef USE_SHADOW_TO_OPACITY

#ifdef ALPHA_SCISSOR_USED
#ifdef MODE_RENDER_MATERIAL
	if (alpha < alpha_scissor_threshold) {
		alpha = 0.0;
	} else {
		alpha = 1.0;
	}
#else
	if (alpha < alpha_scissor_threshold) {
		discard;
	}
#endif // MODE_RENDER_MATERIAL
#endif // ALPHA_SCISSOR_USED

// alpha hash can be used in unison with alpha antialiasing
#ifdef ALPHA_HASH_USED
	vec3 object_pos = (inverse(read_model_matrix) * inv_view_matrix * vec4(vertex, 1.0)).xyz;
#ifdef MODE_RENDER_MATERIAL
	if (alpha < compute_alpha_hash_threshold(object_pos, alpha_hash_scale)) {
		alpha = 0.0;
	} else {
		alpha = 1.0;
	}
#else
	if (alpha < compute_alpha_hash_threshold(object_pos, alpha_hash_scale)) {
		discard;
	}
#endif // MODE_RENDER_MATERIAL
#endif // ALPHA_HASH_USED

// If we are not edge antialiasing, we need to remove the output alpha channel from scissor and hash
#if (defined(ALPHA_SCISSOR_USED) || defined(ALPHA_HASH_USED)) && !defined(ALPHA_ANTIALIASING_EDGE_USED) && !defined(MODE_RENDER_MATERIAL)
	alpha = 1.0;
#endif

#ifdef ALPHA_ANTIALIASING_EDGE_USED
// If alpha scissor is used, we must further the edge threshold, otherwise we won't get any edge feather
#ifdef ALPHA_SCISSOR_USED
	alpha_antialiasing_edge = clamp(alpha_scissor_threshold + alpha_antialiasing_edge, 0.0, 1.0);
#endif
	alpha = compute_alpha_antialiasing_edge(alpha, alpha_texture_coordinate, alpha_antialiasing_edge);
#endif // ALPHA_ANTIALIASING_EDGE_USED

#ifdef MODE_RENDER_DEPTH
#if defined(USE_OPAQUE_PREPASS) || defined(ALPHA_ANTIALIASING_EDGE_USED)
	if (alpha < scene_data.opaque_prepass_threshold) {
		discard;
	}
#endif // USE_OPAQUE_PREPASS || ALPHA_ANTIALIASING_EDGE_USED
#endif // MODE_RENDER_DEPTH

#endif // !USE_SHADOW_TO_OPACITY

#if defined(FELLOW_ALPHA_ZERO_SKIP_OK) && !defined(MODE_RENDER_DEPTH) && !defined(MODE_RENDER_MATERIAL) && !defined(MODE_SEPARATE_SPECULAR)
	// FellowEngine: from here on alpha is only multiplied by finite factors or lowered with min(): a mix-blended
	// fragment whose alpha is 0 now leaves the target unchanged (src * 0 + dst * 1, alpha 0 + dst * 1), so its lighting
	// and fog need not be computed. The test is quad-uniform: a fragment is dropped only when all four fragments of its
	// 2x2 quad (helper invocations included) have alpha 0, read through fine derivatives, which Vulkan defines as the
	// differences within the quad (the mixed one is (a11 - a01) - (a10 - a00), the same for the four). Dropping a
	// single fragment would change its neighbours' derivatives (reflection and ambient LOD, roughness limiter, decal
	// gradients, shadow dithering).
	if (sc_fellow_blended_alpha_zero_skip() && bool(scene_data.flags & SCENE_DATA_FLAGS_FELLOW_BLENDED_ALPHA_ZERO_SKIP)) {
		float fellow_da_x = dFdxFine(alpha);
		float fellow_da_y = dFdyFine(alpha);
		float fellow_da_xy = dFdyFine(fellow_da_x);
		if (alpha == 0.0 && fellow_da_x == 0.0 && fellow_da_y == 0.0 && fellow_da_xy == 0.0) {
			discard;
		}
	}
#endif

#if defined(NORMAL_MAP_USED)
	normal_map.xy = normal_map.xy * 2.0 - 1.0;
	normal_map.z = sqrt(max(0.0, 1.0 - dot(normal_map.xy, normal_map.xy))); //always ignore Z, as it can be RG packed, Z may be pos/neg, etc.

	// Tangent-space transformation is performed using unnormalized TBN vectors, per MikkTSpace.
	// See: http://www.mikktspace.com/
	normal = normalize(mix(normal, tangent * normal_map.x + binormal * normal_map.y + normal * normal_map.z, normal_map_depth));
#elif defined(NORMAL_USED)
	normal = geo_normal;
#endif // NORMAL_MAP_USED

#ifdef BENT_NORMAL_MAP_USED
	bent_normal_map.xy = bent_normal_map.xy * 2.0 - 1.0;
	bent_normal_map.z = sqrt(max(0.0, 1.0 - dot(bent_normal_map.xy, bent_normal_map.xy)));

	bent_normal_vector = normalize(tangent * bent_normal_map.x + binormal * bent_normal_map.y + normal * bent_normal_map.z);
#endif

#ifdef LIGHT_ANISOTROPY_USED

	if (anisotropy > 0.01) {
		mat3 rot = mat3(normalize(tangent), normalize(binormal), normal);
		// Make local to space.
		tangent = normalize(rot * vec3(anisotropy_flow.x, anisotropy_flow.y, 0.0));
		binormal = normalize(rot * vec3(-anisotropy_flow.y, anisotropy_flow.x, 0.0));
	}

#endif

#ifdef ENABLE_CLIP_ALPHA
#ifdef MODE_RENDER_MATERIAL
	if (albedo.a < 0.99) {
		// Used for doublepass and shadowmapping.
		albedo.a = 0.0;
		alpha = 0.0;
	} else {
		albedo.a = 1.0;
		alpha = 1.0;
	}
#else
	if (albedo.a < 0.99) {
		//used for doublepass and shadowmapping
		discard;
	}
#endif // MODE_RENDER_MATERIAL
#endif

	/////////////////////// FOG //////////////////////
#ifndef MODE_RENDER_DEPTH

#ifndef FOG_DISABLED
#ifndef CUSTOM_FOG_USED
	// fog must be processed as early as possible and then packed.
	// to maximize VGPR usage
	// Draw "fixed" fog before volumetric fog to ensure volumetric fog can appear in front of the sky.

	if (bool(scene_data.flags & SCENE_DATA_FLAGS_USE_FOG)) {
		fog = fog_process(vertex);
		// Premultiply by opacity and convert opacity to transmittance to match volumetric fog.
		fog.rgb *= fog.a;
		fog.a = 1.0 - fog.a;
	}

	if (implementation_data.volumetric_fog_enabled) {
#ifdef USE_MULTIVIEW
		vec4 volumetric_fog = volumetric_fog_process(combined_uv, -vertex.z);
#else
		vec4 volumetric_fog = volumetric_fog_process(screen_uv, -vertex.z);
#endif
		vec4 res = vec4(0.0);
		if (bool(scene_data.flags & SCENE_DATA_FLAGS_USE_FOG)) {
			//must use the full blending equation here to blend fogs
			if (sc_fog_use_legacy_blending()) {
				res.a = mix(0.0, fog.a, volumetric_fog.a);
				res.rgb = mix(volumetric_fog.rgb, fog.rgb, volumetric_fog.a);
			} else {
				res.a = fog.a * volumetric_fog.a;
				res.rgb = fog.rgb * volumetric_fog.a + volumetric_fog.rgb;
			}
		} else {
			res = volumetric_fog;
			if (sc_fog_use_legacy_blending()) {
				res.rgb *= 1.0 - res.a;
			}
		}
		fog = res;
	}
#else
	// Premultiply by opacity and convert opacity to transmittance to match volumetric fog.
	fog.rgb *= fog.a;
	fog.a = 1.0 - fog.a;
#endif //!CUSTOM_FOG_USED

	uint fog_rg = packHalf2x16(fog.rg);
	uint fog_ba = packHalf2x16(fog.ba);

#endif //!FOG_DISABLED
#endif //!MODE_RENDER_DEPTH

	/////////////////////// DECALS ////////////////////////////////

#ifndef MODE_RENDER_DEPTH

#ifdef USE_MULTIVIEW
	uvec2 cluster_pos = uvec2(combined_uv.xy / scene_data.screen_pixel_size) >> implementation_data.cluster_shift;
#else
	uvec2 cluster_pos = uvec2(gl_FragCoord.xy) >> implementation_data.cluster_shift;
#endif
	uint cluster_offset = (implementation_data.cluster_width * cluster_pos.y + cluster_pos.x) * (implementation_data.max_cluster_element_count_div_32 + 32);

	uint cluster_z = fellow_cluster_z(-vertex.z, scene_data.z_near, scene_data.z_far, scene_data.flags);

	//used for interpolating anything cluster related
	vec3 vertex_ddx = dFdx(vertex);
	vec3 vertex_ddy = dFdy(vertex);

	if (!sc_fellow_no_decals()) { // process decals (FellowEngine decision 0057: not compiled without decals)

		uint cluster_decal_offset = cluster_offset + implementation_data.cluster_type_size * 3;

		uint item_min;
		uint item_max;
		uint item_from;
		uint item_to;

		cluster_get_item_range(cluster_decal_offset + implementation_data.max_cluster_element_count_div_32 + cluster_z, item_min, item_max, item_from, item_to);

		item_from = subgroupBroadcastFirst(subgroupMin(item_from));
		item_to = subgroupBroadcastFirst(subgroupMax(item_to));
#ifdef FELLOW_EXP_BIT13
		// Diagnostics (debug/scene_shader_experiments 8192; the image changes): no decals and reflection probes, their code kept.
		if (scene_data.taa_frame_count >= 0.0) {
			item_to = item_from;
		}
#endif

		for (uint i = item_from; i < item_to; i++) {
			uint mask = cluster_buffer.data[cluster_decal_offset + i];
			mask &= cluster_get_range_clip_mask(i, item_min, item_max);

			uint merged_mask = subgroupBroadcastFirst(subgroupOr(mask));
			while (merged_mask != 0) {
				uint bit = findMSB(merged_mask);
				merged_mask &= ~(1u << bit);

				if (((1u << bit) & mask) == 0) { //do not process if not originally here
					continue;
				}

				uint decal_index = 32 * i + bit;

				if (!bool(decals.data[decal_index].mask & FELLOW_INSTANCE(layer_mask))) {
					continue; //not masked
				}

				vec3 uv_local = (decals.data[decal_index].xform * vec4(vertex, 1.0)).xyz;
				if (any(lessThan(uv_local, vec3(0.0, -1.0, 0.0))) || any(greaterThan(uv_local, vec3(1.0)))) {
					continue; //out of decal
				}

				float fade = pow(1.0 - (uv_local.y > 0.0 ? uv_local.y : -uv_local.y), uv_local.y > 0.0 ? decals.data[decal_index].upper_fade : decals.data[decal_index].lower_fade);

				if (decals.data[decal_index].normal_fade > 0.0) {
					fade *= smoothstep(decals.data[decal_index].normal_fade, 1.0, dot(geo_normal, decals.data[decal_index].normal) * 0.5 + 0.5);
				}

				//we need ddx/ddy for mipmaps, so simulate them
				vec2 ddx = (decals.data[decal_index].xform * vec4(vertex_ddx, 0.0)).xz;
				vec2 ddy = (decals.data[decal_index].xform * vec4(vertex_ddy, 0.0)).xz;

				if (decals.data[decal_index].albedo_rect != vec4(0.0)) {
					//has albedo
					vec4 decal_albedo;
					if (sc_decal_use_mipmaps()) {
						decal_albedo = textureGrad(sampler2D(decal_atlas_srgb, decal_sampler), uv_local.xz * decals.data[decal_index].albedo_rect.zw + decals.data[decal_index].albedo_rect.xy, ddx * decals.data[decal_index].albedo_rect.zw, ddy * decals.data[decal_index].albedo_rect.zw);
					} else {
						decal_albedo = textureLod(sampler2D(decal_atlas_srgb, decal_sampler), uv_local.xz * decals.data[decal_index].albedo_rect.zw + decals.data[decal_index].albedo_rect.xy, 0.0);
					}
					decal_albedo *= decals.data[decal_index].modulate;
					decal_albedo.a *= fade;
					albedo = mix(albedo, decal_albedo.rgb, decal_albedo.a * decals.data[decal_index].albedo_mix);

					if (decals.data[decal_index].normal_rect != vec4(0.0)) {
						vec3 decal_normal;
						if (sc_decal_use_mipmaps()) {
							decal_normal = textureGrad(sampler2D(decal_atlas, decal_sampler), uv_local.xz * decals.data[decal_index].normal_rect.zw + decals.data[decal_index].normal_rect.xy, ddx * decals.data[decal_index].normal_rect.zw, ddy * decals.data[decal_index].normal_rect.zw).xyz;
						} else {
							decal_normal = textureLod(sampler2D(decal_atlas, decal_sampler), uv_local.xz * decals.data[decal_index].normal_rect.zw + decals.data[decal_index].normal_rect.xy, 0.0).xyz;
						}
						decal_normal.xy = decal_normal.xy * vec2(2.0, -2.0) - vec2(1.0, -1.0); //users prefer flipped y normal maps in most authoring software
						decal_normal.z = sqrt(max(0.0, 1.0 - dot(decal_normal.xy, decal_normal.xy)));
						//convert to view space, use xzy because y is up
						decal_normal = (decals.data[decal_index].normal_xform * decal_normal.xzy).xyz;

						normal = normalize(mix(normal, decal_normal, decal_albedo.a));
					}

					if (decals.data[decal_index].orm_rect != vec4(0.0)) {
						vec3 decal_orm;
						if (sc_decal_use_mipmaps()) {
							decal_orm = textureGrad(sampler2D(decal_atlas, decal_sampler), uv_local.xz * decals.data[decal_index].orm_rect.zw + decals.data[decal_index].orm_rect.xy, ddx * decals.data[decal_index].orm_rect.zw, ddy * decals.data[decal_index].orm_rect.zw).xyz;
						} else {
							decal_orm = textureLod(sampler2D(decal_atlas, decal_sampler), uv_local.xz * decals.data[decal_index].orm_rect.zw + decals.data[decal_index].orm_rect.xy, 0.0).xyz;
						}
						ao = mix(ao, decal_orm.r, decal_albedo.a);
						roughness = mix(roughness, decal_orm.g, decal_albedo.a);
						metallic = mix(metallic, decal_orm.b, decal_albedo.a);
					}
				}

				if (decals.data[decal_index].emission_rect != vec4(0.0)) {
					//emission is additive, so its independent from albedo
					if (sc_decal_use_mipmaps()) {
						emission += textureGrad(sampler2D(decal_atlas_srgb, decal_sampler), uv_local.xz * decals.data[decal_index].emission_rect.zw + decals.data[decal_index].emission_rect.xy, ddx * decals.data[decal_index].emission_rect.zw, ddy * decals.data[decal_index].emission_rect.zw).xyz * decals.data[decal_index].modulate.rgb * decals.data[decal_index].emission_energy * fade;
					} else {
						emission += textureLod(sampler2D(decal_atlas_srgb, decal_sampler), uv_local.xz * decals.data[decal_index].emission_rect.zw + decals.data[decal_index].emission_rect.xy, 0.0).xyz * decals.data[decal_index].modulate.rgb * decals.data[decal_index].emission_energy * fade;
					}
				}
			}
		}
	} else {
		// FellowEngine decision 0057: what the decal loop could have changed (fellow_keep).
		albedo = fellow_keep(albedo);
		normal = fellow_keep(normal);
		ao = fellow_keep(ao);
		roughness = fellow_keep(roughness);
		metallic = fellow_keep(metallic);
		emission = fellow_keep(emission);
	}

	//pack albedo until needed again, saves 2 VGPRs in the meantime

#endif //not render depth

#if !defined(MODE_RENDER_SDF) && (!defined(MODE_RENDER_DEPTH) || defined(MODE_RENDER_NORMAL_ROUGHNESS)) && defined(NORMAL_USED)
	// FellowEngine weather (decision 0124): the same in the depth pre-pass (its normals and roughness feed the
	// reflections) and the colour passes.
	if (sc_fellow_wetness()) {
		fellow_wetness_apply(scene_data, albedo, roughness, metallic, normal, vertex, normalize(normal_interp));
	}
#endif
#if defined(FELLOW_WIND_USED) && !defined(MODE_RENDER_SDF) && !defined(MODE_RENDER_DEPTH)
	// The wind's sheen (decision 0125): bent grass shows its lighter, glossier side (the gusts' waves over a meadow).
	albedo *= 1.0 + fellow_wind_sheen_interp;
#endif
	/////////////////////// LIGHTING //////////////////////////////

#ifdef NORMAL_USED
	if (bool(scene_data.flags & SCENE_DATA_FLAGS_USE_ROUGHNESS_LIMITER)) {
		//https://www.jp.square-enix.com/tech/library/pdf/ImprovedGeometricSpecularAA.pdf
		float roughness2 = roughness * roughness;
		vec3 dndu = dFdx(normal), dndv = dFdy(normal);
		float variance = scene_data.roughness_limiter_amount * (dot(dndu, dndu) + dot(dndv, dndv));
		float kernelRoughness2 = min(2.0 * variance, scene_data.roughness_limiter_limit); //limit effect
		float filteredRoughness2 = min(1.0, roughness2 + kernelRoughness2);
		roughness = sqrt(filteredRoughness2);

		// Reject very small roughness values. Lack of precision can collapse
		// roughness^4 to 0 in GGX specular equations and cause divisions by zero.
		if (roughness < 0.00000001) {
			roughness = 0.0;
		}
	}
#endif
	//apply energy conservation

	vec3 direct_specular_light = vec3(0.0, 0.0, 0.0);
	vec3 indirect_specular_light = vec3(0.0, 0.0, 0.0);
	vec3 diffuse_light = vec3(0.0, 0.0, 0.0);
	vec3 ambient_light = vec3(0.0, 0.0, 0.0);
#ifndef MODE_UNSHADED
	// Used in regular draw pass and when drawing SDFs for SDFGI and materials for VoxelGI.
	emission *= scene_data.emissive_exposure_normalization;
#endif

#if !defined(MODE_RENDER_DEPTH) && !defined(MODE_UNSHADED)

#ifndef AMBIENT_LIGHT_DISABLED
// Use bent normal for indirect lighting where possible
#ifdef BENT_NORMAL_MAP_USED
	vec3 indirect_normal = bent_normal_vector;
#else
	vec3 indirect_normal = normal;
#endif

	if (bool(scene_data.flags & SCENE_DATA_FLAGS_USE_REFLECTION_CUBEMAP)) {
#ifdef LIGHT_ANISOTROPY_USED
		// https://google.github.io/filament/Filament.html#lighting/imagebasedlights/anisotropy
		vec3 anisotropic_direction = anisotropy >= 0.0 ? binormal : tangent;
		vec3 anisotropic_tangent = cross(anisotropic_direction, view);
		vec3 anisotropic_normal = cross(anisotropic_tangent, anisotropic_direction);
		vec3 bent_normal = normalize(mix(normal, anisotropic_normal, abs(anisotropy) * clamp(5.0 * roughness, 0.0, 1.0)));
		vec3 ref_vec = reflect(-view, bent_normal);
		ref_vec = mix(ref_vec, bent_normal, roughness * roughness);
#else
		vec3 ref_vec = reflect(-view, normal);
		ref_vec = mix(ref_vec, normal, roughness * roughness);
#endif

		float horizon = min(1.0 + dot(ref_vec, normal), 1.0);
		ref_vec = scene_data.radiance_inverse_xform * ref_vec;

#ifdef USE_RADIANCE_OCTMAP_ARRAY

		float roughness_lod, blend;

		blend = modf(sqrt(roughness) * MAX_ROUGHNESS_LOD, roughness_lod);

		float ref_lod = vec3_to_oct_lod(dFdx(ref_vec), dFdy(ref_vec), scene_data_block.data.radiance_pixel_size);
		vec2 ref_uv = vec3_to_oct_with_border(ref_vec, vec2(scene_data_block.data.radiance_border_size, 1.0 - scene_data_block.data.radiance_border_size * 2.0));
		vec3 indirect_sample_a = textureLod(sampler2DArray(radiance_octmap, DEFAULT_SAMPLER_LINEAR_WITH_MIPMAPS_CLAMP), vec3(ref_uv, roughness_lod), ref_lod).rgb;
		vec3 indirect_sample_b = textureLod(sampler2DArray(radiance_octmap, DEFAULT_SAMPLER_LINEAR_WITH_MIPMAPS_CLAMP), vec3(ref_uv, roughness_lod + 1), ref_lod).rgb;
		indirect_specular_light = mix(indirect_sample_a, indirect_sample_b, blend);

#else
		float roughness_lod = sqrt(roughness) * MAX_ROUGHNESS_LOD;
		vec2 ref_uv = vec3_to_oct_with_border(ref_vec, vec2(scene_data_block.data.radiance_border_size, 1.0 - scene_data_block.data.radiance_border_size * 2.0));
		indirect_specular_light = textureLod(sampler2D(radiance_octmap, DEFAULT_SAMPLER_LINEAR_WITH_MIPMAPS_CLAMP), ref_uv, roughness_lod).rgb;

#endif //USE_RADIANCE_OCTMAP_ARRAY
#if !defined(MODE_RENDER_DEPTH)
		if (sc_fellow_cloud_shadow() && fellow_weather.data.cloud_shadow3.y > 0.0) {
			// FellowEngine (decision 0133): the sky's reflection under the clouds — where they cover it, the grey of the
			// sky's mean brightness there (its blurriest level: no sun disc), above the horizon.
#ifdef USE_RADIANCE_OCTMAP_ARRAY
			vec3 fellow_sky_mean = textureLod(sampler2DArray(radiance_octmap, DEFAULT_SAMPLER_LINEAR_WITH_MIPMAPS_CLAMP), vec3(ref_uv, MAX_ROUGHNESS_LOD), 0.0).rgb;
#else
			vec3 fellow_sky_mean = textureLod(sampler2D(radiance_octmap, DEFAULT_SAMPLER_LINEAR_WITH_MIPMAPS_CLAMP), ref_uv, MAX_ROUGHNESS_LOD).rgb;
#endif
			float fellow_cover = fellow_weather.data.cloud_shadow3.y * smoothstep(-0.1, 0.15, ref_vec.y);
			indirect_specular_light = mix(indirect_specular_light, vec3(dot(fellow_sky_mean, vec3(0.2126, 0.7152, 0.0722))), fellow_cover);
		}
#endif
		indirect_specular_light *= scene_data.IBL_exposure_normalization;
		indirect_specular_light *= horizon * horizon;
		indirect_specular_light *= scene_data.ambient_light_color_energy.a;
	}

#if defined(CUSTOM_RADIANCE_USED)
	indirect_specular_light = mix(indirect_specular_light, custom_radiance.rgb, custom_radiance.a);
#endif

#ifndef USE_LIGHTMAP
	//lightmap overrides everything
	if (bool(scene_data.flags & SCENE_DATA_FLAGS_USE_AMBIENT_LIGHT)) {
		ambient_light = scene_data.ambient_light_color_energy.rgb;

		if (bool(scene_data.flags & SCENE_DATA_FLAGS_USE_AMBIENT_CUBEMAP)) {
			vec3 ambient_dir = scene_data.radiance_inverse_xform * indirect_normal;
#ifdef USE_RADIANCE_OCTMAP_ARRAY
			float ambient_lod = vec3_to_oct_lod(dFdx(ambient_dir), dFdy(ambient_dir), scene_data_block.data.radiance_pixel_size);
			vec2 ambient_uv = vec3_to_oct_with_border(ambient_dir, vec2(scene_data_block.data.radiance_border_size, 1.0 - scene_data_block.data.radiance_border_size * 2.0));
			vec3 cubemap_ambient = textureLod(sampler2DArray(radiance_octmap, DEFAULT_SAMPLER_LINEAR_WITH_MIPMAPS_CLAMP), vec3(ambient_uv, MAX_ROUGHNESS_LOD), ambient_lod).rgb;
#else
			float roughness_lod = MAX_ROUGHNESS_LOD;
			vec2 ambient_uv = vec3_to_oct_with_border(ambient_dir, vec2(scene_data_block.data.radiance_border_size, 1.0 - scene_data_block.data.radiance_border_size * 2.0));
			vec3 cubemap_ambient = textureLod(sampler2D(radiance_octmap, DEFAULT_SAMPLER_LINEAR_WITH_MIPMAPS_CLAMP), ambient_uv, roughness_lod).rgb;
#endif //USE_RADIANCE_OCTMAP_ARRAY
#if !defined(MODE_RENDER_DEPTH)
			if (sc_fellow_cloud_shadow()) {
				// FellowEngine (decision 0133): the sky's light under the clouds, grey where they cover it.
				cubemap_ambient = mix(cubemap_ambient, vec3(dot(cubemap_ambient, vec3(0.2126, 0.7152, 0.0722))), fellow_weather.data.cloud_shadow3.y);
			}
#endif
			cubemap_ambient *= scene_data.IBL_exposure_normalization;
			ambient_light = mix(ambient_light, cubemap_ambient * scene_data.ambient_light_color_energy.a, scene_data.ambient_color_sky_mix);
		}
	}
#endif // USE_LIGHTMAP
#if !defined(USE_LIGHTMAP)
	// FellowEngine DDGI (decision 0083): the probe volume's irradiance in place of the environment's ambient light
	// (blended with it where the volume ends).
	if (sc_fellow_ddgi() && sc_fellow_ddgi_screen()) {
		vec4 fellow_gi = texelFetch(sampler2D(fellow_ddgi_screen, SAMPLER_NEAREST_CLAMP), ivec2(gl_FragCoord.xy), 0);
		// Plus the sky leak: a share of the environment's ambient light everywhere (no interior entirely black).
		ambient_light = mix(ambient_light, fellow_gi.rgb + ambient_light * fellow_ddgi.volume.shading.x, fellow_gi.a);
	} else if (sc_fellow_ddgi()) {
		mat4 fellow_inv_view = transpose(mat4(scene_data.inv_view_matrix[0], scene_data.inv_view_matrix[1], scene_data.inv_view_matrix[2], vec4(0.0, 0.0, 0.0, 1.0)));
		vec3 fellow_wpos = (fellow_inv_view * vec4(vertex, 1.0)).xyz;
		vec3 fellow_wn = normalize(mat3(fellow_inv_view) * indirect_normal);
		vec3 fellow_weye = normalize(mat3(fellow_inv_view) * -normalize(vertex));
		float fellow_coverage;
		vec3 fellow_gi = fellow_ddgi_irradiance(fellow_ddgi_irradiance_atlas, fellow_ddgi_distance_atlas, SAMPLER_LINEAR_CLAMP, fellow_ddgi.volume, fellow_wpos, fellow_wn, fellow_weye, fellow_coverage);
		ambient_light = mix(ambient_light, fellow_gi + ambient_light * fellow_ddgi.volume.shading.x, clamp(fellow_coverage, 0.0, 1.0));
	}
#endif
#if defined(CUSTOM_IRRADIANCE_USED)
	ambient_light = mix(ambient_light, custom_irradiance.rgb, custom_irradiance.a);
#endif

#ifdef LIGHT_CLEARCOAT_USED
	vec3 cc_specular_light = vec3(0.0);
	vec3 cc_ref_vec = vec3(0.0);

	if (bool(scene_data.flags & SCENE_DATA_FLAGS_USE_REFLECTION_CUBEMAP)) {
		cc_ref_vec = reflect(-view, geo_normal);
		cc_ref_vec = mix(cc_ref_vec, geo_normal, mix(0.001, 0.1, clearcoat_roughness));

		vec3 cc_radiance_ref_vec = scene_data.radiance_inverse_xform * cc_ref_vec;
		float roughness_lod = sqrt(mix(0.001, 0.1, clearcoat_roughness)) * MAX_ROUGHNESS_LOD;
#ifdef USE_RADIANCE_OCTMAP_ARRAY

		float lod, blend;
		blend = modf(roughness_lod, lod);

		float ref_lod = vec3_to_oct_lod(dFdx(cc_radiance_ref_vec), dFdy(cc_radiance_ref_vec), scene_data_block.data.radiance_pixel_size);
		vec2 ref_uv = vec3_to_oct_with_border(cc_radiance_ref_vec, vec2(scene_data_block.data.radiance_border_size, 1.0 - scene_data_block.data.radiance_border_size * 2.0));
		vec3 clearcoat_sample_a = textureLod(sampler2DArray(radiance_octmap, DEFAULT_SAMPLER_LINEAR_WITH_MIPMAPS_CLAMP), vec3(ref_uv, lod), ref_lod).rgb;
		vec3 clearcoat_sample_b = textureLod(sampler2DArray(radiance_octmap, DEFAULT_SAMPLER_LINEAR_WITH_MIPMAPS_CLAMP), vec3(ref_uv, lod + 1), ref_lod).rgb;
		vec3 clearcoat_light = mix(clearcoat_sample_a, clearcoat_sample_b, blend);

#else
		vec2 ref_uv = vec3_to_oct_with_border(cc_radiance_ref_vec, vec2(scene_data_block.data.radiance_border_size, 1.0 - scene_data_block.data.radiance_border_size * 2.0));
		vec3 clearcoat_light = textureLod(sampler2D(radiance_octmap, DEFAULT_SAMPLER_LINEAR_WITH_MIPMAPS_CLAMP), ref_uv, roughness_lod).rgb;

#endif //USE_RADIANCE_OCTMAP_ARRAY
		cc_specular_light += clearcoat_light * scene_data.IBL_exposure_normalization * scene_data.ambient_light_color_energy.a;
	}
#endif // LIGHT_CLEARCOAT_USED
#endif // !AMBIENT_LIGHT_DISABLED
#endif //!defined(MODE_RENDER_DEPTH) && !defined(MODE_UNSHADED)

	//radiance

/// GI ///
#if !defined(MODE_RENDER_DEPTH) && !defined(MODE_UNSHADED)
#ifndef AMBIENT_LIGHT_DISABLED
#ifdef USE_LIGHTMAP

	//lightmap
	if (bool(fellow_flags & INSTANCE_FLAGS_USE_LIGHTMAP_CAPTURE)) { //has lightmap capture
		uint index = fellow_gi_offset;

		// The world normal.
		vec3 wnormal = mat3(inv_view_matrix) * indirect_normal;

		// The SH coefficients used for evaluating diffuse data from SH probes.
		const float c[5] = float[](
				0.886227, // l0 				sqrt(1.0/(4.0*PI)) 	* PI
				1.023327, // l1 				sqrt(3.0/(4.0*PI)) 	* PI*2.0/3.0
				0.858086, // l2n2, l2n1, l2p1	sqrt(15.0/(4.0*PI)) * PI*1.0/4.0
				0.247708, // l20 				sqrt(5.0/(16.0*PI)) * PI*1.0/4.0
				0.429043 // l2p2 				sqrt(15.0/(16.0*PI))* PI*1.0/4.0
		);

		ambient_light += (c[0] * lightmap_captures.data[index].sh[0].rgb +
								 c[1] * lightmap_captures.data[index].sh[1].rgb * wnormal.y +
								 c[1] * lightmap_captures.data[index].sh[2].rgb * wnormal.z +
								 c[1] * lightmap_captures.data[index].sh[3].rgb * wnormal.x +
								 c[2] * lightmap_captures.data[index].sh[4].rgb * wnormal.x * wnormal.y +
								 c[2] * lightmap_captures.data[index].sh[5].rgb * wnormal.y * wnormal.z +
								 c[3] * lightmap_captures.data[index].sh[6].rgb * (3.0 * wnormal.z * wnormal.z - 1.0) +
								 c[2] * lightmap_captures.data[index].sh[7].rgb * wnormal.x * wnormal.z +
								 c[4] * lightmap_captures.data[index].sh[8].rgb * (wnormal.x * wnormal.x - wnormal.y * wnormal.y)) *
				scene_data.IBL_exposure_normalization;

	} else if (bool(fellow_flags & INSTANCE_FLAGS_USE_LIGHTMAP)) { // has actual lightmap
		bool uses_sh = bool(fellow_flags & INSTANCE_FLAGS_USE_SH_LIGHTMAP);
		uint ofs = fellow_gi_offset & 0xFFFF;
		uint slice = fellow_gi_offset >> 16;
		vec3 uvw;
		uvw.xy = uv2 * FELLOW_INSTANCE(lightmap_uv_scale).zw + FELLOW_INSTANCE(lightmap_uv_scale).xy;
		uvw.z = float(slice);

		if (uses_sh) {
			uvw.z *= 4.0; //SH textures use 4 times more data
			vec3 lm_light_l0;
			vec3 lm_light_l1n1;
			vec3 lm_light_l1_0;
			vec3 lm_light_l1p1;

			if (sc_use_lightmap_bicubic_filter()) {
				lm_light_l0 = textureArray_bicubic(lightmap_textures[ofs], uvw + vec3(0.0, 0.0, 0.0), lightmaps.data[ofs].light_texture_size).rgb;
				lm_light_l1n1 = (textureArray_bicubic(lightmap_textures[ofs], uvw + vec3(0.0, 0.0, 1.0), lightmaps.data[ofs].light_texture_size).rgb - vec3(0.5)) * 2.0;
				lm_light_l1_0 = (textureArray_bicubic(lightmap_textures[ofs], uvw + vec3(0.0, 0.0, 2.0), lightmaps.data[ofs].light_texture_size).rgb - vec3(0.5)) * 2.0;
				lm_light_l1p1 = (textureArray_bicubic(lightmap_textures[ofs], uvw + vec3(0.0, 0.0, 3.0), lightmaps.data[ofs].light_texture_size).rgb - vec3(0.5)) * 2.0;
			} else {
				lm_light_l0 = textureLod(sampler2DArray(lightmap_textures[ofs], SAMPLER_LINEAR_CLAMP), uvw + vec3(0.0, 0.0, 0.0), 0.0).rgb;
				lm_light_l1n1 = (textureLod(sampler2DArray(lightmap_textures[ofs], SAMPLER_LINEAR_CLAMP), uvw + vec3(0.0, 0.0, 1.0), 0.0).rgb - vec3(0.5)) * 2.0;
				lm_light_l1_0 = (textureLod(sampler2DArray(lightmap_textures[ofs], SAMPLER_LINEAR_CLAMP), uvw + vec3(0.0, 0.0, 2.0), 0.0).rgb - vec3(0.5)) * 2.0;
				lm_light_l1p1 = (textureLod(sampler2DArray(lightmap_textures[ofs], SAMPLER_LINEAR_CLAMP), uvw + vec3(0.0, 0.0, 3.0), 0.0).rgb - vec3(0.5)) * 2.0;
			}

			vec3 n = normalize(lightmaps.data[ofs].normal_xform * indirect_normal);
			float en = lightmaps.data[ofs].exposure_normalization;

			ambient_light += lm_light_l0 * en;
			ambient_light += lm_light_l1n1 * n.y * (lm_light_l0 * en * 4.0);
			ambient_light += lm_light_l1_0 * n.z * (lm_light_l0 * en * 4.0);
			ambient_light += lm_light_l1p1 * n.x * (lm_light_l0 * en * 4.0);

		} else {
			if (sc_use_lightmap_bicubic_filter()) {
				ambient_light += textureArray_bicubic(lightmap_textures[ofs], uvw, lightmaps.data[ofs].light_texture_size).rgb * lightmaps.data[ofs].exposure_normalization;
			} else {
				ambient_light += textureLod(sampler2DArray(lightmap_textures[ofs], SAMPLER_LINEAR_CLAMP), uvw, 0.0).rgb * lightmaps.data[ofs].exposure_normalization;
			}
		}
	}
#else

	if (sc_use_forward_gi() && bool(fellow_flags & INSTANCE_FLAGS_USE_SDFGI)) { //has lightmap capture

		//make vertex orientation the world one, but still align to camera
		vec3 cam_pos = mat3(inv_view_matrix) * vertex;
		vec3 cam_normal = mat3(inv_view_matrix) * indirect_normal;
		vec3 cam_reflection = mat3(inv_view_matrix) * reflect(-view, indirect_normal);

		//apply y-mult
		cam_pos.y *= sdfgi.y_mult;
		cam_normal.y *= sdfgi.y_mult;
		cam_normal = normalize(cam_normal);
		cam_reflection.y *= sdfgi.y_mult;
		cam_normal = normalize(cam_normal);
		cam_reflection = normalize(cam_reflection);

		vec4 light_accum = vec4(0.0);
		float weight_accum = 0.0;

		vec4 light_blend_accum = vec4(0.0);
		float weight_blend_accum = 0.0;

		float blend = -1.0;

		// helper constants, compute once

		uint cascade = 0xFFFFFFFF;
		vec3 cascade_pos;
		vec3 cascade_normal;

		for (uint i = 0; i < sdfgi.max_cascades; i++) {
			cascade_pos = (cam_pos - sdfgi.cascades[i].position) * sdfgi.cascades[i].to_probe;

			if (any(lessThan(cascade_pos, vec3(0.0))) || any(greaterThanEqual(cascade_pos, sdfgi.cascade_probe_size))) {
				continue; //skip cascade
			}

			cascade = i;
			break;
		}

		if (cascade < SDFGI_MAX_CASCADES) {
			bool use_specular = true;
			float blend;
			vec3 diffuse, specular;
			sdfgi_process(cascade, cascade_pos, cam_pos, cam_normal, cam_reflection, use_specular, roughness, diffuse, specular, blend);

			if (blend > 0.0) {
				//blend
				if (cascade == sdfgi.max_cascades - 1) {
					diffuse = mix(diffuse, ambient_light, blend);
					if (use_specular) {
						indirect_specular_light = mix(specular, indirect_specular_light, blend);
					}
				} else {
					vec3 diffuse2, specular2;
					float blend2;
					cascade_pos = (cam_pos - sdfgi.cascades[cascade + 1].position) * sdfgi.cascades[cascade + 1].to_probe;
					sdfgi_process(cascade + 1, cascade_pos, cam_pos, cam_normal, cam_reflection, use_specular, roughness, diffuse2, specular2, blend2);
					diffuse = mix(diffuse, diffuse2, blend);
					if (use_specular) {
						specular = mix(specular, specular2, blend);
					}
				}
			}

			ambient_light = diffuse;
			if (use_specular) {
				indirect_specular_light = specular;
			}
		}
	}

	if (sc_use_forward_gi() && bool(fellow_flags & INSTANCE_FLAGS_USE_VOXEL_GI)) { // process voxel_gi_instances
		uint index1 = fellow_gi_offset & 0xFFFF;
		// Make vertex orientation the world one, but still align to camera.
		vec3 cam_pos = mat3(inv_view_matrix) * vertex;
		vec3 cam_normal = mat3(inv_view_matrix) * indirect_normal;
		vec3 ref_vec = mat3(inv_view_matrix) * normalize(reflect(-view, indirect_normal));

		//find arbitrary tangent and bitangent, then build a matrix
		vec3 v0 = abs(cam_normal.z) < 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(0.0, 1.0, 0.0);
		vec3 tangent = normalize(cross(v0, cam_normal));
		vec3 bitangent = normalize(cross(tangent, cam_normal));
		mat3 normal_mat = mat3(tangent, bitangent, cam_normal);

		vec4 amb_accum = vec4(0.0);
		vec4 spec_accum = vec4(0.0);
		voxel_gi_compute(index1, cam_pos, cam_normal, ref_vec, normal_mat, roughness * roughness, ambient_light, indirect_specular_light, spec_accum, amb_accum);

		uint index2 = fellow_gi_offset >> 16;

		if (index2 != 0xFFFF) {
			voxel_gi_compute(index2, cam_pos, cam_normal, ref_vec, normal_mat, roughness * roughness, ambient_light, indirect_specular_light, spec_accum, amb_accum);
		}

		if (amb_accum.a > 0.0) {
			amb_accum.rgb /= amb_accum.a;
		}

		if (spec_accum.a > 0.0) {
			spec_accum.rgb /= spec_accum.a;
		}

		indirect_specular_light = spec_accum.rgb;
		ambient_light = amb_accum.rgb;
	}

	if (!sc_use_forward_gi() && bool(fellow_flags & INSTANCE_FLAGS_USE_GI_BUFFERS)) { //use GI buffers

		vec2 coord;

		if (implementation_data.gi_upscale_for_msaa) {
			vec2 base_coord = screen_uv;
			vec2 closest_coord = base_coord;
#ifdef USE_MULTIVIEW
			float closest_ang = dot(indirect_normal, normalize(textureLod(sampler2DArray(normal_roughness_buffer, SAMPLER_LINEAR_CLAMP), vec3(base_coord, ViewIndex), 0.0).xyz * 2.0 - 1.0));
#else // USE_MULTIVIEW
			float closest_ang = dot(indirect_normal, normalize(textureLod(sampler2D(normal_roughness_buffer, SAMPLER_LINEAR_CLAMP), base_coord, 0.0).xyz * 2.0 - 1.0));
#endif // USE_MULTIVIEW

			for (int i = 0; i < 4; i++) {
				const vec2 neighbors[4] = vec2[](vec2(-1, 0), vec2(1, 0), vec2(0, -1), vec2(0, 1));
				vec2 neighbour_coord = base_coord + neighbors[i] * scene_data.screen_pixel_size;
#ifdef USE_MULTIVIEW
				float neighbour_ang = dot(indirect_normal, normalize(textureLod(sampler2DArray(normal_roughness_buffer, SAMPLER_LINEAR_CLAMP), vec3(neighbour_coord, ViewIndex), 0.0).xyz * 2.0 - 1.0));
#else // USE_MULTIVIEW
				float neighbour_ang = dot(indirect_normal, normalize(textureLod(sampler2D(normal_roughness_buffer, SAMPLER_LINEAR_CLAMP), neighbour_coord, 0.0).xyz * 2.0 - 1.0));
#endif // USE_MULTIVIEW
				if (neighbour_ang > closest_ang) {
					closest_ang = neighbour_ang;
					closest_coord = neighbour_coord;
				}
			}

			coord = closest_coord;

		} else {
			coord = screen_uv;
		}

#ifdef USE_MULTIVIEW
		vec4 buffer_ambient = textureLod(sampler2DArray(ambient_buffer, SAMPLER_LINEAR_CLAMP), vec3(coord, ViewIndex), 0.0);
		vec4 buffer_reflection = textureLod(sampler2DArray(reflection_buffer, SAMPLER_LINEAR_CLAMP), vec3(coord, ViewIndex), 0.0);
#else // USE_MULTIVIEW
		vec4 buffer_ambient = textureLod(sampler2D(ambient_buffer, SAMPLER_LINEAR_CLAMP), coord, 0.0);
		vec4 buffer_reflection = textureLod(sampler2D(reflection_buffer, SAMPLER_LINEAR_CLAMP), coord, 0.0);
#endif // USE_MULTIVIEW

		ambient_light = mix(ambient_light, buffer_ambient.rgb, buffer_ambient.a);
		indirect_specular_light = mix(indirect_specular_light, buffer_reflection.rgb, buffer_reflection.a);
	}
#endif // !USE_LIGHTMAP

	if (bool(implementation_data.ss_effects_flags & SCREEN_SPACE_EFFECTS_FLAGS_USE_SSAO)) {
#ifdef USE_MULTIVIEW
		float ssao = texture(sampler2DArray(ao_buffer, SAMPLER_LINEAR_CLAMP), vec3(screen_uv, ViewIndex)).r;
#else
		float ssao = texture(sampler2D(ao_buffer, SAMPLER_LINEAR_CLAMP), screen_uv).r;
#endif
		ao = min(ao, ssao);
		ao_light_affect = mix(ao_light_affect, max(ao_light_affect, implementation_data.ssao_light_affect), implementation_data.ssao_ao_affect);
	}

	{ // process reflections

		vec4 reflection_accum = vec4(0.0, 0.0, 0.0, 0.0);
		vec4 ambient_accum = vec4(0.0, 0.0, 0.0, 0.0);
#ifdef LIGHT_CLEARCOAT_USED
		vec4 cc_reflection_accum = vec4(0.0, 0.0, 0.0, 0.0);
#endif

		uint cluster_reflection_offset = cluster_offset + implementation_data.cluster_type_size * 4;

		uint item_min;
		uint item_max;
		uint item_from;
		uint item_to;

		cluster_get_item_range(cluster_reflection_offset + implementation_data.max_cluster_element_count_div_32 + cluster_z, item_min, item_max, item_from, item_to);

		item_from = subgroupBroadcastFirst(subgroupMin(item_from));
		item_to = subgroupBroadcastFirst(subgroupMax(item_to));
#ifdef FELLOW_EXP_BIT13
		// Diagnostics (debug/scene_shader_experiments 8192; the image changes): no decals and reflection probes, their code kept.
		if (scene_data.taa_frame_count >= 0.0) {
			item_to = item_from;
		}
#endif

#ifdef LIGHT_ANISOTROPY_USED
		// https://google.github.io/filament/Filament.html#lighting/imagebasedlights/anisotropy
		vec3 anisotropic_direction = anisotropy >= 0.0 ? binormal : tangent;
		vec3 anisotropic_tangent = cross(anisotropic_direction, view);
		vec3 anisotropic_normal = cross(anisotropic_tangent, anisotropic_direction);
		vec3 bent_normal = normalize(mix(normal, anisotropic_normal, abs(anisotropy) * clamp(5.0 * roughness, 0.0, 1.0)));
#else
		vec3 bent_normal = normal;
#endif
		vec3 ref_vec = normalize(reflect(-view, bent_normal));
		// Interpolate between mirror and rough reflection by using linear_roughness * linear_roughness.
		ref_vec = mix(ref_vec, bent_normal, roughness * roughness * roughness * roughness);

		if (!sc_fellow_no_reflection_probes()) { // FellowEngine decision 0057: not compiled without reflection probes
			for (uint i = item_from; i < item_to; i++) {
				uint mask = cluster_buffer.data[cluster_reflection_offset + i];
				mask &= cluster_get_range_clip_mask(i, item_min, item_max);

				uint merged_mask = subgroupBroadcastFirst(subgroupOr(mask));
				while (merged_mask != 0) {
					uint bit = findMSB(merged_mask);
					merged_mask &= ~(1u << bit);

					if (((1u << bit) & mask) == 0) { //do not process if not originally here
						continue;
					}

					uint reflection_index = 32 * i + bit;

					if (!bool(reflections.data[reflection_index].mask & FELLOW_INSTANCE(layer_mask))) {
						continue; //not masked
					}

#ifndef LIGHT_CLEARCOAT_USED
					if (reflection_accum.a >= 1.0 && ambient_accum.a >= 1.0) {
						break;
					}
#else
					if (reflection_accum.a >= 1.0 && cc_reflection_accum.a >= 1.0 && ambient_accum.a >= 1.0) {
						break;
					}
#endif // LIGHT_CLEARCOAT_USED

					reflection_process(reflection_index, vertex, ref_vec, normal, roughness, ambient_light,
#ifdef LIGHT_CLEARCOAT_USED
							cc_ref_vec, mix(0.001, 0.1, clearcoat_roughness), cc_reflection_accum,
#endif
							ambient_accum, reflection_accum);
				}
			}
		} else {
			// FellowEngine decision 0057: what the probe loop could have changed (fellow_keep).
			ambient_accum = fellow_keep(ambient_accum);
			reflection_accum = fellow_keep(reflection_accum);
#ifdef LIGHT_CLEARCOAT_USED
			cc_reflection_accum = fellow_keep(cc_reflection_accum);
#endif
			// The loop's reflection vector is still computed, and used (through an AND with the zero): it shares
			// subexpressions with the sky reflection's (reflect(-view, normal)), and without this use the RTX's
			// compiler arranges those differently (m11_f165: City Lab and Bistro exterior then bit for bit, 98.4 and
			// 93.8 dB without it). About twenty arithmetic operations.
			reflection_accum.x = uintBitsToFloat(floatBitsToUint(reflection_accum.x) ^ ((floatBitsToUint(ref_vec.x) ^ floatBitsToUint(ref_vec.y) ^ floatBitsToUint(ref_vec.z)) & implementation_data.fellow_zero));
		}

		if (ambient_accum.a < 1.0) {
			ambient_accum.rgb = ambient_light * (1.0 - ambient_accum.a) + ambient_accum.rgb;
		}

		if (reflection_accum.a < 1.0) {
			reflection_accum.rgb = indirect_specular_light * (1.0 - reflection_accum.a) + reflection_accum.rgb;
		}

#ifdef LIGHT_CLEARCOAT_USED
		if (cc_reflection_accum.a < 1.0) {
			cc_reflection_accum.rgb = cc_specular_light * (1.0 - cc_reflection_accum.a) + cc_reflection_accum.rgb;
		}
#endif

		if (reflection_accum.a > 0.0) {
			indirect_specular_light = reflection_accum.rgb;
		}

#ifdef LIGHT_CLEARCOAT_USED
		if (cc_reflection_accum.a > 0.0) {
			cc_specular_light = cc_reflection_accum.rgb;
		}
#endif

#if !defined(USE_LIGHTMAP)
		if (ambient_accum.a > 0.0) {
			ambient_light = ambient_accum.rgb;
		}
#endif
	}

	//finalize ambient light here
	{
		ambient_light *= ao;
#ifndef SPECULAR_OCCLUSION_DISABLED
#ifdef BENT_NORMAL_MAP_USED
		// Apply cone to cone intersection with cosine weighted assumption:
		// https://blog.selfshadow.com/publications/s2016-shading-course/activision/s2016_pbs_activision_occlusion.pdf
		float cos_a_v = sqrt(1.0 - ao);
		float limited_roughness = max(roughness, 0.01); // Avoid artifacts at really low roughness.
		float cos_a_s = exp2((-log(10.0) / log(2.0)) * limited_roughness * limited_roughness);
		float cos_b = dot(bent_normal_vector, reflect(-view, normal));

		// Intersection between the spherical caps of the visibility and specular cone.
		// Based on Christopher Oat and Pedro V. Sander's "Ambient aperture lighting":
		// https://advances.realtimerendering.com/s2006/Chapter8-Ambient_Aperture_Lighting.pdf
		float r1 = acos(cos_a_v);
		float r2 = acos(cos_a_s);
		float d = acos(cos_b);
		float area = 0.0;

		if (d <= max(r1, r2) - min(r1, r2)) {
			// One cap is enclosed in the other.
			area = M_TAU - M_TAU * max(cos_a_v, cos_a_s);
		} else if (d >= r1 + r2) {
			// No intersection.
			area = 0.0;
		} else {
			float delta = abs(r1 - r2);
			float x = 1.0 - clamp((d - delta) / (r1 + r2 - delta), 0.0, 1.0);
			area = smoothstep(0.0, 1.0, x);
			area *= M_TAU - M_TAU * max(cos_a_v, cos_a_s);
		}

		float specular_occlusion = area / (M_TAU * (1.0 - cos_a_s));
		indirect_specular_light *= specular_occlusion;
#else // BENT_NORMAL_MAP_USED
		float specular_occlusion = (ambient_light.r * 0.3 + ambient_light.g * 0.59 + ambient_light.b * 0.11) * 2.0; // Luminance of ambient light.
		specular_occlusion = min(specular_occlusion * 4.0, 1.0); // This multiplication preserves speculars on bright areas.

		float reflective_f = (1.0 - roughness) * metallic;
		// 10.0 is a magic number, it gives the intended effect in most scenarios.
		// Low enough for occlusion, high enough for reaction to lights and shadows.
		specular_occlusion = max(min(reflective_f * specular_occlusion * 10.0, 1.0), specular_occlusion);
		indirect_specular_light *= specular_occlusion;
#endif // BENT_NORMAL_MAP_USED
#endif // SPECULAR_OCCLUSION_DISABLED
		ambient_light *= albedo.rgb;

		if (bool(implementation_data.ss_effects_flags & SCREEN_SPACE_EFFECTS_FLAGS_USE_SSIL)) {
#ifdef USE_MULTIVIEW
			vec4 ssil = textureLod(sampler2DArray(ssil_buffer, SAMPLER_LINEAR_CLAMP), vec3(screen_uv, ViewIndex), 0.0);
#else
			vec4 ssil = textureLod(sampler2D(ssil_buffer, SAMPLER_LINEAR_CLAMP), screen_uv, 0.0);
#endif // USE_MULTIVIEW
			ambient_light *= 1.0 - ssil.a;
			ambient_light += ssil.rgb * albedo.rgb;
		}

		//process ssr
		if (bool(implementation_data.ss_effects_flags & SCREEN_SPACE_EFFECTS_FLAGS_USE_SSR)) {
			bool resolve_ssr = bool(implementation_data.ss_effects_flags & SCREEN_SPACE_EFFECTS_FLAGS_RESOLVE_SSR);

			float ssr_mip_level = 0.0;
			if (resolve_ssr) {
#ifdef USE_MULTIVIEW
				ssr_mip_level = textureLod(sampler2DArray(ssr_mip_level_buffer, SAMPLER_NEAREST_CLAMP), vec3(screen_uv, ViewIndex), 0.0).x;
#else
				ssr_mip_level = textureLod(sampler2D(ssr_mip_level_buffer, SAMPLER_NEAREST_CLAMP), screen_uv, 0.0).x;
#endif // USE_MULTIVIEW

				ssr_mip_level *= 14.0;
			}

#ifdef USE_MULTIVIEW
			vec4 ssr = textureLod(sampler2DArray(ssr_buffer, SAMPLER_LINEAR_WITH_MIPMAPS_CLAMP), vec3(screen_uv, ViewIndex), ssr_mip_level);
#else
			vec4 ssr = textureLod(sampler2D(ssr_buffer, SAMPLER_LINEAR_WITH_MIPMAPS_CLAMP), screen_uv, ssr_mip_level);
#endif // USE_MULTIVIEW

			if (resolve_ssr) {
				const vec3 rec709_luminance_weights = vec3(0.2126, 0.7152, 0.0722);
				ssr.rgb /= 1.0 - dot(ssr.rgb, rec709_luminance_weights);
			}

			// Apply fade when approaching 0.7 roughness to smoothen the harsh cutoff in the main SSR trace pass.
			ssr *= smoothstep(0.0, 1.0, 1.0 - clamp((roughness - 0.6) / (0.7 - 0.6), 0.0, 1.0));

			// Alpha is premultiplied.
			indirect_specular_light = indirect_specular_light * (1.0 - ssr.a) + ssr.rgb;
		}

#if !defined(USE_MULTIVIEW)
		// FellowEngine ray-traced reflections (decision 0084): the traced radiance in place of the probes' and the sky's,
		// faded towards the maximum roughness.
		if (sc_fellow_rt_reflections()) {
			vec4 fellow_refl = texelFetch(sampler2D(fellow_rt_reflection, SAMPLER_NEAREST_CLAMP), ivec2(gl_FragCoord.xy), 0);
			indirect_specular_light = mix(indirect_specular_light, fellow_refl.rgb, fellow_refl.a);
		}
#endif
	}
#endif // AMBIENT_LIGHT_DISABLED

	// convert ao to direct light ao
	ao = mix(1.0, ao, ao_light_affect);

	//this saves some VGPRs
	vec3 f0 = F0(metallic, specular, albedo);

#ifdef LIGHT_CLEARCOAT_USED
	// The base layer's f0 is computed assuming an interface from air to an IOR
	// of 1.5, but the clear coat layer forms an interface from IOR 1.5 to IOR
	// 1.5. We recompute f0 by first computing its IOR, then reconverting to f0
	// by using the correct interface
	f0 = mix(f0, f0_Clear_Coat_To_Surface(f0), clearcoat);
#endif

#ifndef AMBIENT_LIGHT_DISABLED
	{
#if defined(DIFFUSE_TOON)
		//simplify for toon, as
		indirect_specular_light *= specular * metallic * albedo * 2.0;
#else
		// Base Layer
		float NdotV = clamp(dot(normal, view), 0.0001, 1.0);
		vec2 envBRDF = prefiltered_dfg(roughness, NdotV).xy;
		// Multiscattering
		energy_compensation = get_energy_compensation(f0, envBRDF.y);

		// cheap luminance approximation
		float f90 = clamp(50.0 * f0.g, metallic, 1.0);
		indirect_specular_light *= energy_compensation * ((f90 - f0) * envBRDF.x + f0 * envBRDF.y);

#ifdef LIGHT_CLEARCOAT_USED
		float geo_NdotV = max(dot(geo_normal, view), 0.0001); // We want to use geometric normal, not normal_map
		// The clearcoat layer assumes an IOR of 1.5 (4% reflectance).
		// Attenuate underlying diffuse/specular by clearcoat fresnel (ONLY fresnel, hence we don't just invert the BRDF below).
		float NdotV5 = SchlickFresnel(geo_NdotV);
		float F = mix(0.04, 1.0, NdotV5) * clearcoat;
		float cc_attenuation = 1.0 - F;

		ambient_light *= cc_attenuation;
		indirect_specular_light *= cc_attenuation;

		// Clearcoat Layer
		// We don't need DFG for clearcoat, so we can use the fresnel directly.
		indirect_specular_light += cc_specular_light * F;
#endif

#endif // DIFFUSE_TOON
	}

#endif // !AMBIENT_LIGHT_DISABLED
#endif //GI !defined(MODE_RENDER_DEPTH) && !defined(MODE_UNSHADED)

// LIGHTING
#if !defined(MODE_RENDER_DEPTH) && !defined(MODE_UNSHADED)

#ifdef USE_VERTEX_LIGHTING
	diffuse_light += diffuse_light_interp.rgb;
	direct_specular_light += specular_light_interp.rgb * f0;
#endif

	if (!sc_fellow_no_directional_lights()) { // Directional light. (FellowEngine decision 0057: not compiled without directional lights)

		// Do shadow and lighting in two passes to reduce register pressure.
#ifndef SHADOWS_DISABLED
		uint shadow0 = 0;
		uint shadow1 = 0;

		float shadowmask = 1.0;

#ifdef USE_LIGHTMAP
		uint shadowmask_mode = LIGHTMAP_SHADOWMASK_MODE_NONE;

		if (bool(fellow_flags & INSTANCE_FLAGS_USE_LIGHTMAP)) {
			const uint ofs = fellow_gi_offset & 0xFFFF;
			shadowmask_mode = lightmaps.data[ofs].flags;

			if (shadowmask_mode != LIGHTMAP_SHADOWMASK_MODE_NONE) {
				const uint slice = fellow_gi_offset >> 16;
				const vec2 scaled_uv = uv2 * FELLOW_INSTANCE(lightmap_uv_scale).zw + FELLOW_INSTANCE(lightmap_uv_scale).xy;
				const vec3 uvw = vec3(scaled_uv, float(slice));

				if (sc_use_lightmap_bicubic_filter()) {
					shadowmask = textureArray_bicubic(lightmap_textures[MAX_LIGHTMAP_TEXTURES + ofs], uvw, lightmaps.data[ofs].light_texture_size).x;
				} else {
					shadowmask = textureLod(sampler2DArray(lightmap_textures[MAX_LIGHTMAP_TEXTURES + ofs], SAMPLER_LINEAR_CLAMP), uvw, 0.0).x;
				}
			}
		}

		if (shadowmask_mode != LIGHTMAP_SHADOWMASK_MODE_ONLY) {
#endif // USE_LIGHTMAP

#ifdef USE_VERTEX_LIGHTING
			// Only process the first light's shadow for vertex lighting.
			for (uint i = 0; i < 1; i++) {
#else
		for (uint i = 0; i < 8; i++) {
			if (i >= scene_data.directional_light_count) {
				break;
			}
#endif

				if (!bool(directional_lights.data[i].mask & FELLOW_INSTANCE(layer_mask))) {
					continue; //not masked
				}

				if (directional_lights.data[i].bake_mode == LIGHT_BAKE_STATIC && bool(fellow_flags & INSTANCE_FLAGS_USE_LIGHTMAP)) {
					continue; // Statically baked light and object uses lightmap, skip
				}

				float shadow = 1.0;

#if defined(FELLOW_BACKFACING_LIGHT_SKIP_OK) && !defined(USE_VERTEX_LIGHTING)
				// FellowEngine (rendering/fellow/shadows/skip_backfacing_directional): with N.L <= 0 (computed exactly as in
				// light_compute) this light's diffuse and specular are multiplied by zero, so its shadow value is never used.
				bool fellow_skip_shadow = false;
				if (sc_fellow_skip_backfacing_directional() && bool(scene_data.flags & SCENE_DATA_FLAGS_FELLOW_SKIP_BACKFACING_SHADOW)) {
					half fellow_size_A = sc_use_directional_soft_shadows() ? half(directional_lights.data[i].size) : half(0.0);
					half fellow_NdotL = min(fellow_size_A + dot(hvec3(normal), hvec3(directional_lights.data[i].direction)), half(1.0));
					fellow_skip_shadow = fellow_NdotL <= half(0.0);
				}
#else
				const bool fellow_skip_shadow = false;
#endif

				// FellowEngine ray-traced sun shadows (decision 0082): the traced shadow where the mask is valid.
				float fellow_rt_shadow = -1.0;
#if !defined(USE_VERTEX_LIGHTING) && !defined(USE_MULTIVIEW)
				if (sc_fellow_rt_sun_shadow() && i == 0u && directional_lights.data[i].shadow_opacity > 0.001) {
					vec2 fellow_rt = texelFetch(sampler2D(fellow_rt_shadow_mask, SAMPLER_NEAREST_CLAMP), ivec2(gl_FragCoord.xy), 0).xy;
					if (fellow_rt.y > 0.5) {
						fellow_rt_shadow = fellow_rt.x;
					}
				}
#endif

				if (fellow_rt_shadow >= 0.0) {
					shadow = fellow_rt_shadow;
				} else if (directional_lights.data[i].shadow_opacity > 0.001 && !fellow_skip_shadow) {
					float depth_z = -vertex.z;
					vec3 light_dir = directional_lights.data[i].direction;
					vec3 base_normal_bias = geo_normal * (1.0 - max(0.0, dot(light_dir, -geo_normal)));

#define BIAS_FUNC(m_var, m_idx)                                                                 \
	m_var.xyz += light_dir * directional_lights.data[i].shadow_bias[m_idx];                     \
	vec3 normal_bias = base_normal_bias * directional_lights.data[i].shadow_normal_bias[m_idx]; \
	normal_bias -= light_dir * dot(light_dir, normal_bias);                                     \
	m_var.xyz += normal_bias;

					//version with soft shadows, more expensive
					if (sc_fellow_pcss_blend_band() && sc_use_directional_soft_shadows() && directional_lights.data[i].softshadow_angle > 0 && bool(scene_data.flags & SCENE_DATA_FLAGS_FELLOW_PCSS_BLEND_BAND)) {
						// FellowEngine: sample the cascade that contains the pixel; with blend splits, blend towards the next
						// cascade over the last 10 % of the split only (as the PCF path does) instead of over the whole split.
						vec4 splits = directional_lights.data[i].shadow_split_offsets;
						uint c = depth_z < splits.x ? 0u : (depth_z < splits.y ? 1u : (depth_z < splits.z ? 2u : 3u));
						shadow = fellow_directional_soft_cascade(i, c, vertex, base_normal_bias, scene_data.taa_frame_count);
						if (directional_lights.data[i].blend_splits && c < 3u) {
							float split_end = c == 0u ? splits.x : (c == 1u ? splits.y : splits.z);
							float w = smoothstep(split_end * 0.9, split_end, depth_z);
							if (w > 0.0) {
								float s2 = fellow_directional_soft_cascade(i, c + 1u, vertex, base_normal_bias, scene_data.taa_frame_count);
								shadow = mix(shadow, s2, w);
							}
						}
					} else if (sc_use_directional_soft_shadows() && directional_lights.data[i].softshadow_angle > 0) {
#if defined(FELLOW_PCSS_PAIR)
						// FellowEngine experiment (debug/scene_shader_experiments 2048): with blend splits, the pixel's
						// cascade and the next through one PCSS that fetches both cascades' taps together. The same
						// cascades, coordinates, blend and per-cascade arithmetic as the code below.
						const vec4 fellow_splits = directional_lights.data[i].shadow_split_offsets;
						uint fellow_c0 = depth_z < fellow_splits.x ? 0u : (depth_z < fellow_splits.y ? 1u : (depth_z < fellow_splits.z ? 2u : 3u));
						bool fellow_two = directional_lights.data[i].blend_splits && fellow_c0 < 3u;
						vec3 fellow_coord[2];
						vec2 fellow_scale[2];
						[[unroll]] for (uint k = 0u; k < 2u; k++) {
							if (k == 0u || fellow_two) {
								uint c = fellow_c0 + k;
								vec4 v = vec4(vertex, 1.0);

								BIAS_FUNC(v, c)

								vec4 pssm_coord;
								float range_begin;
								vec2 uv_scale;
								if (c == 0u) {
									pssm_coord = (directional_lights.data[i].shadow_matrix1 * v);
									range_begin = directional_lights.data[i].shadow_range_begin.x;
									uv_scale = directional_lights.data[i].uv_scale1;
								} else if (c == 1u) {
									pssm_coord = (directional_lights.data[i].shadow_matrix2 * v);
									range_begin = directional_lights.data[i].shadow_range_begin.y;
									uv_scale = directional_lights.data[i].uv_scale2;
								} else if (c == 2u) {
									pssm_coord = (directional_lights.data[i].shadow_matrix3 * v);
									range_begin = directional_lights.data[i].shadow_range_begin.z;
									uv_scale = directional_lights.data[i].uv_scale3;
								} else {
									pssm_coord = (directional_lights.data[i].shadow_matrix4 * v);
									range_begin = directional_lights.data[i].shadow_range_begin.w;
									uv_scale = directional_lights.data[i].uv_scale4;
								}
								pssm_coord /= pssm_coord.w;

								float range_pos = dot(directional_lights.data[i].direction, v.xyz);
								float test_radius = (range_pos - range_begin) * directional_lights.data[i].softshadow_angle;
								vec2 tex_scale = uv_scale * test_radius;
								fellow_coord[k] = pssm_coord.xyz;
								fellow_scale[k] = tex_scale * directional_lights.data[i].soft_shadow_scale;
							}
						}
						if (fellow_two) {
							hvec2 s2 = sample_directional_soft_shadow_pair(directional_shadow_atlas, fellow_coord[0], fellow_scale[0], fellow_coord[1], fellow_scale[1], scene_data.taa_frame_count);
							float blend = fellow_c0 == 0u ? smoothstep(0.0, fellow_splits.x, depth_z) : (fellow_c0 == 1u ? smoothstep(fellow_splits.x, fellow_splits.y, depth_z) : smoothstep(fellow_splits.y, fellow_splits.z, depth_z));
							shadow = s2.x;
							shadow = mix(shadow, float(s2.y), blend);
						} else {
							shadow = sample_directional_soft_shadow(directional_shadow_atlas, fellow_coord[0], fellow_scale[0], scene_data.taa_frame_count, fellow_c0);
						}
#elif defined(FELLOW_EXP_BIT2)
						// FellowEngine register-pressure experiment (debug/scene_shader_experiments bit 2): the cascades in a
						// loop that is not unrolled, so the soft shadow filter is inlined once instead of four times. The same
						// cascades, the same operations in the same order as the code below.
						uint blend_count = 0;
						const uint blend_max = directional_lights.data[i].blend_splits ? 2 : 1;
						const vec4 fellow_splits = directional_lights.data[i].shadow_split_offsets;
						[[dont_unroll]] for (uint c = 0; c < 4u; c++) {
							if (blend_count >= blend_max) {
								break;
							}
							if (c < 3u && !(depth_z < (c == 0u ? fellow_splits.x : (c == 1u ? fellow_splits.y : fellow_splits.z)))) {
								continue;
							}
							vec4 v = vec4(vertex, 1.0);

							BIAS_FUNC(v, c)

							vec4 pssm_coord;
							float range_begin;
							vec2 uv_scale;
							if (c == 0u) {
								pssm_coord = (directional_lights.data[i].shadow_matrix1 * v);
								range_begin = directional_lights.data[i].shadow_range_begin.x;
								uv_scale = directional_lights.data[i].uv_scale1;
							} else if (c == 1u) {
								pssm_coord = (directional_lights.data[i].shadow_matrix2 * v);
								range_begin = directional_lights.data[i].shadow_range_begin.y;
								uv_scale = directional_lights.data[i].uv_scale2;
							} else if (c == 2u) {
								pssm_coord = (directional_lights.data[i].shadow_matrix3 * v);
								range_begin = directional_lights.data[i].shadow_range_begin.z;
								uv_scale = directional_lights.data[i].uv_scale3;
							} else {
								pssm_coord = (directional_lights.data[i].shadow_matrix4 * v);
								range_begin = directional_lights.data[i].shadow_range_begin.w;
								uv_scale = directional_lights.data[i].uv_scale4;
							}
							pssm_coord /= pssm_coord.w;

							float range_pos = dot(directional_lights.data[i].direction, v.xyz);
							float test_radius = (range_pos - range_begin) * directional_lights.data[i].softshadow_angle;
							vec2 tex_scale = uv_scale * test_radius;
							float s = sample_directional_soft_shadow(directional_shadow_atlas, pssm_coord.xyz, tex_scale * directional_lights.data[i].soft_shadow_scale, scene_data.taa_frame_count, c);

							if (blend_count == 0) {
								shadow = s;
							} else {
								//blend
								float blend = c == 1u ? smoothstep(0.0, fellow_splits.x, depth_z) : (c == 2u ? smoothstep(fellow_splits.x, fellow_splits.y, depth_z) : smoothstep(fellow_splits.y, fellow_splits.z, depth_z));
								shadow = mix(shadow, s, blend);
							}

							blend_count++;
						}
#else
						uint blend_count = 0;
						const uint blend_max = directional_lights.data[i].blend_splits ? 2 : 1;

						if (depth_z < directional_lights.data[i].shadow_split_offsets.x) {
							vec4 v = vec4(vertex, 1.0);

							BIAS_FUNC(v, 0)

							vec4 pssm_coord = (directional_lights.data[i].shadow_matrix1 * v);
							pssm_coord /= pssm_coord.w;

							float range_pos = dot(directional_lights.data[i].direction, v.xyz);
							float range_begin = directional_lights.data[i].shadow_range_begin.x;
							float test_radius = (range_pos - range_begin) * directional_lights.data[i].softshadow_angle;
							vec2 tex_scale = directional_lights.data[i].uv_scale1 * test_radius;
							shadow = sample_directional_soft_shadow(directional_shadow_atlas, pssm_coord.xyz, tex_scale * directional_lights.data[i].soft_shadow_scale, scene_data.taa_frame_count, 0u);
							blend_count++;
						}

						if (blend_count < blend_max && depth_z < directional_lights.data[i].shadow_split_offsets.y) {
							vec4 v = vec4(vertex, 1.0);

							BIAS_FUNC(v, 1)

							vec4 pssm_coord = (directional_lights.data[i].shadow_matrix2 * v);
							pssm_coord /= pssm_coord.w;

							float range_pos = dot(directional_lights.data[i].direction, v.xyz);
							float range_begin = directional_lights.data[i].shadow_range_begin.y;
							float test_radius = (range_pos - range_begin) * directional_lights.data[i].softshadow_angle;
							vec2 tex_scale = directional_lights.data[i].uv_scale2 * test_radius;
							float s = sample_directional_soft_shadow(directional_shadow_atlas, pssm_coord.xyz, tex_scale * directional_lights.data[i].soft_shadow_scale, scene_data.taa_frame_count, 1u);

							if (blend_count == 0) {
								shadow = s;
							} else {
								//blend
								float blend = smoothstep(0.0, directional_lights.data[i].shadow_split_offsets.x, depth_z);
								shadow = mix(shadow, s, blend);
							}

							blend_count++;
						}

						if (blend_count < blend_max && depth_z < directional_lights.data[i].shadow_split_offsets.z) {
							vec4 v = vec4(vertex, 1.0);

							BIAS_FUNC(v, 2)

							vec4 pssm_coord = (directional_lights.data[i].shadow_matrix3 * v);
							pssm_coord /= pssm_coord.w;

							float range_pos = dot(directional_lights.data[i].direction, v.xyz);
							float range_begin = directional_lights.data[i].shadow_range_begin.z;
							float test_radius = (range_pos - range_begin) * directional_lights.data[i].softshadow_angle;
							vec2 tex_scale = directional_lights.data[i].uv_scale3 * test_radius;
							float s = sample_directional_soft_shadow(directional_shadow_atlas, pssm_coord.xyz, tex_scale * directional_lights.data[i].soft_shadow_scale, scene_data.taa_frame_count, 2u);

							if (blend_count == 0) {
								shadow = s;
							} else {
								//blend
								float blend = smoothstep(directional_lights.data[i].shadow_split_offsets.x, directional_lights.data[i].shadow_split_offsets.y, depth_z);
								shadow = mix(shadow, s, blend);
							}

							blend_count++;
						}

						if (blend_count < blend_max) {
							vec4 v = vec4(vertex, 1.0);

							BIAS_FUNC(v, 3)

							vec4 pssm_coord = (directional_lights.data[i].shadow_matrix4 * v);
							pssm_coord /= pssm_coord.w;

							float range_pos = dot(directional_lights.data[i].direction, v.xyz);
							float range_begin = directional_lights.data[i].shadow_range_begin.w;
							float test_radius = (range_pos - range_begin) * directional_lights.data[i].softshadow_angle;
							vec2 tex_scale = directional_lights.data[i].uv_scale4 * test_radius;
							float s = sample_directional_soft_shadow(directional_shadow_atlas, pssm_coord.xyz, tex_scale * directional_lights.data[i].soft_shadow_scale, scene_data.taa_frame_count, 3u);

							if (blend_count == 0) {
								shadow = s;
							} else {
								//blend
								float blend = smoothstep(directional_lights.data[i].shadow_split_offsets.y, directional_lights.data[i].shadow_split_offsets.z, depth_z);
								shadow = mix(shadow, s, blend);
							}
						}
#endif
					} else { //no soft shadows

						vec4 pssm_coord;
						float blur_factor;

						if (depth_z < directional_lights.data[i].shadow_split_offsets.x) {
							vec4 v = vec4(vertex, 1.0);

							BIAS_FUNC(v, 0)

							pssm_coord = (directional_lights.data[i].shadow_matrix1 * v);
							blur_factor = 1.0;
						} else if (depth_z < directional_lights.data[i].shadow_split_offsets.y) {
							vec4 v = vec4(vertex, 1.0);

							BIAS_FUNC(v, 1)

							pssm_coord = (directional_lights.data[i].shadow_matrix2 * v);
							// Adjust shadow blur with reference to the first split to reduce discrepancy between shadow splits.
							blur_factor = directional_lights.data[i].shadow_split_offsets.x / directional_lights.data[i].shadow_split_offsets.y;
						} else if (depth_z < directional_lights.data[i].shadow_split_offsets.z) {
							vec4 v = vec4(vertex, 1.0);

							BIAS_FUNC(v, 2)

							pssm_coord = (directional_lights.data[i].shadow_matrix3 * v);
							// Adjust shadow blur with reference to the first split to reduce discrepancy between shadow splits.
							blur_factor = directional_lights.data[i].shadow_split_offsets.x / directional_lights.data[i].shadow_split_offsets.z;
						} else {
							vec4 v = vec4(vertex, 1.0);

							BIAS_FUNC(v, 3)

							pssm_coord = (directional_lights.data[i].shadow_matrix4 * v);
							// Adjust shadow blur with reference to the first split to reduce discrepancy between shadow splits.
							blur_factor = directional_lights.data[i].shadow_split_offsets.x / directional_lights.data[i].shadow_split_offsets.w;
						}

						pssm_coord /= pssm_coord.w;

						shadow = sample_directional_pcf_shadow(directional_shadow_atlas, scene_data.directional_shadow_pixel_size * directional_lights.data[i].soft_shadow_scale * (blur_factor + (1.0 - blur_factor) * float(directional_lights.data[i].blend_splits)), pssm_coord, scene_data.taa_frame_count);

						if (directional_lights.data[i].blend_splits) {
							float pssm_blend;
							float blur_factor2;

							if (depth_z < directional_lights.data[i].shadow_split_offsets.x) {
								vec4 v = vec4(vertex, 1.0);
								BIAS_FUNC(v, 1)
								pssm_coord = (directional_lights.data[i].shadow_matrix2 * v);
								pssm_blend = smoothstep(directional_lights.data[i].shadow_split_offsets.x - directional_lights.data[i].shadow_split_offsets.x * 0.1, directional_lights.data[i].shadow_split_offsets.x, depth_z);
								// Adjust shadow blur with reference to the first split to reduce discrepancy between shadow splits.
								blur_factor2 = directional_lights.data[i].shadow_split_offsets.x / directional_lights.data[i].shadow_split_offsets.y;
							} else if (depth_z < directional_lights.data[i].shadow_split_offsets.y) {
								vec4 v = vec4(vertex, 1.0);
								BIAS_FUNC(v, 2)
								pssm_coord = (directional_lights.data[i].shadow_matrix3 * v);
								pssm_blend = smoothstep(directional_lights.data[i].shadow_split_offsets.y - directional_lights.data[i].shadow_split_offsets.y * 0.1, directional_lights.data[i].shadow_split_offsets.y, depth_z);
								// Adjust shadow blur with reference to the first split to reduce discrepancy between shadow splits.
								blur_factor2 = directional_lights.data[i].shadow_split_offsets.x / directional_lights.data[i].shadow_split_offsets.z;
							} else if (depth_z < directional_lights.data[i].shadow_split_offsets.z) {
								vec4 v = vec4(vertex, 1.0);
								BIAS_FUNC(v, 3)
								pssm_coord = (directional_lights.data[i].shadow_matrix4 * v);
								pssm_blend = smoothstep(directional_lights.data[i].shadow_split_offsets.z - directional_lights.data[i].shadow_split_offsets.z * 0.1, directional_lights.data[i].shadow_split_offsets.z, depth_z);
								// Adjust shadow blur with reference to the first split to reduce discrepancy between shadow splits.
								blur_factor2 = directional_lights.data[i].shadow_split_offsets.x / directional_lights.data[i].shadow_split_offsets.w;
							} else {
								pssm_blend = 0.0; //if no blend, same coord will be used (divide by z will result in same value, and already cached)
								blur_factor2 = 1.0;
							}

							// FellowEngine (rendering/fellow/shadows/skip_unblended_cascade): mix(a, b, 0.0) == a, skip the lookup.
							if (!(sc_fellow_skip_unblended_cascade() && bool(scene_data.flags & SCENE_DATA_FLAGS_FELLOW_SKIP_UNBLENDED_CASCADE)) || pssm_blend > 0.0) {
								pssm_coord /= pssm_coord.w;

								float shadow2 = sample_directional_pcf_shadow(directional_shadow_atlas, scene_data.directional_shadow_pixel_size * directional_lights.data[i].soft_shadow_scale * (blur_factor2 + (1.0 - blur_factor2) * float(directional_lights.data[i].blend_splits)), pssm_coord, scene_data.taa_frame_count);
								shadow = mix(shadow, shadow2, pssm_blend);
							}
						}
					}

#ifdef USE_LIGHTMAP
					if (shadowmask_mode == LIGHTMAP_SHADOWMASK_MODE_REPLACE) {
						shadow = mix(shadow, shadowmask, smoothstep(directional_lights.data[i].fade_from, directional_lights.data[i].fade_to, vertex.z)); //done with negative values for performance
					} else if (shadowmask_mode == LIGHTMAP_SHADOWMASK_MODE_OVERLAY) {
						shadow = shadowmask * mix(shadow, 1.0, smoothstep(directional_lights.data[i].fade_from, directional_lights.data[i].fade_to, vertex.z)); //done with negative values for performance
					} else {
#endif
						shadow = mix(shadow, 1.0, smoothstep(directional_lights.data[i].fade_from, directional_lights.data[i].fade_to, vertex.z)); //done with negative values for performance
#ifdef USE_LIGHTMAP
					}
#endif

#ifdef USE_VERTEX_LIGHTING
					diffuse_light *= mix(1.0, shadow, diffuse_light_interp.a);
					direct_specular_light *= mix(1.0, shadow, specular_light_interp.a);
#endif

#undef BIAS_FUNC
				} // shadows

#if !defined(USE_VERTEX_LIGHTING) && !defined(USE_MULTIVIEW)
				// FellowEngine contact shadows (decision 0107): the screen-space march's result multiplies the first
				// directional light's shadow (by the light's shadow opacity).
				if (sc_fellow_contact_shadow() && i == 0u && directional_lights.data[i].shadow_opacity > 0.001) {
					float fellow_contact = texelFetch(sampler2D(fellow_rt_shadow_mask, SAMPLER_NEAREST_CLAMP), ivec2(gl_FragCoord.xy), 0).x;
					shadow *= mix(1.0, fellow_contact, directional_lights.data[i].shadow_opacity);
				}
				// FellowEngine cloud shadows (decision 0133): the sun through the clouds over this point (the map holds the
				// ground's; a point above it is moved down along the sun to the ground).
				if (sc_fellow_cloud_shadow() && i == uint(fellow_weather.data.cloud_shadow2.x)) {
					vec3 fellow_wp = (inv_view_matrix * vec4(vertex, 1.0)).xyz;
					vec2 fellow_g = fellow_wp.xz - fellow_weather.data.cloud_shadow2.yz * (fellow_wp.y - fellow_weather.data.cloud_shadow2.w);
					vec2 fellow_uv = (fellow_g - fellow_weather.data.cloud_shadow.xy) * fellow_weather.data.cloud_shadow.z + 0.5;
					float fellow_cloud = textureLod(sampler2D(fellow_cloud_shadow, SAMPLER_LINEAR_CLAMP), fellow_uv, 0.0).r;
					// Towards the map's edges (8 km away) the share of clear sky instead.
					float fellow_edge = max(abs(fellow_uv.x - 0.5), abs(fellow_uv.y - 0.5));
					fellow_cloud = mix(fellow_cloud, fellow_weather.data.cloud_shadow3.x, smoothstep(0.4, 0.5, fellow_edge));
					shadow *= mix(1.0, fellow_cloud, fellow_weather.data.cloud_shadow.w);
				}
#endif

				if (i < 4) {
					shadow0 |= uint(clamp(shadow * 255.0, 0.0, 255.0)) << (i * 8);
				} else {
					shadow1 |= uint(clamp(shadow * 255.0, 0.0, 255.0)) << ((i - 4) * 8);
				}
			}

#ifdef USE_LIGHTMAP
		} else { // shadowmask_mode == LIGHTMAP_SHADOWMASK_MODE_ONLY

#ifdef USE_VERTEX_LIGHTING
			diffuse_light *= mix(1.0, shadowmask, diffuse_light_interp.a);
			direct_specular_light *= mix(1.0, shadowmask, specular_light_interp.a);
#endif

			shadow0 |= uint(clamp(shadowmask * 255.0, 0.0, 255.0));
		}
#endif // USE_LIGHTMAP

#endif // SHADOWS_DISABLED

#ifndef USE_VERTEX_LIGHTING

		for (uint i = 0; i < 8; i++) {
			if (i >= scene_data.directional_light_count) {
				break;
			}

			if (!bool(directional_lights.data[i].mask & FELLOW_INSTANCE(layer_mask))) {
				continue; //not masked
			}

			if (directional_lights.data[i].bake_mode == LIGHT_BAKE_STATIC && bool(fellow_flags & INSTANCE_FLAGS_USE_LIGHTMAP)) {
				continue; // Statically baked light and object uses lightmap, skip
			}

#ifdef LIGHT_TRANSMITTANCE_USED
			float transmittance_z = transmittance_depth;
#ifndef SHADOWS_DISABLED
			if (directional_lights.data[i].shadow_opacity > 0.001) {
				float depth_z = -vertex.z;

				if (depth_z < directional_lights.data[i].shadow_split_offsets.x) {
					vec4 trans_vertex = vec4(vertex - geo_normal * directional_lights.data[i].shadow_transmittance_bias.x, 1.0);
					vec4 trans_coord = directional_lights.data[i].shadow_matrix1 * trans_vertex;
					trans_coord /= trans_coord.w;

					float shadow_z = textureLod(sampler2D(directional_shadow_atlas, SAMPLER_LINEAR_CLAMP), trans_coord.xy, 0.0).r;
					shadow_z *= directional_lights.data[i].shadow_z_range.x;
					float z = trans_coord.z * directional_lights.data[i].shadow_z_range.x;

					transmittance_z = z - shadow_z;
				} else if (depth_z < directional_lights.data[i].shadow_split_offsets.y) {
					vec4 trans_vertex = vec4(vertex - geo_normal * directional_lights.data[i].shadow_transmittance_bias.y, 1.0);
					vec4 trans_coord = directional_lights.data[i].shadow_matrix2 * trans_vertex;
					trans_coord /= trans_coord.w;

					float shadow_z = textureLod(sampler2D(directional_shadow_atlas, SAMPLER_LINEAR_CLAMP), trans_coord.xy, 0.0).r;
					shadow_z *= directional_lights.data[i].shadow_z_range.y;
					float z = trans_coord.z * directional_lights.data[i].shadow_z_range.y;

					transmittance_z = z - shadow_z;
				} else if (depth_z < directional_lights.data[i].shadow_split_offsets.z) {
					vec4 trans_vertex = vec4(vertex - geo_normal * directional_lights.data[i].shadow_transmittance_bias.z, 1.0);
					vec4 trans_coord = directional_lights.data[i].shadow_matrix3 * trans_vertex;
					trans_coord /= trans_coord.w;

					float shadow_z = textureLod(sampler2D(directional_shadow_atlas, SAMPLER_LINEAR_CLAMP), trans_coord.xy, 0.0).r;
					shadow_z *= directional_lights.data[i].shadow_z_range.z;
					float z = trans_coord.z * directional_lights.data[i].shadow_z_range.z;

					transmittance_z = z - shadow_z;
				} else {
					vec4 trans_vertex = vec4(vertex - geo_normal * directional_lights.data[i].shadow_transmittance_bias.w, 1.0);
					vec4 trans_coord = directional_lights.data[i].shadow_matrix4 * trans_vertex;
					trans_coord /= trans_coord.w;

					float shadow_z = textureLod(sampler2D(directional_shadow_atlas, SAMPLER_LINEAR_CLAMP), trans_coord.xy, 0.0).r;
					shadow_z *= directional_lights.data[i].shadow_z_range.w;
					float z = trans_coord.z * directional_lights.data[i].shadow_z_range.w;

					transmittance_z = z - shadow_z;
				}
			}
#endif // !SHADOWS_DISABLED
#endif // LIGHT_TRANSMITTANCE_USED

			float shadow = 1.0;
#ifndef SHADOWS_DISABLED
			if (i < 4) {
				shadow = float(shadow0 >> (i * 8u) & 0xFFu) / 255.0;
			} else {
				shadow = float(shadow1 >> ((i - 4u) * 8u) & 0xFFu) / 255.0;
			}

			shadow = mix(1.0, shadow, directional_lights.data[i].shadow_opacity);
#endif

			blur_shadow(shadow);

#ifdef DEBUG_DRAW_PSSM_SPLITS
			vec3 tint = vec3(1.0);
			if (-vertex.z < directional_lights.data[i].shadow_split_offsets.x) {
				tint = vec3(1.0, 0.0, 0.0);
			} else if (-vertex.z < directional_lights.data[i].shadow_split_offsets.y) {
				tint = vec3(0.0, 1.0, 0.0);
			} else if (-vertex.z < directional_lights.data[i].shadow_split_offsets.z) {
				tint = vec3(0.0, 0.0, 1.0);
			} else {
				tint = vec3(1.0, 1.0, 0.0);
			}
			tint = mix(tint, vec3(1.0), shadow);
			shadow = 1.0;
#endif

			float size_A = sc_use_directional_soft_shadows() ? directional_lights.data[i].size : 0.0;

			light_compute(normal, directional_lights.data[i].direction, normalize(view), size_A,
#ifndef DEBUG_DRAW_PSSM_SPLITS
					directional_lights.data[i].color * directional_lights.data[i].energy,
#else
					directional_lights.data[i].color * directional_lights.data[i].energy * tint,
#endif
					true, shadow, f0, roughness, metallic, directional_lights.data[i].specular, albedo, alpha, screen_uv, energy_compensation,
#ifdef LIGHT_BACKLIGHT_USED
					backlight,
#endif
#ifdef LIGHT_TRANSMITTANCE_USED
					transmittance_color,
					transmittance_depth,
					transmittance_boost,
					transmittance_z,
#endif
#ifdef LIGHT_RIM_USED
					rim, rim_tint,
#endif
#ifdef LIGHT_CLEARCOAT_USED
					clearcoat, clearcoat_roughness, geo_normal,
#endif // LIGHT_CLEARCOAT_USED
#ifdef LIGHT_ANISOTROPY_USED
					binormal,
					tangent, anisotropy,
#endif
					diffuse_light,
					direct_specular_light);
		}
#endif // USE_VERTEX_LIGHTING
	} else {
		// FellowEngine decision 0057: what the light loop could have changed (fellow_keep). The light sums are 0 or
		// sums when they reach the loop, never a product that a later add could fuse with, except with vertex lighting.
#ifdef USE_VERTEX_LIGHTING
		diffuse_light = fellow_keep(diffuse_light);
		direct_specular_light = fellow_keep(direct_specular_light);
#endif
#ifdef USE_SHADOW_TO_OPACITY
		alpha = fellow_keep(alpha);
#endif
	}

#ifndef USE_VERTEX_LIGHTING
	if (!sc_fellow_no_omni_lights()) { //omni lights (FellowEngine decision 0057: not compiled without omni lights)

		uint cluster_omni_offset = cluster_offset;

		uint item_min;
		uint item_max;
		uint item_from;
		uint item_to;

		cluster_get_item_range(cluster_omni_offset + implementation_data.max_cluster_element_count_div_32 + cluster_z, item_min, item_max, item_from, item_to);

		item_from = subgroupBroadcastFirst(subgroupMin(item_from));
		item_to = subgroupBroadcastFirst(subgroupMax(item_to));
#ifdef FELLOW_EXP_BIT12
		// Diagnostics (debug/scene_shader_experiments 4096; the image changes): no omni and spot lights, their code kept.
		if (scene_data.taa_frame_count >= 0.0) {
			item_to = item_from;
		}
#endif

#ifdef FELLOW_HAS_LIGHT_PREFETCH_NEXT
		// FellowEngine (lights/prefetch_next, decision 0046): the next light's layer mask, bake mode and range test fields
		// (2: and the other hot fields) are read while the current light is processed. The order of the lights is the
		// warp's (merged_mask), the same for every pixel of the warp; the same values reach the same expressions.
		// With 3 and 4 the next cluster word's mask and its first light are read while the current word's lights are
		// processed, too.
#ifndef FELLOW_LIGHT_PREFILTER
		bool fellow_words = sc_fellow_light_prefetch_words();
#else
		const bool fellow_words = false;
#endif
		uint fellow_nx_mask = 0u;
		uint fellow_nx_bake = 0u;
		vec3 fellow_nx_position = vec3(0.0);
		float fellow_nx_inv_radius = 0.0;
		float fellow_nx_attenuation = 0.0;
		vec3 fellow_nx_color = vec3(0.0);
		float fellow_nx_size = 0.0;
		float fellow_nx_specular_amount = 0.0;
		float fellow_nx_shadow_opacity = 0.0;
		bool fellow_nx_valid = false;
		uint fellow_wn_mask = 0u;
		uint fellow_wn_merged = 0u;
		if (fellow_words && item_from < item_to) {
			fellow_wn_mask = cluster_buffer.data[cluster_omni_offset + item_from];
			fellow_wn_mask &= cluster_get_range_clip_mask(item_from, item_min, item_max);
#ifdef FELLOW_TILE_CULL_OK
			if (sc_fellow_light_tile_cull() && fellow_wn_mask != 0u) {
				fellow_wn_mask &= fellow_tile_mask(0u, item_from);
			}
#endif
			fellow_wn_merged = subgroupBroadcastFirst(subgroupOr(fellow_wn_mask));
		}
#endif
		for (uint i = item_from; i < item_to; i++) {
#ifdef FELLOW_HAS_LIGHT_PREFETCH_NEXT
			uint mask;
			uint merged_mask;
			if (fellow_words) {
				mask = fellow_wn_mask;
				merged_mask = fellow_wn_merged;
				fellow_wn_mask = 0u;
				fellow_wn_merged = 0u;
				if (i + 1 < item_to) {
					fellow_wn_mask = cluster_buffer.data[cluster_omni_offset + i + 1];
					fellow_wn_mask &= cluster_get_range_clip_mask(i + 1, item_min, item_max);
#ifdef FELLOW_TILE_CULL_OK
					if (sc_fellow_light_tile_cull() && fellow_wn_mask != 0u) {
						fellow_wn_mask &= fellow_tile_mask(0u, i + 1);
					}
#endif
					fellow_wn_merged = subgroupBroadcastFirst(subgroupOr(fellow_wn_mask));
				}
			} else {
				mask = cluster_buffer.data[cluster_omni_offset + i];
				mask &= cluster_get_range_clip_mask(i, item_min, item_max);
#ifdef FELLOW_TILE_CULL_OK
				if (sc_fellow_light_tile_cull() && mask != 0u) {
					mask &= fellow_tile_mask(0u, i); // light tile culling (decision 0049)
				}
#endif
#ifdef FELLOW_LIGHT_PREFILTER
				mask = fellow_omni_reach(i, mask, vertex, FELLOW_INSTANCE(layer_mask), bool(fellow_flags & INSTANCE_FLAGS_USE_LIGHTMAP));
#endif
				merged_mask = subgroupBroadcastFirst(subgroupOr(mask));
			}
			if (sc_fellow_light_prefetch_next() && !fellow_nx_valid) {
				// The word's first light (with the words: unless the previous word read it; if this word has none, the
				// next word's first light).
				uint fellow_nx_index = 0u;
				bool fellow_nx_any = false;
				if (merged_mask != 0) {
					fellow_nx_index = 32 * i + findMSB(merged_mask);
					fellow_nx_any = true;
				} else if (fellow_words && fellow_wn_merged != 0) {
					fellow_nx_index = 32 * (i + 1) + findMSB(fellow_wn_merged);
					fellow_nx_any = true;
				}
				if (fellow_nx_any) {
					fellow_nx_mask = FELLOW_OMNI_HOT(fellow_nx_index, mask);
					fellow_nx_bake = FELLOW_OMNI_HOT(fellow_nx_index, bake_mode);
					fellow_nx_position = FELLOW_OMNI_HOT(fellow_nx_index, position);
					fellow_nx_inv_radius = FELLOW_OMNI_HOT(fellow_nx_index, inv_radius);
					if (sc_fellow_light_prefetch_all()) {
						fellow_nx_attenuation = FELLOW_OMNI_HOT(fellow_nx_index, attenuation);
						fellow_nx_color = FELLOW_OMNI_HOT(fellow_nx_index, color);
						fellow_nx_size = FELLOW_OMNI_HOT(fellow_nx_index, size);
						fellow_nx_specular_amount = FELLOW_OMNI_HOT(fellow_nx_index, specular_amount);
						fellow_nx_shadow_opacity = FELLOW_OMNI_HOT(fellow_nx_index, shadow_opacity);
					}
				}
				fellow_nx_valid = fellow_words && fellow_nx_any;
			}
#else
			uint mask = cluster_buffer.data[cluster_omni_offset + i];
			mask &= cluster_get_range_clip_mask(i, item_min, item_max);
#ifdef FELLOW_TILE_CULL_OK
			if (sc_fellow_light_tile_cull() && mask != 0u) {
				mask &= fellow_tile_mask(0u, i);
			}
#endif
#ifdef FELLOW_LIGHT_PREFILTER
			mask = fellow_omni_reach(i, mask, vertex, FELLOW_INSTANCE(layer_mask), bool(fellow_flags & INSTANCE_FLAGS_USE_LIGHTMAP));
#endif

			uint merged_mask = subgroupBroadcastFirst(subgroupOr(mask));
#endif
			while (merged_mask != 0) {
				uint bit = findMSB(merged_mask);
				merged_mask &= ~(1u << bit);
#ifdef FELLOW_HAS_LIGHT_PREFETCH_NEXT
				uint fellow_pf_mask = fellow_nx_mask;
				uint fellow_pf_bake = fellow_nx_bake;
				if (sc_fellow_light_prefetch_next()) {
					fellow_lpf_position = fellow_nx_position;
					fellow_lpf_inv_radius = fellow_nx_inv_radius;
					if (sc_fellow_light_prefetch_all()) {
						fellow_lpf_attenuation = fellow_nx_attenuation;
						fellow_lpf_color = fellow_nx_color;
						fellow_lpf_size = fellow_nx_size;
						fellow_lpf_specular_amount = fellow_nx_specular_amount;
						fellow_lpf_shadow_opacity = fellow_nx_shadow_opacity;
					}
					uint fellow_nx_index = 0u;
					bool fellow_nx_any = false;
					if (merged_mask != 0) {
						fellow_nx_index = 32 * i + findMSB(merged_mask);
						fellow_nx_any = true;
					} else if (fellow_words && fellow_wn_merged != 0) {
						fellow_nx_index = 32 * (i + 1) + findMSB(fellow_wn_merged); // the next word's first light
						fellow_nx_any = true;
					}
					if (fellow_nx_any) {
						fellow_nx_mask = FELLOW_OMNI_HOT(fellow_nx_index, mask);
						fellow_nx_bake = FELLOW_OMNI_HOT(fellow_nx_index, bake_mode);
						fellow_nx_position = FELLOW_OMNI_HOT(fellow_nx_index, position);
						fellow_nx_inv_radius = FELLOW_OMNI_HOT(fellow_nx_index, inv_radius);
						if (sc_fellow_light_prefetch_all()) {
							fellow_nx_attenuation = FELLOW_OMNI_HOT(fellow_nx_index, attenuation);
							fellow_nx_color = FELLOW_OMNI_HOT(fellow_nx_index, color);
							fellow_nx_size = FELLOW_OMNI_HOT(fellow_nx_index, size);
							fellow_nx_specular_amount = FELLOW_OMNI_HOT(fellow_nx_index, specular_amount);
							fellow_nx_shadow_opacity = FELLOW_OMNI_HOT(fellow_nx_index, shadow_opacity);
						}
					}
					fellow_nx_valid = fellow_words && fellow_nx_any;
				}
#endif
				FELLOW_DBG_INC(fellow_dbg_light_iter);

				if (((1u << bit) & mask) == 0) { //do not process if not originally here
					continue;
				}

				FELLOW_DBG_INC(fellow_dbg_light_own);
				uint light_index = 32 * i + bit;

#ifdef FELLOW_HAS_LIGHT_FIELDS_EARLY
				// FellowEngine (lights/fields_early, decision 0045): the light's layer mask, bake mode and the fields of its
				// tests and shading read together, before the tests (the same values).
				uint fellow_light_mask;
				uint fellow_light_bake;
#ifdef FELLOW_HAS_LIGHT_PREFETCH_NEXT
				if (sc_fellow_light_prefetch_next()) {
					// Read one light ahead (above); the other hot fields here, as without the prefetch, unless they were too.
					fellow_light_mask = fellow_pf_mask;
					fellow_light_bake = fellow_pf_bake;
					if (!sc_fellow_light_prefetch_all()) {
						fellow_lpf_attenuation = FELLOW_OMNI_HOT(light_index, attenuation);
						fellow_lpf_color = FELLOW_OMNI_HOT(light_index, color);
						fellow_lpf_size = FELLOW_OMNI_HOT(light_index, size);
						fellow_lpf_specular_amount = FELLOW_OMNI_HOT(light_index, specular_amount);
						fellow_lpf_shadow_opacity = FELLOW_OMNI_HOT(light_index, shadow_opacity);
					}
				} else
#endif
				if (sc_fellow_light_fields_early()) {
					fellow_light_mask = FELLOW_OMNI_HOT(light_index, mask);
					fellow_light_bake = FELLOW_OMNI_HOT(light_index, bake_mode);
					fellow_lpf_position = FELLOW_OMNI_HOT(light_index, position);
					fellow_lpf_inv_radius = FELLOW_OMNI_HOT(light_index, inv_radius);
					fellow_lpf_attenuation = FELLOW_OMNI_HOT(light_index, attenuation);
					fellow_lpf_color = FELLOW_OMNI_HOT(light_index, color);
					fellow_lpf_size = FELLOW_OMNI_HOT(light_index, size);
					fellow_lpf_specular_amount = FELLOW_OMNI_HOT(light_index, specular_amount);
					fellow_lpf_shadow_opacity = FELLOW_OMNI_HOT(light_index, shadow_opacity);
				} else {
					fellow_light_mask = FELLOW_OMNI_HOT(light_index, mask);
					fellow_light_bake = FELLOW_OMNI_HOT(light_index, bake_mode);
				}
#else
				uint fellow_light_mask = FELLOW_OMNI_HOT(light_index, mask);
				uint fellow_light_bake = FELLOW_OMNI_HOT(light_index, bake_mode);
#endif

				if (!bool(fellow_light_mask & FELLOW_INSTANCE(layer_mask))) {
					continue; //not masked
				}

				if (fellow_light_bake == LIGHT_BAKE_STATIC && bool(fellow_flags & INSTANCE_FLAGS_USE_LIGHTMAP)) {
					continue; // Statically baked light and object uses lightmap, skip
				}

				light_process_omni(light_index, vertex, view, normal, vertex_ddx, vertex_ddy, f0, roughness, metallic, scene_data.taa_frame_count, albedo, alpha, screen_uv, energy_compensation,
#ifdef LIGHT_BACKLIGHT_USED
						backlight,
#endif
#ifdef LIGHT_TRANSMITTANCE_USED
						transmittance_color,
						transmittance_depth,
						transmittance_boost,
#endif
#ifdef LIGHT_RIM_USED
						rim,
						rim_tint,
#endif
#ifdef LIGHT_CLEARCOAT_USED
						clearcoat, clearcoat_roughness, geo_normal,
#endif // LIGHT_CLEARCOAT_USED
#ifdef LIGHT_ANISOTROPY_USED
						binormal, tangent, anisotropy,
#endif
						diffuse_light, direct_specular_light);
			}
		}
	} else {
		// FellowEngine decision 0057: what the light loop could have changed (fellow_keep). The light sums are 0 or
		// sums when they reach the loop, never a product that a later add could fuse with, except with vertex lighting.
#ifdef USE_VERTEX_LIGHTING
		diffuse_light = fellow_keep(diffuse_light);
		direct_specular_light = fellow_keep(direct_specular_light);
#endif
#ifdef USE_SHADOW_TO_OPACITY
		alpha = fellow_keep(alpha);
#endif
	}

	if (!sc_fellow_no_spot_lights()) { //spot lights (FellowEngine decision 0057: not compiled without spot lights)

		uint cluster_spot_offset = cluster_offset + implementation_data.cluster_type_size;

		uint item_min;
		uint item_max;
		uint item_from;
		uint item_to;

		cluster_get_item_range(cluster_spot_offset + implementation_data.max_cluster_element_count_div_32 + cluster_z, item_min, item_max, item_from, item_to);

		item_from = subgroupBroadcastFirst(subgroupMin(item_from));
		item_to = subgroupBroadcastFirst(subgroupMax(item_to));
#ifdef FELLOW_EXP_BIT12
		// Diagnostics (debug/scene_shader_experiments 4096; the image changes): no omni and spot lights, their code kept.
		if (scene_data.taa_frame_count >= 0.0) {
			item_to = item_from;
		}
#endif

#ifdef FELLOW_HAS_LIGHT_PREFETCH_NEXT
		// FellowEngine (lights/prefetch_next, decision 0046): the next light's layer mask, bake mode and range test fields
		// (2: and the other hot fields) are read while the current light is processed. The order of the lights is the
		// warp's (merged_mask), the same for every pixel of the warp; the same values reach the same expressions.
		// With 3 and 4 the next cluster word's mask and its first light are read while the current word's lights are
		// processed, too.
#ifndef FELLOW_LIGHT_PREFILTER
		bool fellow_words = sc_fellow_light_prefetch_words();
#else
		const bool fellow_words = false;
#endif
		uint fellow_nx_mask = 0u;
		uint fellow_nx_bake = 0u;
		vec3 fellow_nx_position = vec3(0.0);
		float fellow_nx_inv_radius = 0.0;
		vec3 fellow_nx_direction = vec3(0.0);
		float fellow_nx_cone_angle = 0.0;
		float fellow_nx_attenuation = 0.0;
		vec3 fellow_nx_color = vec3(0.0);
		float fellow_nx_size = 0.0;
		float fellow_nx_specular_amount = 0.0;
		float fellow_nx_shadow_opacity = 0.0;
		float fellow_nx_cone_attenuation = 0.0;
		bool fellow_nx_valid = false;
		uint fellow_wn_mask = 0u;
		uint fellow_wn_merged = 0u;
		if (fellow_words && item_from < item_to) {
			fellow_wn_mask = cluster_buffer.data[cluster_spot_offset + item_from];
			fellow_wn_mask &= cluster_get_range_clip_mask(item_from, item_min, item_max);
#ifdef FELLOW_TILE_CULL_OK
			if (sc_fellow_light_tile_cull() && fellow_wn_mask != 0u) {
				fellow_wn_mask &= fellow_tile_mask(1u, item_from);
			}
#endif
			fellow_wn_merged = subgroupBroadcastFirst(subgroupOr(fellow_wn_mask));
		}
#endif
		for (uint i = item_from; i < item_to; i++) {
#ifdef FELLOW_HAS_LIGHT_PREFETCH_NEXT
			uint mask;
			uint merged_mask;
			if (fellow_words) {
				mask = fellow_wn_mask;
				merged_mask = fellow_wn_merged;
				fellow_wn_mask = 0u;
				fellow_wn_merged = 0u;
				if (i + 1 < item_to) {
					fellow_wn_mask = cluster_buffer.data[cluster_spot_offset + i + 1];
					fellow_wn_mask &= cluster_get_range_clip_mask(i + 1, item_min, item_max);
#ifdef FELLOW_TILE_CULL_OK
					if (sc_fellow_light_tile_cull() && fellow_wn_mask != 0u) {
						fellow_wn_mask &= fellow_tile_mask(1u, i + 1);
					}
#endif
					fellow_wn_merged = subgroupBroadcastFirst(subgroupOr(fellow_wn_mask));
				}
			} else {
				mask = cluster_buffer.data[cluster_spot_offset + i];
				mask &= cluster_get_range_clip_mask(i, item_min, item_max);
#ifdef FELLOW_TILE_CULL_OK
				if (sc_fellow_light_tile_cull() && mask != 0u) {
					mask &= fellow_tile_mask(1u, i); // light tile culling (decision 0049)
				}
#endif
#ifdef FELLOW_LIGHT_PREFILTER
				mask = fellow_spot_reach(i, mask, vertex, FELLOW_INSTANCE(layer_mask), bool(fellow_flags & INSTANCE_FLAGS_USE_LIGHTMAP));
#endif
				merged_mask = subgroupBroadcastFirst(subgroupOr(mask));
			}
			if (sc_fellow_light_prefetch_next() && !fellow_nx_valid) {
				// The word's first light (with the words: unless the previous word read it; if this word has none, the
				// next word's first light).
				uint fellow_nx_index = 0u;
				bool fellow_nx_any = false;
				if (merged_mask != 0) {
					fellow_nx_index = 32 * i + findMSB(merged_mask);
					fellow_nx_any = true;
				} else if (fellow_words && fellow_wn_merged != 0) {
					fellow_nx_index = 32 * (i + 1) + findMSB(fellow_wn_merged);
					fellow_nx_any = true;
				}
				if (fellow_nx_any) {
					fellow_nx_mask = FELLOW_SPOT_HOT(fellow_nx_index, mask);
					fellow_nx_bake = FELLOW_SPOT_HOT(fellow_nx_index, bake_mode);
					fellow_nx_position = FELLOW_SPOT_HOT(fellow_nx_index, position);
					fellow_nx_inv_radius = FELLOW_SPOT_HOT(fellow_nx_index, inv_radius);
					fellow_nx_direction = FELLOW_SPOT_HOT(fellow_nx_index, direction);
					fellow_nx_cone_angle = FELLOW_SPOT_HOT(fellow_nx_index, cone_angle);
					if (sc_fellow_light_prefetch_all()) {
						fellow_nx_attenuation = FELLOW_SPOT_HOT(fellow_nx_index, attenuation);
						fellow_nx_color = FELLOW_SPOT_HOT(fellow_nx_index, color);
						fellow_nx_size = FELLOW_SPOT_HOT(fellow_nx_index, size);
						fellow_nx_specular_amount = FELLOW_SPOT_HOT(fellow_nx_index, specular_amount);
						fellow_nx_shadow_opacity = FELLOW_SPOT_HOT(fellow_nx_index, shadow_opacity);
						fellow_nx_cone_attenuation = FELLOW_SPOT_HOT(fellow_nx_index, cone_attenuation);
					}
				}
				fellow_nx_valid = fellow_words && fellow_nx_any;
			}
#else
			uint mask = cluster_buffer.data[cluster_spot_offset + i];
			mask &= cluster_get_range_clip_mask(i, item_min, item_max);
#ifdef FELLOW_TILE_CULL_OK
			if (sc_fellow_light_tile_cull() && mask != 0u) {
				mask &= fellow_tile_mask(1u, i);
			}
#endif
#ifdef FELLOW_LIGHT_PREFILTER
			mask = fellow_spot_reach(i, mask, vertex, FELLOW_INSTANCE(layer_mask), bool(fellow_flags & INSTANCE_FLAGS_USE_LIGHTMAP));
#endif

			uint merged_mask = subgroupBroadcastFirst(subgroupOr(mask));
#endif
			while (merged_mask != 0) {
				uint bit = findMSB(merged_mask);
				merged_mask &= ~(1u << bit);
#ifdef FELLOW_HAS_LIGHT_PREFETCH_NEXT
				uint fellow_pf_mask = fellow_nx_mask;
				uint fellow_pf_bake = fellow_nx_bake;
				if (sc_fellow_light_prefetch_next()) {
					fellow_lpf_position = fellow_nx_position;
					fellow_lpf_inv_radius = fellow_nx_inv_radius;
					fellow_lpf_direction = fellow_nx_direction;
					fellow_lpf_cone_angle = fellow_nx_cone_angle;
					if (sc_fellow_light_prefetch_all()) {
						fellow_lpf_attenuation = fellow_nx_attenuation;
						fellow_lpf_color = fellow_nx_color;
						fellow_lpf_size = fellow_nx_size;
						fellow_lpf_specular_amount = fellow_nx_specular_amount;
						fellow_lpf_shadow_opacity = fellow_nx_shadow_opacity;
						fellow_lpf_cone_attenuation = fellow_nx_cone_attenuation;
					}
					uint fellow_nx_index = 0u;
					bool fellow_nx_any = false;
					if (merged_mask != 0) {
						fellow_nx_index = 32 * i + findMSB(merged_mask);
						fellow_nx_any = true;
					} else if (fellow_words && fellow_wn_merged != 0) {
						fellow_nx_index = 32 * (i + 1) + findMSB(fellow_wn_merged); // the next word's first light
						fellow_nx_any = true;
					}
					if (fellow_nx_any) {
						fellow_nx_mask = FELLOW_SPOT_HOT(fellow_nx_index, mask);
						fellow_nx_bake = FELLOW_SPOT_HOT(fellow_nx_index, bake_mode);
						fellow_nx_position = FELLOW_SPOT_HOT(fellow_nx_index, position);
						fellow_nx_inv_radius = FELLOW_SPOT_HOT(fellow_nx_index, inv_radius);
						fellow_nx_direction = FELLOW_SPOT_HOT(fellow_nx_index, direction);
						fellow_nx_cone_angle = FELLOW_SPOT_HOT(fellow_nx_index, cone_angle);
						if (sc_fellow_light_prefetch_all()) {
							fellow_nx_attenuation = FELLOW_SPOT_HOT(fellow_nx_index, attenuation);
							fellow_nx_color = FELLOW_SPOT_HOT(fellow_nx_index, color);
							fellow_nx_size = FELLOW_SPOT_HOT(fellow_nx_index, size);
							fellow_nx_specular_amount = FELLOW_SPOT_HOT(fellow_nx_index, specular_amount);
							fellow_nx_shadow_opacity = FELLOW_SPOT_HOT(fellow_nx_index, shadow_opacity);
							fellow_nx_cone_attenuation = FELLOW_SPOT_HOT(fellow_nx_index, cone_attenuation);
						}
					}
					fellow_nx_valid = fellow_words && fellow_nx_any;
				}
#endif
				FELLOW_DBG_INC(fellow_dbg_light_iter);

				if (((1u << bit) & mask) == 0) { //do not process if not originally here
					continue;
				}

				FELLOW_DBG_INC(fellow_dbg_light_own);
				uint light_index = 32 * i + bit;

#ifdef FELLOW_HAS_LIGHT_FIELDS_EARLY
				// FellowEngine (lights/fields_early, decision 0045): the light's layer mask, bake mode and the fields of its
				// tests and shading read together, before the tests (the same values).
				uint fellow_light_mask;
				uint fellow_light_bake;
#ifdef FELLOW_HAS_LIGHT_PREFETCH_NEXT
				if (sc_fellow_light_prefetch_next()) {
					// Read one light ahead (above); the other hot fields here, as without the prefetch, unless they were too.
					fellow_light_mask = fellow_pf_mask;
					fellow_light_bake = fellow_pf_bake;
					if (!sc_fellow_light_prefetch_all()) {
						fellow_lpf_attenuation = FELLOW_SPOT_HOT(light_index, attenuation);
						fellow_lpf_color = FELLOW_SPOT_HOT(light_index, color);
						fellow_lpf_size = FELLOW_SPOT_HOT(light_index, size);
						fellow_lpf_specular_amount = FELLOW_SPOT_HOT(light_index, specular_amount);
						fellow_lpf_shadow_opacity = FELLOW_SPOT_HOT(light_index, shadow_opacity);
						fellow_lpf_cone_attenuation = FELLOW_SPOT_HOT(light_index, cone_attenuation);
					}
				} else
#endif
				if (sc_fellow_light_fields_early()) {
					fellow_light_mask = FELLOW_SPOT_HOT(light_index, mask);
					fellow_light_bake = FELLOW_SPOT_HOT(light_index, bake_mode);
					fellow_lpf_position = FELLOW_SPOT_HOT(light_index, position);
					fellow_lpf_inv_radius = FELLOW_SPOT_HOT(light_index, inv_radius);
					fellow_lpf_attenuation = FELLOW_SPOT_HOT(light_index, attenuation);
					fellow_lpf_color = FELLOW_SPOT_HOT(light_index, color);
					fellow_lpf_size = FELLOW_SPOT_HOT(light_index, size);
					fellow_lpf_specular_amount = FELLOW_SPOT_HOT(light_index, specular_amount);
					fellow_lpf_shadow_opacity = FELLOW_SPOT_HOT(light_index, shadow_opacity);
					fellow_lpf_direction = FELLOW_SPOT_HOT(light_index, direction);
					fellow_lpf_cone_angle = FELLOW_SPOT_HOT(light_index, cone_angle);
					fellow_lpf_cone_attenuation = FELLOW_SPOT_HOT(light_index, cone_attenuation);
				} else {
					fellow_light_mask = FELLOW_SPOT_HOT(light_index, mask);
					fellow_light_bake = FELLOW_SPOT_HOT(light_index, bake_mode);
				}
#else
				uint fellow_light_mask = FELLOW_SPOT_HOT(light_index, mask);
				uint fellow_light_bake = FELLOW_SPOT_HOT(light_index, bake_mode);
#endif

				if (!bool(fellow_light_mask & FELLOW_INSTANCE(layer_mask))) {
					continue; //not masked
				}

				if (fellow_light_bake == LIGHT_BAKE_STATIC && bool(fellow_flags & INSTANCE_FLAGS_USE_LIGHTMAP)) {
					continue; // Statically baked light and object uses lightmap, skip
				}

				light_process_spot(light_index, vertex, view, normal, vertex_ddx, vertex_ddy, f0, roughness, metallic, scene_data.taa_frame_count, albedo, alpha, screen_uv, energy_compensation,
#ifdef LIGHT_BACKLIGHT_USED
						backlight,
#endif
#ifdef LIGHT_TRANSMITTANCE_USED
						transmittance_color,
						transmittance_depth,
						transmittance_boost,
#endif
#ifdef LIGHT_RIM_USED
						rim,
						rim_tint,
#endif
#ifdef LIGHT_CLEARCOAT_USED
						clearcoat, clearcoat_roughness, geo_normal,
#endif // LIGHT_CLEARCOAT_USED
#ifdef LIGHT_ANISOTROPY_USED
						binormal, tangent, anisotropy,
#endif
						diffuse_light, direct_specular_light);
			}
		}
	} else {
		// FellowEngine decision 0057: what the light loop could have changed (fellow_keep). The light sums are 0 or
		// sums when they reach the loop, never a product that a later add could fuse with, except with vertex lighting.
#ifdef USE_VERTEX_LIGHTING
		diffuse_light = fellow_keep(diffuse_light);
		direct_specular_light = fellow_keep(direct_specular_light);
#endif
#ifdef USE_SHADOW_TO_OPACITY
		alpha = fellow_keep(alpha);
#endif
	}

	if (sc_cluster_has_area_light()) { // area lights

		uint cluster_area_offset = cluster_offset + implementation_data.cluster_type_size * 2;

		uint item_min;
		uint item_max;
		uint item_from;
		uint item_to;

		cluster_get_item_range(cluster_area_offset + implementation_data.max_cluster_element_count_div_32 + cluster_z, item_min, item_max, item_from, item_to);

		item_from = subgroupBroadcastFirst(subgroupMin(item_from));
		item_to = subgroupBroadcastFirst(subgroupMax(item_to));

		for (uint i = item_from; i < item_to; i++) {
			uint mask = cluster_buffer.data[cluster_area_offset + i];
			mask &= cluster_get_range_clip_mask(i, item_min, item_max);

			uint merged_mask = subgroupBroadcastFirst(subgroupOr(mask));
			while (merged_mask != 0) {
				uint bit = findMSB(merged_mask);
				merged_mask &= ~(1u << bit);

				if (((1u << bit) & mask) == 0) { //do not process if not originally here
					continue;
				}

				uint light_index = 32 * i + bit;

				if (!bool(area_lights.data[light_index].mask & FELLOW_INSTANCE(layer_mask))) {
					continue; //not masked
				}

				if (area_lights.data[light_index].bake_mode == LIGHT_BAKE_STATIC && bool(fellow_flags & INSTANCE_FLAGS_USE_LIGHTMAP)) {
					continue; // Statically baked light and object uses lightmap, skip
				}

				light_process_area(light_index, vertex, view, normal, vertex_ddx, vertex_ddy, f0, roughness, metallic, scene_data.taa_frame_count, albedo, alpha, screen_uv, energy_compensation,
#ifdef LIGHT_BACKLIGHT_USED
						backlight,
#endif
#ifdef LIGHT_TRANSMITTANCE_USED
						transmittance_color,
						transmittance_depth,
						transmittance_boost,
#endif
#ifdef LIGHT_RIM_USED
						rim,
						rim_tint,
#endif
#ifdef LIGHT_CLEARCOAT_USED
						clearcoat, clearcoat_roughness, geo_normal,
#endif
#ifdef LIGHT_ANISOTROPY_USED
						binormal, tangent, anisotropy,
#endif
						diffuse_light, direct_specular_light);
			}
		}
	}
#endif // !USE_VERTEX_LIGHTING
#endif //!defined(MODE_RENDER_DEPTH) && !defined(MODE_UNSHADED)

#ifdef USE_SHADOW_TO_OPACITY
#ifndef MODE_RENDER_DEPTH
	alpha = min(alpha, clamp(length(ambient_light), 0.0, 1.0));

#if defined(ALPHA_SCISSOR_USED)
#ifdef MODE_RENDER_MATERIAL
	if (alpha < alpha_scissor_threshold) {
		alpha = 0.0;
	} else {
		alpha = 1.0;
	}
#else
	if (alpha < alpha_scissor_threshold) {
		discard;
	}
#endif // MODE_RENDER_MATERIAL
#endif // ALPHA_SCISSOR_USED

#endif // !MODE_RENDER_DEPTH
#endif // USE_SHADOW_TO_OPACITY

#ifdef MODE_RENDER_DEPTH

#ifdef MODE_RENDER_SDF

	{
		vec3 local_pos = (implementation_data.sdf_to_bounds * vec4(vertex, 1.0)).xyz;
		ivec3 grid_pos = implementation_data.sdf_offset + ivec3(local_pos * vec3(implementation_data.sdf_size));

		uint albedo16 = 0x1; //solid flag
		albedo16 |= clamp(uint(albedo.r * 31.0), 0, 31) << 11;
		albedo16 |= clamp(uint(albedo.g * 31.0), 0, 31) << 6;
		albedo16 |= clamp(uint(albedo.b * 31.0), 0, 31) << 1;

		imageStore(albedo_volume_grid, grid_pos, uvec4(albedo16));

		uint facing_bits = 0;
		const vec3 aniso_dir[6] = vec3[](
				vec3(1, 0, 0),
				vec3(0, 1, 0),
				vec3(0, 0, 1),
				vec3(-1, 0, 0),
				vec3(0, -1, 0),
				vec3(0, 0, -1));

		vec3 cam_normal = mat3(inv_view_matrix) * normalize(normal_interp);

		float closest_dist = -1e20;

		for (uint i = 0; i < 6; i++) {
			float d = dot(cam_normal, aniso_dir[i]);
			if (d > closest_dist) {
				closest_dist = d;
				facing_bits = (1 << i);
			}
		}

#ifdef NO_IMAGE_ATOMICS
		imageStore(geom_facing_grid, grid_pos, uvec4(imageLoad(geom_facing_grid, grid_pos).r | facing_bits)); //store facing bits
#else
		imageAtomicOr(geom_facing_grid, grid_pos, facing_bits); //store facing bits
#endif

		if (length(emission) > 0.001) {
			float lumas[6];
			vec3 light_total = vec3(0);

			for (int i = 0; i < 6; i++) {
				float strength = max(0.0, dot(cam_normal, aniso_dir[i]));
				vec3 light = emission * strength;
				light_total += light;
				lumas[i] = max(light.r, max(light.g, light.b));
			}

			float luma_total = max(light_total.r, max(light_total.g, light_total.b));

			uint light_aniso = 0;

			for (int i = 0; i < 6; i++) {
				light_aniso |= min(31, uint((lumas[i] / luma_total) * 31.0)) << (i * 5);
			}

			//compress to RGBE9995 to save space

			const float pow2to9 = 512.0f;
			const float B = 15.0f;
			const float N = 9.0f;
			const float LN2 = 0.6931471805599453094172321215;

			float cRed = clamp(light_total.r, 0.0, 65408.0);
			float cGreen = clamp(light_total.g, 0.0, 65408.0);
			float cBlue = clamp(light_total.b, 0.0, 65408.0);

			float cMax = max(cRed, max(cGreen, cBlue));

			float expp = max(-B - 1.0f, floor(log(cMax) / LN2)) + 1.0f + B;

			float sMax = floor((cMax / pow(2.0f, expp - B - N)) + 0.5f);

			float exps = expp + 1.0f;

			if (0.0 <= sMax && sMax < pow2to9) {
				exps = expp;
			}

			float sRed = floor((cRed / pow(2.0f, exps - B - N)) + 0.5f);
			float sGreen = floor((cGreen / pow(2.0f, exps - B - N)) + 0.5f);
			float sBlue = floor((cBlue / pow(2.0f, exps - B - N)) + 0.5f);
			//store as 8985 to have 2 extra neighbor bits
			uint light_rgbe = ((uint(sRed) & 0x1FFu) >> 1) | ((uint(sGreen) & 0x1FFu) << 8) | (((uint(sBlue) & 0x1FFu) >> 1) << 17) | ((uint(exps) & 0x1Fu) << 25);

			imageStore(emission_grid, grid_pos, uvec4(light_rgbe));
			imageStore(emission_aniso_grid, grid_pos, uvec4(light_aniso));
		}
	}

#endif

#ifdef MODE_RENDER_MATERIAL

	albedo_output_buffer.rgb = albedo;
	albedo_output_buffer.a = alpha;

	normal_output_buffer.rgb = encode24(normal) * 0.5 + 0.5;
	normal_output_buffer.a = 0.0;
	depth_output_buffer.r = -vertex.z;

	orm_output_buffer.r = ao;
	orm_output_buffer.g = roughness;
	orm_output_buffer.b = metallic;
	orm_output_buffer.a = sss_strength;

	emission_output_buffer.rgb = emission;
	emission_output_buffer.a = 0.0;
#endif

#ifdef MODE_RENDER_NORMAL_ROUGHNESS
	normal_roughness_output_buffer = vec4(encode24(normal) * 0.5 + 0.5, roughness);

	// We encode the dynamic static into roughness.
	// Values over 0.5 are dynamic, under 0.5 are static.
	normal_roughness_output_buffer.w = normal_roughness_output_buffer.w * (127.0 / 255.0);
	if (bool(fellow_flags & INSTANCE_FLAGS_DYNAMIC)) {
		normal_roughness_output_buffer.w = 1.0 - normal_roughness_output_buffer.w;
	}
	normal_roughness_output_buffer.w = normal_roughness_output_buffer.w;

#ifdef MODE_RENDER_VOXEL_GI
	if (bool(fellow_flags & INSTANCE_FLAGS_USE_VOXEL_GI)) { // process voxel_gi_instances
		uint index1 = fellow_gi_offset & 0xFFFF;
		uint index2 = fellow_gi_offset >> 16;
		voxel_gi_buffer.x = index1 & 0xFFu;
		voxel_gi_buffer.y = index2 & 0xFFu;
	} else {
		voxel_gi_buffer.x = 0xFF;
		voxel_gi_buffer.y = 0xFF;
	}
#endif

#endif //MODE_RENDER_NORMAL_ROUGHNESS

//nothing happens, so a tree-ssa optimizer will result in no fragment shader :)
#else

	// multiply by albedo
	diffuse_light *= albedo; // ambient must be multiplied by albedo at the end

	// apply direct light AO
	diffuse_light *= ao;
	direct_specular_light *= ao;

	// apply metallic
	diffuse_light *= 1.0 - metallic;
	ambient_light *= 1.0 - metallic;

#ifndef FOG_DISABLED
	//restore fog
	fog = vec4(unpackHalf2x16(fog_rg), unpackHalf2x16(fog_ba));
#endif //!FOG_DISABLED

#ifdef MODE_SEPARATE_SPECULAR

#ifdef MODE_UNSHADED
	diffuse_buffer = vec4(albedo.rgb, 0.0);
	specular_buffer = vec4(0.0);

#else

#ifdef SSS_MODE_SKIN
	sss_strength = -sss_strength;
#endif
	diffuse_buffer = vec4(emission + diffuse_light + ambient_light, sss_strength);
	specular_buffer = vec4(direct_specular_light + indirect_specular_light, metallic);
#endif

#ifndef FOG_DISABLED
	diffuse_buffer.rgb = diffuse_buffer.rgb * fog.a + fog.rgb;
	specular_buffer.rgb = specular_buffer.rgb * fog.a;
#endif //!FOG_DISABLED

#if defined(FELLOW_EXP_BIT3) && !defined(MODE_UNSHADED)
	diffuse_buffer.rgb = fellow_dbg_color();
	specular_buffer.rgb = vec3(0.0);
#endif

#else //MODE_SEPARATE_SPECULAR

	alpha *= scene_data.pass_alpha_multiplier;

#ifdef MODE_UNSHADED
	frag_color = vec4(albedo, alpha);
#else
	frag_color = vec4(emission + ambient_light + diffuse_light + direct_specular_light + indirect_specular_light, alpha);
//frag_color = vec4(1.0);
#endif //USE_NO_SHADING

#ifndef FOG_DISABLED
	frag_color.rgb = frag_color.rgb * fog.a + fog.rgb;
#endif //!FOG_DISABLED

#if defined(FELLOW_EXP_BIT3) && !defined(MODE_UNSHADED)
	// Diagnostics (see fellow_dbg_color()).
	frag_color.rgb = fellow_dbg_color();
#endif

#if defined(PREMUL_ALPHA_USED) && !defined(MODE_RENDER_DEPTH)
	frag_color.rgb *= premul_alpha;
#endif //PREMUL_ALPHA_USED

#endif //MODE_SEPARATE_SPECULAR

#endif //MODE_RENDER_DEPTH
#ifdef MOTION_VECTORS
	vec2 position_clip = (screen_position.xy / screen_position.w) - scene_data.taa_jitter;
	vec2 prev_position_clip = (prev_screen_position.xy / prev_screen_position.w) - scene_data_block.prev_data.taa_jitter;

	vec2 position_uv = position_clip * vec2(0.5, 0.5);
	vec2 prev_position_uv = prev_position_clip * vec2(0.5, 0.5);

	motion_vector = prev_position_uv - position_uv;
#endif
}

void main() {
#ifdef UBERSHADER
	bool front_facing = gl_FrontFacing;
	if (uc_cull_mode() == POLYGON_CULL_BACK && !front_facing) {
		discard;
	} else if (uc_cull_mode() == POLYGON_CULL_FRONT && front_facing) {
		discard;
	}
#endif
#ifdef MODE_DUAL_PARABOLOID

	if (dp_clip > 0.0) {
		discard;
	}
#endif

	fragment_shader(scene_data_block.data);
}
