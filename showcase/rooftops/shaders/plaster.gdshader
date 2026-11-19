// FellowEngine showcase (rooftops): lime plaster over brick — its tone drifting over the wall, warm blotches, rain's
// streaks, the dirt splashed up from the street, and here and there a patch fallen off with the bricks beneath.
shader_type spatial;
render_mode diffuse_burley, specular_schlick_ggx;

#include "rooftops_common.gdshaderinc"

uniform vec3 color : source_color = vec3(0.80, 0.58, 0.32);
uniform float variation = 0.15;
uniform float stains = 0.55;
uniform float fallen = 0.3; // how much of the wall has lost its plaster (0..1)
uniform vec3 brick_color : source_color = vec3(0.56, 0.30, 0.20);
uniform float ground_dirt = 1.4; // how high the street's dirt reaches (m)
uniform float seed = 0.0;
uniform bool far_windows = false; // windows, shutters and string courses drawn on the wall (the distant city)
// Milestone 153: the city's houses (a MultiMesh's instances): each instance's colour the plaster's, its alpha the seed's.
uniform bool instance_tint = false;

varying vec3 world_pos;
varying vec4 inst;

void vertex() {
	world_pos = (MODEL_MATRIX * vec4(VERTEX, 1.0)).xyz;
	// The instance's colour given as sRGB (as a source_color uniform is): to linear.
	inst = vec4(mix(COLOR.rgb / 12.92, pow((COLOR.rgb + 0.055) / 1.055, vec3(2.4)), step(vec3(0.04045), COLOR.rgb)), COLOR.a);
}

void fragment() {
	float sd = instance_tint ? seed + inst.a * 13.0 : seed;
	vec3 color_ = instance_tint ? inst.rgb : color;
	vec2 uv = UV + vec2(sd * 17.31, sd * 5.13);
	float fade = rt_detail_fade(uv, 0.03);
	float big = rt_fbm(uv * 0.17, 3);
	float mid = rt_fbm(uv * 0.85 + 4.0, 4);
	vec3 col = color_ * (1.0 + variation * (big - 0.5) * 2.4);
	col = mix(col, col * vec3(1.06, 0.96, 0.86), smoothstep(0.45, 0.75, mid) * 0.8);
	// Rain's streaks: long and vertical.
	float streak = rt_fbm(vec2(uv.x * 2.4, world_pos.y * 0.09 + uv.x * 0.13), 3);
	streak = smoothstep(0.42, 0.78, streak) * stains;
	col *= 1.0 - 0.3 * streak;
	col = mix(col, col * vec3(0.84, 0.86, 0.9), streak * 0.6);
	// The street's dirt, greyer and darker at the foot of the wall.
	float dirt = (1.0 - smoothstep(0.0, ground_dirt, world_pos.y)) * (0.65 + 0.35 * rt_noise(uv * vec2(3.0, 9.0)));
	col = mix(col, col * vec3(0.6, 0.58, 0.55), dirt * 0.65);
	float h = 0.0018 * rt_fbm(uv * 13.0, 3) * fade + 0.0012 * (mid - 0.5);
	float rough = 0.88 + 0.08 * (big - 0.5);
	// Plaster fallen off: bricks beneath, their mortar, the plaster's broken rim darker.
	float t = mix(0.92, 0.6, fallen);
	float pn = rt_fbm(uv * 0.22 + 11.0, 4) + 0.07 * rt_noise(uv * 7.0) - 0.06 * (1.0 - smoothstep(0.0, 3.0, world_pos.y));
	float m = smoothstep(t, t + 0.012, pn);
	if (m > 0.0) {
		vec2 id;
		vec4 b = rt_blocks(uv, vec2(0.27, 0.075), 0.5, id);
		float tone = rt_hash12(id + 5.0);
		vec3 bc = brick_color * (0.75 + 0.5 * tone);
		bc = mix(bc, vec3(0.3, 0.2, 0.16), step(0.93, tone) * 0.6);
		float mortar = 1.0 - smoothstep(0.004, 0.008, b.z);
		bc = mix(bc, vec3(0.62, 0.58, 0.5), mortar);
		float bh = -0.014 - 0.004 * mortar + 0.002 * rt_noise(uv * 30.0);
		float rim = smoothstep(t + 0.04, t + 0.012, pn);
		bc *= 1.0 - 0.35 * rim;
		col = mix(col, bc, m);
		h = mix(h, bh, m);
		rough = mix(rough, 0.92, m);
	}
	if (far_windows) {
		vec2 q = vec2(UV.x / 2.7, (world_pos.y + 0.6) / 3.3);
		vec2 f = fract(q);
		vec2 wid = floor(q);
		float r = rt_hash12(wid + seed);
		float upper = step(1.0, wid.y);
		float win = step(abs(f.x - 0.5), 0.16) * step(abs(f.y - 0.5), 0.27) * upper;
		float shut = step(abs(f.x - 0.5), 0.33) * (1.0 - step(abs(f.x - 0.5), 0.16)) * step(abs(f.y - 0.5), 0.27) * upper * step(0.35, r);
		float closed = win * step(0.8, r);
		col = mix(col, vec3(0.04, 0.035, 0.03), win * (1.0 - closed));
		col = mix(col, mix(vec3(0.18, 0.27, 0.2), vec3(0.32, 0.18, 0.11), step(0.6, fract(seed * 0.37))), max(shut, closed));
		col = mix(col, col * vec3(1.12, 1.1, 1.05), step(fract((world_pos.y + 0.6) / 3.3), 0.05) * upper);
		col = mix(col, col * 0.75, (1.0 - upper) * step(0.4, f.y) * step(abs(f.x - 0.5), 0.3));
	}
	ALBEDO = col;
	ROUGHNESS = rough;
	SPECULAR = 0.35;
	NORMAL = rt_bump(NORMAL, VERTEX, h, 1.0);
}
