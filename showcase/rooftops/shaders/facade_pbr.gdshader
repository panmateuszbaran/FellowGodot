// FellowEngine showcase (rooftops): the buildings' photographic materials (Poly Haven's CC0 textures, fetched by
// tools/fetch_textures.py) on UVs in metres — plaster tinted to each house's colour, stone, rusticated blocks, brick,
// roof tiles, wood, painted shutters — sampled twice at different scales and turns blended by noise (no visible
// repeat on a long wall), with the weathering of the procedural version over them: the tone drifting over the wall,
// rain's streaks, the street's dirt at the foot, and (plaster) patches fallen off with the bricks beneath.
shader_type spatial;
render_mode diffuse_burley, specular_schlick_ggx;

#include "rooftops_common.gdshaderinc"

uniform sampler2D albedo_tex : source_color, filter_linear_mipmap_anisotropic, repeat_enable;
uniform sampler2D normal_tex : hint_normal, filter_linear_mipmap_anisotropic, repeat_enable;
uniform sampler2D arm_tex : hint_default_white, filter_linear_mipmap_anisotropic, repeat_enable;
uniform float tex_size = 2.0; // the texture's width in metres
uniform vec3 tint : source_color = vec3(1.0);
uniform float tint_strength = 0.0; // 0: the texture's colour, 1: its brightness times the tint
uniform float tex_lum = 0.25; // the texture's mean (linear) luminance: the tint is its colour where the texture is average
uniform float brightness = 1.0;
uniform float normal_strength = 1.0;
uniform float roughness_bias = 0.0;
uniform float specular = 0.3;
uniform bool anti_tile = true;
uniform float variation = 0.12;
uniform float stains = 0.4;
uniform float ground_dirt = 1.2;
uniform float seed = 0.0;
// Plaster fallen off: the bricks under it.
uniform float fallen = 0.0;
uniform sampler2D patch_albedo : source_color, filter_linear_mipmap_anisotropic, repeat_enable;
uniform sampler2D patch_normal : hint_normal, filter_linear_mipmap_anisotropic, repeat_enable;
uniform float patch_size = 3.0;

varying vec3 world_pos;

void vertex() {
	world_pos = (MODEL_MATRIX * vec4(VERTEX, 1.0)).xyz;
}

void fragment() {
	vec2 uv_m = UV + vec2(seed * 7.31, seed * 3.17);
	vec2 uv_a = uv_m / tex_size;
	vec3 col = texture(albedo_tex, uv_a).rgb;
	vec3 nrm = texture(normal_tex, uv_a).rgb;
	vec3 arm = texture(arm_tex, uv_a).rgb;
	if (anti_tile) {
		float ang = 1.1 + seed;
		vec2 uv_b = rt_rotate(uv_m / (tex_size * 1.37), ang) + vec2(0.37, 0.61);
		float m = smoothstep(0.42, 0.58, rt_fbm(uv_m * 0.11 + seed, 3));
		vec3 col_b = texture(albedo_tex, uv_b).rgb;
		vec3 nrm_b = texture(normal_tex, uv_b).rgb;
		// The second sample's normal turned back into the first's frame.
		vec2 nb = rt_rotate(nrm_b.xy * 2.0 - 1.0, -ang) * 0.5 + 0.5;
		nrm_b = vec3(nb, nrm_b.z);
		col = mix(col, col_b, m);
		nrm = mix(nrm, nrm_b, m);
		arm = mix(arm, texture(arm_tex, uv_b).rgb, m);
	}
	float lum = dot(col, vec3(0.3, 0.55, 0.15));
	col = mix(col, tint * lum / tex_lum, tint_strength) * brightness;
	// The tone drifting over the wall, warm blotches.
	float big = rt_fbm(uv_m * 0.17, 3);
	col *= 1.0 + variation * (big - 0.5) * 2.0;
	// Rain's streaks, long and vertical; the street's dirt at the foot.
	float streak = smoothstep(0.42, 0.8, rt_fbm(vec2(uv_m.x * 2.4, world_pos.y * 0.09 + uv_m.x * 0.13), 3)) * stains;
	col *= 1.0 - 0.28 * streak;
	col = mix(col, col * vec3(0.85, 0.87, 0.9), streak * 0.5);
	float dirt = (1.0 - smoothstep(0.0, ground_dirt, world_pos.y)) * (0.65 + 0.35 * rt_noise(uv_m * vec2(3.0, 9.0)));
	col = mix(col, col * vec3(0.62, 0.6, 0.57), dirt * 0.6);
	float ao = arm.r;
	float rough = clamp(arm.g + roughness_bias, 0.05, 1.0);
	float nstr = normal_strength;
	if (fallen > 0.0) {
		float t = mix(0.92, 0.6, fallen);
		float pn = rt_fbm(uv_m * 0.22 + 11.0, 4) + 0.07 * rt_noise(uv_m * 7.0);
		float pm = smoothstep(t, t + 0.01, pn);
		if (pm > 0.0) {
			vec2 puv = uv_m / patch_size;
			vec3 pc = texture(patch_albedo, puv).rgb * 0.9;
			float rim = smoothstep(t + 0.035, t + 0.01, pn);
			pc *= 1.0 - 0.4 * rim;
			col = mix(col, pc, pm);
			nrm = mix(nrm, texture(patch_normal, puv).rgb, pm);
			ao = mix(ao, 0.75, pm);
			rough = mix(rough, 0.9, pm);
		}
	}
	ALBEDO = clamp(col, 0.0, 1.0);
	ROUGHNESS = rough;
	SPECULAR = specular;
	AO = ao;
	AO_LIGHT_AFFECT = 0.25;
	mat3 tbn = rt_cotangent_frame(normalize(NORMAL), VERTEX, uv_a);
	NORMAL = rt_normal_from_map(tbn, nrm, nstr);
}
