#[compute]

#version 450

#VERSION_DEFINES

// FellowEngine adaptive shading rate (decision 0027).
//
// MODE_ESTIMATE (after a frame's final image): each shading rate tile of that image gets the coarsest rate (per axis:
// 1, 2 or 4 pixels per fragment) whose estimated error stays under the threshold: the luma's gradient along the axis is
// estimated from differences at strides 1, 2 and 4 (the largest is kept, so that a tile shaded coarsely in that frame,
// flat inside its fragments, is not mistaken for flat content), and a fragment covering n pixels misses about 0.5
// (n = 2) or 1.12 (n = 4) gradients per pixel; the largest step between neighbours must stay small too.
//
// MODE_APPLY (the next frame, after its depth pre-pass, before its opaque pass): each tile takes the finest rate (per
// axis) of the estimated tiles around where its content was in the estimated frame: five of its pixels (corners and
// centre) are reprojected with this frame's depth and the camera motion, and the tiles their positions fall in, one
// more on every side (moving objects, rounding), are read. A tile whose content was not on screen, or spread over too
// many tiles, gets full rate. Then two checks on this frame's own content: glossy surfaces whose normals vary between
// neighbouring pixels (their highlights alias when shaded coarsely, which the previous image cannot predict) keep full
// rate, and every tile is shaded at full rate one frame in refresh_period (its next estimate then sees the content's
// full detail).
//
// Disocclusion: the estimate also keeps each tile's range of view depths; a reprojected point whose depth in the
// estimated frame falls outside the range of the tile it lands in (by more than 5 %) was not what that tile showed
// (hidden behind something, newly in front, or moving): its tile gets full rate.
//
// MODE_CLEAR: every tile at full rate.

#if defined(MODE_ESTIMATE) || defined(MODE_APPLY)
layout(local_size_x = 64, local_size_y = 1, local_size_z = 1) in;
#else
layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;
#endif

#ifdef MODE_ESTIMATE
layout(set = 0, binding = 0) uniform sampler2D source_image;
layout(r8ui, set = 0, binding = 1) uniform restrict writeonly uimage2D tile_rates;
layout(set = 0, binding = 5) uniform sampler2D depth_buffer; // the estimated frame's depth (internal size)
layout(set = 0, binding = 6, std140) uniform EstimateData {
	vec4 inv_projection_z; // rows 2 and 3 of the estimated frame's inverse projection: view z and w from NDC
	vec4 inv_projection_w;
}
estimate_data;
layout(rg16f, set = 0, binding = 7) uniform restrict writeonly image2D tile_depths; // nearest, farthest view depth
#endif

#ifdef MODE_APPLY
layout(r8ui, set = 0, binding = 1) uniform restrict readonly uimage2D tile_rates;
layout(r8ui, set = 0, binding = 2) uniform restrict writeonly uimage2D shading_rates;
// Spread over 64 slots (tile index modulo 64) so that the tiles' atomics rarely meet: coarse tiles, fragments saved.
layout(set = 0, binding = 3, std430) buffer restrict Counters {
	uvec2 slots[64];
}
counters;
// This frame's depth pre-pass: normals and roughness, depth (internal size).
layout(set = 0, binding = 4) uniform sampler2D normal_roughness;
layout(set = 0, binding = 5) uniform sampler2D depth_buffer;
layout(set = 0, binding = 6, std140) uniform Reprojection {
	mat4 matrix; // this frame's NDC (pixel centres, depth buffer value) to the estimated frame's clip space
	vec4 world_w; // row 3 of this frame's inverse view-projection: the homogeneous scale of the point the NDC unproject to
}
reprojection;
layout(rg16f, set = 0, binding = 7) uniform restrict readonly image2D tile_depths;
#endif

#ifdef MODE_CLEAR
layout(r8ui, set = 0, binding = 2) uniform restrict writeonly uimage2D shading_rates;
#endif

layout(push_constant, std430) uniform Params {
	ivec2 tile_size; // pixels per shading rate texel (powers of two)
	ivec2 tiles; // shading rate texels
	ivec2 screen_size; // internal size (pixels)
	vec2 source_scale; // source pixels per internal pixel
	ivec2 source_size;
	float threshold;
	uint max_log2; // 1: up to 2 x 2, 2: up to 4 x 4
	uint linear_source; // the source holds linear values (HDR 2D)
	uint frame;
	uint refresh_period; // 0: none; otherwise each tile is shaded at full rate one frame in this many
	float max_step; // largest error allowed at any pixel (the largest neighbour difference times 0.5 or 1.5)
	uint use_normals; // normal_roughness holds this frame's normals and roughness
	float gloss_factor; // a pixel is at risk if 1 - dot(n, neighbour) > gloss_factor * roughness^2
	uint gloss_pixels; // a tile with at least this many pixels at risk keeps full rate
	uint use_depth; // perspective: the tiles' depth ranges are kept and checked (disocclusion)
	uint count_stats; // this frame's apply pass adds its tiles to the counters (one frame in 16)
	uint pad0;
}
params;

uint encode_rate(uint p_lx, uint p_ly) {
	// No 1 x 4 or 4 x 1.
	p_lx = min(p_lx, p_ly + 1u);
	p_ly = min(p_ly, p_lx + 1u);
	return (p_lx << 2u) | p_ly;
}

#ifdef MODE_ESTIMATE
shared float s_luma[32 * 32];
// The tile's partial results, reduced over the workgroup: squared differences at strides 1, 2, 4 along x and stride 1
// along y (a); strides 2, 4 along y and the largest neighbour differences along x and y (b).
shared vec4 s_reduce_a[64];
shared vec4 s_reduce_b[64];
shared vec2 s_reduce_depth[64]; // nearest, farthest view depth

float load_luma(ivec2 p_pixel) {
	ivec2 p = clamp(p_pixel, ivec2(0), params.screen_size - 1);
	ivec2 s = clamp(ivec2((vec2(p) + 0.5) * params.source_scale), ivec2(0), params.source_size - 1);
	vec3 c = texelFetch(source_image, s, 0).rgb;
	if (params.linear_source != 0u) {
		c = pow(clamp(c, vec3(0.0), vec3(1.0)), vec3(1.0 / 2.2));
	}
	return dot(c, vec3(0.2126, 0.7152, 0.0722));
}

void main() {
	ivec2 tile = ivec2(gl_WorkGroupID.xy);
	uint lid = gl_LocalInvocationID.x;
	ivec2 ts = params.tile_size;
	int width_log2 = findMSB(ts.x);
	int n = ts.x * ts.y;
	ivec2 origin = tile * ts;
	vec2 depth_range = vec2(1e30, 0.0);
	for (int i = int(lid); i < n; i += 64) {
		ivec2 pixel = origin + ivec2(i & (ts.x - 1), i >> width_log2);
		s_luma[i] = load_luma(pixel);
		if (params.use_depth != 0u) {
			ivec2 p = min(pixel, params.screen_size - 1);
			vec4 ndc = vec4((vec2(p) + 0.5) / vec2(params.screen_size) * 2.0 - 1.0, texelFetch(depth_buffer, p, 0).r, 1.0);
			float view_depth = -dot(estimate_data.inv_projection_z, ndc) / dot(estimate_data.inv_projection_w, ndc);
			depth_range = vec2(min(depth_range.x, view_depth), max(depth_range.y, view_depth));
		}
	}
	s_reduce_depth[lid] = depth_range;
	barrier();
	// Squared differences at strides 1, 2, 4 along x (sums 0-2) and y (3-5).
	float sums[6] = float[](0.0, 0.0, 0.0, 0.0, 0.0, 0.0);
	float maxima[2] = float[](0.0, 0.0); // the largest difference between neighbours along x and y

	for (int i = int(lid); i < n; i += 64) {
		int x = i & (ts.x - 1);
		int y = i >> width_log2;
		float l = s_luma[i];
		for (int k = 0; k < 3; k++) {
			int s = 1 << k;
			if (x + s < ts.x) {
				float d = s_luma[i + s] - l;
				sums[k] += d * d;
				maxima[0] = max(maxima[0], abs(d) / float(s));
			}
			if (y + s < ts.y) {
				float d = s_luma[i + s * ts.x] - l;
				sums[3 + k] += d * d;
				maxima[1] = max(maxima[1], abs(d) / float(s));
			}
		}
	}
	s_reduce_a[lid] = vec4(sums[0], sums[1], sums[2], sums[3]);
	s_reduce_b[lid] = vec4(sums[4], sums[5], maxima[0], maxima[1]);
	barrier();
	for (uint k = 32u; k > 0u; k >>= 1u) {
		if (lid < k) {
			vec4 a = s_reduce_a[lid + k];
			vec4 b = s_reduce_b[lid + k];
			vec4 own_b = s_reduce_b[lid];
			vec2 d = s_reduce_depth[lid + k];
			vec2 own_d = s_reduce_depth[lid];
			s_reduce_a[lid] += a;
			s_reduce_b[lid] = vec4(own_b.xy + b.xy, max(own_b.zw, b.zw));
			s_reduce_depth[lid] = vec2(min(own_d.x, d.x), max(own_d.y, d.y));
		}
		barrier();
	}
	if (lid == 0u) {
		if (params.use_depth != 0u) {
			imageStore(tile_depths, tile, vec4(s_reduce_depth[0], 0.0, 0.0));
		}
		vec4 a = s_reduce_a[0];
		vec4 b = s_reduce_b[0];
		float total[6] = float[](a.x, a.y, a.z, a.w, b.x, b.y);
		float m[2] = float[](b.z, b.w);
		float g[2] = float[](0.0, 0.0);
		for (int axis = 0; axis < 2; axis++) {
			int along = axis == 0 ? ts.x : ts.y;
			int across = axis == 0 ? ts.y : ts.x;
			for (int k = 0; k < 3; k++) {
				int count = max(along - (1 << k), 0) * across; // pairs of pixels at this stride
				if (count > 0) {
					g[axis] = max(g[axis], sqrt(total[axis * 3 + k] / float(count)) / float(1 << k));
				}
			}
		}
		uint l[2];
		for (int axis = 0; axis < 2; axis++) {
			l[axis] = 0u;
			// Both the tile's RMS error and its largest local step must stay small: a few thin bright features (specular
			// streaks, wires) in a dark tile have a small RMS but would turn into visible blocks.
			if (params.threshold > 0.0) {
				if (params.max_log2 >= 2u && 1.12 * g[axis] < params.threshold && 1.5 * m[axis] < params.max_step) {
					l[axis] = 2u;
				} else if (0.5 * g[axis] < params.threshold && 0.5 * m[axis] < params.max_step) {
					l[axis] = 1u;
				}
			}
		}
		imageStore(tile_rates, tile, uvec4(encode_rate(l[0], l[1])));
	}
}
#endif

#ifdef MODE_APPLY
shared uint s_gloss_risk;
shared int s_box[4]; // estimated tiles reached: min x, min y, max x, max y
shared uint s_unknown; // a sampled point was off the estimated frame's screen

vec4 load_normal_roughness(ivec2 p_pixel) {
	vec4 nr = texelFetch(normal_roughness, clamp(p_pixel, ivec2(0), params.screen_size - 1), 0);
	float r = nr.w > 0.5 ? 1.0 - nr.w : nr.w; // over 0.5: dynamic objects (stored inverted)
	return vec4(normalize(nr.xyz * 2.0 - 1.0), r * (255.0 / 127.0));
}

void main() {
	ivec2 tile = ivec2(gl_WorkGroupID.xy);
	uint lid = gl_LocalInvocationID.x;
	ivec2 ts = params.tile_size;
	int width_log2 = findMSB(ts.x);
	int n = ts.x * ts.y;
	ivec2 origin = tile * ts;
	if (lid == 0u) {
		s_gloss_risk = 0u;
		s_box[0] = 0x7FFFFFFF;
		s_box[1] = 0x7FFFFFFF;
		s_box[2] = -0x7FFFFFFF;
		s_box[3] = -0x7FFFFFFF;
		s_unknown = 0u;
	}
	barrier();
	if (params.use_normals != 0u) {
		uint gloss_risk = 0u;
		for (int i = int(lid); i < n; i += 64) {
			ivec2 pixel = origin + ivec2(i & (ts.x - 1), i >> width_log2);
			vec4 c = load_normal_roughness(pixel);
			if (c.w < 0.5) {
				float limit = params.gloss_factor * c.w * c.w;
				vec3 nx = load_normal_roughness(pixel + ivec2(1, 0)).xyz;
				vec3 ny = load_normal_roughness(pixel + ivec2(0, 1)).xyz;
				if (1.0 - dot(c.xyz, nx) > limit || 1.0 - dot(c.xyz, ny) > limit) {
					gloss_risk++;
				}
			}
		}
		if (gloss_risk != 0u) {
			atomicAdd(s_gloss_risk, gloss_risk);
		}
	}
	if (lid < 5u) {
		ivec2 corner = ivec2(lid & 1u, lid >> 1u) * (ts - 1);
		ivec2 pixel = min(origin + (lid == 4u ? ts / 2 : corner), params.screen_size - 1);
		float depth = texelFetch(depth_buffer, pixel, 0).r;
		vec2 ndc = (vec2(pixel) + 0.5) / vec2(params.screen_size) * 2.0 - 1.0;
		vec4 clip = reprojection.matrix * vec4(ndc, depth, 1.0);
		// The point's depth in the estimated frame: clip.w for the unprojected point with w = 1.
		float estimated_depth = clip.w / dot(reprojection.world_w, vec4(ndc, depth, 1.0));
		vec2 previous = (clip.xy / clip.w * 0.5 + 0.5) * vec2(params.screen_size);
		if (!(clip.w > 1e-6) || any(lessThan(previous, vec2(0.0))) || any(greaterThanEqual(previous, vec2(params.screen_size)))) {
			atomicOr(s_unknown, 1u);
		} else {
			ivec2 t = ivec2(previous) / ts;
			if (params.use_depth != 0u) {
				// Disocclusion: this point's depth in the estimated frame against what its tile showed there.
				vec2 range = imageLoad(tile_depths, t).xy;
				if (estimated_depth < range.x * 0.95 || estimated_depth > range.y * 1.05) {
					atomicOr(s_unknown, 1u);
				}
			}
			atomicMin(s_box[0], t.x);
			atomicMin(s_box[1], t.y);
			atomicMax(s_box[2], t.x);
			atomicMax(s_box[3], t.y);
		}
	}
	barrier();
	if (lid == 0u) {
		uint rate = 0u;
		ivec2 lo = ivec2(s_box[0], s_box[1]) - 1;
		ivec2 hi = ivec2(s_box[2], s_box[3]) + 1;
		if (s_unknown == 0u && all(lessThanEqual(hi - lo, ivec2(6))) && s_gloss_risk < params.gloss_pixels) {
			lo = max(lo, ivec2(0));
			hi = min(hi, params.tiles - 1);
			uint lx = 3u;
			uint ly = 3u;
			for (int y = lo.y; y <= hi.y; y++) {
				for (int x = lo.x; x <= hi.x; x++) {
					uint r = imageLoad(tile_rates, ivec2(x, y)).r;
					lx = min(lx, r >> 2u);
					ly = min(ly, r & 3u);
				}
			}
			rate = encode_rate(lx, ly);
		}
		if (params.refresh_period > 0u && (uint(tile.x) + 3u * uint(tile.y) + params.frame) % params.refresh_period == 0u) {
			// Full rate now and then: a tile shaded coarsely looks flatter than its content (fine texture detail is
			// lost), so its estimate is renewed from a full-rate image.
			rate = 0u;
		}
		imageStore(shading_rates, tile, uvec4(rate));
		if (rate != 0u && params.count_stats != 0u) {
			uint pixels = uint(n);
			uint slot = (uint(tile.x) + uint(tile.y) * uint(params.tiles.x)) & 63u;
			atomicAdd(counters.slots[slot].x, 1u);
			atomicAdd(counters.slots[slot].y, pixels - (pixels >> ((rate >> 2u) + (rate & 3u))));
		}
	}
}
#endif

#ifdef MODE_CLEAR
void main() {
	ivec2 t = ivec2(gl_GlobalInvocationID.xy);
	if (all(lessThan(t, params.tiles))) {
		imageStore(shading_rates, t, uvec4(0u));
	}
}
#endif
