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
// The depth pre-pass's normals and roughness (internal size): glossy surfaces whose normals vary between neighbouring
// pixels alias their highlights when shaded coarsely (a coarse fragment can land on a peak the full-rate image only
// shows as faint sub-pixel streaks), which the previous frame's image cannot predict.
layout(set = 0, binding = 4) uniform sampler2D normal_roughness;
#endif

#ifdef MODE_DILATE
shared uint s_tiles;
shared uint s_coarse_tiles;
shared uint s_pixels;
shared uint s_fragments;
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
	float max_step; // largest error allowed at any pixel (the largest neighbour difference times 0.5 or 1.5)
	uint use_normals; // normal_roughness holds this frame's normals and roughness
	float gloss_factor; // a pixel is at risk if 1 - dot(n, neighbour) > gloss_factor * roughness^2
	uint gloss_pixels; // a tile with at least this many pixels at risk keeps full rate
	uint pad3;
}
params;

#ifdef MODE_ESTIMATE
shared float s_luma[32 * 32];
// The tile's partial results, reduced over the workgroup: squared differences at strides 1, 2, 4 along x and stride 1
// along y (a); strides 2, 4 along y and the largest neighbour differences along x and y (b).
shared vec4 s_reduce_a[64];
shared vec4 s_reduce_b[64];
shared uint s_gloss_risk;

vec4 load_normal_roughness(ivec2 p_pixel) {
	vec4 nr = texelFetch(normal_roughness, clamp(p_pixel, ivec2(0), params.screen_size - 1), 0);
	float r = nr.w > 0.5 ? 1.0 - nr.w : nr.w; // over 0.5: dynamic objects (stored inverted)
	return vec4(normalize(nr.xyz * 2.0 - 1.0), r * (255.0 / 127.0));
}

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
	ivec2 ts = params.tile_size; // powers of two (Vulkan's shading rate texel sizes)
	int width_log2 = findMSB(ts.x);
	int n = ts.x * ts.y;
	ivec2 origin = tile * ts;
	if (lid == 0u) {
		s_gloss_risk = 0u;
	}
	barrier();
	uint gloss_risk = 0u;
	for (int i = int(lid); i < n; i += 64) {
		ivec2 pixel = origin + ivec2(i & (ts.x - 1), i >> width_log2);
		s_luma[i] = load_luma(pixel);
		if (params.use_normals != 0u) {
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
	}
	if (gloss_risk != 0u) {
		atomicAdd(s_gloss_risk, gloss_risk);
	}
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
			s_reduce_a[lid] += a;
			s_reduce_b[lid] = vec4(own_b.xy + b.xy, max(own_b.zw, b.zw));
		}
		barrier();
	}
	if (lid == 0u) {
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
			if (params.threshold > 0.0 && s_gloss_risk < params.gloss_pixels) {
				if (params.max_log2 >= 2u && 1.12 * g[axis] < params.threshold && 1.5 * m[axis] < params.max_step) {
					l[axis] = 2u;
				} else if (0.5 * g[axis] < params.threshold && 0.5 * m[axis] < params.max_step) {
					l[axis] = 1u;
				}
			}
		}
		imageStore(tile_rates, tile, uvec4(encode_rate(l[0], l[1])));
	}
#endif

#ifdef MODE_DILATE
	ivec2 t = ivec2(gl_GlobalInvocationID.xy);
	uint lid = gl_LocalInvocationIndex;
	if (lid == 0u) {
		s_tiles = 0u;
		s_coarse_tiles = 0u;
		s_pixels = 0u;
		s_fragments = 0u;
	}
	barrier();
	if (all(lessThan(t, params.tiles))) {
		uint lx = 3u;
		uint ly = 3u;
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
		atomicAdd(s_tiles, 1u);
		if (rate != 0u) {
			atomicAdd(s_coarse_tiles, 1u);
		}
		atomicAdd(s_pixels, pixels);
		atomicAdd(s_fragments, pixels >> (lx + ly));
	}
	barrier();
	if (lid == 0u && s_tiles != 0u) {
		atomicAdd(counters.tiles, s_tiles);
		atomicAdd(counters.coarse_tiles, s_coarse_tiles);
		atomicAdd(counters.pixels, s_pixels);
		atomicAdd(counters.fragments, s_fragments);
	}
#endif

#ifdef MODE_CLEAR
	ivec2 t = ivec2(gl_GlobalInvocationID.xy);
	if (all(lessThan(t, params.tiles))) {
		imageStore(shading_rates, t, uvec4(0u));
	}
#endif
}
