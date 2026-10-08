#[compute]

#version 450

#VERSION_DEFINES

// FellowEngine depth pyramid (decision 0026): each texel holds the farthest depth (reverse Z: the smallest value) of the
// 2 x 2 texels below it. Level 0 covers 2 x 2 pixels of the depth buffer; its sides are multiples of 64, so that the
// seven levels halve exactly. Reads past the depth buffer repeat its edge (the minimum over a superset of the pixels a
// texel covers is still a lower bound).
//
// MODE_LEVELS_0_5: one workgroup per 32 x 32 texels of level 0 (64 x 64 pixels) writes levels 0 to 5 of that block.
// Otherwise: one level from the previous one.

#ifdef MODE_LEVELS_0_5

layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;

layout(set = 0, binding = 0) uniform sampler2D source_depth;
layout(r32f, set = 1, binding = 0) uniform restrict writeonly image2D level0;
layout(r32f, set = 1, binding = 1) uniform restrict writeonly image2D level1;
layout(r32f, set = 1, binding = 2) uniform restrict writeonly image2D level2;
layout(r32f, set = 1, binding = 3) uniform restrict writeonly image2D level3;
layout(r32f, set = 1, binding = 4) uniform restrict writeonly image2D level4;
layout(r32f, set = 1, binding = 5) uniform restrict writeonly image2D level5;

#else

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

layout(r32f, set = 0, binding = 0) uniform restrict readonly image2D source_level;
layout(r32f, set = 1, binding = 0) uniform restrict writeonly image2D dest_level;

#endif

layout(push_constant, std430) uniform Params {
	ivec2 source_size;
	ivec2 dest_size;
}
params;

#ifdef MODE_LEVELS_0_5

shared float s_level[16][16];

float depth_at(ivec2 p) {
	return texelFetch(source_depth, min(p, params.source_size - 1), 0).r;
}

void main() {
	ivec2 lid = ivec2(gl_LocalInvocationID.xy);
	ivec2 block = ivec2(gl_WorkGroupID.xy);
	// Level 0: this thread's 2 x 2 texels (4 x 4 pixels).
	ivec2 t0 = block * 32 + lid * 2;
	float m1 = 1.0;
	for (int y = 0; y < 2; y++) {
		for (int x = 0; x < 2; x++) {
			ivec2 t = t0 + ivec2(x, y);
			ivec2 p = t * 2;
			float d = min(min(depth_at(p), depth_at(p + ivec2(1, 0))), min(depth_at(p + ivec2(0, 1)), depth_at(p + ivec2(1, 1))));
			imageStore(level0, t, vec4(d));
			m1 = min(m1, d);
		}
	}
	// Level 1: one texel per thread.
	ivec2 t1 = block * 16 + lid;
	imageStore(level1, t1, vec4(m1));
	s_level[lid.y][lid.x] = m1;
	barrier();
	// Levels 2 to 5: 8 x 8, 4 x 4, 2 x 2, 1 x 1 texels of the block.
	int n = 8;
	for (int level = 2; level <= 5; level++) {
		float m = 1.0;
		bool in_level = all(lessThan(lid, ivec2(n)));
		if (in_level) {
			ivec2 s = lid * 2;
			m = min(min(s_level[s.y][s.x], s_level[s.y][s.x + 1]), min(s_level[s.y + 1][s.x], s_level[s.y + 1][s.x + 1]));
			ivec2 t = block * n + lid;
			if (level == 2) {
				imageStore(level2, t, vec4(m));
			} else if (level == 3) {
				imageStore(level3, t, vec4(m));
			} else if (level == 4) {
				imageStore(level4, t, vec4(m));
			} else {
				imageStore(level5, t, vec4(m));
			}
		}
		barrier();
		if (in_level) {
			s_level[lid.y][lid.x] = m;
		}
		barrier();
		n /= 2;
	}
}

#else

void main() {
	ivec2 pos = ivec2(gl_GlobalInvocationID.xy);
	if (any(greaterThanEqual(pos, params.dest_size))) {
		return;
	}
	ivec2 s = pos * 2;
	float d = min(min(imageLoad(source_level, s).r, imageLoad(source_level, s + ivec2(1, 0)).r), min(imageLoad(source_level, s + ivec2(0, 1)).r, imageLoad(source_level, s + ivec2(1, 1)).r));
	imageStore(dest_level, pos, vec4(d));
}

#endif
