#[vertex]

#version 450

#VERSION_DEFINES

// FellowEngine directional shadow cache copy (docs/decisions/0004-directional-shadow-cache.md).
void main() {
	vec2 base_arr[3] = vec2[](vec2(-1.0, -1.0), vec2(-1.0, 3.0), vec2(3.0, -1.0));
	gl_Position = vec4(base_arr[gl_VertexIndex], 0.0, 1.0);
}

#[fragment]

#version 450

#VERSION_DEFINES

layout(set = 0, binding = 0) uniform sampler2D source_depth;

layout(push_constant, std430) uniform Params {
	ivec2 dst_origin;
	ivec2 src_offset;
	float scale;
	float bias;
	vec2 pad;
}
params;

void main() {
	// The cascade rect and the cache share one light-space texel lattice: an integer offset maps texels exactly.
	ivec2 p = ivec2(gl_FragCoord.xy) - params.dst_origin + params.src_offset;
	float d = texelFetch(source_depth, p, 0).r;
	// Re-base reverse-Z orthographic depth from the cache's range to the cascade's; 0.9999 is where the shadow pass
	// clamps ("pancakes") casters in front of the near plane, 0 is the cleared value (nothing).
	gl_FragDepth = clamp(d * params.scale + params.bias, 0.0, 0.9999);
}
