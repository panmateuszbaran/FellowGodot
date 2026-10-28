#[versions]

evolve = "#define MODE_EVOLVE";
fft = "#define MODE_FFT";
assemble = "#define MODE_ASSEMBLE";
downsample = "#define MODE_DOWNSAMPLE";
caustics = "#define MODE_CAUSTICS";

#[compute]

#version 450

#VERSION_DEFINES

// FellowEngine wind waves (FellowWater3D): spectral ocean / lake waves after Tessendorf, in up to four cascades
// (tiles of different length sharing one N x N grid each; a cascade covers one band of wave numbers).
//
// Per frame:
//   EVOLVE    h(k, t) = h0(k) e^{i w t} + conj(h0(-k)) e^{-i w t}; the eight fields (displacement x, y, z, the height's
//             slopes, the horizontal displacement's derivatives) packed into four complex spectra (two images).
//   FFT       inverse FFT along rows, then along columns (Stockham radix 2 in shared memory, one row per group).
//   ASSEMBLE  displacement (x, y, z, foam) and slopes (sx, sz, sx^2, sz^2) per cascade; the foam accumulates where
//             the surface folds (Jacobian below a threshold) and decays.
//   DOWNSAMPLE mip levels (2 x 2 means: slope variance survives as E[s^2] - E[s]^2).
//   CAUSTICS  the sunlight's convergence below a cascade's surface at two depths (area ratio of the refracted rays).
//
// The dispersion relation is quantized to multiples of 2 pi / repeat_period so the waves repeat exactly and the
// phase stays precise however long the clock runs (the CPU passes t / repeat_period, fractional part).

#define PI 3.14159265358979

#ifdef MODE_FFT
#define GROUP_SIZE 256
#else
#define GROUP_SIZE 8
#endif

#ifdef MODE_FFT
layout(local_size_x = GROUP_SIZE, local_size_y = 1, local_size_z = 1) in;
#else
layout(local_size_x = GROUP_SIZE, local_size_y = GROUP_SIZE, local_size_z = 1) in;
#endif

layout(push_constant, std430) uniform Params {
	int size; // N
	int log2_size;
	int cascades;
	int fft_vertical; // FFT: 0 rows, 1 columns
	vec4 lengths; // tile length of each cascade (m)
	float time_fraction; // fract(t / repeat_period)
	float repeat_period; // s
	float gravity;
	float choppiness;
	float foam_decay; // exp(-decay * dt) applied per frame (CPU computes the factor)
	float foam_threshold; // Jacobian below which foam is produced
	float foam_gain; // foam produced per unit of (threshold - J) per frame
	float caustic_depth0; // m
	vec4 sun_direction; // xyz: the sunlight's direction of travel in air (normalized, y < 0); w: caustic_depth1 (m)
	int dst_size; // DOWNSAMPLE: size of the destination level
	int pad0;
	int pad1;
	int pad2;
}
params;

#ifdef MODE_EVOLVE
layout(rgba32f, set = 0, binding = 0) uniform restrict readonly image2DArray h0_image;
layout(rgba32f, set = 0, binding = 1) uniform restrict writeonly image2DArray spectrum0;
layout(rgba32f, set = 0, binding = 2) uniform restrict writeonly image2DArray spectrum1;

vec2 cmul(vec2 a, vec2 b) {
	return vec2(a.x * b.x - a.y * b.y, a.x * b.y + a.y * b.x);
}

void main() {
	ivec3 c = ivec3(gl_GlobalInvocationID.xy, gl_GlobalInvocationID.z);
	if (c.x >= params.size || c.y >= params.size || c.z >= params.cascades) {
		return;
	}
	float L = params.lengths[c.z];
	vec2 k = (vec2(c.xy) - float(params.size / 2)) * (2.0 * PI / L);
	float kl = length(k);
	vec4 s0 = vec4(0.0);
	vec4 s1 = vec4(0.0);
	if (kl > 1e-6) {
		vec4 h0 = imageLoad(h0_image, c);
		float w0 = 2.0 * PI / params.repeat_period;
		float w = floor(sqrt(params.gravity * kl) / w0); // quantized: an integer multiple of w0
		float phase = 2.0 * PI * fract(w * params.time_fraction);
		vec2 e = vec2(cos(phase), sin(phase));
		vec2 h = cmul(h0.xy, e) + cmul(h0.zw, vec2(e.x, -e.y));
		vec2 ih = vec2(-h.y, h.x);
		vec2 dx = ih * (k.x / kl);
		vec2 dz = ih * (k.y / kl);
		vec2 dydx = ih * k.x;
		vec2 dydz = ih * k.y;
		vec2 dxdx = h * (-k.x * k.x / kl);
		vec2 dzdz = h * (-k.y * k.y / kl);
		vec2 dxdz = h * (-k.x * k.y / kl);
		// Two real fields per complex spectrum: F + i G.
		s0 = vec4(dx.x - dz.y, dx.y + dz.x, h.x - dxdz.y, h.y + dxdz.x);
		s1 = vec4(dydx.x - dydz.y, dydx.y + dydz.x, dxdx.x - dzdz.y, dxdx.y + dzdz.x);
	}
	imageStore(spectrum0, c, s0);
	imageStore(spectrum1, c, s1);
}
#endif

#ifdef MODE_FFT
// Inverse FFT (unnormalized, e^{+i}) of one row or column of one image per group: two complex numbers per element.
layout(rgba32f, set = 0, binding = 0) uniform restrict image2DArray spectrum0;
layout(rgba32f, set = 0, binding = 1) uniform restrict image2DArray spectrum1;

shared vec4 buf[2][512];

vec2 cmul(vec2 a, vec2 b) {
	return vec2(a.x * b.x - a.y * b.y, a.x * b.y + a.y * b.x);
}

ivec3 coord(int line, int i, int layer) {
	return params.fft_vertical != 0 ? ivec3(line, i, layer) : ivec3(i, line, layer);
}

void main() {
	int j = int(gl_LocalInvocationID.x);
	int line = int(gl_WorkGroupID.x);
	int layer = int(gl_WorkGroupID.y);
	bool second = gl_WorkGroupID.z != 0;
	int n = params.size;
	int half_n = n / 2;
	if (j < half_n) {
		if (second) {
			buf[0][j] = imageLoad(spectrum1, coord(line, j, layer));
			buf[0][j + half_n] = imageLoad(spectrum1, coord(line, j + half_n, layer));
		} else {
			buf[0][j] = imageLoad(spectrum0, coord(line, j, layer));
			buf[0][j + half_n] = imageLoad(spectrum0, coord(line, j + half_n, layer));
		}
	}
	barrier();
	int src = 0;
	for (int s = 0; s < params.log2_size; s++) {
		int ns = 1 << s;
		if (j < half_n) {
			vec4 v0 = buf[src][j];
			vec4 v1 = buf[src][j + half_n];
			int jm = j & (ns - 1);
			float a = PI * float(jm) / float(ns);
			vec2 tw = vec2(cos(a), sin(a));
			v1 = vec4(cmul(v1.xy, tw), cmul(v1.zw, tw));
			int d = (j - jm) * 2 + jm;
			buf[1 - src][d] = v0 + v1;
			buf[1 - src][d + ns] = v0 - v1;
		}
		barrier();
		src = 1 - src;
	}
	if (j < half_n) {
		if (second) {
			imageStore(spectrum1, coord(line, j, layer), buf[src][j]);
			imageStore(spectrum1, coord(line, j + half_n, layer), buf[src][j + half_n]);
		} else {
			imageStore(spectrum0, coord(line, j, layer), buf[src][j]);
			imageStore(spectrum0, coord(line, j + half_n, layer), buf[src][j + half_n]);
		}
	}
}
#endif

#ifdef MODE_ASSEMBLE
layout(rgba32f, set = 0, binding = 0) uniform restrict readonly image2DArray spectrum0;
layout(rgba32f, set = 0, binding = 1) uniform restrict readonly image2DArray spectrum1;
layout(rgba16f, set = 0, binding = 2) uniform restrict image2DArray displacement;
layout(rgba16f, set = 0, binding = 3) uniform restrict writeonly image2DArray slopes;

void main() {
	ivec3 c = ivec3(gl_GlobalInvocationID.xy, gl_GlobalInvocationID.z);
	if (c.x >= params.size || c.y >= params.size || c.z >= params.cascades) {
		return;
	}
	// The spectrum is centred (k index - N/2): the result carries a (-1)^(x + y) factor.
	float sgn = ((c.x + c.y) & 1) != 0 ? -1.0 : 1.0;
	vec4 a = imageLoad(spectrum0, c) * sgn;
	vec4 b = imageLoad(spectrum1, c) * sgn;
	float lambda = params.choppiness;
	float dx = a.x;
	float dz = a.y;
	float dy = a.z;
	float dxdz = a.w;
	float dydx = b.x;
	float dydz = b.y;
	float dxdx = b.z;
	float dzdz = b.w;
	float jxx = 1.0 + lambda * dxdx;
	float jzz = 1.0 + lambda * dzdz;
	float jac = jxx * jzz - lambda * lambda * dxdz * dxdz;
	float foam = imageLoad(displacement, c).w * params.foam_decay;
	foam = clamp(foam + max(params.foam_threshold - jac, 0.0) * params.foam_gain, 0.0, 1.0);
	// Slopes of the displaced surface (the horizontal displacement compresses crests).
	vec2 slope = vec2(dydx / max(jxx, 0.5), dydz / max(jzz, 0.5));
	imageStore(displacement, c, vec4(lambda * dx, dy, lambda * dz, foam));
	imageStore(slopes, c, vec4(slope, slope * slope));
}
#endif

#ifdef MODE_DOWNSAMPLE
layout(rgba16f, set = 0, binding = 0) uniform restrict readonly image2DArray src_level;
layout(rgba16f, set = 0, binding = 1) uniform restrict writeonly image2DArray dst_level;

void main() {
	ivec3 c = ivec3(gl_GlobalInvocationID.xy, gl_GlobalInvocationID.z);
	if (c.x >= params.dst_size || c.y >= params.dst_size || c.z >= params.cascades) {
		return;
	}
	ivec3 s = ivec3(c.xy * 2, c.z);
	vec4 v = imageLoad(src_level, s) + imageLoad(src_level, s + ivec3(1, 0, 0)) + imageLoad(src_level, s + ivec3(0, 1, 0)) + imageLoad(src_level, s + ivec3(1, 1, 0));
	imageStore(dst_level, c, v * 0.25);
}
#endif

#ifdef MODE_CAUSTICS
// Light focused by the waves of one cascade: the sun's rays refracted at each texel of the (periodic) surface reach
// a plane `depth` below at p + offset(p); the light per unit area there is the area ratio 1 / det(d(p + offset) / dp).
// Two depths per texel (r: caustic_depth0, g: caustic_depth1); the shader interpolates and fades with depth.
layout(rgba16f, set = 0, binding = 0) uniform restrict readonly image2DArray slopes;
layout(rgba16f, set = 0, binding = 1) uniform restrict writeonly image2DArray caustics;

vec2 ray_offset(ivec3 c, float depth) {
	ivec2 p = (c.xy + params.size) % params.size;
	vec2 s = imageLoad(slopes, ivec3(p, c.z)).xy;
	vec3 n = normalize(vec3(-s.x, 1.0, -s.y));
	vec3 t = refract(params.sun_direction.xyz, n, 1.0 / 1.333);
	return t.xz * (depth / max(-t.y, 0.05));
}

float intensity(ivec3 c, float depth, float texel) {
	vec2 ox0 = ray_offset(c + ivec3(-1, 0, 0), depth);
	vec2 ox1 = ray_offset(c + ivec3(1, 0, 0), depth);
	vec2 oz0 = ray_offset(c + ivec3(0, -1, 0), depth);
	vec2 oz1 = ray_offset(c + ivec3(0, 1, 0), depth);
	vec2 ddx = vec2(1.0, 0.0) + (ox1 - ox0) / (2.0 * texel);
	vec2 ddz = vec2(0.0, 1.0) + (oz1 - oz0) / (2.0 * texel);
	float det = abs(ddx.x * ddz.y - ddx.y * ddz.x);
	return min(1.0 / max(det, 0.1), 5.0);
}

void main() {
	ivec3 c = ivec3(gl_GlobalInvocationID.xy, gl_GlobalInvocationID.z);
	if (c.x >= params.size || c.y >= params.size || c.z >= params.cascades) {
		return;
	}
	float texel = params.lengths[c.z] / float(params.size);
	float i0 = intensity(c, params.caustic_depth0, texel);
	float i1 = intensity(c, params.sun_direction.w, texel);
	imageStore(caustics, c, vec4(i0, i1, 0.0, 1.0));
}
#endif
