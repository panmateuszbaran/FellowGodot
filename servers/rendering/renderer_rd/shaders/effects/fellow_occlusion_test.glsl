#[compute]

#version 450

#VERSION_DEFINES

// FellowEngine occlusion test (decision 0026): each list element's world AABB is projected, and the element is hidden if
// its nearest point (reverse Z: the largest depth) is farther than the farthest depth of the depth pyramid over the
// pixels its projection covers. Conservative: an element with a corner behind the camera counts as visible.

layout(local_size_x = 64, local_size_y = 1, local_size_z = 1) in;

layout(set = 0, binding = 0) uniform sampler2D hzb; // level m: one texel per 2^(m+1) x 2^(m+1) pixels
layout(set = 0, binding = 1, std430) buffer restrict readonly Elements {
	vec4 data[]; // per element: AABB min (w: weight, uint bits), AABB max
}
elements;
layout(set = 0, binding = 2, std430) buffer restrict Counters {
	uint tested;
	uint total_weight;
	uint hidden;
	uint hidden_weight;
}
counters;

layout(push_constant, std430) uniform Params {
	mat4 view_projection;
	uvec2 screen_size;
	uint count;
	uint levels;
}
params;

void main() {
	uint i = gl_GlobalInvocationID.x;
	if (i >= params.count) {
		return;
	}
	vec4 a = elements.data[i * 2];
	vec4 b = elements.data[i * 2 + 1];
	uint weight = floatBitsToUint(a.w);
	atomicAdd(counters.tested, 1);
	atomicAdd(counters.total_weight, weight);

	float nearest = 0.0;
	vec2 lo = vec2(1e30);
	vec2 hi = vec2(-1e30);
	for (uint k = 0; k < 8; k++) {
		vec3 p = vec3((k & 1u) != 0 ? b.x : a.x, (k & 2u) != 0 ? b.y : a.y, (k & 4u) != 0 ? b.z : a.z);
		vec4 clip = params.view_projection * vec4(p, 1.0);
		if (clip.w <= 1e-5) {
			return; // crosses the camera plane: visible
		}
		vec3 ndc = clip.xyz / clip.w;
		nearest = max(nearest, ndc.z);
		lo = min(lo, ndc.xy);
		hi = max(hi, ndc.xy);
	}
	vec2 size = vec2(params.screen_size);
	vec2 plo = (lo * 0.5 + 0.5) * size;
	vec2 phi = (hi * 0.5 + 0.5) * size;
	if (any(greaterThan(plo, size)) || any(lessThan(phi, vec2(0.0)))) {
		return; // off screen
	}
	ivec2 p0 = clamp(ivec2(floor(plo)), ivec2(0), ivec2(params.screen_size) - 1);
	ivec2 p1 = clamp(ivec2(floor(phi)), ivec2(0), ivec2(params.screen_size) - 1);
	// The finest level where the rectangle spans at most 3 x 3 texels.
	int level = 0;
	while (level + 1 < int(params.levels) && any(greaterThan((p1 >> (level + 1)) - (p0 >> (level + 1)), ivec2(2)))) {
		level++;
	}
	ivec2 t0 = p0 >> (level + 1);
	ivec2 t1 = p1 >> (level + 1);
	if (any(greaterThan(t1 - t0, ivec2(2)))) {
		return; // larger than the coarsest level can bound with 3 x 3 texels: visible
	}
	float farthest = 1.0;
	for (int y = t0.y; y <= t1.y; y++) {
		for (int x = t0.x; x <= t1.x; x++) {
			farthest = min(farthest, texelFetch(hzb, ivec2(x, y), level).r);
		}
	}
	if (nearest < farthest) {
		atomicAdd(counters.hidden, 1);
		atomicAdd(counters.hidden_weight, weight);
	}
}
