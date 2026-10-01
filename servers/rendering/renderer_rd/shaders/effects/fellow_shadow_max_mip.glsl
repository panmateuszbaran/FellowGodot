#[compute]

#version 450

#VERSION_DEFINES

// FellowEngine directional shadow min/max pyramid (docs/decisions/0005-directional-shadow-sampling.md): level k holds the
// minimum (.r, reverse Z: farthest from the light) and maximum (.g, closest to the light) of each 2^(k+1) x 2^(k+1)
// block of the directional shadow atlas, so the shadow filters can prove with four fetches that a region holds no
// blocker (fully lit) or only blockers (fully shadowed).

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

layout(set = 0, binding = 0) uniform sampler2D source; // atlas depth (level 0) or the previous pyramid level
#ifdef DEST_RG16
layout(rg16, set = 1, binding = 0) uniform restrict writeonly image2D dest;
#else
layout(rg32f, set = 1, binding = 0) uniform restrict writeonly image2D dest;
#endif

layout(push_constant, std430) uniform Params {
	ivec2 source_size;
	ivec2 dest_size;
	int source_is_pyramid; // 0: single-channel depth atlas, 1: previous (min, max) level
	int round_mode; // 1: round the minimum down and the maximum up to 16-bit UNORM steps (32-bit atlas into RG16)
	ivec2 dest_offset; // first texel of the rebuilt region
}
params;

vec2 fetch_minmax(ivec2 p) {
	vec4 v = texelFetch(source, p, 0);
	return params.source_is_pyramid != 0 ? v.rg : v.rr;
}

void main() {
	ivec2 pos = ivec2(gl_GlobalInvocationID.xy) + params.dest_offset;
	if (any(greaterThanEqual(pos, params.dest_size))) {
		return;
	}
	// dest_size = ceil(source_size / 2): every source texel is covered (odd sizes: the last texel covers one column/row).
	ivec2 s0 = pos * 2;
	ivec2 s1 = min(s0 + ivec2(1), params.source_size - 1);
	vec2 a = fetch_minmax(s0);
	vec2 b = fetch_minmax(ivec2(s1.x, s0.y));
	vec2 c = fetch_minmax(ivec2(s0.x, s1.y));
	vec2 d = fetch_minmax(s1);
	vec2 mm = vec2(min(min(a.x, b.x), min(c.x, d.x)), max(max(a.y, b.y), max(c.y, d.y)));
	if (params.round_mode != 0) {
		// Conservative: a smaller minimum and a larger maximum only make the early-outs rarer, never wrong.
		mm = vec2(floor(mm.x * 65535.0), ceil(mm.y * 65535.0)) / 65535.0;
	}
	imageStore(dest, pos, vec4(mm, 0.0, 0.0));
}
