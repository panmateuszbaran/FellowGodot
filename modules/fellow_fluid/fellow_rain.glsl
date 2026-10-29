#[versions]

ripples = "#define MODE_RIPPLES";
tail = "#define MODE_TAIL";

#[compute]

#version 450

#VERSION_DEFINES

// FellowEngine rain ripples (FellowRain3D, decision 0124): rings spreading from drops on standing water, in a tiling
// texture (rg: the surface's slopes, b: its height, a: a tiling value noise the puddles are shaped with). Three layers
// of cells; in each cell, each period of its own length brings a drop with probability `rain`, at a random point; its
// ring spreads at 0.25 m/s and fades within 0.55 s (it stays within its cell's neighbours: a 3 x 3 search finds every
// ring). Two dispatches build the texture and its mip levels: `ripples`, one 16 x 16 tile per group, writes level 0
// and reduces the tile in shared memory to levels 1-4; `tail`, one group, reduces level 4 (16 x 16 at 256²) to the
// last levels.

layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;

layout(push_constant, std430) uniform Params {
	int size;
	int levels;
	float tile; // m
	float time;
	float rain; // 0..1
	float amplitude; // m
	float pad0;
	float pad1;
}
params;

shared vec4 tile_data[16][16];

#ifdef MODE_RIPPLES

layout(rgba16f, set = 0, binding = 0) uniform restrict writeonly image2D level0;
layout(rgba16f, set = 0, binding = 1) uniform restrict writeonly image2D level1;
layout(rgba16f, set = 0, binding = 2) uniform restrict writeonly image2D level2;
layout(rgba16f, set = 0, binding = 3) uniform restrict writeonly image2D level3;
layout(rgba16f, set = 0, binding = 4) uniform restrict writeonly image2D level4;

float hash(vec3 p) {
	p = fract(p * vec3(0.1031, 0.1030, 0.0973));
	p += dot(p, p.yxz + 33.33);
	return fract((p.x + p.y) * p.z);
}

// Value noise on a lattice of NOISE_CELLS x NOISE_CELLS over the texture (wrapping: it tiles).
const int NOISE_CELLS = 16;

float lattice(ivec2 c) {
	c = (c % NOISE_CELLS + NOISE_CELLS) % NOISE_CELLS;
	return hash(vec3(vec2(c), 91.7));
}

float value_noise(vec2 uv) {
	vec2 p = uv * float(NOISE_CELLS);
	ivec2 i = ivec2(floor(p));
	vec2 f = fract(p);
	vec2 u = f * f * (3.0 - 2.0 * f);
	return mix(mix(lattice(i), lattice(i + ivec2(1, 0)), u.x), mix(lattice(i + ivec2(0, 1)), lattice(i + ivec2(1, 1)), u.x), u.y);
}

void main() {
	ivec2 c = ivec2(gl_GlobalInvocationID.xy);
	ivec2 l = ivec2(gl_LocalInvocationID.xy);
	vec2 uv = (vec2(c) + 0.5) / float(params.size);
	vec2 p = uv * params.tile;
	vec2 slope = vec2(0.0);
	float height = 0.0;
	const int CELLS = 5;
	const float SPEED = 0.25; // m/s
	const float LIFE = 0.55; // s
	const float WIDTH = 0.012; // m
	float cell = params.tile / float(CELLS);
	if (params.rain > 0.0) {
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
				if (abs(x) > 4.0) {
					continue;
				}
				float fade = 1.0 - age / LIFE;
				float amp = params.amplitude * fade * fade;
				float e = exp(-x * x);
				float cs = cos(2.5 * x);
				height += amp * e * cs;
				float dhdr = amp / WIDTH * e * (-2.0 * x * cs - 2.5 * sin(2.5 * x));
				slope += dhdr * d / r;
			}
		}
	}
	vec4 v = vec4(slope, height, value_noise(uv));
	imageStore(level0, c, v);

	// Levels 1-4 of this 16 x 16 tile (2 x 2 means).
	tile_data[l.y][l.x] = v;
	barrier();
	ivec2 g = ivec2(gl_WorkGroupID.xy);
	for (int lv = 1; lv <= 4; lv++) {
		int n = 16 >> lv;
		bool inside = l.x < n && l.y < n;
		vec4 m = vec4(0.0);
		if (inside) {
			ivec2 s = l * 2;
			m = (tile_data[s.y][s.x] + tile_data[s.y][s.x + 1] + tile_data[s.y + 1][s.x] + tile_data[s.y + 1][s.x + 1]) * 0.25;
		}
		barrier();
		if (inside) {
			tile_data[l.y][l.x] = m;
			ivec2 dst = g * n + l;
			if (lv == 1) {
				imageStore(level1, dst, m);
			} else if (lv == 2) {
				imageStore(level2, dst, m);
			} else if (lv == 3) {
				imageStore(level3, dst, m);
			} else {
				imageStore(level4, dst, m);
			}
		}
		barrier();
	}
}

#endif

#ifdef MODE_TAIL

// Level 4 (at most 16 x 16) to the last level, in one group.
layout(rgba16f, set = 0, binding = 0) uniform restrict readonly image2D level4;
layout(rgba16f, set = 0, binding = 1) uniform restrict writeonly image2D level5;
layout(rgba16f, set = 0, binding = 2) uniform restrict writeonly image2D level6;
layout(rgba16f, set = 0, binding = 3) uniform restrict writeonly image2D level7;
layout(rgba16f, set = 0, binding = 4) uniform restrict writeonly image2D level8;

void main() {
	ivec2 l = ivec2(gl_LocalInvocationID.xy);
	int n0 = max(params.size >> 4, 1);
	tile_data[l.y][l.x] = (l.x < n0 && l.y < n0) ? imageLoad(level4, l) : vec4(0.0);
	barrier();
	for (int lv = 5; lv < params.levels; lv++) {
		int n = max(params.size >> lv, 1);
		bool inside = l.x < n && l.y < n;
		vec4 m = vec4(0.0);
		if (inside) {
			ivec2 s = l * 2;
			m = (tile_data[s.y][s.x] + tile_data[s.y][s.x + 1] + tile_data[s.y + 1][s.x] + tile_data[s.y + 1][s.x + 1]) * 0.25;
		}
		barrier();
		if (inside) {
			tile_data[l.y][l.x] = m;
			if (lv == 5) {
				imageStore(level5, l, m);
			} else if (lv == 6) {
				imageStore(level6, l, m);
			} else if (lv == 7) {
				imageStore(level7, l, m);
			} else {
				imageStore(level8, l, m);
			}
		}
		barrier();
	}
}

#endif
