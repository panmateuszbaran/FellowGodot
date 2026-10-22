#[compute]

#version 450

#VERSION_DEFINES

// FellowEngine motion blur (docs/decisions/0078-motion-blur.md): the reconstruction filter of McGuire et al., "A
// Reconstruction Filter for Plausible Motion Blur" (I3D 2012), with Jimenez's refinements (SIGGRAPH 2014 "Next
// Generation Post Processing in Call of Duty: Advanced Warfare"): the largest blur of each tile and of its neighbours
// bounds where a pixel can gather from; the gather weighs each sample by which of the two surfaces is in front and
// whether its blur reaches the pixel.
//
// Blur vectors are in pixels and are half extents: a pixel moving by m pixels during the exposure smears m / 2 to
// each side. The renderer's velocity buffer holds, per pixel, the UV offset from this frame to the previous one; the
// motion during the exposure is -velocity * resolution * amount (amount: the fraction of the frame the shutter is open).
// Where nothing was drawn (the cleared depth: the sky, the background) the velocity buffer holds 0; there the motion is
// the camera's, reprojected as the renderer does for static surfaces (motion_vector_inc.glsl), so a turning camera blurs
// the sky too.

#include "motion_vector_inc.glsl"

#define TILE_SIZE 32

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

layout(push_constant, std430) uniform Params {
	mat4 sky_reprojection; // this frame's NDC (unjittered) to the previous frame's, for the background
	ivec2 size; // pixels
	ivec2 tile_count;
	vec2 velocity_scale; // UV offset to half-extent pixels: -0.5 * amount * resolution
	float max_radius; // pixels (at most TILE_SIZE)
	int sample_count;
	float z_near;
	float z_far;
	int orthogonal;
	float soft_z; // relative depth over which the front/back test fades
}
params;

// The blur vector (half extent, pixels, at most max_radius) of the pixel at p_pos with depth p_depth.
vec2 blur_vector_at(sampler2D p_velocity_buffer, ivec2 p_pos, float p_depth) {
	vec2 velocity;
	if (p_depth == 0.0) {
		velocity = derive_motion_vector((vec2(p_pos) + 0.5) / vec2(params.size), 0.0, params.sky_reprojection);
	} else {
		velocity = texelFetch(p_velocity_buffer, p_pos, 0).xy;
	}
	vec2 v = velocity * params.velocity_scale;
	float l = length(v);
	return l > params.max_radius ? v * (params.max_radius / l) : v;
}

#if defined(MODE_TILE_MAX)

layout(set = 0, binding = 0) uniform sampler2D velocity_buffer;
layout(set = 0, binding = 1) uniform sampler2D depth_buffer;
layout(rg16f, set = 1, binding = 0) uniform restrict writeonly image2D tile_max;

shared vec2 shared_v[64];
shared float shared_len[64];

void main() {
	// One workgroup per tile; each invocation takes 4 x 4 pixels of the 32 x 32 tile.
	ivec2 tile = ivec2(gl_WorkGroupID.xy);
	ivec2 base = tile * TILE_SIZE + ivec2(gl_LocalInvocationID.xy) * 4;
	vec2 best = vec2(0.0);
	float best_len = 0.0;
	for (int y = 0; y < 4; y++) {
		for (int x = 0; x < 4; x++) {
			ivec2 p = base + ivec2(x, y);
			if (any(greaterThanEqual(p, params.size))) {
				continue;
			}
			vec2 v = blur_vector_at(velocity_buffer, p, texelFetch(depth_buffer, p, 0).r);
			float l = dot(v, v);
			if (l > best_len) {
				best_len = l;
				best = v;
			}
		}
	}
	uint index = gl_LocalInvocationIndex;
	shared_v[index] = best;
	shared_len[index] = best_len;
	barrier();
	for (uint stride = 32u; stride > 0u; stride >>= 1u) {
		if (index < stride && shared_len[index + stride] > shared_len[index]) {
			shared_len[index] = shared_len[index + stride];
			shared_v[index] = shared_v[index + stride];
		}
		barrier();
	}
	if (index == 0u) {
		imageStore(tile_max, tile, vec4(shared_v[0], 0.0, 0.0));
	}
}

#elif defined(MODE_NEIGHBOR_MAX)

layout(set = 0, binding = 0) uniform sampler2D tile_max;
layout(rg16f, set = 1, binding = 0) uniform restrict writeonly image2D neighbor_max;

void main() {
	ivec2 tile = ivec2(gl_GlobalInvocationID.xy);
	if (any(greaterThanEqual(tile, params.tile_count))) {
		return;
	}
	vec2 best = vec2(0.0);
	float best_len = 0.0;
	for (int y = -1; y <= 1; y++) {
		for (int x = -1; x <= 1; x++) {
			ivec2 t = clamp(tile + ivec2(x, y), ivec2(0), params.tile_count - 1);
			vec2 v = texelFetch(tile_max, t, 0).xy;
			float l = dot(v, v);
			if (l > best_len) {
				best_len = l;
				best = v;
			}
		}
	}
	imageStore(neighbor_max, tile, vec4(best, 0.0, 0.0));
}

#else // MODE_GATHER

layout(set = 0, binding = 0) uniform sampler2D source_color;
layout(set = 0, binding = 1) uniform sampler2D source_depth;
layout(set = 0, binding = 2) uniform sampler2D velocity_buffer;
layout(set = 0, binding = 3) uniform sampler2D neighbor_max;
layout(rgba16f, set = 1, binding = 0) uniform restrict writeonly image2D dest;

float linear_depth(float p_depth) {
	// Reverse Z: 1 at the near plane, 0 at the far plane.
	if (params.orthogonal != 0) {
		return mix(params.z_far, params.z_near, p_depth);
	}
	return params.z_near * params.z_far / (params.z_near + p_depth * (params.z_far - params.z_near));
}

// 1 when a surface at depth p_a is at or in front of one at depth p_b, fading to 0 behind it.
float soft_depth_compare(float p_a, float p_b) {
	return clamp(1.0 - (p_a - p_b) / (params.soft_z * min(p_a, p_b)), 0.0, 1.0);
}

float cone(float p_dist, float p_len) {
	return clamp(1.0 - p_dist / max(p_len, 1e-4), 0.0, 1.0);
}

float cylinder(float p_dist, float p_len) {
	return 1.0 - smoothstep(0.95 * p_len, 1.05 * p_len, p_dist);
}

void main() {
	ivec2 pos = ivec2(gl_GlobalInvocationID.xy);
	if (any(greaterThanEqual(pos, params.size))) {
		return;
	}
	vec4 color_x = texelFetch(source_color, pos, 0);
	vec2 vn = texelFetch(neighbor_max, pos / TILE_SIZE, 0).xy;
	float vn_len = length(vn);
	if (vn_len < 0.5) {
		imageStore(dest, pos, color_x); // Nothing within reach moves by half a pixel: no blur.
		return;
	}

	float dx = texelFetch(source_depth, pos, 0).r;
	vec2 vx = blur_vector_at(velocity_buffer, pos, dx);
	float vx_len = max(length(vx), 0.5);
	float zx = linear_depth(dx);

	// Interleaved gradient noise (Jimenez 2014): the samples' offset along the blur, different for neighbouring pixels.
	float jitter = fract(52.9829189 * fract(dot(vec2(pos), vec2(0.06711056, 0.00583715)))) - 0.5;

	vec2 inv_size = 1.0 / vec2(params.size);
	float weight = float(params.sample_count) / (2.0 * vx_len); // the pixel itself (McGuire: 1 / |vx|, per sample pair)
	vec3 sum = color_x.rgb * weight;
	for (int i = 0; i < params.sample_count; i++) {
		// t in (-1, 1): samples spread evenly over the blur's two directions.
		float t = mix(-1.0, 1.0, (float(i) + jitter + 1.0) / float(params.sample_count + 1));
		vec2 offset = vn * t;
		ivec2 py = clamp(pos + ivec2(round(offset)), ivec2(0), params.size - 1);
		float dist = length(offset);
		float dy = texelFetch(source_depth, py, 0).r;
		float zy = linear_depth(dy);
		vec2 vy = blur_vector_at(velocity_buffer, py, dy);
		float vy_len = max(length(vy), 0.5);
		float f = soft_depth_compare(zy, zx); // the sample in front of the pixel
		float b = soft_depth_compare(zx, zy); // the sample behind it
		float alpha = f * cone(dist, vy_len) + b * cone(dist, vx_len) + cylinder(dist, vy_len) * cylinder(dist, vx_len) * 2.0;
		vec3 c = textureLod(source_color, (vec2(pos) + offset + 0.5) * inv_size, 0.0).rgb;
		sum += c * alpha;
		weight += alpha;
	}
	imageStore(dest, pos, vec4(sum / weight, color_x.a));
}

#endif
