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
// Milestone 153: the city's houses (a MultiMesh's instances): each instance's colour the tint, its alpha the seed's.
uniform bool instance_tint = false;
// Plaster fallen off: the bricks under it.
uniform float fallen = 0.0;
uniform sampler2D patch_albedo : source_color, filter_linear_mipmap_anisotropic, repeat_enable;
uniform sampler2D patch_normal : hint_normal, filter_linear_mipmap_anisotropic, repeat_enable;
uniform float patch_size = 3.0;
// Milestone 158 (the owner's reference): the photograph's relief seen in depth — parallax occlusion over its height map
// (Poly Haven's displacement): the joints between the blocks and the tiles' troughs sink, the faces stand, as the view
// slants. The depth in metres (0: off); laid textures only (not with the anti-tiling's second, turned sample).
uniform sampler2D height_tex : hint_default_white, filter_linear_mipmap, repeat_enable;
uniform float parallax_depth = 0.0;
// The height map's levels taken as the bottom and the top of the relief (a dressed block's face flat over its joints:
// the photograph's rough face left to the normal map).
uniform float height_lo = 0.0;
uniform float height_hi = 1.0;
// How much darker the relief's bottom is (the joints', the troughs': less light reaches them).
uniform float cavity = 0.55;
// The photograph's fine contrast (its pits and grains) eased towards its colour over a few centimetres: a dressed,
// weathered face (the reference's), not a rock-faced one.
uniform float albedo_soften = 0.0;
uniform float soften_lod = 5.0;

// Milestone 188 (the owner: the buildings' shadows flickering): the height map read at the pixel's own level of detail,
// and the relief faded out where its texels are finer than the pixels (its level 1.5-3.5: an 8th of a texel per pixel),
// the textures read with the surface's own gradients (not the parallax's, which jump from pixel to pixel). Read at
// level 0 the far walls' joints and their darkening had changed from one TAA-jittered frame to the next.
float pom_depth(vec2 uv, float lod) {
	return 1.0 - smoothstep(height_lo, height_hi, textureLod(height_tex, uv, lod).r);
}

varying vec3 world_pos;
varying vec4 inst;

void vertex() {
	world_pos = (MODEL_MATRIX * vec4(VERTEX, 1.0)).xyz;
	// The instance's colour given as sRGB (as a source_color uniform is): to linear.
	inst = vec4(mix(COLOR.rgb / 12.92, pow((COLOR.rgb + 0.055) / 1.055, vec3(2.4)), step(vec3(0.04045), COLOR.rgb)), COLOR.a);
}

void fragment() {
	float sd = instance_tint ? seed + inst.a * 13.0 : seed;
	vec3 tn = instance_tint ? inst.rgb : tint;
	vec2 uv_m = UV + vec2(sd * 7.31, sd * 3.17);
	vec2 uv_a = uv_m / tex_size;
	mat3 tbn = rt_cotangent_frame(normalize(NORMAL), VERTEX, uv_a);
	float pdepth = 0.0;
	vec2 gdx = dFdx(uv_a);
	vec2 gdy = dFdy(uv_a);
	float hlod = max(textureQueryLod(height_tex, uv_a).y, 0.0);
	float pfade = 1.0 - smoothstep(1.5, 3.5, hlod);
	float cdepth = 0.0;
	if (parallax_depth > 0.0 && !anti_tile) {
		cdepth = pom_depth(uv_a, hlod);
	}
	if (parallax_depth > 0.0 && !anti_tile && pfade > 0.0) {
		// The view in the texture's frame; marched into the height field (the texture's units: depth / size).
		vec3 v = normalize(VIEW);
		vec3 vt = vec3(dot(v, tbn[0]), dot(v, tbn[1]), dot(v, normalize(NORMAL)));
		float d = parallax_depth * pfade / tex_size;
		float n = mix(28.0, 8.0, clamp(vt.z, 0.0, 1.0));
		float layer = 1.0 / n;
		vec2 step_uv = vt.xy / max(vt.z, 0.15) * d / n;
		vec2 cur = uv_a;
		float cur_d = 0.0;
		float h = pom_depth(cur, hlod);
		for (int i = 0; i < 32; i++) {
			if (cur_d >= h) {
				break;
			}
			cur -= step_uv;
			h = pom_depth(cur, hlod);
			cur_d += layer;
		}
		vec2 prev = cur + step_uv;
		float after = h - cur_d;
		float before = pom_depth(prev, hlod) - cur_d + layer;
		float w = after / min(after - before, -1e-5);
		vec2 hit = mix(cur, prev, clamp(w, 0.0, 1.0));
		pdepth = cur_d;
		uv_m += (hit - uv_a) * tex_size;
		uv_a = hit;
		// The joints' darkening: the relief's depth where it is seen, the height map's mean where it has faded.
		cdepth = mix(cdepth, pdepth, pfade);
	}
	vec3 col = textureGrad(albedo_tex, uv_a, gdx, gdy).rgb;
	if (albedo_soften > 0.0) {
		col = mix(col, textureLod(albedo_tex, uv_a, soften_lod).rgb, albedo_soften);
	}
	vec3 nrm = textureGrad(normal_tex, uv_a, gdx, gdy).rgb;
	vec3 arm = textureGrad(arm_tex, uv_a, gdx, gdy).rgb;
	if (anti_tile) {
		float ang = 1.1 + sd;
		vec2 uv_b = rt_rotate(uv_m / (tex_size * 1.37), ang) + vec2(0.37, 0.61);
		float m = smoothstep(0.42, 0.58, rt_fbm(uv_m * 0.11 + sd, 3));
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
	col = mix(col, tn * lum / tex_lum, tint_strength) * brightness;
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
	// Deep in the relief (the joints), darker: the light that reaches it is less.
	ao *= 1.0 - cavity * smoothstep(0.35, 1.0, cdepth);
	AO = ao;
	AO_LIGHT_AFFECT = 0.25;
	NORMAL = rt_normal_from_map(tbn, nrm, nstr);
}
