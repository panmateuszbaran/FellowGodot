#[compute]

#version 450

#VERSION_DEFINES

// FellowEngine exposure (docs/decisions/0105-exposure-histogram.md, 0106-local-exposure.md).
//
// MODE_BUILD: one workgroup per 32 x 32 pixel cell: each pixel's log2 luminance into the frame's histogram (global
// exposure) and into the cell's column of a bilateral grid (local exposure: per log-luminance slice the sum of the log
// luminances and the count, over the cell's pixels), and the cell's mean log luminance.
// MODE_GRID_BLUR: the grid blurred by a 3 x 3 x 3 binomial kernel.
// MODE_TILE_BLUR: the cells' mean log luminance blurred by a separable Gaussian (one axis per dispatch).
// MODE_ADAPT: the histogram's mean luminance between two percentiles, the eye's adaptation to it (exponential in log2,
// faster as the exposure rises), clamped; written where the tone mapper reads the frame's average luminance.

#define TILE 32
#define SLICES 32
#define BINS 64
#define HIST_MIN -14.0
#define HIST_MAX 14.0
#define GRID_MIN -16.0
#define GRID_MAX 16.0

#define FLAG_HISTOGRAM 1
#define FLAG_LOCAL 2
#define FLAG_IMMEDIATE 4

layout(push_constant, std430) uniform Params {
	ivec2 size;
	ivec2 cells;

	float luminance_multiplier;
	float low_percent;
	float high_percent;
	float speed_up;

	float speed_down;
	float dt;
	float min_luminance;
	float max_luminance;

	int flags;
	int blur_radius;
	float blur_sigma;
	int blur_axis;
}
params;

float fellow_log_luminance(vec3 c) {
	float lum = dot(c, vec3(0.2126, 0.7152, 0.0722));
	if (!(lum > 0.0)) {
		lum = 0.0; // negative, zero or NaN
	}
	return log2(max(min(lum, 65504.0), 1.0 / 65536.0));
}

#ifdef MODE_BUILD

layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;

layout(set = 0, binding = 0) uniform sampler2D source_color;

layout(set = 1, binding = 0, std430) restrict buffer Histogram {
	uint bins[BINS];
}
histogram;
layout(rg16f, set = 1, binding = 1) uniform restrict writeonly image3D grid;
layout(r16f, set = 1, binding = 2) uniform restrict writeonly image2D tile_mean;

shared uint s_hist[BINS];
shared uint s_sum[SLICES];
shared uint s_count[SLICES];
shared uint s_total;

void main() {
	uint t = gl_LocalInvocationIndex;
	if (t < BINS) {
		s_hist[t] = 0u;
	}
	if (t < SLICES) {
		s_sum[t] = 0u;
		s_count[t] = 0u;
	}
	if (t == 0u) {
		s_total = 0u;
	}
	barrier();

	bool use_histogram = (params.flags & FLAG_HISTOGRAM) != 0;
	bool use_local = (params.flags & FLAG_LOCAL) != 0;
	ivec2 cell = ivec2(gl_WorkGroupID.xy);
	ivec2 base = cell * TILE + ivec2(gl_LocalInvocationID.xy) * 2;
	for (int j = 0; j < 4; j++) {
		ivec2 p = base + ivec2(j & 1, j >> 1);
		if (any(greaterThanEqual(p, params.size))) {
			continue;
		}
		vec3 c = texelFetch(source_color, p, 0).rgb;
		if (use_histogram) {
			// The stored average is in the source's units (the tone mapper applies the luminance multiplier).
			float l = fellow_log_luminance(c);
			int b = clamp(int(floor((l - HIST_MIN) * (float(BINS) / (HIST_MAX - HIST_MIN)))), 0, BINS - 1);
			atomicAdd(s_hist[b], 1u);
		}
		if (use_local) {
			float l = fellow_log_luminance(c * params.luminance_multiplier);
			float g = clamp(l, GRID_MIN, GRID_MAX - 0.001) - GRID_MIN;
			int s = clamp(int(g * (float(SLICES) / (GRID_MAX - GRID_MIN))), 0, SLICES - 1);
			uint fx = uint(g * 256.0 + 0.5);
			atomicAdd(s_sum[s], fx);
			atomicAdd(s_count[s], 1u);
			atomicAdd(s_total, fx);
		}
	}
	barrier();

	if (use_histogram && t < BINS && s_hist[t] != 0u) {
		atomicAdd(histogram.bins[t], s_hist[t]);
	}
	if (use_local) {
		ivec2 tile_px = min(params.size - cell * TILE, ivec2(TILE));
		float n = float(max(tile_px.x * tile_px.y, 1));
		if (t < SLICES) {
			// Premultiplied by the slice's share of the cell's pixels: interpolated, the ratio is a weighted mean.
			imageStore(grid, ivec3(cell, int(t)), vec4(float(s_sum[t]) / (256.0 * n), float(s_count[t]) / n, 0.0, 0.0));
		}
		if (t == 0u) {
			imageStore(tile_mean, cell, vec4(float(s_total) / (256.0 * n)));
		}
	}
}

#endif

#ifdef MODE_GRID_BLUR

layout(local_size_x = 4, local_size_y = 4, local_size_z = 4) in;

layout(set = 0, binding = 0) uniform sampler3D grid_in;
layout(rg16f, set = 1, binding = 0) uniform restrict writeonly image3D grid_out;

void main() {
	ivec3 p = ivec3(gl_GlobalInvocationID.xyz);
	ivec3 dims = ivec3(params.cells, SLICES);
	if (any(greaterThanEqual(p, dims))) {
		return;
	}
	vec2 sum = vec2(0.0);
	for (int z = -1; z <= 1; z++) {
		for (int y = -1; y <= 1; y++) {
			for (int x = -1; x <= 1; x++) {
				float w = float((2 - abs(x)) * (2 - abs(y)) * (2 - abs(z)));
				ivec3 q = clamp(p + ivec3(x, y, z), ivec3(0), dims - 1);
				sum += texelFetch(grid_in, q, 0).rg * w;
			}
		}
	}
	imageStore(grid_out, p, vec4(sum / 64.0, 0.0, 0.0));
}

#endif

#ifdef MODE_TILE_BLUR

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

layout(set = 0, binding = 0) uniform sampler2D tile_in;
layout(r16f, set = 1, binding = 0) uniform restrict writeonly image2D tile_out;

void main() {
	ivec2 p = ivec2(gl_GlobalInvocationID.xy);
	if (any(greaterThanEqual(p, params.cells))) {
		return;
	}
	ivec2 dir = params.blur_axis == 0 ? ivec2(1, 0) : ivec2(0, 1);
	float sum = 0.0;
	float wsum = 0.0;
	float k = -0.5 / max(params.blur_sigma * params.blur_sigma, 1e-4);
	for (int i = -params.blur_radius; i <= params.blur_radius; i++) {
		ivec2 q = clamp(p + dir * i, ivec2(0), params.cells - 1);
		float w = exp(float(i * i) * k);
		sum += texelFetch(tile_in, q, 0).r * w;
		wsum += w;
	}
	imageStore(tile_out, p, vec4(sum / wsum));
}

#endif

#ifdef MODE_ADAPT

layout(local_size_x = BINS, local_size_y = 1, local_size_z = 1) in;

layout(set = 0, binding = 0, std430) restrict buffer Histogram {
	uint bins[BINS];
}
histogram;
layout(r32f, set = 0, binding = 1) uniform restrict image2D current;

shared float s_vals[BINS];

void main() {
	uint t = gl_LocalInvocationIndex;
	s_vals[t] = float(histogram.bins[t]);
	histogram.bins[t] = 0u; // for the next frame
	barrier();
	if (t != 0u) {
		return;
	}
	float total = 0.0;
	for (int i = 0; i < BINS; i++) {
		total += s_vals[i];
	}
	// The mean luminance of the pixels between the two percentiles (as Unreal's histogram eye adaptation).
	float low = total * params.low_percent;
	float high = total * params.high_percent;
	float sum = 0.0;
	float count = 0.0;
	for (int i = 0; i < BINS; i++) {
		float v = s_vals[i];
		float sub = min(v, low);
		v -= sub;
		low -= sub;
		high -= sub;
		v = min(v, high);
		high -= v;
		float bucket_log = HIST_MIN + (float(i) + 0.5) * ((HIST_MAX - HIST_MIN) / float(BINS));
		sum += exp2(bucket_log) * v;
		count += v;
	}
	float prev = imageLoad(current, ivec2(0, 0)).r;
	if (!(count > 0.0)) {
		if (prev > 0.0) {
			return; // nothing measured: keep the exposure
		}
		sum = 1.0;
		count = 1.0;
	}
	float target = clamp(sum / count, params.min_luminance, params.max_luminance);
	float result = target;
	if ((params.flags & FLAG_IMMEDIATE) == 0 && prev > 0.0 && !isinf(prev)) {
		float diff = log2(target) - log2(prev);
		// The luminance falling is the exposure rising (Unreal's "speed up").
		float speed = diff < 0.0 ? params.speed_up : params.speed_down;
		result = exp2(log2(prev) + diff * (1.0 - exp2(-params.dt * speed)));
	}
	imageStore(current, ivec2(0, 0), vec4(result));
}

#endif
