// FellowEngine showcase (rooftops): the ground with photographic textures (Poly Haven, CC0) by where it is (world
// space, metres): the square's stone slabs crossed every 7 m by bands of pale travertine; round the fountain a ring of
// cobbles laid round it (the texture bent into the ring), edged by marble; the pavements' flags along the fronts; the
// streets' cobbles in fish-scale arcs. Dirt and damp at the foot of the walls, moss in the damp, worn paths, a few
// puddles; the textures sampled twice at different scales and turns (no visible repeat).
shader_type spatial;
render_mode diffuse_burley, specular_schlick_ggx;

#include "rooftops_common.gdshaderinc"

uniform vec4 walls[12];
uniform int wall_count = 0;
uniform vec4 square = vec4(-16.0, -13.0, 17.5, 14.0);
// Milestone 160: the west street before the reference's house, flagged as the video's (no bands): x0, z0, x1, z1.
uniform vec4 flagged = vec4(0.0);
uniform vec2 fountain = vec2(-3.5, 1.0);
uniform float pavement_width = 1.2;
uniform float puddles = 0.3;

uniform sampler2D slabs_albedo : source_color, filter_linear_mipmap_anisotropic, repeat_enable;
uniform sampler2D slabs_normal : hint_normal, filter_linear_mipmap_anisotropic, repeat_enable;
uniform sampler2D slabs_arm : hint_default_white, filter_linear_mipmap_anisotropic, repeat_enable;
uniform float slabs_size = 1.85;
// The slabs' stone: the photograph's brightness times this colour (its mean luminance slabs_lum: the colour where the
// photograph is average) — pale grey-brown sandstone, not the photograph's dark tile.
uniform vec3 slabs_tint : source_color = vec3(0.6, 0.57, 0.52);
uniform float slabs_lum = 0.2;
uniform float slabs_tint_strength = 0.0;
uniform sampler2D fan_albedo : source_color, filter_linear_mipmap_anisotropic, repeat_enable;
uniform sampler2D fan_normal : hint_normal, filter_linear_mipmap_anisotropic, repeat_enable;
uniform sampler2D fan_arm : hint_default_white, filter_linear_mipmap_anisotropic, repeat_enable;
uniform float fan_size = 2.5;
uniform sampler2D cob_albedo : source_color, filter_linear_mipmap_anisotropic, repeat_enable;
uniform sampler2D cob_normal : hint_normal, filter_linear_mipmap_anisotropic, repeat_enable;
uniform sampler2D cob_arm : hint_default_white, filter_linear_mipmap_anisotropic, repeat_enable;
uniform float cob_size = 2.0;
uniform sampler2D pave_albedo : source_color, filter_linear_mipmap_anisotropic, repeat_enable;
uniform sampler2D pave_normal : hint_normal, filter_linear_mipmap_anisotropic, repeat_enable;
uniform sampler2D pave_arm : hint_default_white, filter_linear_mipmap_anisotropic, repeat_enable;
uniform float pave_size = 2.24;

varying vec3 world_pos;

void vertex() {
	world_pos = (MODEL_MATRIX * vec4(VERTEX, 1.0)).xyz;
}

float sd_box(vec2 p, vec4 r) {
	vec2 d = abs(p - r.xy) - r.zw;
	return length(max(d, 0.0)) + min(max(d.x, d.y), 0.0);
}

float wall_distance(vec2 p) {
	float d = 1e6;
	for (int i = 0; i < 12; i++) {
		if (i >= wall_count) {
			break;
		}
		d = min(d, sd_box(p, walls[i]));
	}
	return d;
}

// A texture set sampled at uv (in texture units) and again turned and scaled, blended by noise; the normal map's
// second sample turned back. Returns the colour; the normal texel and ARM out.
vec3 sample_set(sampler2D a, sampler2D n, sampler2D r, vec2 uv, vec2 noise_p, out vec3 nrm, out vec3 arm) {
	vec3 c = texture(a, uv).rgb;
	nrm = texture(n, uv).rgb;
	arm = texture(r, uv).rgb;
	float ang = 2.1;
	vec2 uv2 = rt_rotate(uv * 0.71, ang) + vec2(0.29, 0.53);
	float m = smoothstep(0.42, 0.58, rt_fbm(noise_p * 0.09, 3));
	vec3 n2 = texture(n, uv2).rgb;
	vec2 nb = rt_rotate(n2.xy * 2.0 - 1.0, -ang) * 0.5 + 0.5;
	nrm = mix(nrm, vec3(nb, n2.z), m);
	arm = mix(arm, texture(r, uv2).rgb, m);
	return mix(c, texture(a, uv2).rgb, m);
}

void fragment() {
	vec2 p = world_pos.xz;
	float wd = wall_distance(p);
	bool in_square = p.x > square.x && p.y > square.y && p.x < square.z && p.y < square.w;
	bool in_flagged = p.x > flagged.x && p.y > flagged.y && p.x < flagged.z && p.y < flagged.w;
	vec3 col;
	vec3 nrm;
	vec3 arm;
	vec2 uv;
	float rf = length(p - fountain);
	if (wd < pavement_width) {
		uv = p / pave_size;
		col = sample_set(pave_albedo, pave_normal, pave_arm, uv, p, nrm, arm) * vec3(0.95, 0.96, 0.98);
	} else if (in_square && rf < 7.9) {
		if (rf < 3.75 || rf > 7.45) {
			// The marble bands, cut in segments round the fountain.
			float a = atan(p.y - fountain.y, p.x - fountain.x);
			float seg = fract(a / TAU * 24.0);
			float edge = min(min(seg, 1.0 - seg) * rf * TAU / 24.0, rf < 3.75 ? 3.75 - rf : rf - 7.45);
			float joint = 1.0 - smoothstep(0.003, 0.008, edge);
			float tone = rt_hash12(vec2(floor(a / TAU * 24.0), rf < 3.75 ? 1.0 : 2.0));
			col = mix(vec3(0.74, 0.72, 0.66), vec3(0.84, 0.82, 0.77), tone) * (0.9 + 0.15 * rt_fbm(p * 3.0, 3));
			col = mix(col, vec3(0.2, 0.19, 0.17), joint);
			nrm = vec3(0.5, 0.5, 1.0);
			arm = vec3(1.0 - 0.4 * joint, 0.45 + 0.4 * joint, 0.0);
			uv = p;
		} else {
			// The cobbles laid round the fountain: the texture bent into the ring.
			float a = atan(p.y - fountain.y, p.x - fountain.x);
			uv = vec2(a * 5.6, rf) / cob_size;
			col = sample_set(cob_albedo, cob_normal, cob_arm, uv, p, nrm, arm);
		}
	} else if (in_square || in_flagged) {
		uv = p / slabs_size;
		col = sample_set(slabs_albedo, slabs_normal, slabs_arm, uv, p, nrm, arm);
		col = mix(col, slabs_tint * dot(col, vec3(0.3, 0.55, 0.15)) / slabs_lum, slabs_tint_strength);
		// The travertine bands every 7 m.
		vec2 g = abs(fract(p / 7.0 + 0.5) - 0.5) * 7.0;
		float band = 1.0 - smoothstep(0.15, 0.17, min(g.x, g.y));
		if (band > 0.0 && in_square) {
			float along = g.x < g.y ? p.y : p.x;
			float seg = fract(along / 0.62);
			float joint = 1.0 - smoothstep(0.004, 0.01, min(min(seg, 1.0 - seg) * 0.62, 0.16 - min(g.x, g.y)));
			vec3 tc = vec3(0.8, 0.77, 0.69) * (0.88 + 0.2 * rt_fbm(p * 4.0, 3)) * (0.95 + 0.1 * rt_hash12(vec2(floor(along / 0.62), 3.0)));
			tc = mix(tc, vec3(0.22, 0.2, 0.18), joint);
			col = mix(col, tc, band);
			nrm = mix(nrm, vec3(0.5, 0.5, 1.0), band);
			arm = mix(arm, vec3(1.0 - 0.4 * joint, 0.5, 0.0), band);
		}
	} else {
		uv = p / fan_size;
		col = sample_set(fan_albedo, fan_normal, fan_arm, uv, p, nrm, arm);
	}
	// The stone's tone over the square; dirt and damp by the walls, moss in it; worn paths.
	col *= 0.9 + 0.2 * rt_fbm(p * 0.05, 3);
	float damp = 1.0 - smoothstep(0.0, 1.3, wd);
	col = mix(col, col * vec3(0.66, 0.66, 0.6), damp * 0.55);
	float moss = damp * smoothstep(0.55, 0.75, rt_fbm(p * 0.7, 3)) * (1.0 - smoothstep(0.35, 0.6, dot(col, vec3(0.33))));
	col = mix(col, vec3(0.18, 0.22, 0.1), moss * 0.5);
	float grime = smoothstep(0.4, 0.85, rt_fbm(p * 0.3, 4));
	col *= 1.0 - 0.15 * grime;
	float rough = arm.g;
	float wear = smoothstep(0.55, 0.8, rt_fbm(p * 0.1 + 7.0, 3)) * (1.0 - damp);
	rough -= 0.12 * wear;
	// A few puddles in the low places: still water over the stone.
	float pud = smoothstep(0.69, 0.72, rt_fbm(p * 0.08 + 31.0, 4) + 0.03 * rt_noise(p * 3.0)) * puddles;
	col *= 1.0 - 0.4 * pud;
	rough = mix(rough, 0.06, pud);
	ALBEDO = clamp(col, 0.0, 1.0);
	ROUGHNESS = clamp(rough, 0.05, 1.0);
	SPECULAR = mix(0.3, 0.5, pud);
	AO = arm.r;
	AO_LIGHT_AFFECT = 0.3;
	// The ground is flat: its frame from world axes (u along x, v along z: Godot's v down = +z towards the camera).
	vec3 nw = rt_normal_from_map(mat3(vec3(1.0, 0.0, 0.0), vec3(0.0, 0.0, 1.0), vec3(0.0, 1.0, 0.0)), mix(nrm, vec3(0.5, 0.5, 1.0), pud), 1.0);
	NORMAL = normalize((VIEW_MATRIX * vec4(nw, 0.0)).xyz);
}
