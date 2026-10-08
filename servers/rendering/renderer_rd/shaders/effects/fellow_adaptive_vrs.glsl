#[compute]

#version 450

#VERSION_DEFINES

// FellowEngine adaptive shading rate (decision 0027). From the previous frame's final image, each shading rate tile
// gets the coarsest rate (per axis: 1, 2 or 4 pixels per fragment) whose estimated error stays under the threshold:
// the luma's gradient along the axis is estimated from differences at strides 1, 2 and 4 (the largest is kept, so
// that a tile shaded coarsely in the previous frame, flat inside its fragments, is not mistaken for flat content), and
// a fragment covering n pixels misses about 0.5 (n = 2) or 1.12 (n = 4) gradients per pixel. The rates are then
// dilated over 3 x 3 tiles (the finest per axis), for content moving between frames, and every tile is shaded at full
// rate one frame in refresh_period (its next estimate then sees the content's full detail).

#if defined(MODE_ESTIMATE)
layout(local_size_x = 64, local_size_y = 1, local_size_z = 1) in;
#else
layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;
#endif

#ifdef MODE_ESTIMATE
layout(set = 0, binding = 0) uniform sampler2D source_image;
layout(r8ui, set = 0, binding = 1) uniform restrict writeonly uimage2D tile_rates;
#endif

#ifdef MODE_DILATE
layout(r8ui, set = 0, binding = 1) uniform restrict readonly uimage2D tile_rates;
layout(r8ui, set = 0, binding = 2) uniform restrict writeonly uimage2D shading_rates;
layout(set = 0, binding = 3, std430) buffer restrict Counters {
	uint tiles;
	uint coarse_tiles;
	uint pixels;
	uint fragments;
}
counters;
#endif

#ifdef MODE_CLEAR
layout(r8ui, set = 0, binding = 2) uniform restrict writeonly uimage2D shading_rates;
#endif

layout(push_constant, std430) uniform Params {
	ivec2 tile_size; // pixels per shading rate texel
	ivec2 tiles; // shading rate texels
	ivec2 screen_size; // internal size (pixels)
	vec2 source_scale; // source pixels per internal pixel
	ivec2 source_size;
	float threshold;
	uint max_log2; // 1: up to 2 x 2, 2: up to 4 x 4
	uint linear_source; // the source holds linear values (HDR 2D)
	uint frame;
	uint refresh_period; // 0: none; otherwise each tile is shaded at full rate one frame in this many
	uint pad2;
}
params;

#ifdef MODE_ESTIMATE
shared float s_luma[32 * 32];
shared float s_sums[6][64];
shared uint s_counts[6][64];

float load_luma(ivec2 p_pixel) {
	ivec2 p = clamp(p_pixel, ivec2(0), params.screen_size - 1);
	ivec2 s = clamp(ivec2((vec2(p) + 0.5) * params.source_scale), ivec2(0), params.source_size - 1);
	vec3 c = texelFetch(source_image, s, 0).rgb;
	if (params.linear_source != 0u) {
		c = pow(clamp(c, vec3(0.0), vec3(1.0)), vec3(1.0 / 2.2));
	}
	return dot(c, vec3(0.2126, 0.7152, 0.0722));
}
#endif

uint encode_rate(uint p_lx, uint p_ly) {
	// No 1 x 4 or 4 x 1.
	p_lx = min(p_lx, p_ly + 1u);
	p_ly = min(p_ly, p_lx + 1u);
	return (p_lx << 2u) | p_ly;
}

void main() {
#ifdef MODE_ESTIMATE
	ivec2 tile = ivec2(gl_WorkGroupID.xy);
	uint lid = gl_LocalInvocationID.x;
	ivec2 ts = params.tile_size;
	int n = ts.x * ts.y;
	ivec2 origin = tile * ts;
	for (int i = int(lid); i < n; i += 64) {
		s_luma[i] = load_luma(origin + ivec2(i % ts.x, i / ts.x));
	}
	barrier();
	// Squared differences at strides 1, 2, 4 along x (sums 0-2) and y (3-5).
	float sums[6] = float[](0.0, 0.0, 0.0, 0.0, 0.0, 0.0);
	uint counts[6] = uint[](0u, 0u, 0u, 0u, 0u, 0u);
	for (int i = int(lid); i < n; i += 64) {
		int x = i % ts.x;
		int y = i / ts.x;
		float l = s_luma[i];
		for (int k = 0; k < 3; k++) {
			int s = 1 << k;
			if (x + s < ts.x) {
				float d = s_luma[i + s] - l;
				sums[k] += d * d;
				counts[k]++;
			}
			if (y + s < ts.y) {
				float d = s_luma[i + s * ts.x] - l;
				sums[3 + k] += d * d;
				counts[3 + k]++;
			}
		}
	}
	for (int k = 0; k < 6; k++) {
		s_sums[k][lid] = sums[k];
		s_counts[k][lid] = counts[k];
	}
	barrier();
	if (lid == 0u) {
		float g[2] = float[](0.0, 0.0);
		for (int axis = 0; axis < 2; axis++) {
			for (int k = 0; k < 3; k++) {
				float sum = 0.0;
				uint count = 0u;
				for (int t = 0; t < 64; t++) {
					sum += s_sums[axis * 3 + k][t];
					count += s_counts[axis * 3 + k][t];
				}
				if (count > 0u) {
					g[axis] = max(g[axis], sqrt(sum / float(count)) / float(1 << k));
				}
			}
		}
		uint l[2];
		for (int axis = 0; axis < 2; axis++) {
			l[axis] = 0u;
			if (params.threshold > 0.0) {
				if (params.max_log2 >= 2u && 1.12 * g[axis] < params.threshold) {
					l[axis] = 2u;
				} else if (0.5 * g[axis] < params.threshold) {
					l[axis] = 1u;
				}
			}
		}
		imageStore(tile_rates, tile, uvec4(encode_rate(l[0], l[1])));
	}
#endif

#ifdef MODE_DILATE
	ivec2 t = ivec2(gl_GlobalInvocationID.xy);
	bool inside = all(lessThan(t, params.tiles));
	uint lx = 3u;
	uint ly = 3u;
	if (inside) {
		for (int y = -1; y <= 1; y++) {
			for (int x = -1; x <= 1; x++) {
				ivec2 q = clamp(t + ivec2(x, y), ivec2(0), params.tiles - 1);
				uint r = imageLoad(tile_rates, q).r;
				lx = min(lx, r >> 2u);
				ly = min(ly, r & 3u);
			}
		}
		uint rate = encode_rate(lx, ly);
		if (params.refresh_period > 0u && (uint(t.x) + 3u * uint(t.y) + params.frame) % params.refresh_period == 0u) {
			// Full rate now and then: a tile shaded coarsely looks flatter than its content (fine texture detail is
			// lost), so its estimate is renewed from a full-rate image.
			rate = 0u;
		}
		imageStore(shading_rates, t, uvec4(rate));
		lx = rate >> 2u;
		ly = rate & 3u;
		uint pixels = uint(params.tile_size.x * params.tile_size.y);
		atomicAdd(counters.tiles, 1u);
		if (rate != 0u) {
			atomicAdd(counters.coarse_tiles, 1u);
		}
		atomicAdd(counters.pixels, pixels);
		atomicAdd(counters.fragments, pixels >> (lx + ly));
	}
#endif

#ifdef MODE_CLEAR
	ivec2 t = ivec2(gl_GlobalInvocationID.xy);
	if (all(lessThan(t, params.tiles))) {
		imageStore(shading_rates, t, uvec4(0u));
	}
#endif
}
