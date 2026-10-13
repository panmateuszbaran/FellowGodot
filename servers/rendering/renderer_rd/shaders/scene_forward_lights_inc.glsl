// Functions related to lighting

#extension GL_EXT_control_flow_attributes : require

#include "area_lights_inc.glsl"

// This annotation macro must be placed before any loops that rely on specialization constants as their upper bound.
// Drivers may choose to unroll these loops based on the possible range of the value that can be deduced from the
// spec constant, which can lead to their code generation taking a much longer time than desired.
#if defined(UBERSHADER) || defined(FELLOW_EXP_DONT_UNROLL)
// Prefer to not unroll loops on the ubershader to reduce code size as much as possible. (FELLOW_EXP_DONT_UNROLL:
// FellowEngine register-pressure experiment, rendering/fellow/debug/scene_shader_experiments.)
#define SPEC_CONSTANT_LOOP_ANNOTATION [[dont_unroll]]
#elif defined(FELLOW_EXP_UNROLL)
#define SPEC_CONSTANT_LOOP_ANNOTATION [[unroll]]
#else
// Don't make an explicit hint on specialized shaders.
#define SPEC_CONSTANT_LOOP_ANNOTATION
#endif

// FellowEngine (rendering/fellow/shadows/batched_taps, decision 0040): the soft shadow filters' tap loops (PCF; PCSS
// blocker search and filter; directional and positional) fetch FELLOW_TAP_BATCH taps, then use their results in the
// same order (the same arithmetic). The loops are not unrolled, so otherwise each fetch waits for the previous one's
// result; batched, the batch's fetches are in flight together. Sample counts that are not a multiple of the batch take
// the stock loop. Experiments (debug/scene_shader_experiments): 256 batches of 2, 512 batches of 6.
#if defined(FELLOW_EXP_BIT8)
#define FELLOW_TAP_BATCH 2u
#elif defined(FELLOW_EXP_BIT9)
#define FELLOW_TAP_BATCH 6u
#else
#define FELLOW_TAP_BATCH 4u
#endif
#ifdef FELLOW_HAS_SHADOW_TAP_BATCH
#define FELLOW_BATCH_TAPS_DIR(m_count) (sc_fellow_shadow_tap_batch() && ((m_count) % FELLOW_TAP_BATCH) == 0u)
#define FELLOW_BATCH_TAPS_POS(m_count) (sc_fellow_shadow_tap_batch_positional() && ((m_count) % FELLOW_TAP_BATCH) == 0u)
#else
#define FELLOW_BATCH_TAPS_DIR(m_count) false
#define FELLOW_BATCH_TAPS_POS(m_count) false
#endif

#ifdef FELLOW_EXP_BIT10
// Experiment (debug/scene_shader_experiments 1024): only the directional PCSS loops (blocker search, filter) unrolled
// (with batched_taps off: they take the stock loops).
#define FELLOW_DIR_PCSS_LOOP_ANNOTATION [[unroll]]
#else
#define FELLOW_DIR_PCSS_LOOP_ANNOTATION SPEC_CONSTANT_LOOP_ANNOTATION
#endif

#ifdef FELLOW_EXP_BIT3
// FellowEngine diagnostics (debug/scene_shader_experiments bit 8): how often the shadow filters run their taps or take
// the max-mip early-outs; the colour pass writes the counts instead of the lit colour (see scene_forward_clustered.glsl).
uint fellow_dbg_dir_call = 0u;
uint fellow_dbg_dir_full = 0u;
uint fellow_dbg_dir_uniform = 0u; // full path, every tap the same (result exactly 0 or 1)
uint fellow_dbg_dir_pcf = 0u; // PCSS: the filter loop ran (after the blocker search)
uint fellow_dbg_pos_call = 0u;
uint fellow_dbg_pos_full = 0u;
uint fellow_dbg_pos_uniform = 0u;
#define FELLOW_DBG_INC(m_var) m_var++
half fellow_dbg_result(inout uint r_counter, half p_value) {
	if (p_value == half(0.0) || p_value == half(1.0)) {
		r_counter++;
	}
	return p_value;
}
#define FELLOW_DBG_RESULT(m_var, m_value) fellow_dbg_result(m_var, m_value)
#else
#define FELLOW_DBG_INC(m_var)
#define FELLOW_DBG_RESULT(m_var, m_value) (m_value)
#endif

half D_GGX(half NoH, half roughness, hvec3 n, hvec3 h) {
	half a = NoH * roughness;
#ifdef EXPLICIT_FP16
	hvec3 NxH = cross(n, h);
	half k = roughness / (dot(NxH, NxH) + a * a);
#else
	float k = roughness / (1.0 - NoH * NoH + a * a);
#endif
	half d = k * k * half(1.0 / M_PI);
	return saturateHalf(d);
}

#ifdef LIGHT_TRANSMITTANCE_USED
#ifdef SSS_MODE_SKIN
hvec3 SSS_skin(half NdotL, half transmittance_depth, half transmittance_z, half transmittance_boost, hvec4 transmittance_color, hvec3 light_color) {
	half scale = half(8.25) / transmittance_depth;
	half d = scale * abs(transmittance_z);
	float dd = float(-d * d);
	hvec3 profile = hvec3(vec3(0.233, 0.455, 0.649) * exp(dd / 0.0064) +
			vec3(0.1, 0.336, 0.344) * exp(dd / 0.0484) +
			vec3(0.118, 0.198, 0.0) * exp(dd / 0.187) +
			vec3(0.113, 0.007, 0.007) * exp(dd / 0.567) +
			vec3(0.358, 0.004, 0.0) * exp(dd / 1.99) +
			vec3(0.078, 0.0, 0.0) * exp(dd / 7.41));

	return profile * transmittance_color.a * light_color * clamp(transmittance_boost - NdotL, half(0.0), half(1.0)) * half(1.0 / M_PI);
}
#else
hvec3 SSS(half NdotL, half transmittance_depth, half transmittance_z, half transmittance_boost, hvec4 transmittance_color, hvec3 light_color) {
	half scale = half(8.25) / transmittance_depth;
	half d = scale * abs(transmittance_z);
	half dd = -d * d;
	return exp(dd) * transmittance_color.rgb * transmittance_color.a * light_color * clamp(transmittance_boost - NdotL, half(0.0), half(1.0)) * half(1.0 / M_PI);
}
#endif
#endif

// From Earl Hammon, Jr. "PBR Diffuse Lighting for GGX+Smith Microsurfaces" https://www.gdcvault.com/play/1024478/PBR-Diffuse-Lighting-for-GGX
half V_GGX(half NdotL, half NdotV, half alpha) {
	half v = half(0.5) / mix(half(2.0) * NdotL * NdotV, NdotL + NdotV, alpha);
	return saturateHalf(v);
}

half D_GGX_anisotropic(half cos_theta_m, half alpha_x, half alpha_y, half cos_phi, half sin_phi) {
	half alpha2 = alpha_x * alpha_y;
	vec3 v = vec3(alpha_y * cos_phi, alpha_x * sin_phi, alpha2 * cos_theta_m);
	float v2 = dot(v, v);
	half w2 = half(float(alpha2) / v2);
	return alpha2 * w2 * w2 * half(1.0 / M_PI);
}

half V_GGX_anisotropic(half alpha_x, half alpha_y, half TdotV, half TdotL, half BdotV, half BdotL, half NdotV, half NdotL) {
	half Lambda_V = NdotL * length(hvec3(alpha_x * TdotV, alpha_y * BdotV, NdotV));
	half Lambda_L = NdotV * length(hvec3(alpha_x * TdotL, alpha_y * BdotL, NdotL));
	half v = half(0.5) / (Lambda_V + Lambda_L);
	return saturateHalf(v);
}

half SchlickFresnel(half u) {
	half m = half(1.0) - u;
	half m2 = m * m;
	return m2 * m2 * m; // pow(m,5)
}

hvec3 F0(half metallic, half specular, hvec3 albedo) {
	half dielectric = half(0.16) * specular * specular;
	// use albedo * metallic as colored specular reflectance at 0 angle for metallic materials;
	// see https://google.github.io/filament/Filament.md.html
	return mix(hvec3(dielectric), albedo, hvec3(metallic));
}

half V_Kelemen(half LdotH) {
	// Kelemen 2001, "A Microfacet Based Coupled Specular-Matte BRDF Model with Importance Sampling"
	return saturateHalf(half(0.25) / (LdotH * LdotH + half(1e-4)));
}

hvec3 f0_Clear_Coat_To_Surface(hvec3 f0) {
	// Approximation of iorTof0(f0ToIor(f0), 1.5)
	// This assumes that the clear coat layer has an IOR of 1.5
	// see https://github.com/google/filament/blob/837b2715a05f4656d4f524bce50d1b23ff8f84c9/shaders/src/surface_material.fs#L54-L62
	return clamp(f0 * (f0 * (half(0.941892) - half(0.263008) * f0) + half(0.346479)) - half(0.0285998), half(0.0), half(1.0));
}

// FellowEngine: materials for which a light with N.L <= 0 (N.L as computed in light_compute) contributes exactly
// nothing, so its shadow lookup (and, for positional lights, the whole light) can be skipped. Anything that can
// still use the light or its attenuation behind the terminator is excluded.
#if !defined(LIGHT_CODE_USED) && !defined(LIGHT_RIM_USED) && !defined(LIGHT_BACKLIGHT_USED) && !defined(LIGHT_TRANSMITTANCE_USED) && !defined(LIGHT_CLEARCOAT_USED) && !defined(DIFFUSE_LAMBERT_WRAP) && !defined(DIFFUSE_TOON) && !defined(SPECULAR_TOON) && !defined(USE_SHADOW_TO_OPACITY)
#define FELLOW_BACKFACING_LIGHT_SKIP_OK
#endif

void light_compute(hvec3 N, hvec3 L, hvec3 V, half A, hvec3 light_color, bool is_directional, half attenuation, hvec3 f0, half roughness, half metallic, half specular_amount, hvec3 albedo, inout half alpha, vec2 screen_uv, hvec3 energy_compensation,
#ifdef LIGHT_BACKLIGHT_USED
		hvec3 backlight,
#endif
#ifdef LIGHT_TRANSMITTANCE_USED
		hvec4 transmittance_color,
		half transmittance_depth,
		half transmittance_boost,
		half transmittance_z,
#endif
#ifdef LIGHT_RIM_USED
		half rim, half rim_tint,
#endif
#ifdef LIGHT_CLEARCOAT_USED
		half clearcoat, half clearcoat_roughness, hvec3 vertex_normal,
#endif
#ifdef LIGHT_ANISOTROPY_USED
		hvec3 B, hvec3 T, half anisotropy,
#endif
		inout hvec3 diffuse_light, inout hvec3 specular_light) {
#if defined(LIGHT_CODE_USED)
	// Light is written by the user shader.
	mat4 inv_view_matrix = transpose(mat4(scene_data_block.data.inv_view_matrix[0],
			scene_data_block.data.inv_view_matrix[1],
			scene_data_block.data.inv_view_matrix[2],
			vec4(0.0, 0.0, 0.0, 1.0)));
	mat4 read_view_matrix = transpose(mat4(scene_data_block.data.view_matrix[0],
			scene_data_block.data.view_matrix[1],
			scene_data_block.data.view_matrix[2],
			vec4(0.0, 0.0, 0.0, 1.0)));

#ifdef USING_MOBILE_RENDERER
	uint instance_index = draw_call.instance_index;
#else
	uint instance_index = instance_index_interp;
#endif

	mat4 read_model_matrix = transpose(mat4(instances.data[instance_index].transform[0],
			instances.data[instance_index].transform[1],
			instances.data[instance_index].transform[2],
			vec4(0.0, 0.0, 0.0, 1.0)));

#undef projection_matrix
#define projection_matrix scene_data_block.data.projection_matrix
#undef inv_projection_matrix
#define inv_projection_matrix scene_data_block.data.inv_projection_matrix

	vec2 read_viewport_size = scene_data_block.data.viewport_size;

#ifdef LIGHT_BACKLIGHT_USED
	vec3 backlight_highp = vec3(backlight);
#endif
	float roughness_highp = float(roughness);
	float metallic_highp = float(metallic);
	vec3 albedo_highp = vec3(albedo);
	float alpha_highp = float(alpha);
	vec3 normal_highp = vec3(N);
	vec3 light_highp = vec3(L);
	vec3 view_highp = vec3(V);
	float specular_amount_highp = float(specular_amount);
	vec3 light_color_highp = vec3(light_color);
	float attenuation_highp = float(attenuation);
	vec3 diffuse_light_highp = vec3(diffuse_light);
	vec3 specular_light_highp = vec3(specular_light);
	bool is_area = false;
	float area_diffuse = 1.0;
	float area_specular = 1.0;
	vec3 area_diffuse_tex_color = vec3(1.0);
	vec3 area_specular_tex_color = vec3(1.0);

#CODE : LIGHT

	alpha = half(alpha_highp);
	diffuse_light = hvec3(diffuse_light_highp);
	specular_light = hvec3(specular_light_highp);
#else // !LIGHT_CODE_USED
	half NdotL = min(A + dot(N, L), half(1.0));
	half cNdotV = max(dot(N, V), half(1e-4));

#ifdef LIGHT_TRANSMITTANCE_USED
	{
#ifdef SSS_MODE_SKIN
		diffuse_light += SSS_skin(NdotL, transmittance_depth, transmittance_z, transmittance_boost, transmittance_color, light_color);
#else
		diffuse_light += SSS(NdotL, transmittance_depth, transmittance_z, transmittance_boost, transmittance_color, light_color);
#endif
	}
#endif //LIGHT_TRANSMITTANCE_USED

#if defined(LIGHT_RIM_USED)
	// Epsilon min to prevent pow(0, 0) singularity which results in undefined behavior.
	half rim_light = pow(max(half(1e-4), half(1.0) - cNdotV), max(half(0.0), (half(1.0) - roughness) * half(16.0)));
	diffuse_light += rim_light * rim * mix(hvec3(1.0), albedo, rim_tint) * light_color;
#endif

	half cc_attenuation = half(1.0);

	// We skip checking on attenuation on directional lights to avoid a branch that is not as beneficial for directional lights as the other ones.
	if (is_directional || attenuation > HALF_FLT_MIN) {
		half cNdotL = max(NdotL, half(0.0));
#if defined(DIFFUSE_BURLEY) || defined(SPECULAR_SCHLICK_GGX) || defined(LIGHT_CLEARCOAT_USED)
		hvec3 H = normalize(V + L);
		half cLdotH = clamp(A + dot(L, H), half(0.0), half(1.0));
#endif

#if defined(LIGHT_CLEARCOAT_USED)
		// Clearcoat ignores normal_map, use vertex normal instead
		half ccNdotL = clamp(A + dot(vertex_normal, L), half(0.0), half(1.0));
		half ccNdotH = clamp(A + dot(vertex_normal, H), half(0.0), half(1.0));
		half cLdotH5 = SchlickFresnel(cLdotH);

		half Dr = D_GGX(ccNdotH, half(mix(half(0.001), half(0.1), clearcoat_roughness)), vertex_normal, H);
		half Gr = V_Kelemen(cLdotH);
		half Fr = mix(half(0.04), half(1.0), cLdotH5) * clearcoat;
		cc_attenuation = half(1.0) - Fr;
		half clearcoat_specular_brdf_NL = Dr * Gr * Fr * ccNdotL;
		specular_light += clearcoat_specular_brdf_NL * light_color * attenuation * specular_amount;
#endif // LIGHT_CLEARCOAT_USED

		if (metallic < half(1.0)) {
			half diffuse_brdf_NL; // BRDF times N.L for calculating diffuse radiance

#if defined(DIFFUSE_LAMBERT_WRAP)
			// Energy conserving lambert wrap shader.
			// https://web.archive.org/web/20210228210901/http://blog.stevemcauley.com/2011/12/03/energy-conserving-wrapped-diffuse/
			half op_roughness = half(1.0) + roughness;
			diffuse_brdf_NL = max(half(0.0), (NdotL + roughness) / (op_roughness * op_roughness)) * half(1.0 / M_PI);
#elif defined(DIFFUSE_TOON)

			diffuse_brdf_NL = smoothstep(-roughness, max(roughness, half(0.01)), NdotL) * half(1.0 / M_PI);

#elif defined(DIFFUSE_BURLEY)
			{
				half FD90_minus_1 = half(2.0) * cLdotH * cLdotH * roughness - half(0.5);
				half FdV = half(1.0) + FD90_minus_1 * SchlickFresnel(cNdotV);
				half FdL = half(1.0) + FD90_minus_1 * SchlickFresnel(cNdotL);
				diffuse_brdf_NL = half(1.0 / M_PI) * FdV * FdL * cNdotL;
			}
#else
			// lambert
			diffuse_brdf_NL = cNdotL * half(1.0 / M_PI);
#endif

			diffuse_light += light_color * diffuse_brdf_NL * attenuation * cc_attenuation;

#if defined(LIGHT_BACKLIGHT_USED)
			diffuse_light += light_color * (hvec3(1.0 / M_PI) - diffuse_brdf_NL) * backlight * attenuation;
#endif
		}

		if (roughness > half(0.0)) {
#if defined(SPECULAR_SCHLICK_GGX)
			half cNdotH = clamp(A + dot(N, H), half(0.0), half(1.0));
#endif
			// Apply specular light.
			// FIXME: roughness == 0 should not disable specular light entirely
#if defined(SPECULAR_TOON)
			hvec3 R = normalize(-reflect(L, N));
			half RdotV = dot(R, V);
			half mid = half(1.0) - roughness;
			mid *= mid;
			half intensity = smoothstep(mid - roughness * half(0.5), mid + roughness * half(0.5), RdotV) * mid;
			diffuse_light += light_color * intensity * attenuation * specular_amount; // write to diffuse_light, as in toon shading you generally want no reflection

#elif defined(SPECULAR_DISABLED)
			// Do nothing.

#elif defined(SPECULAR_SCHLICK_GGX)
			// shlick+ggx as default
			half alpha_ggx = roughness * roughness;
#if defined(LIGHT_ANISOTROPY_USED)
			half aspect = sqrt(half(1.0) - anisotropy * half(0.9));
			half ax = alpha_ggx / aspect;
			half ay = alpha_ggx * aspect;
			half XdotH = dot(T, H);
			half YdotH = dot(B, H);
			half D = D_GGX_anisotropic(cNdotH, ax, ay, XdotH, YdotH);
			half G = V_GGX_anisotropic(ax, ay, dot(T, V), dot(T, L), dot(B, V), dot(B, L), cNdotV, cNdotL);
#else // LIGHT_ANISOTROPY_USED
			half D = D_GGX(cNdotH, alpha_ggx, N, H);
			half G = V_GGX(cNdotL, cNdotV, alpha_ggx);
#endif // LIGHT_ANISOTROPY_USED
	   // F
#if !defined(LIGHT_CLEARCOAT_USED)
			half cLdotH5 = SchlickFresnel(cLdotH);
#endif
			// Calculate Fresnel using specular occlusion term from Filament:
			// https://google.github.io/filament/Filament.html#lighting/occlusion/specularocclusion
			half f90 = clamp(dot(f0, hvec3(50.0 * 0.33)), metallic, half(1.0));
			hvec3 F = f0 + (f90 - f0) * cLdotH5;
			hvec3 specular_brdf_NL = energy_compensation * cNdotL * D * F * G;
			specular_light += specular_brdf_NL * light_color * attenuation * cc_attenuation * specular_amount;
#endif
		}

#ifdef USE_SHADOW_TO_OPACITY
		alpha = min(alpha, clamp(half(1.0 - attenuation), half(0.0), half(1.0)));
#endif
	}
#endif // LIGHT_CODE_USED
}

#ifndef SHADOWS_DISABLED

// Interleaved Gradient Noise
// https://www.iryoku.com/next-generation-post-processing-in-call-of-duty-advanced-warfare
float quick_hash(vec2 pos) {
	const vec3 magic = vec3(0.06711056f, 0.00583715f, 52.9829189f);
	return fract(magic.z * fract(dot(pos, magic.xy)));
}

#ifdef FELLOW_SHADOW_MAX_MIP
// Receivers within this of the largest depth take the stock path: a filtered fetch may convert a texel to float
// differently (by an ulp) than the pyramid's texelFetch. 1/16 of a 16-bit depth step.
#define FELLOW_SHADOW_MAX_MIP_EPSILON (1.0 / 1048576.0)
#define FELLOW_SHADOW_MAX_MIP_FILTER_EPSILON (1.0 / 32768.0)

// FellowEngine: smallest (.x, farthest from the light in reverse Z) and largest (.y, closest) shadow atlas depth in
// the box of half size p_half_uv around p_uv (atlas UV), widened by 1.5 texels for bilinear / 2x2 PCF footprints, from
// the min/max window pyramid p_pyramid (level L: windows of 2^(L+3) texels every 2^(L+2); power-of-two atlases): the
// box fits in the single window of the smallest level whose stride is at least the box size, one fetch. (0, 1) when
// the box is too large: no early-out can fire.
vec2 fellow_shadow_window_minmax(texture2D p_pyramid, vec2 p_uv, vec2 p_half_uv) {
	ivec2 atlas = textureSize(sampler2D(p_pyramid, SAMPLER_NEAREST_CLAMP), 0) * 4;
	vec2 c = p_uv * vec2(atlas);
	vec2 r = min(p_half_uv * vec2(atlas), vec2(atlas)) + 1.5;
	// Integer texel box (clamped like the sampler).
	ivec2 lo = clamp(ivec2(floor(c - r)), ivec2(0), atlas - 1);
	ivec2 hi = clamp(ivec2(floor(c + r)), ivec2(0), atlas - 1);
	int extent = max(hi.x - lo.x, hi.y - lo.y) + 1;
	// Stride 2^k >= extent (k >= 2: boxes of up to 4 texels use the finest stored windows), level k - 2.
	int k = extent <= 4 ? 2 : findMSB(extent - 1) + 1;
#ifdef FELLOW_EXP_BIT5
	// Experiment (debug/scene_shader_experiments 32): windows of a level one finer,
	// as many as cover the box (each covers two cells of its stride: cells c, c + 2, ... up to the box's last cell), so
	// the union overshoots the box by at most one stride on each side instead of up to the box's size.
	int kf = max(2, k - 1);
	int mipf = kf - 2;
	if (mipf >= textureQueryLevels(sampler2D(p_pyramid, SAMPLER_NEAREST_CLAMP))) {
		return vec2(0.0, 1.0);
	}
	ivec2 c0 = lo >> kf;
	ivec2 c1 = hi >> kf;
	vec2 mmf = vec2(1.0, 0.0);
	for (int y = c0.y; y <= c1.y; y += 2) {
		for (int x = c0.x; x <= c1.x; x += 2) {
			vec2 w = texelFetch(sampler2D(p_pyramid, SAMPLER_NEAREST_CLAMP), ivec2(x, y), mipf).rg;
			mmf = vec2(min(mmf.x, w.x), max(mmf.y, w.y));
		}
	}
	return mmf;
#else
	int mip = k - 2;
	if (mip >= textureQueryLevels(sampler2D(p_pyramid, SAMPLER_NEAREST_CLAMP))) {
		return vec2(0.0, 1.0);
	}
	return texelFetch(sampler2D(p_pyramid, SAMPLER_NEAREST_CLAMP), lo >> k, mip).rg;
#endif
}

vec2 fellow_shadow_box_minmax(vec2 p_uv, vec2 p_half_uv) {
	return fellow_shadow_window_minmax(directional_shadow_max_mip, p_uv, p_half_uv);
}
#endif

half sample_directional_pcf_shadow(texture2D shadow, vec2 shadow_pixel_size, vec4 coord, float taa_frame_count) {
	vec2 pos = coord.xy;
	float depth = coord.z;
	FELLOW_DBG_INC(fellow_dbg_dir_call);

	//if only one sample is taken, take it from the center
	if (sc_directional_soft_shadow_samples() == 0) {
		return half(textureProj(sampler2DShadow(shadow, shadow_sampler), vec4(pos, depth, 1.0)));
	}

#ifdef FELLOW_SHADOW_MAX_MIP
	// FellowEngine: every tap below is a 2x2 comparison (receiver > stored depth = lit) within |shadow_pixel_size| of
	// pos. If the receiver is closer to the light than every atlas texel there, all taps are lit: return the loop's
	// result for that case (same arithmetic).
	// If every atlas texel there is at least as close to the light as the receiver (plus a margin), all taps compare 0:
	// fully shadowed.
	if (sc_fellow_directional_max_mip() && bool(scene_data_block.data.flags & SCENE_DATA_FLAGS_FELLOW_SHADOW_MAX_MIP)) {
		vec2 mm = fellow_shadow_box_minmax(pos, abs(shadow_pixel_size));
		if (mm.y < depth - FELLOW_SHADOW_MAX_MIP_EPSILON) {
			return half(float(sc_directional_soft_shadow_samples()) * (1.0 / float(sc_directional_soft_shadow_samples())));
		}
		if (mm.x > depth + FELLOW_SHADOW_MAX_MIP_EPSILON) {
			return half(0.0);
		}
	}
#endif

	FELLOW_DBG_INC(fellow_dbg_dir_full);
	mat2 disk_rotation;
	{
		float r = quick_hash(gl_FragCoord.xy + vec2(taa_frame_count * 5.588238)) * 2.0 * M_PI;
		float sr = sin(r);
		float cr = cos(r);
		disk_rotation = mat2(vec2(cr, -sr), vec2(sr, cr));
	}

	float avg = 0.0;

	if (FELLOW_BATCH_TAPS_DIR(sc_directional_soft_shadow_samples())) {
		SPEC_CONSTANT_LOOP_ANNOTATION
		for (uint i = 0; i < sc_directional_soft_shadow_samples(); i += FELLOW_TAP_BATCH) {
			float t[FELLOW_TAP_BATCH];
			[[unroll]] for (uint j = 0u; j < FELLOW_TAP_BATCH; j++) {
				t[j] = textureProj(sampler2DShadow(shadow, shadow_sampler), vec4(pos + shadow_pixel_size * (disk_rotation * scene_data_block.data.directional_soft_shadow_kernel[i + j].xy), depth, 1.0));
			}
			[[unroll]] for (uint j = 0u; j < FELLOW_TAP_BATCH; j++) {
				avg += t[j];
			}
		}
	} else {
		SPEC_CONSTANT_LOOP_ANNOTATION
		for (uint i = 0; i < sc_directional_soft_shadow_samples(); i++) {
			avg += textureProj(sampler2DShadow(shadow, shadow_sampler), vec4(pos + shadow_pixel_size * (disk_rotation * scene_data_block.data.directional_soft_shadow_kernel[i].xy), depth, 1.0));
		}
	}

	return FELLOW_DBG_RESULT(fellow_dbg_dir_uniform, half(avg * (1.0 / float(sc_directional_soft_shadow_samples()))));
}

half sample_pcf_shadow(texture2D shadow, vec2 shadow_pixel_size, vec3 coord, float taa_frame_count) {
	vec2 pos = coord.xy;
	float depth = coord.z;
	FELLOW_DBG_INC(fellow_dbg_pos_call);

	//if only one sample is taken, take it from the center
	if (sc_soft_shadow_samples() == 0) {
		return half(textureProj(sampler2DShadow(shadow, shadow_sampler), vec4(pos, depth, 1.0)));
	}

#ifdef FELLOW_SHADOW_MAX_MIP
	// FellowEngine (rendering/fellow/shadows/positional_max_mip), spot lights: every tap below is a 2x2 comparison within
	// |shadow_pixel_size| of pos. Receiver closer to the light than every atlas texel there: all taps lit (the loop's
	// result for that case, same arithmetic); every texel at least as close (plus a margin): all taps shadowed.
	if (sc_fellow_positional_max_mip() && bool(scene_data_block.data.flags & SCENE_DATA_FLAGS_FELLOW_POSITIONAL_SHADOW_MAX_MIP)) {
		vec2 mm = fellow_shadow_window_minmax(positional_shadow_max_mip, pos, abs(shadow_pixel_size));
		if (mm.y < depth - FELLOW_SHADOW_MAX_MIP_EPSILON) {
			return half(float(sc_soft_shadow_samples()) * (1.0 / float(sc_soft_shadow_samples())));
		}
		if (mm.x > depth + FELLOW_SHADOW_MAX_MIP_EPSILON) {
			return half(0.0);
		}
	}
#endif

	FELLOW_DBG_INC(fellow_dbg_pos_full);
	mat2 disk_rotation;
	{
		float r = quick_hash(gl_FragCoord.xy + vec2(taa_frame_count * 5.588238)) * 2.0 * M_PI;
		float sr = sin(r);
		float cr = cos(r);
		disk_rotation = mat2(vec2(cr, -sr), vec2(sr, cr));
	}

	float avg = 0.0;

	if (FELLOW_BATCH_TAPS_POS(sc_soft_shadow_samples())) {
		SPEC_CONSTANT_LOOP_ANNOTATION
		for (uint i = 0; i < sc_soft_shadow_samples(); i += FELLOW_TAP_BATCH) {
			float t[FELLOW_TAP_BATCH];
			[[unroll]] for (uint j = 0u; j < FELLOW_TAP_BATCH; j++) {
				t[j] = textureProj(sampler2DShadow(shadow, shadow_sampler), vec4(pos + shadow_pixel_size * (disk_rotation * scene_data_block.data.soft_shadow_kernel[i + j].xy), depth, 1.0));
			}
			[[unroll]] for (uint j = 0u; j < FELLOW_TAP_BATCH; j++) {
				avg += t[j];
			}
		}
	} else {
		SPEC_CONSTANT_LOOP_ANNOTATION
		for (uint i = 0; i < sc_soft_shadow_samples(); i++) {
			avg += textureProj(sampler2DShadow(shadow, shadow_sampler), vec4(pos + shadow_pixel_size * (disk_rotation * scene_data_block.data.soft_shadow_kernel[i].xy), depth, 1.0));
		}
	}

	return FELLOW_DBG_RESULT(fellow_dbg_pos_uniform, half(avg * (1.0 / float(sc_soft_shadow_samples()))));
}

half sample_omni_pcf_shadow(texture2D shadow, float blur_scale, vec2 coord, vec4 uv_rect, vec2 flip_offset, float depth, float taa_frame_count) {
	FELLOW_DBG_INC(fellow_dbg_pos_call);
	//if only one sample is taken, take it from the center
	if (sc_soft_shadow_samples() == 0) {
		vec2 pos = coord * 0.5 + 0.5;
		pos = uv_rect.xy + pos * uv_rect.zw;
		return half(textureProj(sampler2DShadow(shadow, shadow_sampler), vec4(pos, depth, 1.0)));
	}

#ifdef FELLOW_SHADOW_MAX_MIP
	// FellowEngine (rendering/fellow/shadows/positional_max_mip), omni (and area) lights: every tap below lies within
	// |offset_scale| (per axis, kernel inside the unit disk) of coord in paraboloid space. If no tap can cross the rim
	// (|coord| + |offset| < 1, so none flips to the other paraboloid), all taps are 2x2 comparisons in this paraboloid's
	// rect within that box: decide them all from the atlas's min/max pyramid as in sample_pcf_shadow().
	if (sc_fellow_positional_max_mip() && bool(scene_data_block.data.flags & SCENE_DATA_FLAGS_FELLOW_POSITIONAL_SHADOW_MAX_MIP)) {
		vec2 fellow_r = abs(blur_scale * 2.0 * scene_data_block.data.shadow_atlas_pixel_size / uv_rect.zw);
		if (length(coord) + length(fellow_r) < 0.999) {
			vec2 center = uv_rect.xy + (coord * 0.5 + 0.5) * uv_rect.zw;
			vec2 mm = fellow_shadow_window_minmax(positional_shadow_max_mip, center, fellow_r * 0.5 * abs(uv_rect.zw));
			if (mm.y < depth - FELLOW_SHADOW_MAX_MIP_EPSILON) {
				return half(float(sc_soft_shadow_samples()) * (1.0 / float(sc_soft_shadow_samples())));
			}
			if (mm.x > depth + FELLOW_SHADOW_MAX_MIP_EPSILON) {
				return half(0.0);
			}
		}
	}
#endif

	FELLOW_DBG_INC(fellow_dbg_pos_full);
	mat2 disk_rotation;
	{
		float r = quick_hash(gl_FragCoord.xy + vec2(taa_frame_count * 5.588238)) * 2.0 * M_PI;
		float sr = sin(r);
		float cr = cos(r);
		disk_rotation = mat2(vec2(cr, -sr), vec2(sr, cr));
	}

	float avg = 0.0;
	vec2 offset_scale = blur_scale * 2.0 * scene_data_block.data.shadow_atlas_pixel_size / uv_rect.zw;

	if (FELLOW_BATCH_TAPS_POS(sc_soft_shadow_samples())) {
		SPEC_CONSTANT_LOOP_ANNOTATION
		for (uint i = 0; i < sc_soft_shadow_samples(); i += FELLOW_TAP_BATCH) {
			float t[FELLOW_TAP_BATCH];
			[[unroll]] for (uint j = 0u; j < FELLOW_TAP_BATCH; j++) {
				vec2 offset = offset_scale * (disk_rotation * scene_data_block.data.soft_shadow_kernel[i + j].xy);
				vec2 sample_coord = coord + offset;

				float sample_coord_length_squared = dot(sample_coord, sample_coord);
				bool do_flip = sample_coord_length_squared > 1.0;

				if (do_flip) {
					float len = sqrt(sample_coord_length_squared);
					sample_coord = sample_coord * (2.0 / len - 1.0);
				}

				sample_coord = sample_coord * 0.5 + 0.5;
				sample_coord = uv_rect.xy + sample_coord * uv_rect.zw;

				if (do_flip) {
					sample_coord += flip_offset;
				}
				t[j] = textureProj(sampler2DShadow(shadow, shadow_sampler), vec4(sample_coord, depth, 1.0));
			}
			[[unroll]] for (uint j = 0u; j < FELLOW_TAP_BATCH; j++) {
				avg += t[j];
			}
		}
	} else {
		SPEC_CONSTANT_LOOP_ANNOTATION
		for (uint i = 0; i < sc_soft_shadow_samples(); i++) {
			vec2 offset = offset_scale * (disk_rotation * scene_data_block.data.soft_shadow_kernel[i].xy);
			vec2 sample_coord = coord + offset;

			float sample_coord_length_squared = dot(sample_coord, sample_coord);
			bool do_flip = sample_coord_length_squared > 1.0;

			if (do_flip) {
				float len = sqrt(sample_coord_length_squared);
				sample_coord = sample_coord * (2.0 / len - 1.0);
			}

			sample_coord = sample_coord * 0.5 + 0.5;
			sample_coord = uv_rect.xy + sample_coord * uv_rect.zw;

			if (do_flip) {
				sample_coord += flip_offset;
			}
			avg += textureProj(sampler2DShadow(shadow, shadow_sampler), vec4(sample_coord, depth, 1.0));
		}
	}

	return FELLOW_DBG_RESULT(fellow_dbg_pos_uniform, half(avg * (1.0 / float(sc_soft_shadow_samples()))));
}

half sample_directional_soft_shadow(texture2D shadow, vec3 pssm_coord, vec2 tex_scale, float taa_frame_count) {
	FELLOW_DBG_INC(fellow_dbg_dir_call);
#ifdef FELLOW_EXP_BIT6
	// Diagnostics (debug/scene_shader_experiments 64; the image changes): the filter's code stays in the shader (the
	// condition is not known when it is compiled) but returns lit at once, separating the cost of its work from the cost
	// of its registers.
	if (taa_frame_count >= 0.0) {
		return half(1.0);
	}
#endif
#ifdef FELLOW_SHADOW_MAX_MIP
	// FellowEngine: the blocker search below takes bilinear samples inside |tex_scale| (per axis) of pssm_coord.xy. If no
	// atlas texel there is closer to the light than the receiver, it finds no blocker and returns 1.0: return it now.
	// If every texel there is closer to the light than the receiver, every search tap finds a blocker, the blocker
	// average is at most the region's largest depth, so the penumbra (monotonic in the blocker depth) and with it the
	// filter radius are at most the ones computed from that largest depth: if every texel within that radius is closer
	// to the light than the receiver too, every filter tap compares 0: return the filter's result, 0.
	if (sc_fellow_directional_max_mip() && bool(scene_data_block.data.flags & SCENE_DATA_FLAGS_FELLOW_SHADOW_MAX_MIP)) {
		vec2 mm = fellow_shadow_box_minmax(pssm_coord.xy, abs(tex_scale));
		if (mm.y < pssm_coord.z - FELLOW_SHADOW_MAX_MIP_EPSILON) {
			return half(1.0);
		}
		if (mm.x > pssm_coord.z + FELLOW_SHADOW_MAX_MIP_EPSILON) {
			float max_penumbra;
			bool bounded = true;
			if (bool(scene_data_block.data.flags & SCENE_DATA_FLAGS_FELLOW_PCSS_DIRECTIONAL_PENUMBRA)) {
				max_penumbra = (-pssm_coord.z + mm.y) / max(1.0 - pssm_coord.z, 1e-6);
			} else {
				bounded = mm.y < 1.0 - 1e-6;
				max_penumbra = (-pssm_coord.z + mm.y) / max(1.0 - mm.y, 1e-6);
			}
			if (bounded && fellow_shadow_box_minmax(pssm_coord.xy, abs(tex_scale) * max_penumbra).x > pssm_coord.z + FELLOW_SHADOW_MAX_MIP_EPSILON) {
				return half(0.0);
			}
		}
	}
#endif
	//find blocker
	float blocker_count = 0.0;
	float blocker_average = 0.0;

	FELLOW_DBG_INC(fellow_dbg_dir_full);
	mat2 disk_rotation;
	{
		float r = quick_hash(gl_FragCoord.xy + vec2(taa_frame_count * 5.588238)) * 2.0 * M_PI;
		float sr = sin(r);
		float cr = cos(r);
		disk_rotation = mat2(vec2(cr, -sr), vec2(sr, cr));
	}

	if (FELLOW_BATCH_TAPS_DIR(sc_directional_penumbra_shadow_samples())) {
		SPEC_CONSTANT_LOOP_ANNOTATION
		for (uint i = 0; i < sc_directional_penumbra_shadow_samples(); i += FELLOW_TAP_BATCH) {
			float d[FELLOW_TAP_BATCH];
			[[unroll]] for (uint j = 0u; j < FELLOW_TAP_BATCH; j++) {
				vec2 suv = pssm_coord.xy + (disk_rotation * scene_data_block.data.directional_penumbra_shadow_kernel[i + j].xy) * tex_scale;
				d[j] = textureLod(sampler2D(shadow, SAMPLER_LINEAR_CLAMP), suv, 0.0).r;
			}
			[[unroll]] for (uint j = 0u; j < FELLOW_TAP_BATCH; j++) {
				if (d[j] > pssm_coord.z) {
					blocker_average += d[j];
					blocker_count += 1.0;
				}
			}
		}
	} else {
		FELLOW_DIR_PCSS_LOOP_ANNOTATION
		for (uint i = 0; i < sc_directional_penumbra_shadow_samples(); i++) {
			vec2 suv = pssm_coord.xy + (disk_rotation * scene_data_block.data.directional_penumbra_shadow_kernel[i].xy) * tex_scale;
			float d = textureLod(sampler2D(shadow, SAMPLER_LINEAR_CLAMP), suv, 0.0).r;
			if (d > pssm_coord.z) {
				blocker_average += d;
				blocker_count += 1.0;
			}
		}
	}

	if (blocker_count > 0.0) {
		//blockers found, do soft shadow
		blocker_average /= blocker_count;
		float penumbra;
		if (bool(scene_data_block.data.flags & SCENE_DATA_FLAGS_FELLOW_PCSS_DIRECTIONAL_PENUMBRA)) {
			// FellowEngine: parallel light. tex_scale is the search radius, light size x receiver distance from the near
			// plane (1 - z in reverse Z); the penumbra is light size x (receiver - blocker distance), i.e. that radius
			// times (blocker - receiver depth) / (1 - receiver depth). Always within the search radius.
			penumbra = (-pssm_coord.z + blocker_average) / max(1.0 - pssm_coord.z, 1e-6);
		} else {
			penumbra = (-pssm_coord.z + blocker_average) / (1.0 - blocker_average);
		}
		tex_scale *= penumbra;

#ifdef FELLOW_SHADOW_MAX_MIP
		// FellowEngine (rendering/fellow/shadows/pcss_filter_early_out, decision 0039; experiment bit 128 forces it): the
		// filter below takes 2x2 comparisons within |tex_scale| (per axis) of pssm_coord.xy. Every atlas texel there
		// farther from the light than the receiver: all taps lit, the loop's result is 1.0; every texel there closer (plus
		// the margin): 0.0. (The early-outs above decide from the search radius before the search; this one from the
		// penumbra after it.)
#ifdef FELLOW_EXP_BIT7
		bool fellow_filter_early_out = true;
#else
		bool fellow_filter_early_out = sc_fellow_pcss_filter_early_out();
#endif
		if (fellow_filter_early_out && sc_fellow_directional_max_mip() && bool(scene_data_block.data.flags & SCENE_DATA_FLAGS_FELLOW_SHADOW_MAX_MIP)) {
			// A wider margin than the early-outs above (m8_f112: with theirs, RTX differed from the filter in Foliage Lab
			// and Bistro exterior): the filter's small radius puts the receiver's own texels within a hair of the
			// reference, and the comparison sampler may compare at the 16-bit atlas's precision; two 16-bit steps.
			vec2 mm = fellow_shadow_box_minmax(pssm_coord.xy, abs(tex_scale));
			if (mm.y < pssm_coord.z - FELLOW_SHADOW_MAX_MIP_FILTER_EPSILON) {
				return FELLOW_DBG_RESULT(fellow_dbg_dir_uniform, half(1.0));
			}
			if (mm.x > pssm_coord.z + FELLOW_SHADOW_MAX_MIP_FILTER_EPSILON) {
				return FELLOW_DBG_RESULT(fellow_dbg_dir_uniform, half(0.0));
			}
		}
#endif
		FELLOW_DBG_INC(fellow_dbg_dir_pcf);
		float s = 0.0;

		if (FELLOW_BATCH_TAPS_DIR(sc_directional_penumbra_shadow_samples())) {
			SPEC_CONSTANT_LOOP_ANNOTATION
			for (uint i = 0; i < sc_directional_penumbra_shadow_samples(); i += FELLOW_TAP_BATCH) {
				float t[FELLOW_TAP_BATCH];
				[[unroll]] for (uint j = 0u; j < FELLOW_TAP_BATCH; j++) {
					vec2 suv = pssm_coord.xy + (disk_rotation * scene_data_block.data.directional_penumbra_shadow_kernel[i + j].xy) * tex_scale;
					t[j] = textureProj(sampler2DShadow(shadow, shadow_sampler), vec4(suv, pssm_coord.z, 1.0));
				}
				[[unroll]] for (uint j = 0u; j < FELLOW_TAP_BATCH; j++) {
					s += t[j];
				}
			}
		} else {
			FELLOW_DIR_PCSS_LOOP_ANNOTATION
			for (uint i = 0; i < sc_directional_penumbra_shadow_samples(); i++) {
				vec2 suv = pssm_coord.xy + (disk_rotation * scene_data_block.data.directional_penumbra_shadow_kernel[i].xy) * tex_scale;
				s += textureProj(sampler2DShadow(shadow, shadow_sampler), vec4(suv, pssm_coord.z, 1.0));
			}
		}

		return FELLOW_DBG_RESULT(fellow_dbg_dir_uniform, half(s / float(sc_directional_penumbra_shadow_samples())));

	} else {
		//no blockers found, so no shadow
		return FELLOW_DBG_RESULT(fellow_dbg_dir_uniform, half(1.0));
	}
}

#if defined(FELLOW_EXP_BIT11) && !defined(FELLOW_EXP_BIT3) && !defined(FELLOW_EXP_BIT6)
#define FELLOW_PCSS_PAIR
// Experiment (debug/scene_shader_experiments 2048): a blended pixel's two cascades through one PCSS whose taps of both
// cascades are fetched together (search with search, filter with filter); per cascade the same arithmetic as
// sample_directional_soft_shadow().
hvec2 sample_directional_soft_shadow_pair(texture2D shadow, vec3 p_coord0, vec2 p_scale0, vec3 p_coord1, vec2 p_scale1, float taa_frame_count) {
	vec3 pssm_coord[2] = vec3[2](p_coord0, p_coord1);
	vec2 tex_scale[2] = vec2[2](p_scale0, p_scale1);
	half result[2] = half[2](half(1.0), half(1.0));
	bool fellow_active[2] = bool[2](true, true);
#ifdef FELLOW_SHADOW_MAX_MIP
	if (sc_fellow_directional_max_mip() && bool(scene_data_block.data.flags & SCENE_DATA_FLAGS_FELLOW_SHADOW_MAX_MIP)) {
		[[unroll]] for (uint k = 0u; k < 2u; k++) {
			vec2 mm = fellow_shadow_box_minmax(pssm_coord[k].xy, abs(tex_scale[k]));
			if (mm.y < pssm_coord[k].z - FELLOW_SHADOW_MAX_MIP_EPSILON) {
				result[k] = half(1.0);
				fellow_active[k] = false;
			} else if (mm.x > pssm_coord[k].z + FELLOW_SHADOW_MAX_MIP_EPSILON) {
				float max_penumbra;
				bool bounded = true;
				if (bool(scene_data_block.data.flags & SCENE_DATA_FLAGS_FELLOW_PCSS_DIRECTIONAL_PENUMBRA)) {
					max_penumbra = (-pssm_coord[k].z + mm.y) / max(1.0 - pssm_coord[k].z, 1e-6);
				} else {
					bounded = mm.y < 1.0 - 1e-6;
					max_penumbra = (-pssm_coord[k].z + mm.y) / max(1.0 - mm.y, 1e-6);
				}
				if (bounded && fellow_shadow_box_minmax(pssm_coord[k].xy, abs(tex_scale[k]) * max_penumbra).x > pssm_coord[k].z + FELLOW_SHADOW_MAX_MIP_EPSILON) {
					result[k] = half(0.0);
					fellow_active[k] = false;
				}
			}
		}
	}
#endif
	if (!fellow_active[0] && !fellow_active[1]) {
		return hvec2(result[0], result[1]);
	}

	mat2 disk_rotation;
	{
		float r = quick_hash(gl_FragCoord.xy + vec2(taa_frame_count * 5.588238)) * 2.0 * M_PI;
		float sr = sin(r);
		float cr = cos(r);
		disk_rotation = mat2(vec2(cr, -sr), vec2(sr, cr));
	}

	float blocker_count[2] = float[2](0.0, 0.0);
	float blocker_average[2] = float[2](0.0, 0.0);
	if (FELLOW_BATCH_TAPS_DIR(sc_directional_penumbra_shadow_samples())) {
		SPEC_CONSTANT_LOOP_ANNOTATION
		for (uint i = 0; i < sc_directional_penumbra_shadow_samples(); i += FELLOW_TAP_BATCH) {
			float d[2 * FELLOW_TAP_BATCH];
			[[unroll]] for (uint k = 0u; k < 2u; k++) {
				if (fellow_active[k]) {
					[[unroll]] for (uint j = 0u; j < FELLOW_TAP_BATCH; j++) {
						vec2 suv = pssm_coord[k].xy + (disk_rotation * scene_data_block.data.directional_penumbra_shadow_kernel[i + j].xy) * tex_scale[k];
						d[k * FELLOW_TAP_BATCH + j] = textureLod(sampler2D(shadow, SAMPLER_LINEAR_CLAMP), suv, 0.0).r;
					}
				}
			}
			[[unroll]] for (uint k = 0u; k < 2u; k++) {
				if (fellow_active[k]) {
					[[unroll]] for (uint j = 0u; j < FELLOW_TAP_BATCH; j++) {
						if (d[k * FELLOW_TAP_BATCH + j] > pssm_coord[k].z) {
							blocker_average[k] += d[k * FELLOW_TAP_BATCH + j];
							blocker_count[k] += 1.0;
						}
					}
				}
			}
		}
	} else {
		SPEC_CONSTANT_LOOP_ANNOTATION
		for (uint i = 0; i < sc_directional_penumbra_shadow_samples(); i++) {
			[[unroll]] for (uint k = 0u; k < 2u; k++) {
				if (fellow_active[k]) {
					vec2 suv = pssm_coord[k].xy + (disk_rotation * scene_data_block.data.directional_penumbra_shadow_kernel[i].xy) * tex_scale[k];
					float d = textureLod(sampler2D(shadow, SAMPLER_LINEAR_CLAMP), suv, 0.0).r;
					if (d > pssm_coord[k].z) {
						blocker_average[k] += d;
						blocker_count[k] += 1.0;
					}
				}
			}
		}
	}

	[[unroll]] for (uint k = 0u; k < 2u; k++) {
		if (fellow_active[k]) {
			if (blocker_count[k] > 0.0) {
				blocker_average[k] /= blocker_count[k];
				float penumbra;
				if (bool(scene_data_block.data.flags & SCENE_DATA_FLAGS_FELLOW_PCSS_DIRECTIONAL_PENUMBRA)) {
					penumbra = (-pssm_coord[k].z + blocker_average[k]) / max(1.0 - pssm_coord[k].z, 1e-6);
				} else {
					penumbra = (-pssm_coord[k].z + blocker_average[k]) / (1.0 - blocker_average[k]);
				}
				tex_scale[k] *= penumbra;
#ifdef FELLOW_SHADOW_MAX_MIP
#ifdef FELLOW_EXP_BIT7
				bool fellow_filter_early_out = true;
#else
				bool fellow_filter_early_out = sc_fellow_pcss_filter_early_out();
#endif
				if (fellow_filter_early_out && sc_fellow_directional_max_mip() && bool(scene_data_block.data.flags & SCENE_DATA_FLAGS_FELLOW_SHADOW_MAX_MIP)) {
					vec2 mm = fellow_shadow_box_minmax(pssm_coord[k].xy, abs(tex_scale[k]));
					if (mm.y < pssm_coord[k].z - FELLOW_SHADOW_MAX_MIP_FILTER_EPSILON) {
						result[k] = half(1.0);
						fellow_active[k] = false;
					} else if (mm.x > pssm_coord[k].z + FELLOW_SHADOW_MAX_MIP_FILTER_EPSILON) {
						result[k] = half(0.0);
						fellow_active[k] = false;
					}
				}
#endif
			} else {
				//no blockers found, so no shadow
				result[k] = half(1.0);
				fellow_active[k] = false;
			}
		}
	}
	if (!fellow_active[0] && !fellow_active[1]) {
		return hvec2(result[0], result[1]);
	}

	float s[2] = float[2](0.0, 0.0);
	if (FELLOW_BATCH_TAPS_DIR(sc_directional_penumbra_shadow_samples())) {
		SPEC_CONSTANT_LOOP_ANNOTATION
		for (uint i = 0; i < sc_directional_penumbra_shadow_samples(); i += FELLOW_TAP_BATCH) {
			float t[2 * FELLOW_TAP_BATCH];
			[[unroll]] for (uint k = 0u; k < 2u; k++) {
				if (fellow_active[k]) {
					[[unroll]] for (uint j = 0u; j < FELLOW_TAP_BATCH; j++) {
						vec2 suv = pssm_coord[k].xy + (disk_rotation * scene_data_block.data.directional_penumbra_shadow_kernel[i + j].xy) * tex_scale[k];
						t[k * FELLOW_TAP_BATCH + j] = textureProj(sampler2DShadow(shadow, shadow_sampler), vec4(suv, pssm_coord[k].z, 1.0));
					}
				}
			}
			[[unroll]] for (uint k = 0u; k < 2u; k++) {
				if (fellow_active[k]) {
					[[unroll]] for (uint j = 0u; j < FELLOW_TAP_BATCH; j++) {
						s[k] += t[k * FELLOW_TAP_BATCH + j];
					}
				}
			}
		}
	} else {
		SPEC_CONSTANT_LOOP_ANNOTATION
		for (uint i = 0; i < sc_directional_penumbra_shadow_samples(); i++) {
			[[unroll]] for (uint k = 0u; k < 2u; k++) {
				if (fellow_active[k]) {
					vec2 suv = pssm_coord[k].xy + (disk_rotation * scene_data_block.data.directional_penumbra_shadow_kernel[i].xy) * tex_scale[k];
					s[k] += textureProj(sampler2DShadow(shadow, shadow_sampler), vec4(suv, pssm_coord[k].z, 1.0));
				}
			}
		}
	}
	[[unroll]] for (uint k = 0u; k < 2u; k++) {
		if (fellow_active[k]) {
			result[k] = half(s[k] / float(sc_directional_penumbra_shadow_samples()));
		}
	}
	return hvec2(result[0], result[1]);
}
#endif

#endif // SHADOWS_DISABLED

half get_omni_attenuation(float distance, float inv_range, float decay) {
	float nd = distance * inv_range;
	nd *= nd;
	nd *= nd; // nd^4
	nd = max(1.0 - nd, 0.0);
	nd *= nd; // nd^2
	return half(nd * pow(max(distance, 0.0001), -decay));
}

void light_process_omni(uint idx, vec3 vertex, hvec3 eye_vec, hvec3 normal, vec3 vertex_ddx, vec3 vertex_ddy, hvec3 f0, half roughness, half metallic, float taa_frame_count, hvec3 albedo, inout half alpha, vec2 screen_uv, hvec3 energy_compensation,
#ifdef LIGHT_BACKLIGHT_USED
		hvec3 backlight,
#endif
#ifdef LIGHT_TRANSMITTANCE_USED
		hvec4 transmittance_color,
		half transmittance_depth,
		half transmittance_boost,
#endif
#ifdef LIGHT_RIM_USED
		half rim, half rim_tint,
#endif
#ifdef LIGHT_CLEARCOAT_USED
		half clearcoat, half clearcoat_roughness, hvec3 vertex_normal,
#endif
#ifdef LIGHT_ANISOTROPY_USED
		hvec3 binormal, hvec3 tangent, half anisotropy,
#endif
		inout hvec3 diffuse_light, inout hvec3 specular_light) {

	// Omni light attenuation.
	vec3 light_rel_vec = omni_lights.data[idx].position - vertex;
	float light_length = length(light_rel_vec);
#ifdef FELLOW_BACKFACING_LIGHT_SKIP_OK
	// FellowEngine (rendering/fellow/lights/zero_attenuation_skip): at or beyond the range get_omni_attenuation() is
	// exactly 0 (distance * inv_range >= 1, the same product it computes), and a light with zero attenuation adds
	// nothing for these materials: skip it before the attenuation's pow().
	const bool fellow_skip_zero = sc_fellow_zero_attenuation_skip() && bool(scene_data_block.data.flags & SCENE_DATA_FLAGS_FELLOW_LIGHT_ZERO_ATTENUATION_SKIP);
	if (fellow_skip_zero && light_length * omni_lights.data[idx].inv_radius >= 1.0) {
		return;
	}
#endif
	half omni_attenuation = get_omni_attenuation(light_length, omni_lights.data[idx].inv_radius, omni_lights.data[idx].attenuation);
#ifdef FELLOW_BACKFACING_LIGHT_SKIP_OK
	// (The shadow lookup and light_compute() both do nothing at or below HALF_FLT_MIN.)
	if (fellow_skip_zero && omni_attenuation <= HALF_FLT_MIN) {
		return;
	}
#endif

	// Compute size.
	half size = half(0.0);
	if (sc_use_light_soft_shadows() && omni_lights.data[idx].size > 0.0) {
		half t = half(omni_lights.data[idx].size / max(0.001, light_length));
		size = half(1.0) / sqrt(half(1.0) + t * t);
		size = max(half(1.0) - size, half(0.0));
	}

#ifdef FELLOW_BACKFACING_LIGHT_SKIP_OK
	// FellowEngine (rendering/fellow/shadows/skip_backfacing_positional): N.L <= 0 -> this light adds exactly nothing.
	if (sc_fellow_skip_backfacing_positional() && bool(scene_data_block.data.flags & SCENE_DATA_FLAGS_FELLOW_SKIP_BACKFACING_POSITIONAL) && min(size + dot(normal, hvec3(light_rel_vec / light_length)), half(1.0)) <= half(0.0)) {
		return;
	}
#endif

	half shadow = half(1.0);
#ifndef SHADOWS_DISABLED
	// Omni light shadow.
	if (omni_attenuation > HALF_FLT_MIN && omni_lights.data[idx].shadow_opacity > 0.001) {
		// there is a shadowmap
		vec2 texel_size = scene_data_block.data.shadow_atlas_pixel_size;
		vec4 base_uv_rect = omni_lights.data[idx].atlas_rect;
		base_uv_rect.xy += texel_size;
		base_uv_rect.zw -= texel_size * 2.0;

		// Omni lights use direction.xy to store to store the offset between the two paraboloid regions
		vec2 flip_offset = omni_lights.data[idx].direction.xy;

		vec3 local_vert = (omni_lights.data[idx].shadow_matrix * vec4(vertex, 1.0)).xyz;

		float shadow_len = length(local_vert); //need to remember shadow len from here
		vec3 shadow_dir = normalize(local_vert);

		vec3 local_normal = normalize(mat3(omni_lights.data[idx].shadow_matrix) * vec3(normal));
		vec3 normal_bias = local_normal * omni_lights.data[idx].shadow_normal_bias * (1.0 - abs(dot(local_normal, shadow_dir)));

		if (sc_use_light_soft_shadows() && omni_lights.data[idx].soft_shadow_size > 0.0) {
			//soft shadow

			//find blocker

			float blocker_count = 0.0;
			float blocker_average = 0.0;

			mat2 disk_rotation;
			{
				float r = quick_hash(gl_FragCoord.xy + vec2(taa_frame_count * 5.588238)) * 2.0 * M_PI;
				float sr = sin(r);
				float cr = cos(r);
				disk_rotation = mat2(vec2(cr, -sr), vec2(sr, cr));
			}

			vec3 basis_normal = shadow_dir;
			vec3 v0 = abs(basis_normal.z) < 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(0.0, 1.0, 0.0);
			vec3 tangent = normalize(cross(v0, basis_normal));
			vec3 bitangent = normalize(cross(tangent, basis_normal));
			float z_norm = 1.0 - shadow_len * omni_lights.data[idx].inv_radius;

			tangent *= omni_lights.data[idx].soft_shadow_size * omni_lights.data[idx].soft_shadow_scale;
			bitangent *= omni_lights.data[idx].soft_shadow_size * omni_lights.data[idx].soft_shadow_scale;

			if (FELLOW_BATCH_TAPS_POS(sc_penumbra_shadow_samples())) {
				SPEC_CONSTANT_LOOP_ANNOTATION
				for (uint i = 0; i < sc_penumbra_shadow_samples(); i += FELLOW_TAP_BATCH) {
					float d[FELLOW_TAP_BATCH];
					[[unroll]] for (uint j = 0u; j < FELLOW_TAP_BATCH; j++) {
						vec2 disk = disk_rotation * scene_data_block.data.penumbra_shadow_kernel[i + j].xy;

						vec3 pos = local_vert + tangent * disk.x + bitangent * disk.y;

						pos = normalize(pos);

						vec4 uv_rect = base_uv_rect;

						if (pos.z >= 0.0) {
							uv_rect.xy += flip_offset;
						}

						pos.z = 1.0 + abs(pos.z);
						pos.xy /= pos.z;

						pos.xy = pos.xy * 0.5 + 0.5;
						pos.xy = uv_rect.xy + pos.xy * uv_rect.zw;

						d[j] = textureLod(sampler2D(shadow_atlas, SAMPLER_LINEAR_CLAMP), pos.xy, 0.0).r;
					}
					[[unroll]] for (uint j = 0u; j < FELLOW_TAP_BATCH; j++) {
						if (d[j] > z_norm) {
							blocker_average += d[j];
							blocker_count += 1.0;
						}
					}
				}
			} else {
				SPEC_CONSTANT_LOOP_ANNOTATION
				for (uint i = 0; i < sc_penumbra_shadow_samples(); i++) {
					vec2 disk = disk_rotation * scene_data_block.data.penumbra_shadow_kernel[i].xy;

					vec3 pos = local_vert + tangent * disk.x + bitangent * disk.y;

					pos = normalize(pos);

					vec4 uv_rect = base_uv_rect;

					if (pos.z >= 0.0) {
						uv_rect.xy += flip_offset;
					}

					pos.z = 1.0 + abs(pos.z);
					pos.xy /= pos.z;

					pos.xy = pos.xy * 0.5 + 0.5;
					pos.xy = uv_rect.xy + pos.xy * uv_rect.zw;

					float d = textureLod(sampler2D(shadow_atlas, SAMPLER_LINEAR_CLAMP), pos.xy, 0.0).r;
					if (d > z_norm) {
						blocker_average += d;
						blocker_count += 1.0;
					}
				}
			}

			if (blocker_count > 0.0) {
				//blockers found, do soft shadow
				blocker_average /= blocker_count;
				float penumbra = (-z_norm + blocker_average) / (1.0 - blocker_average);
				tangent *= penumbra;
				bitangent *= penumbra;

				z_norm += omni_lights.data[idx].inv_radius * omni_lights.data[idx].shadow_bias;

				shadow = half(0.0);

				if (FELLOW_BATCH_TAPS_POS(sc_penumbra_shadow_samples())) {
					SPEC_CONSTANT_LOOP_ANNOTATION
					for (uint i = 0; i < sc_penumbra_shadow_samples(); i += FELLOW_TAP_BATCH) {
						float t[FELLOW_TAP_BATCH];
						[[unroll]] for (uint j = 0u; j < FELLOW_TAP_BATCH; j++) {
							vec2 disk = disk_rotation * scene_data_block.data.penumbra_shadow_kernel[i + j].xy;
							vec3 pos = local_vert + tangent * disk.x + bitangent * disk.y;

							pos = normalize(pos);
							pos = normalize(pos + normal_bias);

							vec4 uv_rect = base_uv_rect;

							if (pos.z >= 0.0) {
								uv_rect.xy += flip_offset;
							}

							pos.z = 1.0 + abs(pos.z);
							pos.xy /= pos.z;

							pos.xy = pos.xy * 0.5 + 0.5;
							pos.xy = uv_rect.xy + pos.xy * uv_rect.zw;
							t[j] = textureProj(sampler2DShadow(shadow_atlas, shadow_sampler), vec4(pos.xy, z_norm, 1.0));
						}
						[[unroll]] for (uint j = 0u; j < FELLOW_TAP_BATCH; j++) {
							shadow += half(t[j]);
						}
					}
				} else {
					SPEC_CONSTANT_LOOP_ANNOTATION
					for (uint i = 0; i < sc_penumbra_shadow_samples(); i++) {
						vec2 disk = disk_rotation * scene_data_block.data.penumbra_shadow_kernel[i].xy;
						vec3 pos = local_vert + tangent * disk.x + bitangent * disk.y;

						pos = normalize(pos);
						pos = normalize(pos + normal_bias);

						vec4 uv_rect = base_uv_rect;

						if (pos.z >= 0.0) {
							uv_rect.xy += flip_offset;
						}

						pos.z = 1.0 + abs(pos.z);
						pos.xy /= pos.z;

						pos.xy = pos.xy * 0.5 + 0.5;
						pos.xy = uv_rect.xy + pos.xy * uv_rect.zw;
						shadow += half(textureProj(sampler2DShadow(shadow_atlas, shadow_sampler), vec4(pos.xy, z_norm, 1.0)));
					}
				}

				shadow /= half(sc_penumbra_shadow_samples());
				shadow = mix(half(1.0), shadow, half(omni_lights.data[idx].shadow_opacity));

			} else {
				//no blockers found, so no shadow
				shadow = half(1.0);
			}
		} else {
			vec4 uv_rect = base_uv_rect;

			vec3 shadow_sample = normalize(shadow_dir + normal_bias);
			if (shadow_sample.z >= 0.0) {
				uv_rect.xy += flip_offset;
				flip_offset *= -1.0;
			}

			shadow_sample.z = 1.0 + abs(shadow_sample.z);
			vec2 pos = shadow_sample.xy / shadow_sample.z;
			float depth = shadow_len - omni_lights.data[idx].shadow_bias;
			depth *= omni_lights.data[idx].inv_radius;
			depth = 1.0 - depth;
			shadow = mix(half(1.0), sample_omni_pcf_shadow(shadow_atlas, omni_lights.data[idx].soft_shadow_scale / shadow_sample.z, pos, uv_rect, flip_offset, depth, taa_frame_count), half(omni_lights.data[idx].shadow_opacity));
		}
	}
#endif

	vec3 color = omni_lights.data[idx].color;

#ifdef LIGHT_TRANSMITTANCE_USED
	half transmittance_z = transmittance_depth; //no transmittance by default
	transmittance_color.a *= omni_attenuation;
#ifndef SHADOWS_DISABLED
	if (omni_lights.data[idx].shadow_opacity > 0.001) {
		// Redo shadowmapping, but shrink the model a bit to avoid artifacts.
		vec2 texel_size = scene_data_block.data.shadow_atlas_pixel_size;
		vec4 uv_rect = omni_lights.data[idx].atlas_rect;
		uv_rect.xy += texel_size;
		uv_rect.zw -= texel_size * 2.0;

		// Omni lights use direction.xy to store to store the offset between the two paraboloid regions
		vec2 flip_offset = omni_lights.data[idx].direction.xy;

		vec3 local_vert = (omni_lights.data[idx].shadow_matrix * vec4(vertex - normal * omni_lights.data[idx].transmittance_bias, 1.0)).xyz;

		float shadow_len = length(local_vert); //need to remember shadow len from here
		vec3 shadow_sample = normalize(local_vert);

		if (shadow_sample.z >= 0.0) {
			uv_rect.xy += flip_offset;
			flip_offset *= -1.0;
		}

		shadow_sample.z = 1.0 + abs(shadow_sample.z);
		vec2 pos = shadow_sample.xy / shadow_sample.z;
		float depth = shadow_len * omni_lights.data[idx].inv_radius;
		depth = 1.0 - depth;

		pos = pos * 0.5 + 0.5;
		pos = uv_rect.xy + pos * uv_rect.zw;
		float shadow_z = textureLod(sampler2D(shadow_atlas, SAMPLER_LINEAR_CLAMP), pos, 0.0).r;
		transmittance_z = half((depth - shadow_z) / omni_lights.data[idx].inv_radius);
	}
#endif // !SHADOWS_DISABLED
#endif // LIGHT_TRANSMITTANCE_USED

	if (sc_use_light_projector() && omni_lights.data[idx].projector_rect != vec4(0.0)) {
		vec3 local_v = (omni_lights.data[idx].shadow_matrix * vec4(vertex, 1.0)).xyz;
		local_v = normalize(local_v);

		vec4 atlas_rect = omni_lights.data[idx].projector_rect;

		if (local_v.z >= 0.0) {
			atlas_rect.y += atlas_rect.w;
		}

		local_v.z = 1.0 + abs(local_v.z);

		local_v.xy /= local_v.z;
		local_v.xy = local_v.xy * 0.5 + 0.5;
		vec2 proj_uv = local_v.xy * atlas_rect.zw;

		// Clamp the uv coordinates to prevent bleeding from neighboring decals.
		vec2 texel_size = 1.0 / vec2(textureSize(sampler2D(decal_atlas_srgb, light_projector_sampler), 0));
		proj_uv = clamp(proj_uv, texel_size * 0.5, atlas_rect.zw - texel_size * 0.5);

		if (sc_projector_use_mipmaps()) {
			vec2 proj_uv_ddx;
			vec2 proj_uv_ddy;
			{
				vec3 local_v_ddx = (omni_lights.data[idx].shadow_matrix * vec4(vertex + vertex_ddx, 1.0)).xyz;
				local_v_ddx = normalize(local_v_ddx);

				if (local_v_ddx.z >= 0.0) {
					local_v_ddx.z += 1.0;
				} else {
					local_v_ddx.z = 1.0 - local_v_ddx.z;
				}

				local_v_ddx.xy /= local_v_ddx.z;
				local_v_ddx.xy = local_v_ddx.xy * 0.5 + 0.5;

				proj_uv_ddx = local_v_ddx.xy * atlas_rect.zw - proj_uv;

				vec3 local_v_ddy = (omni_lights.data[idx].shadow_matrix * vec4(vertex + vertex_ddy, 1.0)).xyz;
				local_v_ddy = normalize(local_v_ddy);

				if (local_v_ddy.z >= 0.0) {
					local_v_ddy.z += 1.0;
				} else {
					local_v_ddy.z = 1.0 - local_v_ddy.z;
				}

				local_v_ddy.xy /= local_v_ddy.z;
				local_v_ddy.xy = local_v_ddy.xy * 0.5 + 0.5;

				proj_uv_ddy = local_v_ddy.xy * atlas_rect.zw - proj_uv;
			}

			vec4 proj = textureGrad(sampler2D(decal_atlas_srgb, light_projector_sampler), proj_uv + atlas_rect.xy, proj_uv_ddx, proj_uv_ddy);
			color *= proj.rgb * proj.a;
		} else {
			vec4 proj = textureLod(sampler2D(decal_atlas_srgb, light_projector_sampler), proj_uv + atlas_rect.xy, 0.0);
			color *= proj.rgb * proj.a;
		}
	}

	vec3 light_rel_vec_norm = light_rel_vec / light_length;
	light_compute(normal, hvec3(light_rel_vec_norm), eye_vec, size, hvec3(color), false, omni_attenuation * shadow, f0, roughness, metallic, half(omni_lights.data[idx].specular_amount), albedo, alpha, screen_uv, energy_compensation,
#ifdef LIGHT_BACKLIGHT_USED
			backlight,
#endif
#ifdef LIGHT_TRANSMITTANCE_USED
			transmittance_color,
			transmittance_depth,
			transmittance_boost,
			transmittance_z,
#endif
#ifdef LIGHT_RIM_USED
			rim * omni_attenuation, rim_tint,
#endif
#ifdef LIGHT_CLEARCOAT_USED
			clearcoat, clearcoat_roughness, vertex_normal,
#endif
#ifdef LIGHT_ANISOTROPY_USED
			binormal, tangent, anisotropy,
#endif
			diffuse_light,
			specular_light);
}

vec2 normal_to_panorama(vec3 n) {
	n = normalize(n);
	vec2 panorama_coords = vec2(atan(n.x, n.z), acos(-n.y));

	if (panorama_coords.x < 0.0) {
		panorama_coords.x += M_PI * 2.0;
	}

	panorama_coords /= vec2(M_PI * 2.0, M_PI);
	return panorama_coords;
}

void light_process_spot(uint idx, vec3 vertex, hvec3 eye_vec, hvec3 normal, vec3 vertex_ddx, vec3 vertex_ddy, hvec3 f0, half roughness, half metallic, float taa_frame_count, hvec3 albedo, inout half alpha, vec2 screen_uv, hvec3 energy_compensation,
#ifdef LIGHT_BACKLIGHT_USED
		hvec3 backlight,
#endif
#ifdef LIGHT_TRANSMITTANCE_USED
		hvec4 transmittance_color,
		half transmittance_depth,
		half transmittance_boost,
#endif
#ifdef LIGHT_RIM_USED
		half rim, half rim_tint,
#endif
#ifdef LIGHT_CLEARCOAT_USED
		half clearcoat, half clearcoat_roughness, hvec3 vertex_normal,
#endif
#ifdef LIGHT_ANISOTROPY_USED
		hvec3 binormal, hvec3 tangent, half anisotropy,
#endif
		inout hvec3 diffuse_light,
		inout hvec3 specular_light) {

	// Spot light attenuation.
	vec3 light_rel_vec = spot_lights.data[idx].position - vertex;
	float light_length = length(light_rel_vec);
	hvec3 light_rel_vec_norm = hvec3(light_rel_vec / light_length);
#ifdef FELLOW_BACKFACING_LIGHT_SKIP_OK
	// FellowEngine (rendering/fellow/lights/zero_attenuation_skip): beyond the range or outside the cone the attenuation
	// below is exactly 0 (range: as for omni lights; cone: scos clamps to cone_angle, spot_rim = 1, 1 - pow(1, x) = 0).
	const bool fellow_skip_zero = sc_fellow_zero_attenuation_skip() && bool(scene_data_block.data.flags & SCENE_DATA_FLAGS_FELLOW_LIGHT_ZERO_ATTENUATION_SKIP);
	if (fellow_skip_zero && (light_length * spot_lights.data[idx].inv_radius >= 1.0 || dot(-vec3(light_rel_vec_norm), spot_lights.data[idx].direction) <= spot_lights.data[idx].cone_angle)) {
		return;
	}
#endif
	half spot_attenuation = get_omni_attenuation(light_length, spot_lights.data[idx].inv_radius, spot_lights.data[idx].attenuation);
	vec3 spot_dir = spot_lights.data[idx].direction;
	float cone_angle = spot_lights.data[idx].cone_angle;
	float scos = max(dot(-vec3(light_rel_vec_norm), spot_dir), cone_angle);

	// This conversion to a highp float is crucial to prevent light leaking due to precision errors.
	float spot_rim = max(1e-4, (1.0 - scos) / (1.0 - cone_angle));
	spot_attenuation *= half(1.0 - pow(spot_rim, spot_lights.data[idx].cone_attenuation));
#ifdef FELLOW_BACKFACING_LIGHT_SKIP_OK
	if (fellow_skip_zero && spot_attenuation <= HALF_FLT_MIN) {
		return;
	}
#endif

	// Compute size.
	half size = half(0.0);
	if (sc_use_light_soft_shadows() && spot_lights.data[idx].size > 0.0) {
		half t = half(spot_lights.data[idx].size / max(0.001, light_length));
		size = half(1.0) / sqrt(half(1.0) + t * t);
		size = max(half(1.0) - size, half(0.0));
	}

#ifdef FELLOW_BACKFACING_LIGHT_SKIP_OK
	// FellowEngine (rendering/fellow/shadows/skip_backfacing_positional): N.L <= 0 -> this light adds exactly nothing.
	if (sc_fellow_skip_backfacing_positional() && bool(scene_data_block.data.flags & SCENE_DATA_FLAGS_FELLOW_SKIP_BACKFACING_POSITIONAL) && min(size + dot(normal, light_rel_vec_norm), half(1.0)) <= half(0.0)) {
		return;
	}
#endif

	half shadow = half(1.0);
#ifndef SHADOWS_DISABLED
	// Spot light shadow.
	if (spot_attenuation > HALF_FLT_MIN && spot_lights.data[idx].shadow_opacity > 0.001) {
		vec3 normal_bias = vec3(normal) * light_length * spot_lights.data[idx].shadow_normal_bias * (1.0 - abs(dot(normal, light_rel_vec_norm)));

		//there is a shadowmap
		vec4 v = vec4(vertex + normal_bias, 1.0);

		vec4 splane = (spot_lights.data[idx].shadow_matrix * v);
		splane.z += spot_lights.data[idx].shadow_bias;
		splane /= splane.w;

		if (sc_use_light_soft_shadows() && spot_lights.data[idx].soft_shadow_size > 0.0) {
			//soft shadow

			//find blocker
			float z_norm = dot(spot_dir, -light_rel_vec) * spot_lights.data[idx].inv_radius;

			vec2 shadow_uv = splane.xy * spot_lights.data[idx].atlas_rect.zw + spot_lights.data[idx].atlas_rect.xy;

			float blocker_count = 0.0;
			float blocker_average = 0.0;

			mat2 disk_rotation;
			{
				float r = quick_hash(gl_FragCoord.xy + vec2(taa_frame_count * 5.588238)) * 2.0 * M_PI;
				float sr = sin(r);
				float cr = cos(r);
				disk_rotation = mat2(vec2(cr, -sr), vec2(sr, cr));
			}

			float uv_size = spot_lights.data[idx].soft_shadow_size * z_norm * spot_lights.data[idx].soft_shadow_scale;
			vec2 clamp_max = spot_lights.data[idx].atlas_rect.xy + spot_lights.data[idx].atlas_rect.zw;

			if (FELLOW_BATCH_TAPS_POS(sc_penumbra_shadow_samples())) {
				SPEC_CONSTANT_LOOP_ANNOTATION
				for (uint i = 0; i < sc_penumbra_shadow_samples(); i += FELLOW_TAP_BATCH) {
					float d[FELLOW_TAP_BATCH];
					[[unroll]] for (uint j = 0u; j < FELLOW_TAP_BATCH; j++) {
						vec2 suv = shadow_uv + (disk_rotation * scene_data_block.data.penumbra_shadow_kernel[i + j].xy) * uv_size;
						suv = clamp(suv, spot_lights.data[idx].atlas_rect.xy, clamp_max);
						d[j] = textureLod(sampler2D(shadow_atlas, SAMPLER_LINEAR_CLAMP), suv, 0.0).r;
					}
					[[unroll]] for (uint j = 0u; j < FELLOW_TAP_BATCH; j++) {
						if (d[j] > splane.z) {
							blocker_average += d[j];
							blocker_count += 1.0;
						}
					}
				}
			} else {
				SPEC_CONSTANT_LOOP_ANNOTATION
				for (uint i = 0; i < sc_penumbra_shadow_samples(); i++) {
					vec2 suv = shadow_uv + (disk_rotation * scene_data_block.data.penumbra_shadow_kernel[i].xy) * uv_size;
					suv = clamp(suv, spot_lights.data[idx].atlas_rect.xy, clamp_max);
					float d = textureLod(sampler2D(shadow_atlas, SAMPLER_LINEAR_CLAMP), suv, 0.0).r;
					if (d > splane.z) {
						blocker_average += d;
						blocker_count += 1.0;
					}
				}
			}

			if (blocker_count > 0.0) {
				//blockers found, do soft shadow
				blocker_average /= blocker_count;
				float penumbra = (-z_norm + blocker_average) / (1.0 - blocker_average);
				uv_size *= penumbra;

				shadow = half(0.0);

				if (FELLOW_BATCH_TAPS_POS(sc_penumbra_shadow_samples())) {
					SPEC_CONSTANT_LOOP_ANNOTATION
					for (uint i = 0; i < sc_penumbra_shadow_samples(); i += FELLOW_TAP_BATCH) {
						float t[FELLOW_TAP_BATCH];
						[[unroll]] for (uint j = 0u; j < FELLOW_TAP_BATCH; j++) {
							vec2 suv = shadow_uv + (disk_rotation * scene_data_block.data.penumbra_shadow_kernel[i + j].xy) * uv_size;
							suv = clamp(suv, spot_lights.data[idx].atlas_rect.xy, clamp_max);
							t[j] = textureProj(sampler2DShadow(shadow_atlas, shadow_sampler), vec4(suv, splane.z, 1.0));
						}
						[[unroll]] for (uint j = 0u; j < FELLOW_TAP_BATCH; j++) {
							shadow += half(t[j]);
						}
					}
				} else {
					SPEC_CONSTANT_LOOP_ANNOTATION
					for (uint i = 0; i < sc_penumbra_shadow_samples(); i++) {
						vec2 suv = shadow_uv + (disk_rotation * scene_data_block.data.penumbra_shadow_kernel[i].xy) * uv_size;
						suv = clamp(suv, spot_lights.data[idx].atlas_rect.xy, clamp_max);
						shadow += half(textureProj(sampler2DShadow(shadow_atlas, shadow_sampler), vec4(suv, splane.z, 1.0)));
					}
				}

				shadow /= half(sc_penumbra_shadow_samples());
				shadow = mix(half(1.0), shadow, half(spot_lights.data[idx].shadow_opacity));

			} else {
				//no blockers found, so no shadow
				shadow = half(1.0);
			}
		} else {
			//hard shadow
			vec3 shadow_uv = vec3(splane.xy * spot_lights.data[idx].atlas_rect.zw + spot_lights.data[idx].atlas_rect.xy, splane.z);
			shadow = mix(half(1.0), sample_pcf_shadow(shadow_atlas, spot_lights.data[idx].soft_shadow_scale * scene_data_block.data.shadow_atlas_pixel_size, shadow_uv, taa_frame_count), half(spot_lights.data[idx].shadow_opacity));
		}
	}
#endif // SHADOWS_DISABLED

	vec3 color = spot_lights.data[idx].color;

#ifdef LIGHT_TRANSMITTANCE_USED
	half transmittance_z = transmittance_depth;
	transmittance_color.a *= spot_attenuation;
#ifndef SHADOWS_DISABLED
	if (spot_lights.data[idx].shadow_opacity > 0.001) {
		vec4 splane = (spot_lights.data[idx].shadow_matrix * vec4(vertex - vec3(normal) * spot_lights.data[idx].transmittance_bias, 1.0));
		splane /= splane.w;

		vec3 shadow_uv = vec3(splane.xy * spot_lights.data[idx].atlas_rect.zw + spot_lights.data[idx].atlas_rect.xy, splane.z);
		float shadow_z = textureLod(sampler2D(shadow_atlas, SAMPLER_LINEAR_CLAMP), shadow_uv.xy, 0.0).r;

		shadow_z = shadow_z * 2.0 - 1.0;
		float z_far = 1.0 / spot_lights.data[idx].inv_radius;
		float z_near = 0.01;
		shadow_z = 2.0 * z_near * z_far / (z_far + z_near - shadow_z * (z_far - z_near));

		//distance to light plane
		float z = dot(spot_dir, -light_rel_vec);
		transmittance_z = half(z - shadow_z);
	}
#endif // !SHADOWS_DISABLED
#endif // LIGHT_TRANSMITTANCE_USED

	if (sc_use_light_projector() && spot_lights.data[idx].projector_rect != vec4(0.0)) {
		vec4 splane = (spot_lights.data[idx].shadow_matrix * vec4(vertex, 1.0));
		splane /= splane.w;

		vec2 proj_uv = splane.xy * spot_lights.data[idx].projector_rect.zw;

		if (sc_projector_use_mipmaps()) {
			//ensure we have proper mipmaps
			vec4 splane_ddx = (spot_lights.data[idx].shadow_matrix * vec4(vertex + vertex_ddx, 1.0));
			splane_ddx /= splane_ddx.w;
			vec2 proj_uv_ddx = splane_ddx.xy * spot_lights.data[idx].projector_rect.zw - proj_uv;

			vec4 splane_ddy = (spot_lights.data[idx].shadow_matrix * vec4(vertex + vertex_ddy, 1.0));
			splane_ddy /= splane_ddy.w;
			vec2 proj_uv_ddy = splane_ddy.xy * spot_lights.data[idx].projector_rect.zw - proj_uv;

			vec4 proj = textureGrad(sampler2D(decal_atlas_srgb, light_projector_sampler), proj_uv + spot_lights.data[idx].projector_rect.xy, proj_uv_ddx, proj_uv_ddy);
			color *= proj.rgb * proj.a;
		} else {
			vec4 proj = textureLod(sampler2D(decal_atlas_srgb, light_projector_sampler), proj_uv + spot_lights.data[idx].projector_rect.xy, 0.0);
			color *= proj.rgb * proj.a;
		}
	}

	light_compute(normal, hvec3(light_rel_vec_norm), eye_vec, size, hvec3(color), false, spot_attenuation * shadow, f0, roughness, metallic, half(spot_lights.data[idx].specular_amount), albedo, alpha, screen_uv, energy_compensation,
#ifdef LIGHT_BACKLIGHT_USED
			backlight,
#endif
#ifdef LIGHT_TRANSMITTANCE_USED
			transmittance_color,
			transmittance_depth,
			transmittance_boost,
			transmittance_z,
#endif
#ifdef LIGHT_RIM_USED
			rim * spot_attenuation, rim_tint,
#endif
#ifdef LIGHT_CLEARCOAT_USED
			clearcoat, clearcoat_roughness, vertex_normal,
#endif
#ifdef LIGHT_ANISOTROPY_USED
			binormal, tangent, anisotropy,
#endif

			diffuse_light, specular_light);
}

// implementation of area lights with Linearly Transformed Cosines (LTC): https://eheitzresearch.wordpress.com/415-2/
void light_process_area(uint idx, vec3 vertex, hvec3 eye_vec, hvec3 normal, vec3 vertex_ddx, vec3 vertex_ddy, hvec3 f0, half roughness, half metallic, float taa_frame_count, hvec3 albedo, inout half alpha, vec2 screen_uv, hvec3 energy_compensation,
#ifdef LIGHT_BACKLIGHT_USED
		hvec3 backlight,
#endif
#ifdef LIGHT_TRANSMITTANCE_USED
		hvec4 transmittance_color,
		half transmittance_depth,
		half transmittance_boost,
#endif
#ifdef LIGHT_RIM_USED
		half rim, half rim_tint,
#endif
#ifdef LIGHT_CLEARCOAT_USED
		half clearcoat, half clearcoat_roughness, hvec3 vertex_normal,
#endif
#ifdef LIGHT_ANISOTROPY_USED
		hvec3 binormal, hvec3 tangent, half anisotropy,
#endif
		inout hvec3 diffuse_light, inout hvec3 specular_light) {
	half EPSILON = half(1e-7);
	hvec3 area_width = hvec3(area_lights.data[idx].area_width);
	hvec3 area_height = hvec3(area_lights.data[idx].area_height);

	if (dot(area_width, area_width) < EPSILON || dot(area_height, area_height) < EPSILON) { // area is 0
		return;
	}

	if (dot(area_lights.data[idx].direction, vertex - area_lights.data[idx].position) <= 0) {
		return; // vertex is behind light
	}

	half a_len = length(area_width);
	half b_len = length(area_height);
	half a_half_len = a_len / half(2.0);
	half b_half_len = b_len / half(2.0);
	hvec3 light_center = hvec3(area_lights.data[idx].position);
	hvec3 light_to_vert = hvec3(vertex) - light_center;
	hvec3 area_a_dir = normalize(area_width);
	hvec3 area_b_dir = normalize(area_height);
	hvec3 area_direction = hvec3(area_lights.data[idx].direction);
	hvec3 pos_local_to_light = hvec3(dot(light_to_vert, area_a_dir), dot(light_to_vert, area_b_dir), dot(light_to_vert, -area_direction));

	hvec3 closest_point_local_to_light = hvec3(clamp(pos_local_to_light.x, -a_half_len, a_half_len), clamp(pos_local_to_light.y, -b_half_len, b_half_len), 0);
	half dist = length(closest_point_local_to_light - pos_local_to_light);

	half light_length = max(half(0.0), dist);
	half light_attenuation_raw = get_omni_attenuation(float(light_length), area_lights.data[idx].inv_radius, area_lights.data[idx].attenuation);
	half light_attenuation_ltc = light_attenuation_raw * half(light_length * light_length); // solid angle already decreases by inverse square, so attenuation power is 2.0 by default -> subtract 2.0
	half shadow = half(1.0);

#ifndef SHADOWS_DISABLED
	// Area light shadow.
	if (light_attenuation_raw > HALF_FLT_MIN && area_lights.data[idx].shadow_opacity > 0.001) {
		// there is a shadowmap
		vec2 texel_size = scene_data_block.data.shadow_atlas_pixel_size;
		vec4 base_uv_rect = area_lights.data[idx].atlas_rect;
		base_uv_rect.xy += texel_size;
		base_uv_rect.zw -= texel_size * 2.0;

		vec3 local_vert = (area_lights.data[idx].shadow_matrix * vec4(vertex, 1.0)).xyz;

		float shadow_len = length(local_vert); //need to remember shadow len from here
		vec3 shadow_dir = normalize(local_vert);

		vec3 local_normal = normalize(mat3(area_lights.data[idx].shadow_matrix) * vec3(normal));
		vec3 normal_bias = local_normal * area_lights.data[idx].shadow_normal_bias * (1.0 - abs(dot(local_normal, shadow_dir)));

		float inv_center_range = area_lights.data[idx].cone_attenuation;

		if (sc_use_light_soft_shadows() && area_lights.data[idx].soft_shadow_size > 0.0) {
			//soft shadow

			//find blocker

			float blocker_count = 0.0;
			float blocker_average = 0.0;

			mat2 disk_rotation;
			{
				float r = quick_hash(gl_FragCoord.xy + vec2(taa_frame_count * 5.588238)) * 2.0 * M_PI;
				float sr = sin(r);
				float cr = cos(r);
				disk_rotation = mat2(vec2(cr, -sr), vec2(sr, cr));
			}

			vec3 basis_normal = shadow_dir;
			vec3 v0 = abs(basis_normal.z) < 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(0.0, 1.0, 0.0);
			vec3 tangent = normalize(cross(v0, basis_normal));
			vec3 bitangent = normalize(cross(tangent, basis_normal));
			float z_norm = 1.0 - shadow_len * inv_center_range;

			tangent *= area_lights.data[idx].soft_shadow_size * area_lights.data[idx].soft_shadow_scale;
			bitangent *= area_lights.data[idx].soft_shadow_size * area_lights.data[idx].soft_shadow_scale;

			SPEC_CONSTANT_LOOP_ANNOTATION
			for (uint i = 0; i < sc_penumbra_shadow_samples(); i++) {
				vec2 disk = disk_rotation * scene_data_block.data.penumbra_shadow_kernel[i].xy;

				vec3 pos = local_vert + tangent * disk.x + bitangent * disk.y;

				pos = normalize(pos);

				vec4 uv_rect = base_uv_rect;

				pos.z = 1.0 + abs(pos.z);
				pos.xy /= pos.z;

				pos.xy = pos.xy * 0.5 + 0.5;
				pos.xy = uv_rect.xy + pos.xy * uv_rect.zw;

				float d = textureLod(sampler2D(shadow_atlas, SAMPLER_LINEAR_CLAMP), pos.xy, 0.0).r;
				if (d > z_norm) {
					blocker_average += d;
					blocker_count += 1.0;
				}
			}

			if (blocker_count > 0.0) {
				//blockers found, do soft shadow
				blocker_average /= blocker_count;
				float penumbra = (-z_norm + blocker_average) / (1.0 - blocker_average);
				tangent *= penumbra;
				bitangent *= penumbra;

				z_norm += inv_center_range * area_lights.data[idx].shadow_bias;

				shadow = half(0.0);

				SPEC_CONSTANT_LOOP_ANNOTATION
				for (uint i = 0; i < sc_penumbra_shadow_samples(); i++) {
					vec2 disk = disk_rotation * scene_data_block.data.penumbra_shadow_kernel[i].xy;
					vec3 pos = local_vert + tangent * disk.x + bitangent * disk.y;

					pos = normalize(pos);
					pos = normalize(pos + normal_bias);

					vec4 uv_rect = base_uv_rect;

					pos.z = 1.0 + abs(pos.z);
					pos.xy /= pos.z;

					pos.xy = pos.xy * 0.5 + 0.5;
					pos.xy = uv_rect.xy + pos.xy * uv_rect.zw;
					shadow += half(textureProj(sampler2DShadow(shadow_atlas, shadow_sampler), vec4(pos.xy, z_norm, 1.0)));
				}

				shadow /= half(sc_penumbra_shadow_samples());
				shadow = mix(half(1.0), shadow, half(area_lights.data[idx].shadow_opacity));

			} else {
				//no blockers found, so no shadow
				shadow = half(1.0);
			}
		} else {
			vec4 uv_rect = base_uv_rect;

			vec3 shadow_sample = normalize(shadow_dir + normal_bias);

			shadow_sample.z = 1.0 + abs(shadow_sample.z);
			vec2 pos = shadow_sample.xy / shadow_sample.z;
			float depth = shadow_len - area_lights.data[idx].shadow_bias;
			depth *= inv_center_range;
			depth = 1.0 - depth;
			shadow = mix(half(1.0), sample_omni_pcf_shadow(shadow_atlas, area_lights.data[idx].soft_shadow_scale / shadow_sample.z, pos, uv_rect, vec2(0), depth, taa_frame_count), half(area_lights.data[idx].shadow_opacity));
		}
	}
#endif
	light_attenuation_ltc = light_attenuation_ltc * shadow;
	half light_attenuation = light_attenuation_raw * shadow;
	hvec3 color = hvec3(area_lights.data[idx].color);
	float max_mipmap = area_lights.data[idx].cone_angle;

	vec3 points[4];
	hvec3 h_area_width = area_width / half(2.0);
	hvec3 h_area_height = area_height / half(2.0);
	points[0] = area_lights.data[idx].position - h_area_width - h_area_height - vertex;
	points[1] = area_lights.data[idx].position + h_area_width - h_area_height - vertex;
	points[2] = area_lights.data[idx].position + h_area_width + h_area_height - vertex;
	points[3] = area_lights.data[idx].position - h_area_width + h_area_height - vertex;

	float ltc_diffuse = 0.0;
	vec3 ltc_diffuse_tex_color = vec3(1.0);
	float ltc_specular = 0.0;
	vec3 ltc_specular_tex_color = vec3(1.0);
	vec2 ltc_fresnel = vec2(0.0);
	hvec3 fresnel_color = hvec3(0.0);
	ltc_evaluate(vec3(normal), vec3(eye_vec), mat3(1), points, area_lights.data[idx].projector_rect, max_mipmap, area_light_atlas, SAMPLER_LINEAR_WITH_MIPMAPS_CLAMP, ltc_diffuse, ltc_diffuse_tex_color);

#if !defined(SPECULAR_DISABLED) || (defined(LIGHT_CODE_USED) && defined(AREA_LIGHT_CODE_USED))
	ltc_evaluate_specular(vec3(normal), vec3(eye_vec), roughness, points, area_lights.data[idx].projector_rect, max_mipmap, area_light_atlas, SAMPLER_LINEAR_WITH_MIPMAPS_CLAMP, ltc_lut1, ltc_lut2, ltc_specular, ltc_fresnel, ltc_specular_tex_color);
	half f90 = clamp(dot(f0, hvec3(50.0 * 0.33)), metallic, half(1.0));
	fresnel_color = f0 * max(half(ltc_fresnel.x), half(0.0)) + (f90 - f0) * max(half(ltc_fresnel.y), half(0.0));
#endif

#if defined(LIGHT_CODE_USED) && defined(AREA_LIGHT_CODE_USED)
	// Light is written by the user shader.
	mat4 inv_view_matrix = transpose(mat4(scene_data_block.data.inv_view_matrix[0],
			scene_data_block.data.inv_view_matrix[1],
			scene_data_block.data.inv_view_matrix[2],
			vec4(0.0, 0.0, 0.0, 1.0)));
	mat4 read_view_matrix = transpose(mat4(scene_data_block.data.view_matrix[0],
			scene_data_block.data.view_matrix[1],
			scene_data_block.data.view_matrix[2],
			vec4(0.0, 0.0, 0.0, 1.0)));

#ifdef USING_MOBILE_RENDERER
	uint instance_index = draw_call.instance_index;
#else
	uint instance_index = instance_index_interp;
#endif

	mat4 read_model_matrix = transpose(mat4(instances.data[instance_index].transform[0],
			instances.data[instance_index].transform[1],
			instances.data[instance_index].transform[2],
			vec4(0.0, 0.0, 0.0, 1.0)));

#undef projection_matrix
#define projection_matrix scene_data_block.data.projection_matrix
#undef inv_projection_matrix
#define inv_projection_matrix scene_data_block.data.inv_projection_matrix

	vec2 read_viewport_size = scene_data_block.data.viewport_size;

#ifdef LIGHT_BACKLIGHT_USED
	vec3 backlight_highp = vec3(backlight);
#endif
	float roughness_highp = float(roughness);
	float metallic_highp = float(metallic);
	vec3 albedo_highp = vec3(albedo);
	float alpha_highp = float(alpha);
	vec3 normal_highp = vec3(normal);
	vec3 light_highp = (light_center - vertex) / light_length;
	vec3 view_highp = vec3(eye_vec);
	float specular_amount_highp = float(area_lights.data[idx].specular_amount);
	vec3 light_color_highp = vec3(color);
	float attenuation_highp = float(light_attenuation_ltc);
	vec3 diffuse_light_highp = vec3(diffuse_light);
	vec3 specular_light_highp = vec3(specular_light);
	bool is_directional = false;
	bool is_area = true;
	vec3 area_diffuse = ltc_diffuse * ltc_diffuse_tex_color;
	vec3 area_specular = ltc_specular * vec3(fresnel_color) * ltc_specular_tex_color;

#CODE : LIGHT

	alpha = half(alpha_highp);
	diffuse_light = hvec3(diffuse_light_highp);
	specular_light = hvec3(specular_light_highp);

#else
	half specular_amount = half(area_lights.data[idx].specular_amount);
	half area = a_len * b_len;
	half cc_attenuation = half(1.0);

#if defined(LIGHT_TRANSMITTANCE_USED) || defined(LIGHT_BACKLIGHT_USED) || defined(LIGHT_RIM_USED) || defined(DIFFUSE_TOON)
	hvec3 isotropic_light_color = hvec3(1.0); // independent of normal
	if (area_lights.data[idx].projector_rect != vec4(0.0)) {
		half lod = dist / sqrt(area);
		lod = log(half(2048.0) * lod) / log(half(3.0));

		hvec2 uv = (closest_point_local_to_light.xy + hvec2(a_half_len, b_half_len)) / hvec2(a_len, b_len);
		isotropic_light_color = hvec3(fetch_ltc_lod(vec2(hvec2(1.0) - uv), area_lights.data[idx].projector_rect, float(lod), max_mipmap, area_light_atlas, SAMPLER_LINEAR_WITH_MIPMAPS_CLAMP));
	}
#endif
#ifdef LIGHT_TRANSMITTANCE_USED
	{
		half transmittance_z = transmittance_depth;
		transmittance_color.a *= light_attenuation_raw; // not including shadows or ltc falloff
#ifndef SHADOWS_DISABLED
		if (area_lights.data[idx].shadow_opacity > 0.001) {
			// Redo shadowmapping, but shrink the model a bit to avoid artifacts.
			vec2 texel_size = scene_data_block.data.shadow_atlas_pixel_size;
			vec4 uv_rect = area_lights.data[idx].atlas_rect;
			uv_rect.xy += texel_size;
			uv_rect.zw -= texel_size * 2.0;
			vec3 local_vert = (area_lights.data[idx].shadow_matrix * vec4(vertex - vec3(normal) * area_lights.data[idx].transmittance_bias, 1.0)).xyz;

			float shadow_len = length(local_vert); //need to remember shadow len from here
			vec3 shadow_sample = normalize(local_vert);

			shadow_sample.z = 1.0 + abs(shadow_sample.z);
			vec2 pos = shadow_sample.xy / shadow_sample.z;
			float inv_center_range = area_lights.data[idx].cone_attenuation;
			float depth = shadow_len * inv_center_range;
			depth = 1.0 - depth;

			pos = pos * 0.5 + 0.5;
			pos = uv_rect.xy + pos * uv_rect.zw;
			float shadow_z = textureLod(sampler2D(shadow_atlas, SAMPLER_LINEAR_CLAMP), pos, 0.0).r;
			transmittance_z = half((depth - shadow_z) / inv_center_range);
		}
#endif
		// transmission can't use ltc_diffuse_tex_color, because for backface pixels texture would have to be sampled for opposite normal direction.
#ifdef SSS_MODE_SKIN
		diffuse_light += SSS_skin(half(ltc_diffuse), transmittance_depth, transmittance_z, transmittance_boost, transmittance_color, color * isotropic_light_color * area);
#else
		diffuse_light += SSS(half(ltc_diffuse), transmittance_depth, transmittance_z, transmittance_boost, transmittance_color, color * isotropic_light_color * area);
#endif
	}
#endif //LIGHT_TRANSMITTANCE_USED

#if defined(LIGHT_BACKLIGHT_USED) || defined(LIGHT_RIM_USED) || defined(DIFFUSE_TOON) || defined(SPECULAR_TOON)
	float solid_angle = quad_solid_angle(points);
#endif

#if defined(LIGHT_CLEARCOAT_USED)
	vec3 cc_specular_tex_color = vec3(1.0);
	float cc_specular_ltc = 0.0;
	vec2 cc_fresnel;
	ltc_evaluate_specular(vec3(vertex_normal), vec3(eye_vec), sqrt(mix(0.001, 0.1, float(clearcoat_roughness))), points, area_lights.data[idx].projector_rect, max_mipmap, area_light_atlas, SAMPLER_LINEAR_WITH_MIPMAPS_CLAMP, ltc_lut1, ltc_lut2, cc_specular_ltc, cc_fresnel, cc_specular_tex_color);
	half Fr = (half(0.04) * max(half(cc_fresnel.x), half(0.0)) + half(1.0 - 0.04) * max(half(cc_fresnel.y), half(0.0))) * clearcoat;
	cc_attenuation = half(1.0) - Fr;
	specular_light += half(cc_specular_ltc) * hvec3(cc_specular_tex_color) * Fr * color * light_attenuation_ltc * specular_amount;
#endif // LIGHT_CLEARCOAT_USED

	if (metallic < half(1.0)) {
#if defined(DIFFUSE_TOON)
		float backface_ltc_diffuse = 0.0;
		vec3 backface_ltc_tex_color_discard = vec3(1.0);
		ltc_evaluate(vec3(-normal), vec3(eye_vec), mat3(1), points, vec4(0.0), max_mipmap, area_light_atlas, SAMPLER_LINEAR_WITH_MIPMAPS_CLAMP, backface_ltc_diffuse, backface_ltc_tex_color_discard);
		half NdotL = half((ltc_diffuse - backface_ltc_diffuse) / (max(solid_angle, 0.001) / M_PI));
		half diffuse_brdf_NL = smoothstep(-roughness, max(roughness, half(0.01)), NdotL) * half(1.0 / M_PI);
		diffuse_light += diffuse_brdf_NL * isotropic_light_color * color * area * light_attenuation * cc_attenuation;
#else
		diffuse_light += half(ltc_diffuse) * hvec3(ltc_diffuse_tex_color) * color * light_attenuation_ltc * cc_attenuation;
#endif // DIFFUSE_TOON

#if defined(LIGHT_BACKLIGHT_USED)
		// backlight can't use ltc_diffuse_tex_color, because for backface pixels texture would have to sampled for opposite normal direction.
		diffuse_light += color * max(half(solid_angle / M_PI) - half(ltc_diffuse), half(0.0)) * backlight * isotropic_light_color * light_attenuation_ltc;
#endif
	}

#if defined(LIGHT_RIM_USED) // same as for point lights
	half cNdotV = max(dot(normal, eye_vec), half(1e-4));
	half rim_light = pow(max(half(1e-4), half(1.0) - cNdotV), max(half(0.0), (half(1.0) - roughness) * half(16.0)));
	diffuse_light += rim_light * rim * mix(hvec3(1.0), albedo, rim_tint) * isotropic_light_color * color * half(solid_angle) * light_attenuation_ltc;
#endif

#if defined(SPECULAR_TOON)
	// If ltc_specular turns out to be not similar enough to RdotV, since its based on GGX, toon shading would need its own lookup-table.
	half mid = half(1.0) - roughness;
	mid *= mid;

	half RdotV = half(ltc_specular / (max(solid_angle, 0.001) / (M_PI)));
	half intensity = smoothstep(mid - roughness * half(0.5), mid + roughness * half(0.5), RdotV) * mid; // should we use specular tex color here?? or diffuse? or white?
	diffuse_light += intensity * hvec3(ltc_specular_tex_color) * color * area * light_attenuation * specular_amount; // write to diffuse_light, as in toon shading you generally want no reflection
#elif defined(SPECULAR_DISABLED)
	// do nothing
#else
	hvec3 spec = half(ltc_specular) * hvec3(ltc_specular_tex_color) * color * fresnel_color;
	specular_light += spec * specular_amount * light_attenuation_ltc * cc_attenuation;
#endif // SPECULAR_TOON

#endif // LIGHT_CODE_USED
}

void reflection_process(uint ref_index, vec3 vertex, hvec3 ref_vec, hvec3 normal, half roughness, hvec3 ambient_light,
#ifdef LIGHT_CLEARCOAT_USED
		hvec3 cc_ref_vec, half cc_roughness, inout hvec4 cc_reflection_accum,
#endif
		inout hvec4 ambient_accum, inout hvec4 reflection_accum) {
	vec3 box_extents = reflections.data[ref_index].box_extents;
	vec3 local_pos = (reflections.data[ref_index].local_matrix * vec4(vertex, 1.0)).xyz;

	if (any(greaterThan(abs(local_pos), box_extents))) { //out of the reflection box
		return;
	}

	half blend = half(1.0);
	if (reflections.data[ref_index].blend_distance != 0.0) {
		vec3 axis_blend_distance = min(vec3(reflections.data[ref_index].blend_distance), box_extents);
		vec3 blend_axes_highp = abs(local_pos) - box_extents + axis_blend_distance;
		hvec3 blend_axes = hvec3(blend_axes_highp / axis_blend_distance);
		blend_axes = clamp(half(1.0) - blend_axes, hvec3(0.0), hvec3(1.0));
		blend = pow(blend_axes.x * blend_axes.y * blend_axes.z, half(2.0));
	}

	vec2 border_size = scene_data_block.data.reflection_atlas_border_size;
	if (reflections.data[ref_index].intensity > 0.0 && reflection_accum.a < half(1.0)) { // compute reflection

		vec3 local_ref_vec = (reflections.data[ref_index].local_matrix * vec4(ref_vec, 0.0)).xyz;

		if (reflections.data[ref_index].box_project) { //box project

			vec3 nrdir = normalize(local_ref_vec);
			vec3 rbmax = (box_extents - local_pos) / nrdir;
			vec3 rbmin = (-box_extents - local_pos) / nrdir;

			vec3 rbminmax = mix(rbmin, rbmax, greaterThan(nrdir, vec3(0.0, 0.0, 0.0)));

			float fa = min(min(rbminmax.x, rbminmax.y), rbminmax.z);
			vec3 posonbox = local_pos + nrdir * fa;
			local_ref_vec = posonbox - reflections.data[ref_index].box_offset;
		}

		hvec4 reflection;
		half reflection_blend = max(half(0.0), blend - reflection_accum.a);

		float roughness_lod = sqrt(roughness) * MAX_ROUGHNESS_LOD;
		vec2 reflection_uv = vec3_to_oct_with_border(local_ref_vec, border_size);
		reflection.rgb = hvec3(textureLod(sampler2DArray(reflection_atlas, DEFAULT_SAMPLER_LINEAR_WITH_MIPMAPS_CLAMP), vec3(reflection_uv, reflections.data[ref_index].index), roughness_lod).rgb) * REFLECTION_MULTIPLIER;
		reflection.rgb *= half(reflections.data[ref_index].exposure_normalization);
		reflection.a = reflection_blend;

		reflection.rgb *= half(reflections.data[ref_index].intensity);
		reflection.rgb *= reflection.a;

		reflection_accum += reflection;
	}

#ifdef LIGHT_CLEARCOAT_USED
	if (reflections.data[ref_index].intensity > 0.0 && cc_reflection_accum.a < half(1.0)) { // compute clearcoat reflection
		vec3 local_cc_ref_vec = (reflections.data[ref_index].local_matrix * vec4(cc_ref_vec, 0.0)).xyz;

		if (reflections.data[ref_index].box_project) { // Box project.

			vec3 nrdir = normalize(local_cc_ref_vec);
			vec3 rbmax = (box_extents - local_pos) / nrdir;
			vec3 rbmin = (-box_extents - local_pos) / nrdir;

			vec3 rbminmax = mix(rbmin, rbmax, greaterThan(nrdir, vec3(0.0, 0.0, 0.0)));

			float fa = min(min(rbminmax.x, rbminmax.y), rbminmax.z);
			vec3 posonbox = local_pos + nrdir * fa;
			local_cc_ref_vec = posonbox - reflections.data[ref_index].box_offset;
		}

		hvec4 cc_reflection;
		half cc_reflection_blend = max(half(0.0), blend - cc_reflection_accum.a);

		float cc_roughness_lod = sqrt(cc_roughness) * MAX_ROUGHNESS_LOD;
		vec2 cc_reflection_uv = vec3_to_oct_with_border(local_cc_ref_vec, border_size);
		cc_reflection.rgb = hvec3(textureLod(sampler2DArray(reflection_atlas, DEFAULT_SAMPLER_LINEAR_WITH_MIPMAPS_CLAMP), vec3(cc_reflection_uv, reflections.data[ref_index].index), cc_roughness_lod).rgb) * REFLECTION_MULTIPLIER;
		cc_reflection.rgb *= half(reflections.data[ref_index].exposure_normalization);
		cc_reflection.a = cc_reflection_blend;

		cc_reflection.rgb *= half(reflections.data[ref_index].intensity);
		cc_reflection.rgb *= cc_reflection.a;

		cc_reflection_accum += cc_reflection;
	}
#endif // LIGHT_CLEARCOAT_USED

	if (ambient_accum.a >= half(1.0)) {
		return;
	}

	switch (reflections.data[ref_index].ambient_mode) {
		case REFLECTION_AMBIENT_DISABLED: {
			//do nothing
		} break;
		case REFLECTION_AMBIENT_ENVIRONMENT: {
			vec3 local_amb_vec = (reflections.data[ref_index].local_matrix * vec4(normal, 0.0)).xyz;
			hvec4 ambient_out;
			half ambient_blend = max(half(0.0), blend - ambient_accum.a);

			float roughness_lod = MAX_ROUGHNESS_LOD;
			vec2 ambient_uv = vec3_to_oct_with_border(local_amb_vec, border_size);
			ambient_out.rgb = hvec3(textureLod(sampler2DArray(reflection_atlas, DEFAULT_SAMPLER_LINEAR_WITH_MIPMAPS_CLAMP), vec3(ambient_uv, reflections.data[ref_index].index), roughness_lod).rgb);
			ambient_out.rgb *= half(reflections.data[ref_index].exposure_normalization);
			ambient_out.a = ambient_blend;
			ambient_out.rgb *= ambient_out.a;
			ambient_accum += ambient_out;
		} break;
		case REFLECTION_AMBIENT_COLOR: {
			hvec4 ambient_out;
			half ambient_blend = max(half(0.0), blend - ambient_accum.a);

			ambient_out.rgb = hvec3(reflections.data[ref_index].ambient);
			ambient_out.a = ambient_blend;
			ambient_out.rgb *= ambient_out.a;
			ambient_accum += ambient_out;
		} break;
	}
}

half blur_shadow(half shadow) {
	return shadow;
#if 0
	//disabling for now, will investigate later
	float interp_shadow = shadow;
	if (gl_HelperInvocation) {
		interp_shadow = -4.0; // technically anything below -4 will do but just to make sure
	}

	uvec2 fc2 = uvec2(gl_FragCoord.xy);
	interp_shadow -= dFdx(interp_shadow) * (float(fc2.x & 1) - 0.5);
	interp_shadow -= dFdy(interp_shadow) * (float(fc2.y & 1) - 0.5);

	if (interp_shadow >= 0.0) {
		shadow = interp_shadow;
	}
	return shadow;
#endif
}
