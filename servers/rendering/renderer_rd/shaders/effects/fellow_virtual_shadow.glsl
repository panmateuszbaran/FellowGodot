#[compute]

#version 450

#VERSION_DEFINES

// FellowEngine virtual shadow maps (docs/decisions/0396-virtual-shadow-maps.md).
// MODE_CLEAR: the physical pages drawn this frame, emptied (0: no caster) before their passes.
// MODE_MARK: the pages the view's pixels look up, from the depth pre-pass: each pixel's level as the colour passes
// choose it (fellow_vsm_shadow() in scene_forward_clustered.glsl), its page and the pages its filter and normal offset
// reach, as bits (levels * 64 * 64, level by level, row by row), read back for the scene cull.

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

#ifdef MODE_CLEAR

layout(r32ui, set = 0, binding = 0) uniform restrict writeonly uimage2D pool;

layout(set = 0, binding = 1, std430) buffer restrict readonly ClearList {
	uint pages[];
}
clear_list;

layout(push_constant, std430) uniform Params {
	uint pool_pages;
	uint count;
	uint pad[2];
}
params;

void main() {
	uint i = gl_GlobalInvocationID.z;
	if (i >= params.count) {
		return;
	}
	uint phys = clear_list.pages[i];
	ivec2 base = ivec2(int(phys % params.pool_pages), int(phys / params.pool_pages)) * 128;
	imageStore(pool, base + ivec2(gl_GlobalInvocationID.xy), uvec4(0u));
}

#endif

#ifdef MODE_MARK

struct FellowVSMHeader {
	uint levels;
	uint light_index;
	uint pool_pages;
	uint flags;
	float texel0;
	float lod_bias;
	float footprint;
	float max_distance;
	int h_top_fixed;
	int cam_h_fixed;
	float cam_h_frac;
	float pad;
	vec4 axis_x;
	vec4 axis_y;
	vec4 axis_z;
	vec4 origin[16];
};

layout(set = 0, binding = 0) uniform sampler2D depth_buffer;

layout(set = 0, binding = 1, std430) buffer restrict readonly VSM {
	FellowVSMHeader header;
}
vsm;

layout(set = 0, binding = 2, std430) buffer restrict Requests {
	uint bits[];
}
requests;

layout(push_constant, std430) uniform Params {
	mat4 inv_projection; // NDC (x, y, depth * 2 - 1) to view
	ivec2 size;
	ivec2 pad;
}
params;

void mark(int p_level, ivec2 p_page) {
	if (any(lessThan(p_page, ivec2(0))) || any(greaterThanEqual(p_page, ivec2(64)))) {
		return;
	}
	uint index = uint(p_level) * 4096u + uint(p_page.y) * 64u + uint(p_page.x);
	atomicOr(requests.bits[index >> 5], 1u << (index & 31u));
}

void main() {
	ivec2 p = ivec2(gl_GlobalInvocationID.xy);
	if (any(greaterThanEqual(p, params.size))) {
		return;
	}
	float d = texelFetch(depth_buffer, p, 0).r;
	if (d <= 0.0) {
		return; // sky (reverse Z)
	}
	vec2 uv = (vec2(p) + 0.5) / vec2(params.size);
	vec4 v = params.inv_projection * vec4(uv * 2.0 - 1.0, d * 2.0 - 1.0, 1.0);
	vec3 vertex = v.xyz / v.w;
	float z = -vertex.z;
	if (z > vsm.header.max_distance) {
		return;
	}
	int levels = int(vsm.header.levels);
	float texel0 = vsm.header.texel0;
	float size = bool(vsm.header.flags & 1u) ? vsm.header.footprint : z * vsm.header.footprint;
	int l = clamp(int(ceil(log2(max(size / texel0, 1e-8)) + vsm.header.lod_bias)), 0, levels - 1);
	float t = texel0 * exp2(float(l));
	vec2 tc = vsm.header.origin[l].xy + vec2(dot(vsm.header.axis_x.xyz, vertex), dot(vsm.header.axis_y.xyz, vertex)) / t;
	// The filter's texels (two around) and the normal offset (1.5 texels), with a texel to spare.
	ivec2 a = ivec2(floor(tc - 4.5)) >> 7;
	ivec2 b = ivec2(floor(tc + 4.5)) >> 7;
	for (int y = a.y; y <= b.y; y++) {
		for (int x = a.x; x <= b.x; x++) {
			mark(l, ivec2(x, y));
		}
	}
}

#endif
