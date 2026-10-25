#[compute]

#version 450

#VERSION_DEFINES

// FellowEngine bloom (docs/decisions/0112-bloom.md) and lens flare (0113): an energy-conserving bloom of the whole
// image, without threshold or cap (Jimenez, "Next Generation Post Processing in Call of Duty: Advanced Warfare",
// SIGGRAPH 2014; Unreal's convolution bloom keeps the energy too): a chain of half-size downsamples with a 13-tap
// filter, then upsamples with a 3 x 3 tent, each level
// mixing its own downsample with the wider level below it by `scatter`. The weights sum to 1 at every level, so the
// bloom has the image's energy; the tone mapping pass mixes it in. The lens flare's ghosts and halo are sampled from a
// blurred level (Chapman's screen-space lens flare), above a threshold.
//
// MODE_DOWNSAMPLE_FIRST: the scene colour (full resolution, scene-linear) to half resolution; samples sanitized (NaN, and
// infinity: a sun disk beyond half float's range).
// MODE_DOWNSAMPLE: one level to the next.
// MODE_UPSAMPLE: dest = mix(this level's downsample, tent(the level below), scatter).
// MODE_FLARE: ghosts and halo from a blurred level, at quarter resolution.
// MODE_SHAFTS_MASK, MODE_SHAFTS_BLUR: light shafts (decision 0116): the bright sky (above a threshold, fading away from the
// sun) at quarter resolution, blurred radially towards the sun on the screen.

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

layout(push_constant, std430) uniform Params {
	ivec2 dest_size;
	vec2 source_texel; // 1 / the source's size
	float scatter;
	float flare_threshold;
	float flare_ghost_spacing;
	float flare_halo_width;
	float flare_distortion;
	float pad0;
	float pad1;
	float pad2;
	vec4 shafts; // the sun on the screen (uv), the shafts' threshold (scene-linear), their length (of the way to the sun)
}
params;

layout(set = 0, binding = 0) uniform sampler2D source; // linear, clamped
layout(set = 0, binding = 1) uniform sampler2D source_low; // MODE_UPSAMPLE: the level below (linear, clamped)
layout(rgba16f, set = 1, binding = 0) uniform restrict writeonly image2D dest;

float luma(vec3 c) {
	return dot(c, vec3(0.2126, 0.7152, 0.0722));
}

#if defined(MODE_DOWNSAMPLE_FIRST) || defined(MODE_DOWNSAMPLE)
vec3 fetch(vec2 p_uv) {
	vec3 c = textureLod(source, p_uv, 0.0).rgb;
#ifdef MODE_DOWNSAMPLE_FIRST
	// Half float's largest finite value: a sun disk stored as infinity counts as the brightest the buffer holds.
	c = mix(c, vec3(65504.0), greaterThan(c, vec3(65504.0)));
	c = mix(c, vec3(0.0), isnan(c));
	c = max(c, vec3(0.0));
#endif
	return c;
}

void main() {
	ivec2 pos = ivec2(gl_GlobalInvocationID.xy);
	if (any(greaterThanEqual(pos, params.dest_size))) {
		return;
	}
	vec2 uv = (vec2(pos) + 0.5) / vec2(params.dest_size);
	vec2 t = params.source_texel;
	vec3 a = fetch(uv + t * vec2(-2.0, -2.0));
	vec3 b = fetch(uv + t * vec2(0.0, -2.0));
	vec3 c = fetch(uv + t * vec2(2.0, -2.0));
	vec3 d = fetch(uv + t * vec2(-1.0, -1.0));
	vec3 e = fetch(uv + t * vec2(1.0, -1.0));
	vec3 f = fetch(uv + t * vec2(-2.0, 0.0));
	vec3 g = fetch(uv);
	vec3 h = fetch(uv + t * vec2(2.0, 0.0));
	vec3 i = fetch(uv + t * vec2(-1.0, 1.0));
	vec3 j = fetch(uv + t * vec2(1.0, 1.0));
	vec3 k = fetch(uv + t * vec2(-2.0, 2.0));
	vec3 l = fetch(uv + t * vec2(0.0, 2.0));
	vec3 m = fetch(uv + t * vec2(2.0, 2.0));
	// The 13 taps as five overlapping 2 x 2 boxes: the centre one 0.5, the four corner ones 0.125 each. No Karis
	// average: it weights a box by 1 / (1 + its luma) and so removes a sun disk a few pixels wide from the bloom (the input
	// is TAA's, without fireflies to suppress).
	vec3 result = (d + e + i + j) * 0.125 + (a + b + f + g + b + c + g + h + f + g + k + l + g + h + l + m) * 0.03125;
	imageStore(dest, pos, vec4(min(result, vec3(65504.0)), 1.0));
}
#endif

#ifdef MODE_UPSAMPLE
void main() {
	ivec2 pos = ivec2(gl_GlobalInvocationID.xy);
	if (any(greaterThanEqual(pos, params.dest_size))) {
		return;
	}
	vec2 uv = (vec2(pos) + 0.5) / vec2(params.dest_size);
	// A 3 x 3 tent over the lower level's texels (its texel size in source_texel).
	vec2 t = params.source_texel;
	vec3 s = textureLod(source_low, uv, 0.0).rgb * 4.0;
	s += (textureLod(source_low, uv + vec2(-t.x, 0.0), 0.0).rgb + textureLod(source_low, uv + vec2(t.x, 0.0), 0.0).rgb +
				 textureLod(source_low, uv + vec2(0.0, -t.y), 0.0).rgb + textureLod(source_low, uv + vec2(0.0, t.y), 0.0).rgb) *
			2.0;
	s += textureLod(source_low, uv - t, 0.0).rgb + textureLod(source_low, uv + t, 0.0).rgb +
			textureLod(source_low, uv + vec2(-t.x, t.y), 0.0).rgb + textureLod(source_low, uv + vec2(t.x, -t.y), 0.0).rgb;
	vec3 low = s * (1.0 / 16.0);
	vec3 own = texelFetch(source, pos, 0).rgb;
	imageStore(dest, pos, vec4(mix(own, low, params.scatter), 1.0));
}
#endif

#ifdef MODE_FLARE
vec3 bright(vec2 p_uv, vec2 p_dir) {
	// Chromatic: red and blue displaced along the ghost's direction.
	vec2 o = p_dir * params.flare_distortion;
	vec3 c = vec3(textureLod(source, p_uv + o, 0.0).r, textureLod(source, p_uv, 0.0).g, textureLod(source, p_uv - o, 0.0).b);
	float l = luma(c);
	return c * (max(l - params.flare_threshold, 0.0) / max(l, 1e-4));
}

void main() {
	ivec2 pos = ivec2(gl_GlobalInvocationID.xy);
	if (any(greaterThanEqual(pos, params.dest_size))) {
		return;
	}
	vec2 uv = vec2(1.0) - (vec2(pos) + 0.5) / vec2(params.dest_size); // the flare mirrors the image through its centre
	vec2 ghost_vec = (vec2(0.5) - uv) * params.flare_ghost_spacing;
	vec2 dir = normalize(ghost_vec + 1e-6);
	// Six ghosts along the line through the centre, tinted (a lens's coatings), fading away from the centre.
	const vec3 tints[6] = vec3[](vec3(1.0, 0.8, 0.4), vec3(0.4, 0.8, 1.0), vec3(0.9, 0.5, 1.0), vec3(0.5, 1.0, 0.6),
			vec3(1.0, 0.6, 0.5), vec3(0.6, 0.7, 1.0));
	vec3 result = vec3(0.0);
	for (int k = 0; k < 6; k++) {
		vec2 o = fract(uv + ghost_vec * float(k));
		float w = 1.0 - clamp(length(vec2(0.5) - o) / 0.70710678, 0.0, 1.0);
		w = w * w * w;
		result += bright(o, dir) * tints[k] * w;
	}
	// The halo: a ring at a fixed distance from the centre, towards the bright source.
	vec2 aspect = vec2(1.0, float(params.dest_size.y) / float(params.dest_size.x));
	vec2 h = vec2(0.5) + normalize((uv - vec2(0.5)) / aspect + 1e-6) * aspect * params.flare_halo_width;
	float hw = 1.0 - clamp(length(vec2(0.5) - fract(h)) / 0.70710678, 0.0, 1.0);
	result += bright(fract(h), dir) * pow(hw, 5.0) * 0.5;
	imageStore(dest, pos, vec4(result * (1.0 / 6.0), 1.0));
}
#endif

#ifdef MODE_SHAFTS_MASK
// source: the bloom's quarter level (scene colour); source_low: the depth buffer (full resolution; the sky at 0).
void main() {
	ivec2 pos = ivec2(gl_GlobalInvocationID.xy);
	if (any(greaterThanEqual(pos, params.dest_size))) {
		return;
	}
	vec2 uv = (vec2(pos) + 0.5) / vec2(params.dest_size);
	ivec2 depth_size = textureSize(source_low, 0);
	ivec2 base = ivec2(uv * vec2(depth_size));
	float sky = 0.0;
	for (int i = 0; i < 4; i++) {
		ivec2 o = ivec2(i & 1, i >> 1) * 2 - 1;
		sky += texelFetch(source_low, clamp(base + o, ivec2(0), depth_size - 1), 0).r == 0.0 ? 0.25 : 0.0;
	}
	vec3 c = textureLod(source, uv, 0.0).rgb;
	float l = luma(c);
	vec3 bright = c * (max(l - params.shafts.z, 0.0) / max(l, 1e-4));
	// Fading away from the sun (the shafts come from around it).
	vec2 d = (uv - params.shafts.xy) * vec2(float(params.dest_size.x) / float(params.dest_size.y), 1.0);
	float falloff = clamp(1.0 - length(d) * 0.8, 0.0, 1.0);
	imageStore(dest, pos, vec4(bright * sky * falloff * falloff, 1.0));
}
#endif

#ifdef MODE_SHAFTS_BLUR
// source: the mask. A radial blur towards the sun: 48 samples over `length` of the way, each fainter.
void main() {
	ivec2 pos = ivec2(gl_GlobalInvocationID.xy);
	if (any(greaterThanEqual(pos, params.dest_size))) {
		return;
	}
	vec2 uv = (vec2(pos) + 0.5) / vec2(params.dest_size);
	const int SAMPLES = 48;
	vec2 step_uv = (params.shafts.xy - uv) * (params.shafts.w / float(SAMPLES));
	// Dithered start (interleaved gradient noise), so the steps do not band.
	float j = fract(52.9829189 * fract(dot(vec2(pos), vec2(0.06711056, 0.00583715))));
	vec2 p = uv + step_uv * j;
	vec3 sum = vec3(0.0);
	float w = 1.0;
	float wsum = 0.0;
	for (int i = 0; i < SAMPLES; i++) {
		sum += textureLod(source, p, 0.0).rgb * w;
		wsum += w;
		w *= 0.97;
		p += step_uv;
	}
	imageStore(dest, pos, vec4(sum / wsum, 1.0));
}
#endif
