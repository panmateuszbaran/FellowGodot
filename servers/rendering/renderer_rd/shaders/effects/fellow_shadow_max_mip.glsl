#[compute]

#version 450

#VERSION_DEFINES

// FellowEngine shadow min/max window pyramid (docs/decisions/0005-directional-shadow-sampling.md): texel i of level L
// holds the minimum (.r) and maximum (.g) depth of the atlas window [i * 2^(L+2), i * 2^(L+2) + 2^(L+3)) on each axis
// (windows twice the stride, overlapping by half), so any box of at most 2^(L+2) texels lies inside the single window
// of level L that starts at or before it: one fetch proves that a filter region is fully lit or fully shadowed.
// (The 4-texel windows of the chain are not stored: every looked-up box is at least 3 texels wide.)

#ifdef MODE_TAIL
// The last mips (32 x 32 and smaller) in one workgroup: each mip from the previous one held in shared memory, the same
// windows and the same values as one dispatch per mip, without a dispatch and a barrier per mip.
layout(local_size_x = 256, local_size_y = 1, local_size_z = 1) in;
#define TAIL_MAX_LEVELS 8
#define TAIL_MAX_SIZE 32
#else
layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;
#endif

layout(set = 0, binding = 0) uniform sampler2D source; // atlas depth (mip 0) or the previous mip
#ifdef MODE_TAIL
#ifdef DEST_RG16
layout(rg16, set = 1, binding = 0) uniform restrict writeonly image2D dest_tail[TAIL_MAX_LEVELS];
#else
layout(rg32f, set = 1, binding = 0) uniform restrict writeonly image2D dest_tail[TAIL_MAX_LEVELS];
#endif
#else
#ifdef DEST_RG16
layout(rg16, set = 1, binding = 0) uniform restrict writeonly image2D dest;
#else
layout(rg32f, set = 1, binding = 0) uniform restrict writeonly image2D dest;
#endif
#endif

layout(push_constant, std430) uniform Params {
	ivec2 source_size;
	ivec2 dest_size;
	int source_is_pyramid; // 0: single-channel depth atlas, 1: previous (min, max) mip
	int round_mode; // 1: round the minimum down and the maximum up to 16-bit UNORM steps (32-bit atlas into RG16)
	ivec2 dest_offset; // first texel of the rebuilt region (MODE_TAIL: x = number of mips written)
}
params;

#ifdef MODE_TAIL
shared vec2 tail_a[TAIL_MAX_SIZE * TAIL_MAX_SIZE];
shared vec2 tail_b[(TAIL_MAX_SIZE / 2) * (TAIL_MAX_SIZE / 2)];

void store_tail(int p_level, ivec2 p_pos, vec2 p_value) {
	// Constant indices only (no dynamic indexing of image arrays needed).
	switch (p_level) {
		case 0:
			imageStore(dest_tail[0], p_pos, vec4(p_value, 0.0, 0.0));
			break;
		case 1:
			imageStore(dest_tail[1], p_pos, vec4(p_value, 0.0, 0.0));
			break;
		case 2:
			imageStore(dest_tail[2], p_pos, vec4(p_value, 0.0, 0.0));
			break;
		case 3:
			imageStore(dest_tail[3], p_pos, vec4(p_value, 0.0, 0.0));
			break;
		case 4:
			imageStore(dest_tail[4], p_pos, vec4(p_value, 0.0, 0.0));
			break;
		case 5:
			imageStore(dest_tail[5], p_pos, vec4(p_value, 0.0, 0.0));
			break;
		case 6:
			imageStore(dest_tail[6], p_pos, vec4(p_value, 0.0, 0.0));
			break;
		default:
			imageStore(dest_tail[7], p_pos, vec4(p_value, 0.0, 0.0));
			break;
	}
}

void main() {
	// First tail mip from the previous mip (texture), like the per-mip dispatches (source_is_pyramid is always 1 here).
	ivec2 size = params.dest_size;
	for (int i = int(gl_LocalInvocationIndex); i < size.x * size.y; i += 256) {
		ivec2 pos = ivec2(i % size.x, i / size.x);
		vec2 mm = vec2(1.0, 0.0);
		for (int y = 0; y < 2; y++) {
			for (int x = 0; x < 2; x++) {
				vec2 v = texelFetch(source, min(pos * 2 + ivec2(x, y) * 2, params.source_size - 1), 0).rg;
				mm = vec2(min(mm.x, v.x), max(mm.y, v.y));
			}
		}
		tail_a[i] = mm;
		store_tail(0, pos, mm);
	}
	memoryBarrierShared();
	barrier();
	// Further mips from shared memory. Every value kept here is one of the values read from the previous mip (minimum
	// and maximum select, they do not compute), i.e. exactly what reading it back from the pyramid would return.
	bool from_a = true;
	for (int level = 1; level < params.dest_offset.x; level++) {
		ivec2 dst_size = (size + 1) / 2;
		for (int i = int(gl_LocalInvocationIndex); i < dst_size.x * dst_size.y; i += 256) {
			ivec2 pos = ivec2(i % dst_size.x, i / dst_size.x);
			vec2 mm = vec2(1.0, 0.0);
			for (int y = 0; y < 2; y++) {
				for (int x = 0; x < 2; x++) {
					ivec2 q = min(pos * 2 + ivec2(x, y) * 2, size - 1);
					vec2 v = from_a ? tail_a[q.y * size.x + q.x] : tail_b[q.y * size.x + q.x];
					mm = vec2(min(mm.x, v.x), max(mm.y, v.y));
				}
			}
			if (from_a) {
				tail_b[i] = mm;
			} else {
				tail_a[i] = mm;
			}
			store_tail(level, pos, mm);
		}
		memoryBarrierShared();
		barrier();
		from_a = !from_a;
		size = dst_size;
	}
}

#else // !MODE_TAIL

// A window of this level is the union of two neighbouring "parts" per axis, shared with the next window: level 0,
// parts are 4 x 4 atlas blocks (window i = blocks i and i + 1 = atlas texels [4i, 4i + 8)); level L, parts are the
// texels 2p of level L - 1 (window i = texels 2i and 2i + 2). Each workgroup loads the 9 x 9 parts of its 8 x 8 windows
// once (shared memory) instead of 2 x 2 parts per window.
shared vec2 parts[9 * 9];

void main() {
	ivec2 tile = ivec2(gl_WorkGroupID.xy) * 8 + params.dest_offset;
	vec2 inv_size = 1.0 / vec2(params.source_size);
	for (int i = int(gl_LocalInvocationIndex); i < 81; i += 64) {
		ivec2 p = tile + ivec2(i % 9, i / 9);
		vec2 mm;
		if (params.source_is_pyramid == 0) {
			// Atlas texels [4p, 4p + 4) on each axis: four gathers, each at the shared corner of two texel pairs (the
			// sampler clamps to the atlas edge, like the shadow samplers).
			mm = vec2(1.0, 0.0);
			for (int y = 0; y < 2; y++) {
				for (int x = 0; x < 2; x++) {
					vec4 d = textureGather(source, vec2(p * 4 + ivec2(x, y) * 2 + 1) * inv_size, 0);
					mm = vec2(min(mm.x, min(min(d.x, d.y), min(d.z, d.w))), max(mm.y, max(max(d.x, d.y), max(d.z, d.w))));
				}
			}
		} else {
			mm = texelFetch(source, min(p * 2, params.source_size - 1), 0).rg;
		}
		parts[i] = mm;
	}
	memoryBarrierShared();
	barrier();

	ivec2 local = ivec2(gl_LocalInvocationID.xy);
	ivec2 pos = tile + local;
	if (any(greaterThanEqual(pos, params.dest_size))) {
		return;
	}
	vec2 mm = vec2(1.0, 0.0);
	for (int y = 0; y < 2; y++) {
		for (int x = 0; x < 2; x++) {
			vec2 v = parts[(local.y + y) * 9 + local.x + x];
			mm = vec2(min(mm.x, v.x), max(mm.y, v.y));
		}
	}
	if (params.source_is_pyramid == 0 && params.round_mode != 0) {
		// Conservative: a smaller minimum and a larger maximum only make the early-outs rarer, never wrong.
		mm = vec2(floor(mm.x * 65535.0), ceil(mm.y * 65535.0)) / 65535.0;
	}
	imageStore(dest, pos, vec4(mm, 0.0, 0.0));
}

#endif // MODE_TAIL
