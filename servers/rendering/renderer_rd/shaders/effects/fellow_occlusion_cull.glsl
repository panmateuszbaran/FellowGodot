#[compute]

#version 450

#VERSION_DEFINES

// FellowEngine occlusion culling of the opaque pass (decision 0026). Runs after the full depth pre-pass, with a depth
// pyramid of its depth buffer (each texel the farthest depth, reverse Z the smallest value, of the pixels it covers).
//
// MODE_TEST (a thread per list element): the element's surface bounding box (local space, per surface slot) is
// transformed by the instance's transform (its persistent instance data entry) and projected; the element is hidden when
// its nearest point is behind the farthest depth of the pyramid texels covering its projection, or when its projection
// covers no pixel. A hidden element has no fragment that can pass the opaque pass's depth test (equal, or greater or
// equal, against a depth buffer that only gets nearer), so leaving it out changes nothing.
//
// MODE_COMPACT (a thread per chunk: up to 64 consecutive elements of a run, drawn by one indirect draw): the chunk's
// visible elements are copied in order to the same positions of the compacted element buffer, and the chunk's draw gets
// their count.

layout(local_size_x = 64, local_size_y = 1, local_size_z = 1) in;

layout(set = 0, binding = 0) uniform sampler2D hzb; // level m: one texel per 2^(m+1) x 2^(m+1) pixels

layout(set = 0, binding = 1, std430) buffer restrict readonly Runs {
	uvec4 data[]; // per chunk: first element, element count (at most 64), indices per draw, unused
}
runs;

layout(set = 0, binding = 2, std430) buffer restrict readonly SourceElements {
	uvec4 data[]; // the opaque list's elements (persistent instance data: slot, flags, GI offset)
}
source_elements;

layout(set = 0, binding = 3, std430) buffer restrict readonly Slots {
	vec4 data[]; // persistent instance data entries, slot_vec4s each; the first three hold the transform's rows
}
slots;

layout(set = 0, binding = 4, std430) buffer restrict readonly SlotBounds {
	vec4 data[]; // per surface slot: local bounding box minimum (w: 1 known, 0 unknown) and maximum
}
slot_bounds;

layout(set = 0, binding = 5, std430) buffer restrict writeonly CompactedElements {
	uvec4 data[];
}
compacted_elements;

layout(set = 0, binding = 6, std430) buffer restrict writeonly DrawArgs {
	uint data[]; // per chunk: index count, instance count, first index, vertex offset, first instance
}
draw_args;

layout(set = 0, binding = 7, std430) buffer restrict Counters {
	uint elements;
	uint hidden;
	uint indices;
	uint hidden_indices;
}
counters;

layout(set = 0, binding = 8, std430) buffer restrict Visibility {
	uint data[]; // per list element: 1 visible, 0 hidden
}
visibility;

layout(push_constant, std430) uniform Params {
	mat4 view_projection;
	vec2 screen_size;
	uint levels;
	uint count; // list elements (test) or chunks (compact)
	uint slot_vec4s;
	uint pad0;
	uint pad1;
	uint pad2;
}
params;

#ifdef MODE_TEST

bool element_visible(uint p_slot) {
	vec4 bmin = slot_bounds.data[p_slot * 2u];
	vec4 bmax = slot_bounds.data[p_slot * 2u + 1u];
	if (bmin.w == 0.0) {
		return true; // bounds unknown
	}
	uint base = p_slot * params.slot_vec4s;
	mat4 model = transpose(mat4(slots.data[base], slots.data[base + 1u], slots.data[base + 2u], vec4(0.0, 0.0, 0.0, 1.0)));
	mat4 mvp = params.view_projection * model;

	float nearest = 0.0;
	vec2 lo = vec2(1e30);
	vec2 hi = vec2(-1e30);
	for (uint k = 0u; k < 8u; k++) {
		vec3 p = vec3((k & 1u) != 0u ? bmax.x : bmin.x, (k & 2u) != 0u ? bmax.y : bmin.y, (k & 4u) != 0u ? bmax.z : bmin.z);
		vec4 clip = mvp * vec4(p, 1.0);
		if (clip.w <= 1e-5) {
			return true; // reaches the camera plane
		}
		vec3 ndc = clip.xyz / clip.w;
		nearest = max(nearest, ndc.z);
		lo = min(lo, ndc.xy);
		hi = max(hi, ndc.xy);
	}
	// Pixels whose centres the projection can cover, one pixel wider on every side (rounding, rasterizer snapping).
	vec2 size = params.screen_size;
	vec2 plo = (lo * 0.5 + 0.5) * size - 1.0;
	vec2 phi = (hi * 0.5 + 0.5) * size + 1.0;
	if (any(lessThan(phi, vec2(0.0))) || any(greaterThan(plo, size))) {
		return false; // covers no pixel
	}
	ivec2 last = ivec2(size) - 1;
	ivec2 p0 = clamp(ivec2(floor(plo)), ivec2(0), last);
	ivec2 p1 = clamp(ivec2(floor(phi)), ivec2(0), last);
	// The finest level where the rectangle spans at most 3 x 3 texels.
	int level = 0;
	while (level + 1 < int(params.levels) && any(greaterThan((p1 >> (level + 1)) - (p0 >> (level + 1)), ivec2(2)))) {
		level++;
	}
	ivec2 t0 = p0 >> (level + 1);
	ivec2 t1 = p1 >> (level + 1);
	if (any(greaterThan(t1 - t0, ivec2(2)))) {
		return true;
	}
	float farthest = 1.0;
	for (int y = t0.y; y <= t1.y; y++) {
		for (int x = t0.x; x <= t1.x; x++) {
			farthest = min(farthest, texelFetch(hzb, ivec2(x, y), level).r);
		}
	}
	// Margin for the difference between this transform and the vertex shader's (relative in depth: about 0.1 % of the
	// distance in perspective; absolute: a few 24-bit depth steps).
	float safe_nearest = nearest + max(nearest * (1.0 / 1024.0), 1.0 / 1048576.0);
	return !(safe_nearest < farthest);
}

void main() {
	uint i = gl_GlobalInvocationID.x;
	if (i < params.count) {
		visibility.data[i] = element_visible(source_elements.data[i].x) ? 1u : 0u;
	}
}

#else // MODE_COMPACT

shared uint s_counts[4];

void main() {
	uint lid = gl_LocalInvocationID.x;
	if (lid == 0u) {
		s_counts[0] = 0u;
		s_counts[1] = 0u;
		s_counts[2] = 0u;
		s_counts[3] = 0u;
	}
	barrier();
	uint r = gl_GlobalInvocationID.x;
	if (r < params.count) {
		uvec4 run = runs.data[r];
		uint written = 0u;
		for (uint j = 0u; j < run.y; j++) {
			if (visibility.data[run.x + j] != 0u) {
				compacted_elements.data[run.x + written] = source_elements.data[run.x + j];
				written++;
			}
		}
		draw_args.data[r * 5u + 0u] = run.z;
		draw_args.data[r * 5u + 1u] = written;
		draw_args.data[r * 5u + 2u] = 0u;
		draw_args.data[r * 5u + 3u] = 0u;
		draw_args.data[r * 5u + 4u] = 0u;
		atomicAdd(s_counts[0], run.y);
		atomicAdd(s_counts[1], run.y - written);
		atomicAdd(s_counts[2], run.y * run.z);
		atomicAdd(s_counts[3], (run.y - written) * run.z);
	}
	barrier();
	if (lid == 0u && s_counts[0] != 0u) {
		atomicAdd(counters.elements, s_counts[0]);
		atomicAdd(counters.hidden, s_counts[1]);
		atomicAdd(counters.indices, s_counts[2]);
		atomicAdd(counters.hidden_indices, s_counts[3]);
	}
}

#endif
