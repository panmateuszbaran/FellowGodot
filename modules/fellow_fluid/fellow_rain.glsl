#[versions]

ripples = "#define MODE_RIPPLES";
downsample = "#define MODE_DOWNSAMPLE";

#[compute]

#version 450

#VERSION_DEFINES

// FellowEngine rain ripples (FellowRain3D, decision 0124): rings spreading from drops on standing water, in a tiling
// texture (rg: the surface's slopes, b: its height). Three layers of cells; in each cell, each period of its own length
// brings a drop with probability `rain`, at a random point; its ring spreads at 0.3 m/s and fades within 0.8 s (it
// stays within its cell's neighbours: a 3 x 3 search finds every ring).

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

layout(push_constant, std430) uniform Params {
	int size;
	int dst_size;
	float tile; // m
	float time;
	float rain; // 0..1
	float amplitude; // m
	float pad0;
	float pad1;
}
params;

#ifdef MODE_RIPPLES
layout(rgba16f, set = 0, binding = 0) uniform restrict writeonly image2D ripples;

float hash(vec3 p) {
	p = fract(p * vec3(0.1031, 0.1030, 0.0973));
	p += dot(p, p.yxz + 33.33);
	return fract((p.x + p.y) * p.z);
}

void main() {
	ivec2 c = ivec2(gl_GlobalInvocationID.xy);
	if (any(greaterThanEqual(c, ivec2(params.size)))) {
		return;
	}
	vec2 p = (vec2(c) + 0.5) / float(params.size) * params.tile;
	vec2 slope = vec2(0.0);
	float height = 0.0;
	const int CELLS = 5;
	const float SPEED = 0.3; // m/s
	const float LIFE = 0.8; // s
	const float WIDTH = 0.012; // m
	float cell = params.tile / float(CELLS);
	for (int layer = 0; layer < 3; layer++) {
		vec2 shift = vec2(hash(vec3(layer, 1.7, 3.1)), hash(vec3(layer, 5.3, 7.9))) * cell;
		vec2 q = p - shift;
		ivec2 base = ivec2(floor(q / cell));
		for (int k = 0; k < 9; k++) {
			ivec2 cc = base + ivec2(k % 3 - 1, k / 3 - 1);
			ivec2 wc = (cc % CELLS + CELLS) % CELLS; // the tile repeats
			vec3 id = vec3(wc, layer);
			float period = 0.8 + 0.6 * hash(id + vec3(0.0, 0.0, 11.0));
			float tp = params.time + hash(id + vec3(0.0, 0.0, 23.0)) * period;
			float n = floor(tp / period);
			float age = tp - n * period;
			if (age > LIFE || hash(id + vec3(n * 1.37, n * 0.71, 41.0)) >= params.rain) {
				continue;
			}
			vec2 centre = (vec2(cc) + 0.15 + 0.7 * vec2(hash(id + vec3(n, 0.0, 53.0)), hash(id + vec3(0.0, n, 67.0)))) * cell;
			vec2 d = q - centre;
			float r = max(length(d), 1e-4);
			float x = (r - age * SPEED) / WIDTH;
			float fade = 1.0 - age / LIFE;
			float amp = params.amplitude * fade * fade;
			float e = exp(-x * x);
			height += amp * e * cos(2.5 * x);
			float dhdr = amp / WIDTH * e * (-2.0 * x * cos(2.5 * x) - 2.5 * sin(2.5 * x));
			slope += dhdr * d / r;
		}
	}
	imageStore(ripples, c, vec4(slope, height, 1.0));
}
#endif

#ifdef MODE_DOWNSAMPLE
layout(rgba16f, set = 0, binding = 0) uniform restrict readonly image2D src_level;
layout(rgba16f, set = 0, binding = 1) uniform restrict writeonly image2D dst_level;

void main() {
	ivec2 c = ivec2(gl_GlobalInvocationID.xy);
	if (any(greaterThanEqual(c, ivec2(params.dst_size)))) {
		return;
	}
	ivec2 s = c * 2;
	vec4 v = imageLoad(src_level, s) + imageLoad(src_level, s + ivec2(1, 0)) + imageLoad(src_level, s + ivec2(0, 1)) + imageLoad(src_level, s + ivec2(1, 1));
	imageStore(dst_level, c, v * 0.25);
}
#endif
