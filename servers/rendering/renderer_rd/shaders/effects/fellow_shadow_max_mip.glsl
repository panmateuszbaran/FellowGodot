#[compute]

#version 450

#VERSION_DEFINES

// FellowEngine shadow min/max window pyramid (docs/decisions/0005-directional-shadow-sampling.md): texel i of mip m
// holds the minimum (.r) and maximum (.g) depth of the atlas window [i * 2^(m+1), i * 2^(m+1) + 2^(m+2)) on each axis
// (windows twice the stride, overlapping by half), so any box of at most 2^(m+1) texels lies inside the single window
// of mip m that starts at or before it: one fetch proves that a filter region is fully lit or fully shadowed.

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

layout(set = 0, binding = 0) uniform sampler2D source; // atlas depth (mip 0) or the previous mip
#ifdef DEST_RG16
layout(rg16, set = 1, binding = 0) uniform restrict writeonly image2D dest;
#else
layout(rg32f, set = 1, binding = 0) uniform restrict writeonly image2D dest;
#endif

layout(push_constant, std430) uniform Params {
	ivec2 source_size;
	ivec2 dest_size;
	int source_is_pyramid; // 0: single-channel depth atlas, 1: previous (min, max) mip
	int round_mode; // 1: round the minimum down and the maximum up to 16-bit UNORM steps (32-bit atlas into RG16)
	ivec2 dest_offset; // first texel of the rebuilt region
}
params;

void main() {
	ivec2 pos = ivec2(gl_GlobalInvocationID.xy) + params.dest_offset;
	if (any(greaterThanEqual(pos, params.dest_size))) {
		return;
	}
	vec2 mm = vec2(1.0, 0.0);
	if (params.source_is_pyramid == 0) {
		// Mip 0: the 4 x 4 atlas texels [2i, 2i + 4) (clamped to the atlas like the shadow samplers).
		for (int y = 0; y < 4; y++) {
			for (int x = 0; x < 4; x++) {
				float d = texelFetch(source, min(pos * 2 + ivec2(x, y), params.source_size - 1), 0).r;
				mm = vec2(min(mm.x, d), max(mm.y, d));
			}
		}
		if (params.round_mode != 0) {
			// Conservative: a smaller minimum and a larger maximum only make the early-outs rarer, never wrong.
			mm = vec2(floor(mm.x * 65535.0), ceil(mm.y * 65535.0)) / 65535.0;
		}
	} else {
		// Mip m: the windows 2i and 2i + 2 of mip m - 1 (each half of this window), clamped to the previous mip.
		for (int y = 0; y < 2; y++) {
			for (int x = 0; x < 2; x++) {
				vec2 v = texelFetch(source, min(pos * 2 + ivec2(x, y) * 2, params.source_size - 1), 0).rg;
				mm = vec2(min(mm.x, v.x), max(mm.y, v.y));
			}
		}
	}
	imageStore(dest, pos, vec4(mm, 0.0, 0.0));
}
