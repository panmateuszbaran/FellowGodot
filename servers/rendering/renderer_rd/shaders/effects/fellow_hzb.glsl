#[compute]

#version 450

#VERSION_DEFINES

// FellowEngine depth pyramid (decision 0026): each texel holds the farthest depth (reverse Z: the smallest value) of the
// 2 x 2 texels below it; level 0 (power-of-two sides, at least half the screen) from the depth buffer, each further level
// from the previous one. Reads past the source repeat its edge.

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

#ifdef MODE_FROM_DEPTH
layout(set = 0, binding = 0) uniform sampler2D source_depth;
#else
layout(r32f, set = 0, binding = 0) uniform restrict readonly image2D source_level;
#endif
layout(r32f, set = 1, binding = 0) uniform restrict writeonly image2D dest_level;

layout(push_constant, std430) uniform Params {
	ivec2 source_size;
	ivec2 dest_size;
}
params;

float load_source(ivec2 p) {
	p = min(p, params.source_size - 1);
#ifdef MODE_FROM_DEPTH
	return texelFetch(source_depth, p, 0).r;
#else
	return imageLoad(source_level, p).r;
#endif
}

void main() {
	ivec2 pos = ivec2(gl_GlobalInvocationID.xy);
	if (any(greaterThanEqual(pos, params.dest_size))) {
		return;
	}
	ivec2 s = pos * 2;
	float d = min(min(load_source(s), load_source(s + ivec2(1, 0))), min(load_source(s + ivec2(0, 1)), load_source(s + ivec2(1, 1))));
	imageStore(dest_level, pos, vec4(d));
}
