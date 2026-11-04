/**************************************************************************/
/*  fellow_water_shader.h                                                 */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#pragma once

// The FellowWater3D surface material (Godot shading language). See fellow_water_3d.h for what it does; the
// decisions behind it are in docs/decisions/0123-water.md.
static const char *fellow_water_shader_code = R"(
shader_type spatial;
render_mode blend_mix, cull_disabled, depth_draw_always, diffuse_lambert, specular_schlick_ggx, world_vertex_coords;

uniform sampler2DArray wave_disp : filter_linear_mipmap, repeat_enable;
uniform sampler2DArray wave_slope : filter_linear_mipmap_anisotropic, repeat_enable;
uniform sampler2DArray wave_caustics : filter_linear_mipmap, repeat_enable;
uniform vec4 cascade_lengths = vec4(160.0, 27.0, 5.0, 1.0);
uniform int cascade_count = 3;
uniform float wave_texels = 256.0;
uniform float wave_scale = 1.0;

uniform bool sim_enabled = false;
uniform sampler2D sim_surface : filter_linear, repeat_disable;
uniform sampler2D sim_flow : filter_linear, repeat_disable;
uniform mat4 sim_world_to_local;
uniform vec2 sim_domain = vec2(64.0);
uniform float sim_level_offset = 0.0;
uniform float sim_blend = 4.0;
uniform float sim_wave_scale = 0.35;
uniform float flow_period = 1.6;

uniform vec4 extent = vec4(-1e9, -1e9, 1e9, 1e9);

uniform sampler2D screen_tex : hint_screen_texture, filter_linear, repeat_disable;
uniform sampler2D depth_tex : hint_depth_texture, filter_nearest_mipmap, repeat_disable; // the nearest depth's mips (decision 0126)
uniform sampler2D foam_noise : filter_linear_mipmap, repeat_enable;

uniform vec3 absorption = vec3(0.55, 0.16, 0.30);
uniform vec3 scattering : source_color = vec3(0.010, 0.032, 0.022);
uniform vec3 foam_color : source_color = vec3(0.85, 0.87, 0.88);
uniform float refraction_strength = 0.25;
uniform float base_roughness = 0.04;
uniform float shore_foam_depth = 0.15;
uniform float caustics_strength = 0.7;
uniform vec2 caustics_depths = vec2(0.6, 2.5);
uniform int caustics_first = 1;
uniform vec3 sun_direction = vec3(0.0, -1.0, 0.0);
uniform bool ssr_enabled = true;
uniform int ssr_steps = 32;
uniform float ssr_max_distance = 80.0;
uniform float ssr_min_fresnel = 0.04;
uniform float water_time = 0.0;
uniform sampler2D rain_ripples : filter_linear_mipmap, repeat_enable;
uniform float rain_amount = 0.0;
uniform float rain_tile = 1.5;
// FellowWind3D's gusts (decision 0127): its gust field, mapped as the wind's materials map it (direction, offset,
// 1 / (cell size x cells)); the short waves roughen in the gusts and calm in the lulls (cat's paws).
uniform sampler2D wind_gusts : filter_linear, repeat_enable;
uniform vec4 wind_gust_map = vec4(1.0, 0.0, 0.0, 0.005);
uniform float wind_gust_strength = 0.0;
uniform int debug_view = 0;

varying vec3 v_base;
varying float v_sim;
varying float v_glint; // the sun's highlight: off on foam

vec2 sim_uv(vec3 wp) {
	vec3 l = (sim_world_to_local * vec4(wp, 1.0)).xyz;
	return l.xz / sim_domain + 0.5;
}

float sim_weight(vec2 uv) {
	vec2 e = min(uv, 1.0 - uv) * sim_domain;
	float d = min(e.x, e.y);
	return d <= 0.0 ? 0.0 : smoothstep(0.0, sim_blend, d);
}

void vertex() {
	vec3 origin = MODEL_MATRIX[3].xyz;
	vec2 xz = CAMERA_POSITION_WORLD.xz + (VERTEX.xz - origin.xz);
	xz = clamp(xz, extent.xy, extent.zw);
	float spacing = UV.x;
	float level = origin.y;
	float amp = wave_scale;
	float sim_w = 0.0;
	if (sim_enabled) {
		vec2 uv = sim_uv(vec3(xz.x, level, xz.y));
		sim_w = sim_weight(uv);
		if (sim_w > 0.0) {
			vec4 f = textureLod(sim_flow, uv, 0.0);
			float sim_level = f.w + sim_level_offset; // the level extended past the wet cells (fellow_swe.glsl)
			level = mix(level, sim_level, sim_w);
			amp *= mix(1.0, sim_wave_scale * smoothstep(0.02, 0.6, f.z), sim_w);
		}
	}
	vec3 d = vec3(0.0);
	for (int c = 0; c < cascade_count; c++) {
		float len = cascade_lengths[c];
		// Band-limit to the grid: about two texels of the level per vertex spacing.
		float lod = max(log2(spacing * wave_texels / len), 0.0);
		d += textureLod(wave_disp, vec3(xz / len, float(c)), lod).xyz;
	}
	d *= amp;
	v_base = vec3(xz.x, level, xz.y);
	v_sim = sim_w;
	VERTEX = vec3(xz.x + d.x, level + d.y, xz.y + d.z);
}

vec3 view_position(vec2 uv, float depth, mat4 inv_projection) {
	vec4 p = inv_projection * vec4(uv * 2.0 - 1.0, depth, 1.0);
	return p.xyz / p.w;
}

void fragment() {
	vec2 xz = v_base.xz;
	float amp = wave_scale;
	vec2 flow = vec2(0.0);
	vec4 sims = vec4(0.0);
	if (sim_enabled && v_sim > 0.0) {
		vec2 uv = sim_uv(v_base);
		vec4 f = texture(sim_flow, uv);
		sims = texture(sim_surface, uv);
		flow = f.xy * v_sim;
		amp *= mix(1.0, sim_wave_scale * smoothstep(0.02, 0.6, f.z), v_sim);
	}
	// Flow mapping: two phases half a period apart, each restarted when its weight is zero.
	float ph0 = fract(water_time / flow_period);
	float ph1 = fract(water_time / flow_period + 0.5);
	float w0 = 1.0 - abs(2.0 * ph0 - 1.0);
	bool flowing = dot(flow, flow) > 1e-6;
	vec2 o0 = flow * (ph0 * flow_period);
	vec2 o1 = flow * (ph1 * flow_period);

	// The wind's gusts scale the shortest cascade's slopes by the local wind over the mean (1 without them).
	float gust_k = 1.0;
	if (wind_gust_strength > 0.0) {
		vec2 gd = wind_gust_map.xy;
		vec2 guv = vec2((dot(xz, gd) - wind_gust_map.z) * 0.5, dot(xz, vec2(-gd.y, gd.x))) * wind_gust_map.w;
		gust_k = max(0.0, 1.0 + wind_gust_strength * clamp((texture(wind_gusts, guv).r - 0.5) * 3.0, -1.0, 1.0));
	}
	vec2 slope = vec2(0.0);
	float variance = 0.0;
	float whitecap = 0.0;
	for (int c = 0; c < cascade_count; c++) {
		float len = cascade_lengths[c];
		vec4 s;
		float fm;
		if (flowing) {
			vec3 u0 = vec3((xz - o0) / len, float(c));
			vec3 u1 = vec3((xz - o1) / len + vec2(0.37, 0.61), float(c));
			s = texture(wave_slope, u0) * w0 + texture(wave_slope, u1) * (1.0 - w0);
			fm = texture(wave_disp, u0).w * w0 + texture(wave_disp, u1).w * (1.0 - w0);
		} else {
			vec3 u = vec3(xz / len, float(c));
			s = texture(wave_slope, u);
			fm = texture(wave_disp, u).w;
		}
		float k = c == cascade_count - 1 ? gust_k : 1.0;
		slope += s.xy * k;
		variance += (max(s.z - s.x * s.x, 0.0) + max(s.w - s.y * s.y, 0.0)) * (k * k);
		whitecap = max(whitecap, fm);
	}
	slope *= amp;
	variance *= amp * amp;
	if (rain_amount > 0.0) {
		// FellowRain3D's ripples (decision 0124): two offset lookups of its tiling texture.
		vec2 ruv = xz / rain_tile;
		slope += (texture(rain_ripples, ruv).rg + texture(rain_ripples, ruv * 0.77 + vec2(0.37, 0.61)).rg) * rain_amount;
	}
	slope += sims.gb * v_sim;
	vec3 n_world = normalize(vec3(-slope.x, 1.0, -slope.y));
	vec3 n_view = normalize((VIEW_MATRIX * vec4(n_world, 0.0)).xyz);
	NORMAL = n_view;
	vec3 wpos = (INV_VIEW_MATRIX * vec4(VERTEX, 1.0)).xyz;

	// The scene below: undistorted first (thickness), then refracted.
	float d0 = textureLod(depth_tex, SCREEN_UV, 0.0).r;
	vec3 b0 = view_position(SCREEN_UV, d0, INV_PROJECTION_MATRIX);
	float thick0 = max(VERTEX.z - b0.z, 0.0);
	vec3 bw0 = (INV_VIEW_MATRIX * vec4(b0, 1.0)).xyz;
	float down0 = max(wpos.y - bw0.y, 0.0);
	vec2 proj_scale = vec2(PROJECTION_MATRIX[0][0], PROJECTION_MATRIX[1][1]) * 0.5;
	vec2 ruv = SCREEN_UV + n_view.xy * proj_scale * (refraction_strength * min(thick0, 4.0) / max(-VERTEX.z, 0.5));
	ruv = clamp(ruv, vec2(0.001), vec2(0.999));
	float d1 = textureLod(depth_tex, ruv, 0.0).r;
	vec3 b1 = view_position(ruv, d1, INV_PROJECTION_MATRIX);
	if (b1.z > VERTEX.z) {
		ruv = SCREEN_UV;
		b1 = b0;
	}
	vec3 bw = (INV_VIEW_MATRIX * vec4(b1, 1.0)).xyz;
	float path = length(b1 - VERTEX);
	float down = max(wpos.y - bw.y, 0.0);
	vec3 tv = exp(-absorption * path);
	vec3 td = exp(-absorption * down);
	vec3 bottom = textureLod(screen_tex, ruv, 0.0).rgb;

	// Caustics: the bottom point traced back to the surface along the refracted sun, the cascades' focusing there.
	if (caustics_strength > 0.0 && sun_direction.y < -0.05 && down > 0.01) {
		vec3 t0 = refract(sun_direction, vec3(0.0, 1.0, 0.0), 1.0 / 1.333);
		vec2 sp = bw.xz - t0.xz * (down / max(-t0.y, 0.1));
		float k = clamp((down - caustics_depths.x) / max(caustics_depths.y - caustics_depths.x, 0.01), 0.0, 1.0);
		float focus = 0.0;
		for (int c = caustics_first; c < cascade_count; c++) {
			float len = cascade_lengths[c];
			vec2 ci;
			if (flowing) {
				ci = texture(wave_caustics, vec3((sp - o0) / len, float(c))).rg * w0 + texture(wave_caustics, vec3((sp - o1) / len + vec2(0.37, 0.61), float(c))).rg * (1.0 - w0);
			} else {
				ci = texture(wave_caustics, vec3(sp / len, float(c))).rg;
			}
			focus += mix(ci.r, ci.g, k) - 1.0;
		}
		float fade = smoothstep(0.0, 0.2, down) * exp(-0.2 * down) * clamp(-sun_direction.y * 3.0, 0.0, 1.0);
		bottom *= max(1.0 + caustics_strength * fade * amp * focus, 0.0);
	}
	vec3 refracted = bottom * tv * td;

	float nv = clamp(dot(n_view, VIEW), 0.0, 1.0);
	float fresnel = 0.02 + 0.98 * pow(1.0 - nv, 5.0);

)"
		// MSVC: one string literal holds at most 16380 characters (C2026).
		R"(	// Reflections traced against the opaque scene's depth; the sky's IBL where nothing is hit.
	float ssr_weight = 0.0;
	vec3 ssr_color = vec3(0.0);
	float ssr_cost = 0.0; // the share of the steps taken (debug view 7)
	// Where Fresnel is small (looking down into the water) the reflection is a few per cent of the light: the sky's
	// IBL, no march. Faded in over 1.5x the threshold.
	float ssr_need = smoothstep(ssr_min_fresnel, ssr_min_fresnel * 1.5, fresnel);
	if (ssr_enabled && ssr_need > 0.0) {
		vec3 r = reflect(-VIEW, n_view);
		// A wave facet tilted far enough sends the ray below the horizon: it would leave by the next wave, upwards.
		vec3 rw = (INV_VIEW_MATRIX * vec4(r, 0.0)).xyz;
		if (rw.y < 0.0) {
			rw.y = -rw.y;
			r = (VIEW_MATRIX * vec4(rw, 0.0)).xyz;
		}
		bool hit = false;
		vec3 p0 = VERTEX + n_view * 0.02;
		// The depth buffer's view z needs only the inverse projection's z and w rows.
		vec2 iz = vec2(INV_PROJECTION_MATRIX[2][2], INV_PROJECTION_MATRIX[3][2]);
		vec2 iw = vec2(INV_PROJECTION_MATRIX[2][3], INV_PROJECTION_MATRIX[3][3]);
#if CURRENT_RENDERER == RENDERER_COMPATIBILITY
		int levels = 1; // no textureQueryLevels() there (nor the hierarchical depth): the linear march
#else
		int levels = textureQueryLevels(depth_tex);
#endif
		if (levels > 1) {
			// Hierarchical (decision 0126): the depth texture's levels hold the nearest depth of their cells. The ray
			// in screen space, its depth linear there, crosses a cell per step: a level up where nothing in the cell is
			// nearer than the ray, down where something may be (after Godot's SSR). Empty stretches — the water below
			// the ray, the sky — go in a few steps; every pixel the ray crosses is tested, without gaps.
			vec2 vs = VIEWPORT_SIZE;
			vec3 pe = p0 + r;
			if (pe.z > -0.00001) {
				pe -= r / r.z * (pe.z + 0.00001); // to the camera's plane: the near plane's depth is the largest
			}
			vec4 h0 = PROJECTION_MATRIX * vec4(p0, 1.0);
			vec4 h1 = PROJECTION_MATRIX * vec4(pe, 1.0);
			vec3 s0 = vec3(h0.xy / h0.w * 0.5 + 0.5, h0.z / h0.w);
			vec3 sd = vec3(h1.xy / h1.w * 0.5 + 0.5, h1.z / h1.w) - s0;
			if (abs(sd.z) > 1e-12) {
				// t: the change of the depth buffer's value along the ray.
				sd /= abs(sd.z);
				bool toward = sd.z >= 0.0;
				vec2 inv_sd = 1.0 / sd.xy;
				vec2 tb = max(-s0.xy * inv_sd, (vec2(1.0) - s0.xy) * inv_sd);
				float t_max = min(tb.x, tb.y);
				vec2 cstep = vec2(sd.x < 0.0 ? -1.0 : 1.0, sd.y < 0.0 ? -1.0 : 1.0);
				vec2 cnext = clamp(cstep, vec2(0.0), vec2(1.0));
				vec2 cbias = cstep * 0.000001 - s0.xy;
				vec2 pt0 = ((floor(s0.xy * vs) + cnext) / vs + cbias) * inv_sd;
				float t = min(pt0.x, pt0.y); // from the next pixel
				int level = 0;
				int iterations = 2 * ssr_steps;
				int behind = 0;
				while (level >= 0 && iterations > 0 && t < t_max) {
					vec3 sc = s0 + sd * t;
					vec2 cells = max(floor(vs / exp2(float(level))), vec2(1.0));
					vec2 ci = min(floor(sc.xy * cells), cells - 1.0);
					float cd = texelFetch(depth_tex, ivec2(ci), level).r;
					float depth_t = (cd - s0.z) * sd.z;
					vec2 pt = ((ci + cnext) / cells + cbias) * inv_sd;
					float edge_t = min(pt.x, pt.y);
					bool meets = toward ? (t <= depth_t) : (depth_t <= edge_t);
					int dl = meets ? -1 : 1;
					if (level == 0 && !meets) {
						behind = 0;
					} else if (level == 0) {
						// Behind the surface by more than its thickness (40 cm, at least twice the ray's depth across
						// the pixel): the ray passes behind it — a post, a trunk. Behind for more than four pixels (a
						// deck, a bank), what it meets there is hidden from the screen: no hit (the fallback below).
						float zs = (iz.x * cd + iz.y) / (iw.x * cd + iw.y);
						float zr = (iz.x * sc.z + iz.y) / (iw.x * sc.z + iw.y);
						float ze = s0.z + sd.z * edge_t;
						ze = (iz.x * ze + iz.y) / (iw.x * ze + iw.y);
						if (zs - zr > max(0.4, 2.0 * abs(zr - ze))) {
							behind++;
							if (behind > 4) {
								break;
							}
							meets = false;
							dl = 0;
						}
					}
					if (meets) {
						if (!toward) {
							t = max(t, depth_t);
						}
					} else {
						t = edge_t;
					}
					level = min(level + dl, levels - 1);
					iterations--;
				}
				ssr_cost = 1.0 - float(iterations) / float(2 * ssr_steps);
				if (level < 0) {
					vec2 uh = (s0 + sd * t).xy;
					if (texelFetch(depth_tex, min(ivec2(uh * vs), ivec2(vs) - ivec2(1)), 0).r > 0.0) { // not the sky
						vec2 edge = min(uh, 1.0 - uh);
						ssr_weight = smoothstep(0.0, 0.08, min(edge.x, edge.y)) * ssr_need;
						ssr_color = textureLod(screen_tex, uh, 0.0).rgb;
						hit = true;
					}
				}
			}
		} else {
			// Steps by the ray's length on the screen (about one per 10 pixels): a far pixel's ray crosses few pixels.
			float t_end = r.z > 0.0 ? min(ssr_max_distance, (-p0.z - 0.06) / r.z) : ssr_max_distance;
			vec4 ce = PROJECTION_MATRIX * vec4(p0 + r * t_end, 1.0);
			vec4 cs = PROJECTION_MATRIX * vec4(p0, 1.0);
			float px = length((ce.xy / ce.w - cs.xy / cs.w) * 0.5 * VIEWPORT_SIZE);
			int steps = clamp(int(px / 10.0), 6, ssr_steps);
			// A per-pixel, per-frame offset of the samples (interleaved gradient noise): TAA averages the gaps.
			float jitter = fract(52.9829189 * fract(dot(FRAGCOORD.xy + 5.588238 * fract(water_time * 7.31), vec2(0.06711056, 0.00583715))));
			// The ray's clip position is linear in t (two vectors, once).
			vec4 c0 = PROJECTION_MATRIX * vec4(p0, 1.0);
			vec4 c1 = PROJECTION_MATRIX * vec4(r, 0.0);
			float t_prev = 0.0;
			int lookups = 0;
			for (int i = 1; i <= steps; i++) {
				lookups = i;
				float f = (float(i) - 1.0 + max(jitter, 0.05)) / float(steps);
				float t = ssr_max_distance * f * f;
				float pz = p0.z + r.z * t;
				if (pz > -0.05) {
					break;
				}
				vec4 cp = c0 + c1 * t;
				vec2 uv = cp.xy / cp.w * 0.5 + 0.5;
				if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) {
					break;
				}
				float d = textureLod(depth_tex, uv, 0.0).r;
				float dz = (iz.x * d + iz.y) / (iw.x * d + iw.y) - pz;
				if (dz > 0.0 && dz < max(0.4, (t - t_prev) * 1.5)) {
					// Refine between the last two samples.
					lookups += 5;
					float lo = t_prev;
					float hi = t;
					for (int j = 0; j < 5; j++) {
						float mid = 0.5 * (lo + hi);
						vec4 cm = c0 + c1 * mid;
						vec2 um = cm.xy / cm.w * 0.5 + 0.5;
						float dm = textureLod(depth_tex, um, 0.0).r;
						if ((iz.x * dm + iz.y) / (iw.x * dm + iw.y) > p0.z + r.z * mid) {
							hi = mid;
						} else {
							lo = mid;
						}
					}
					vec4 ch = c0 + c1 * hi;
					vec2 uh = ch.xy / ch.w * 0.5 + 0.5;
					vec2 edge = min(uh, 1.0 - uh);
					ssr_weight = smoothstep(0.0, 0.08, min(edge.x, edge.y)) * (1.0 - smoothstep(0.75, 1.0, hi / ssr_max_distance)) * ssr_need;
					ssr_color = textureLod(screen_tex, uh, 0.0).rgb;
					hit = true;
					break;
				}
				t_prev = t;
			}
			ssr_cost = float(lookups) / float(2 * ssr_steps);
		}
		if (!hit && r.z < -0.01) {
			// Nothing near: what the screen shows in the ray's direction, if it is distant (the far shore, the
			// sky); else the sky's IBL.
			vec4 ci = PROJECTION_MATRIX * vec4(r, 0.0);
			vec2 ui = ci.xy / ci.w * 0.5 + 0.5;
			if (ui.x > 0.0 && ui.x < 1.0 && ui.y > 0.0 && ui.y < 1.0) {
				float zi = view_position(ui, textureLod(depth_tex, ui, 0.0).r, INV_PROJECTION_MATRIX).z;
				if (-zi > max(ssr_max_distance, -VERTEX.z * 0.5)) {
					vec2 edge = min(ui, 1.0 - ui);
					ssr_weight = smoothstep(0.0, 0.08, min(edge.x, edge.y)) * ssr_need;
					ssr_color = textureLod(screen_tex, ui, 0.0).rgb;
				}
			}
		}
	}

	// Foam: breaking waves, shores, the simulation's.
	float shore = 1.0 - smoothstep(0.0, shore_foam_depth, down0);
	shore *= shore;
	float foam_amount = clamp(whitecap * smoothstep(0.0, 0.1, amp) + shore * 0.4 + sims.a * v_sim, 0.0, 1.0);
	vec2 fuv = xz * 0.31;
	float noise = texture(foam_noise, fuv - o0 * 0.31 * float(flowing) + vec2(water_time * 0.011, 0.0)).r * 0.65 + texture(foam_noise, fuv * 2.3 + vec2(0.0, water_time * 0.017)).r * 0.35;
	float foam = smoothstep(1.0 - foam_amount, 1.0 - foam_amount + 0.22, noise) * step(0.001, foam_amount);

	float alpha_r = base_roughness * base_roughness;
	float rough = sqrt(sqrt(alpha_r * alpha_r + variance));
	ALBEDO = mix(scattering * (1.0 - tv) * (1.0 - fresnel), foam_color, foam);
	EMISSION = (refracted * (1.0 - fresnel) + ssr_color * (fresnel * ssr_weight)) * (1.0 - foam);
	ROUGHNESS = mix(rough, 0.75, foam);
	METALLIC = 0.0;
	// SPECULAR only scales Godot's IBL here (the sky where nothing was traced); the lights' highlights are light()'s.
	SPECULAR = 0.3536 * sqrt(1.0 - ssr_weight) * (1.0 - foam);
	v_glint = 1.0 - foam;
	ALPHA = smoothstep(0.0, 0.04, thick0);
	if (debug_view != 0) {
		vec3 dbg = vec3(0.0);
		if (debug_view == 1) {
			dbg = vec3(ssr_weight, fresnel, 0.0);
		} else if (debug_view == 2) {
			dbg = vec3(foam_amount, foam, shore);
		} else if (debug_view == 3) {
			dbg = refracted;
		} else if (debug_view == 4) {
			dbg = n_world * 0.5 + 0.5;
		} else if (debug_view == 5) {
			dbg = vec3(v_sim, sims.a, length(flow) * 0.5);
		} else if (debug_view == 6) {
			dbg = vec3(sqrt(variance), rough, 0.0);
		} else if (debug_view == 7) {
			dbg = vec3(ssr_cost, ssr_weight, 0.0);
		}
		ALBEDO = vec3(0.0);
		EMISSION = dbg;
		SPECULAR = 0.0;
		ROUGHNESS = 1.0;
		ALPHA = 1.0;
		v_glint = 0.0;
	}
}

void light() {
	float ndl = clamp(dot(NORMAL, LIGHT), 0.0, 1.0);
	DIFFUSE_LIGHT += ndl * ATTENUATION * LIGHT_COLOR / PI;
	// GGX with water's F0 (0.02), Smith-Schlick visibility.
	vec3 h = normalize(VIEW + LIGHT);
	float ndh = clamp(dot(NORMAL, h), 0.0, 1.0);
	float ndv = clamp(dot(NORMAL, VIEW), 1e-4, 1.0);
	float a = max(ROUGHNESS * ROUGHNESS, 0.002);
	float a2 = a * a;
	float dd = ndh * ndh * (a2 - 1.0) + 1.0;
	float d = a2 / (PI * dd * dd);
	float k = a * 0.5;
	float g = 1.0 / ((ndl * (1.0 - k) + k) * (ndv * (1.0 - k) + k));
	float f = 0.02 + 0.98 * pow(1.0 - clamp(dot(VIEW, h), 0.0, 1.0), 5.0);
	SPECULAR_LIGHT += (0.25 * d * g * f * ndl * v_glint) * ATTENUATION * LIGHT_COLOR;
}
)";
