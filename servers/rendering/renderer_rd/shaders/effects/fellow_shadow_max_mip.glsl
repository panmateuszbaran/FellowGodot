#[compute]

#version 450

#VERSION_DEFINES

// FellowEngine directional shadow max pyramid (docs/decisions/0005-directional-shadow-sampling.md): level k holds the
// maximum (reverse Z: closest to the light) of each 2^k x 2^k block of the directional shadow atlas, so the PCSS blocker
// search can prove "no blocker in the search region" with four fetches instead of twelve filtered samples.

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

layout(set = 0, binding = 0) uniform sampler2D source; // atlas depth (level 1) or the previous pyramid level
layout(r32f, set = 1, binding = 0) uniform restrict writeonly image2D dest;

layout(push_constant, std430) uniform Params {
	ivec2 source_size;
	ivec2 dest_size;
}
params;

void main() {
	ivec2 pos = ivec2(gl_GlobalInvocationID.xy);
	if (any(greaterThanEqual(pos, params.dest_size))) {
		return;
	}
	// dest_size = ceil(source_size / 2): every source texel is covered (odd sizes: the last texel covers one column/row).
	ivec2 s0 = pos * 2;
	ivec2 s1 = min(s0 + ivec2(1), params.source_size - 1);
	float m = max(max(texelFetch(source, s0, 0).r, texelFetch(source, ivec2(s1.x, s0.y), 0).r),
			max(texelFetch(source, ivec2(s0.x, s1.y), 0).r, texelFetch(source, s1, 0).r));
	imageStore(dest, pos, vec4(m));
}
