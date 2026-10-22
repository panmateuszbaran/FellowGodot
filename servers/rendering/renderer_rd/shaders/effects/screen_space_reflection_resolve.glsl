#[compute]

#version 450

#VERSION_DEFINES

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

layout(set = 0, binding = 0) uniform sampler2D source_depth;
layout(set = 0, binding = 1) uniform sampler2D source_normal_roughness;
layout(set = 0, binding = 2) uniform sampler2D source_depth_half;
layout(set = 0, binding = 3) uniform sampler2D source_normal_roughness_half;
layout(set = 0, binding = 4) uniform sampler2D source_color;
layout(set = 0, binding = 5) uniform sampler2D source_mip_level;
layout(rgba16f, set = 0, binding = 6) uniform restrict writeonly image2D output_color;

layout(push_constant, std430) uniform Params {
	ivec2 screen_size;
	ivec2 pad;
}
params;

void get_sample(float depth, vec3 normal, float roughness, ivec2 pixel_pos, out vec4 color, out float weight) {
	float sample_depth = texelFetch(source_depth_half, pixel_pos, 0).x;
	vec4 sample_normal_roughness = texelFetch(source_normal_roughness_half, pixel_pos, 0);
	vec3 sample_normal = normalize(sample_normal_roughness.xyz * 2.0 - 1.0);
	float sample_roughness = sample_normal_roughness.w;
	if (sample_roughness > 0.5) {
		sample_roughness = 1.0 - sample_roughness;
	}
	sample_roughness /= (127.0 / 255.0);

	vec2 uv = (pixel_pos + 0.5) / (params.screen_size * 0.5);

	float mip_level = texelFetch(source_mip_level, pixel_pos, 0).x * 14.0;
	color = textureLod(source_color, uv, mip_level);

	// Invert the tone mapping we applied in the main trace pass.
	const vec3 rec709_luminance_weights = vec3(0.2126, 0.7152, 0.0722);
	color.rgb /= 1.0 - dot(color.rgb, rec709_luminance_weights);

	const float DEPTH_FACTOR = 2048.0;
	const float NORMAL_FACTOR = 32.0;
	const float ROUGHNESS_FACTOR = 16.0;

	float depth_diff = abs(depth - sample_depth);
	float weight_depth = exp(-depth_diff * DEPTH_FACTOR);

	float normal_diff = clamp(1.0 - dot(normal, sample_normal), 0.0, 1.0);
	float weight_normal = exp(-normal_diff * NORMAL_FACTOR);

	float roughness_diff = abs(roughness - sample_roughness);
	float weight_roughness = exp(-roughness_diff * ROUGHNESS_FACTOR);

	weight = weight_depth * weight_normal * weight_roughness;
}

// FellowEngine (postfx/ssr_resolve_shared, decision 0074, params.pad.x != 0): the texture reads of the half-resolution
// samples of a workgroup's 8 x 8 pixels (a 6 x 6 tile) are made once, into shared memory, instead of once by each of the
// up to four pixels that use them. Only the reads are shared: every pixel computes from the values read exactly what
// get_sample() computes from them (sharing the computed values too gave other roundings on the RTX, m15_f218).
shared float fellow_tile_depth[6][6];
shared vec4 fellow_tile_normal_roughness[6][6];
shared vec4 fellow_tile_color[6][6];

void fellow_get_sample(float depth, vec3 normal, float roughness, ivec2 t, out vec4 color, out float weight) {
	// get_sample() with its three texelFetch() and its textureLod() replaced by the tile's values.
	float sample_depth = fellow_tile_depth[t.y][t.x];
	vec4 sample_normal_roughness = fellow_tile_normal_roughness[t.y][t.x];
	vec3 sample_normal = normalize(sample_normal_roughness.xyz * 2.0 - 1.0);
	float sample_roughness = sample_normal_roughness.w;
	if (sample_roughness > 0.5) {
		sample_roughness = 1.0 - sample_roughness;
	}
	sample_roughness /= (127.0 / 255.0);

	color = fellow_tile_color[t.y][t.x];

	// Invert the tone mapping we applied in the main trace pass.
	const vec3 rec709_luminance_weights = vec3(0.2126, 0.7152, 0.0722);
	color.rgb /= 1.0 - dot(color.rgb, rec709_luminance_weights);

	const float DEPTH_FACTOR = 2048.0;
	const float NORMAL_FACTOR = 32.0;
	const float ROUGHNESS_FACTOR = 16.0;

	float depth_diff = abs(depth - sample_depth);
	float weight_depth = exp(-depth_diff * DEPTH_FACTOR);

	float normal_diff = clamp(1.0 - dot(normal, sample_normal), 0.0, 1.0);
	float weight_normal = exp(-normal_diff * NORMAL_FACTOR);

	float roughness_diff = abs(roughness - sample_roughness);
	float weight_roughness = exp(-roughness_diff * ROUGHNESS_FACTOR);

	weight = weight_depth * weight_normal * weight_roughness;
}

void main() {
	ivec2 pixel_pos = ivec2(gl_GlobalInvocationID.xy);

	if (params.pad.x != 0) {
		// The tile's first sample: the smallest (pixel_pos - 1) / 2 of the group (integer division as below).
		const ivec2 group_first = ivec2(gl_WorkGroupID.xy) * 8;
		const ivec2 tile_origin = (group_first - 1) / 2;
		const uint index = gl_LocalInvocationIndex;
		if (index < 36u) {
			const ivec2 t = ivec2(int(index % 6u), int(index / 6u));
			const ivec2 sample_pos = tile_origin + t;
			// get_sample()'s reads, with its coordinates and level.
			fellow_tile_depth[t.y][t.x] = texelFetch(source_depth_half, sample_pos, 0).x;
			fellow_tile_normal_roughness[t.y][t.x] = texelFetch(source_normal_roughness_half, sample_pos, 0);
			vec2 uv = (sample_pos + 0.5) / (params.screen_size * 0.5);
			float mip_level = texelFetch(source_mip_level, sample_pos, 0).x * 14.0;
			fellow_tile_color[t.y][t.x] = textureLod(source_color, uv, mip_level);
		}
		barrier();

		if (any(greaterThanEqual(pixel_pos, params.screen_size))) {
			return;
		}

		float depth = texelFetch(source_depth, pixel_pos, 0).x;
		vec4 normal_roughness = texelFetch(source_normal_roughness, pixel_pos, 0);
		vec3 normal = normalize(normal_roughness.xyz * 2.0 - 1.0);
		float roughness = normal_roughness.w;
		if (roughness > 0.5) {
			roughness = 1.0 - roughness;
		}
		roughness /= (127.0 / 255.0);

		vec2 half_tex_coord = (pixel_pos + 0.5) * 0.5;
		vec2 bilinear_weights = fract(half_tex_coord);

		vec4 color0, color1, color2, color3;
		float weight0, weight1, weight2, weight3;

		const ivec2 t0 = ((pixel_pos - 1) / 2) - tile_origin;
		fellow_get_sample(depth, normal, roughness, t0 + ivec2(0, 0), color0, weight0);
		fellow_get_sample(depth, normal, roughness, t0 + ivec2(1, 0), color1, weight1);
		fellow_get_sample(depth, normal, roughness, t0 + ivec2(0, 1), color2, weight2);
		fellow_get_sample(depth, normal, roughness, t0 + ivec2(1, 1), color3, weight3);

		weight0 *= bilinear_weights.x * bilinear_weights.y;
		weight1 *= (1.0 - bilinear_weights.x) * bilinear_weights.y;
		weight2 *= bilinear_weights.x * (1.0 - bilinear_weights.y);
		weight3 *= (1.0 - bilinear_weights.x) * (1.0 - bilinear_weights.y);

		vec4 result_color = color0 * weight0 + color1 * weight1 + color2 * weight2 + color3 * weight3;
		float result_weight = weight0 + weight1 + weight2 + weight3;
		if (result_weight > 0.0) {
			result_color /= result_weight;
		} else {
			result_color = vec4(0.0);
		}

		imageStore(output_color, pixel_pos, result_color);
		return;
	}

	if (any(greaterThanEqual(pixel_pos, params.screen_size))) {
		return;
	}

	float depth = texelFetch(source_depth, pixel_pos, 0).x;
	vec4 normal_roughness = texelFetch(source_normal_roughness, pixel_pos, 0);
	vec3 normal = normalize(normal_roughness.xyz * 2.0 - 1.0);
	float roughness = normal_roughness.w;
	if (roughness > 0.5) {
		roughness = 1.0 - roughness;
	}
	roughness /= (127.0 / 255.0);

	vec2 half_tex_coord = (pixel_pos + 0.5) * 0.5;
	vec2 bilinear_weights = fract(half_tex_coord);

	vec4 color0, color1, color2, color3;
	float weight0, weight1, weight2, weight3;

	get_sample(depth, normal, roughness, ((pixel_pos - 1) / 2) + ivec2(0, 0), color0, weight0);
	get_sample(depth, normal, roughness, ((pixel_pos - 1) / 2) + ivec2(1, 0), color1, weight1);
	get_sample(depth, normal, roughness, ((pixel_pos - 1) / 2) + ivec2(0, 1), color2, weight2);
	get_sample(depth, normal, roughness, ((pixel_pos - 1) / 2) + ivec2(1, 1), color3, weight3);

	weight0 *= bilinear_weights.x * bilinear_weights.y;
	weight1 *= (1.0 - bilinear_weights.x) * bilinear_weights.y;
	weight2 *= bilinear_weights.x * (1.0 - bilinear_weights.y);
	weight3 *= (1.0 - bilinear_weights.x) * (1.0 - bilinear_weights.y);

	vec4 result_color = color0 * weight0 + color1 * weight1 + color2 * weight2 + color3 * weight3;
	float result_weight = weight0 + weight1 + weight2 + weight3;
	if (result_weight > 0.0) {
		result_color /= result_weight;
	} else {
		result_color = vec4(0.0);
	}

	imageStore(output_color, pixel_pos, result_color);
}
