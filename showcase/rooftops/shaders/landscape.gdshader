// FellowEngine showcase (rooftops): the Tuscan country round the city — a patchwork of plots on the hills (wheat,
// vineyards in rows, olive groves, pasture, ploughed earth, dry grass), hedges between some, woods on the steep slopes,
// white dirt roads winding through; each pattern averaged out where it is smaller than a pixel (no shimmer far off).
shader_type spatial;
render_mode diffuse_burley;

#include "rooftops_common.gdshaderinc"

varying vec3 world_pos;
varying vec3 world_nrm;

void vertex() {
	world_pos = (MODEL_MATRIX * vec4(VERTEX, 1.0)).xyz;
	world_nrm = normalize((MODEL_MATRIX * vec4(NORMAL, 0.0)).xyz);
}

// A stripe pattern (0..1) of the given period, averaged to `mean` where the period is under a few pixels.
float stripes(float x, float period, float width, float mean, float pix) {
	float s = smoothstep(width * 0.5 + 0.08, width * 0.5 - 0.08, abs(fract(x / period) - 0.5));
	return mix(mean, s, clamp(1.0 - pix * 3.0 / period, 0.0, 1.0));
}

void fragment() {
	vec2 p = world_pos.xz;
	float pix = max(fwidth(p.x), fwidth(p.y));
	float ang = 0.35;
	mat2 rot = mat2(vec2(cos(ang), sin(ang)), vec2(-sin(ang), cos(ang)));
	vec2 warp = (vec2(rt_noise(p * 0.005), rt_noise(p * 0.005 + 5.0)) - 0.5) * 120.0;
	vec2 q = rot * p + warp;
	vec2 size = vec2(78.0, 52.0);
	vec2 cid = floor(q / size);
	vec2 f = fract(q / size);
	float k = rt_hash12(cid + 17.0);
	float macro = 0.85 + 0.3 * rt_fbm(p * 0.008, 4);
	vec3 earth = vec3(0.5, 0.4, 0.28);
	vec3 col;
	if (k < 0.16) {
		col = mix(vec3(0.74, 0.62, 0.33), vec3(0.66, 0.53, 0.28), stripes(q.x, 1.4, 0.5, 0.5, pix));
	} else if (k < 0.32) {
		// A vineyard: rows of vines along the plot, earth between.
		col = mix(earth, vec3(0.24, 0.31, 0.13), stripes(q.y + 0.0 * q.x, 2.6, 0.45, 0.45, pix));
	} else if (k < 0.5) {
		// An olive grove: grey-green crowns on a grid over pale earth.
		vec2 g = abs(fract(q / 7.0) - 0.5) * 7.0;
		float dot_ = smoothstep(2.0, 1.4, length(g));
		dot_ = mix(0.25, dot_, clamp(1.0 - pix * 3.0 / 7.0, 0.0, 1.0));
		col = mix(vec3(0.56, 0.48, 0.35), vec3(0.36, 0.4, 0.28), dot_);
	} else if (k < 0.64) {
		col = vec3(0.34, 0.41, 0.19) * (0.85 + 0.3 * rt_noise(p * 0.08));
	} else if (k < 0.76) {
		col = mix(vec3(0.43, 0.31, 0.2), vec3(0.36, 0.26, 0.17), stripes(q.x, 0.9, 0.5, 0.5, pix));
	} else if (k < 0.9) {
		col = vec3(0.62, 0.56, 0.36) * (0.9 + 0.2 * rt_noise(p * 0.1));
	} else {
		col = vec3(0.17, 0.23, 0.11) * (0.8 + 0.4 * rt_fbm(p * 0.15, 3));
	}
	// Hedges along some plots' edges.
	vec2 e = min(f, 1.0 - f) * size;
	float hedge = (1.0 - smoothstep(0.8, 1.8, min(e.x, e.y))) * step(0.45, rt_hash12(cid + 3.0));
	col = mix(col, vec3(0.14, 0.2, 0.09), hedge * clamp(1.0 - pix / 1.5, 0.3, 1.0));
	// Woods on the steep slopes.
	float steep = smoothstep(0.12, 0.3, 1.0 - world_nrm.y + 0.08 * (rt_noise(p * 0.02) - 0.5));
	col = mix(col, vec3(0.15, 0.21, 0.1) * (0.75 + 0.5 * rt_fbm(p * 0.12, 3)), steep);
	// White dirt roads winding over the hills.
	float rn = rt_fbm(p * 0.0035 + 40.0, 3);
	float road_w = max(0.0012, pix * 0.0035 * 0.8);
	float road = 1.0 - smoothstep(road_w * 0.5, road_w, abs(rn - 0.5));
	col = mix(col, vec3(0.78, 0.72, 0.6), road * 0.85 * (1.0 - steep));
	// A little richer than life: the haze of the distance takes it back.
	vec3 grey = vec3(dot(col, vec3(0.3, 0.55, 0.15)));
	col = mix(grey, col, 1.35) * 0.92;
	ALBEDO = clamp(col * macro, 0.0, 1.0);
	ROUGHNESS = 0.95;
	SPECULAR = 0.25;
}
