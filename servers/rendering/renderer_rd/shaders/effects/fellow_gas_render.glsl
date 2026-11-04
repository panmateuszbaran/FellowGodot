#[compute]

#version 450

#VERSION_DEFINES

// FellowEngine gas volumes: rendering (docs/decisions/0118-gas-rendering.md). The simulation is fellow_gas_sim.glsl.
//
// MODE_LIGHT      per volume, at half its resolution: the optical depth towards the sun (the smoke's self-shadow,
//                 and the shadow it casts), marched through the smoke with the empty bricks skipped;
// MODE_SHADOW     per pixel of the depth pre-pass: the sun's transmittance through the volumes, multiplied into the
//                 contact shadow mask the opaque passes read (decision 0107);
// MODE_TRACE      per pixel at half resolution: a ray through every volume the camera's ray crosses, front to back, up
//                 to the depth buffer: in-scattered sunlight (Wrenninge's multiple-scattering octaves of a
//                 Henyey-Greenstein phase), the sky's light, the fire light's, the fire's blackbody emission;
// MODE_COMPOSITE  per pixel: the half-resolution result upsampled by depth (the four nearest, weighted by how close
//                 their depth is to the pixel's) over the colour buffer, before the transparent pass.

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

#ifdef MODE_TRACE
#include "../light_data_inc.glsl"
#include "../oct_inc.glsl"
#endif

#define MAX_VOLUMES 8

struct Volume {
	vec4 w2g0; // world -> grid (cells), rows; w: translation
	vec4 w2g1;
	vec4 w2g2;
	ivec4 size; // nx, ny, nz, flags (bit 0: casts shadows)
	ivec4 light_size; // the light volume's size; w: coarse bricks per axis (x)
	vec4 albedo_density; // rgb scattering albedo, w: extinction per unit smoke per metre
	vec4 emission; // x intensity, y ambient temperature (K), z march step (m), w anisotropy (g)
	vec4 fire_light_position; // world; w: 1 when the fire light is on
	vec4 fire_light_intensity; // rgb radiant intensity (the scene's units)
	vec4 misc; // x ambient factor, y self-shadow density factor, z scene shadow factor, w metres per cell
	vec4 fire; // x the colour temperature's scale (the emission's colour from temperature * this), yzw unused
};

layout(set = 0, binding = 0, std140) uniform Params {
	mat4 inv_view_projection; // NDC (x, y, depth * 2 - 1) to world
	vec4 camera_position; // world; w: exposure normalization
	vec4 sun_direction; // world, towards the sun; w: 1 with a sun
	vec4 sun_illuminance; // rgb
	vec4 ambient_color; // the environment's ambient light; a: the sky's contribution
	vec4 sky_params; // x 1: ambient light, y 1: from the sky's radiance, z the radiance's factor, w its level
	vec4 radiance_border; // x border, y 1 - 2 border
	mat4 sky_xform; // world -> the sky's orientation (3 x 3)
	mat4 view; // world -> view (the directional shadow's cascades are in view space)
	ivec4 sizes; // trace w, h; full w, h
	ivec4 rect; // the screen rectangle covered (full resolution): x, y, w, h
	ivec4 counts; // x volumes, y max steps, z frame, w 1: the sun's shadow map darkens the smoke
	Volume volumes[MAX_VOLUMES];
}
params;

vec3 to_grid(int p_v, vec3 p_world) {
	Volume v = params.volumes[p_v];
	vec4 w = vec4(p_world, 1.0);
	return vec3(dot(v.w2g0, w), dot(v.w2g1, w), dot(v.w2g2, w));
}

vec3 dir_to_grid(int p_v, vec3 p_dir) {
	Volume v = params.volumes[p_v];
	return vec3(dot(v.w2g0.xyz, p_dir), dot(v.w2g1.xyz, p_dir), dot(v.w2g2.xyz, p_dir));
}

// The ray o + t d (t >= 0) against the box [0, n]: the entry and exit t (t0 > t1: missed).
vec2 slab(vec3 p_o, vec3 p_d, vec3 p_n) {
	vec3 inv = 1.0 / mix(p_d, vec3(1e-12), equal(p_d, vec3(0.0)));
	vec3 ta = (vec3(0.0) - p_o) * inv;
	vec3 tb = (p_n - p_o) * inv;
	vec3 tmin = min(ta, tb);
	vec3 tmax = max(ta, tb);
	return vec2(max(max(tmin.x, tmin.y), max(tmin.z, 0.0)), min(tmax.x, min(tmax.y, tmax.z)));
}

#if defined(MODE_TRACE)
vec3 blackbody(float p_t) {
	// Krystek's (1985) approximation of the Planckian locus in CIE 1960 uv, to linear sRGB of luminance 1 (the same as
	// the simulation's COARSE pass, which sums the fire's emission for its light).
	float t = clamp(p_t, 1000.0, 15000.0);
	float u = (0.860117757 + 1.54118254e-4 * t + 1.28641212e-7 * t * t) / (1.0 + 8.42420235e-4 * t + 7.08145163e-7 * t * t);
	float v = (0.317398726 + 4.22806245e-5 * t + 4.20481691e-8 * t * t) / (1.0 - 2.89741816e-5 * t + 1.61456053e-7 * t * t);
	float x = 3.0 * u / (2.0 * u - 8.0 * v + 4.0);
	float y = 2.0 * v / (2.0 * u - 8.0 * v + 4.0);
	vec3 xyz = vec3(x / y, 1.0, (1.0 - x - y) / y);
	vec3 rgb = mat3(vec3(3.2404542, -0.9692660, 0.0556434), vec3(-1.5371385, 1.8760108, -0.2040259), vec3(-0.4985314, 0.0415560, 1.0572252)) * xyz;
	rgb = max(rgb, vec3(0.0));
	return rgb / max(dot(rgb, vec3(0.2126, 0.7152, 0.0722)), 1e-4);
}

// Radiance emitted per metre at a temperature above ambient: brightness by the fourth power of the absolute temperature
// (1 at 1500 K), visible from about 700 K; the colour a blackbody's at the temperature times the volume's colour
// temperature scale (above 1: yellower flames, as the soot's glow looks beside the cooler gas around it).
vec3 emission_rgb(int p_v, float p_temperature) {
	float t = params.volumes[p_v].emission.y + p_temperature;
	float k = t * (1.0 / 1500.0);
	return blackbody(t * params.volumes[p_v].fire.x) * (params.volumes[p_v].emission.x * k * k * k * k * smoothstep(700.0, 1000.0, t));
}

float henyey_greenstein(float p_cos, float p_g) {
	float g2 = p_g * p_g;
	return (1.0 - g2) / (4.0 * 3.14159265 * pow(max(1.0 + g2 - 2.0 * p_g * p_cos, 1e-4), 1.5));
}
#endif

/* clang-format off */

#ifdef MODE_LIGHT
layout(push_constant, std430) uniform Push {
	int volume; // the volume (index into params.volumes)
	int pad0;
	int pad1;
	int pad2;
}
push;

layout(set = 0, binding = 1) uniform sampler3D scalars;
layout(set = 0, binding = 2) uniform sampler3D coarse; // nearest
layout(r16f, set = 0, binding = 3) uniform restrict writeonly image3D light_out;

void main() {
	ivec3 l = ivec3(gl_GlobalInvocationID);
	int vi = push.volume;
	Volume v = params.volumes[vi];
	if (any(greaterThanEqual(l, v.light_size.xyz))) {
		return;
	}
	vec3 n = vec3(v.size.xyz);
	vec3 g = (vec3(l) + 0.5) * n / vec3(v.light_size.xyz);
	vec3 dg = dir_to_grid(vi, params.sun_direction.xyz);
	float metres_per_unit = 1.0 / max(length(dg), 1e-6); // grid units per metre along the sun -> metres per unit
	dg = normalize(dg);
	vec2 t = slab(g, dg, n);
	float tau = 0.0;
	const float STEP = 1.5; // cells
	float s = 0.5 * STEP;
	float density = v.albedo_density.w * metres_per_unit;
	for (int i = 0; i < 512 && s < t.y; i++) {
		vec3 p = g + dg * s;
		ivec3 b = ivec3(p * 0.25);
		if (texelFetch(coarse, clamp(b, ivec3(0), ivec3(textureSize(coarse, 0)) - 1), 0).x * v.albedo_density.w < 1e-4) {
			// An empty brick: to where the ray leaves it.
			vec3 lo = vec3(b) * 4.0;
			vec3 inv = 1.0 / mix(dg, vec3(1e-12), equal(dg, vec3(0.0)));
			vec3 ta = (lo - p) * inv;
			vec3 tb = (lo + 4.0 - p) * inv;
			float exit_t = min(max(ta.x, tb.x), min(max(ta.y, tb.y), max(ta.z, tb.z)));
			s += max(exit_t, 0.0) + 0.01;
			continue;
		}
		tau += textureLod(scalars, p / n, 0.0).x * density * STEP;
		s += STEP;
	}
	imageStore(light_out, l, vec4(tau));
}
#endif

#ifdef MODE_SHADOW
layout(set = 0, binding = 1) uniform sampler3D light_tex[MAX_VOLUMES];
layout(set = 0, binding = 2) uniform sampler2D depth_buffer;
layout(rg8, set = 0, binding = 3) uniform restrict image2D mask;

layout(push_constant, std430) uniform Push {
	int multiply; // 1: the contact shadows wrote the mask
	int pad0;
	int pad1;
	int pad2;
}
push;

void main() {
	ivec2 pos = ivec2(gl_GlobalInvocationID.xy);
	if (any(greaterThanEqual(pos, params.sizes.zw))) {
		return;
	}
	float base = push.multiply != 0 ? imageLoad(mask, pos).x : 1.0;
	float depth = texelFetch(depth_buffer, pos, 0).r;
	float tr = 1.0;
	if (depth != 0.0) {
		vec2 uv = (vec2(pos) + 0.5) / vec2(params.sizes.zw);
		vec4 wp = params.inv_view_projection * vec4(uv * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
		vec3 p = wp.xyz / wp.w;
		for (int i = 0; i < params.counts.x; i++) {
			Volume v = params.volumes[i];
			if ((v.size.w & 1) == 0) {
				continue;
			}
			vec3 o = to_grid(i, p);
			vec3 d = dir_to_grid(i, params.sun_direction.xyz);
			vec2 t = slab(o, d, vec3(v.size.xyz));
			if (t.x < t.y) {
				vec3 e = o + d * t.x;
				tr *= exp(-textureLod(light_tex[i], e / vec3(v.size.xyz), 0.0).r * v.misc.z);
			}
		}
	}
	imageStore(mask, pos, vec4(base * tr, 1.0, 0.0, 0.0));
}
#endif

#ifdef MODE_TRACE
// Textures and one sampler, not combined samplers: 3 x MAX_VOLUMES of those are beyond the 16 a stage of some
// devices (Apple's through MoltenVK, Android's baselines); decision 0150.
layout(set = 0, binding = 1) uniform texture3D scalars[MAX_VOLUMES];
layout(set = 0, binding = 2) uniform texture3D light_tex[MAX_VOLUMES];
layout(set = 0, binding = 3) uniform texture3D coarse[MAX_VOLUMES]; // texelFetch
layout(set = 0, binding = 10) uniform sampler linear_clamp;
layout(set = 0, binding = 4) uniform sampler2D depth_buffer;
layout(rgba16f, set = 0, binding = 5) uniform restrict writeonly image2D trace_out;
layout(r32f, set = 0, binding = 6) uniform restrict writeonly image2D trace_depth_out;
#if defined(FELLOW_RADIANCE_ARRAY)
layout(set = 0, binding = 7) uniform sampler2DArray radiance_octmap;
#else
layout(set = 0, binding = 7) uniform sampler2D radiance_octmap;
#endif

layout(set = 0, binding = 8, std140) uniform DirectionalLights {
	DirectionalLightData data[MAX_DIRECTIONAL_LIGHT_DATA_STRUCTS];
}
directional_lights;
layout(set = 0, binding = 9) uniform sampler2D directional_shadow_atlas;

// The sun's shadow map at a point (the first directional light, as Godot's volumetric fog samples it: the cascade by
// view depth, a soft exponential comparison, faded out where the shadow ends).
float sun_shadow(vec3 p_world) {
	if (params.counts.w == 0) {
		return 1.0;
	}
	vec4 v = params.view * vec4(p_world, 1.0);
	float depth_z = -v.z;
	vec4 pssm;
	float z_range;
	if (depth_z < directional_lights.data[0].shadow_split_offsets.x) {
		pssm = directional_lights.data[0].shadow_matrix1 * v;
		z_range = directional_lights.data[0].shadow_z_range.x;
	} else if (depth_z < directional_lights.data[0].shadow_split_offsets.y) {
		pssm = directional_lights.data[0].shadow_matrix2 * v;
		z_range = directional_lights.data[0].shadow_z_range.y;
	} else if (depth_z < directional_lights.data[0].shadow_split_offsets.z) {
		pssm = directional_lights.data[0].shadow_matrix3 * v;
		z_range = directional_lights.data[0].shadow_z_range.z;
	} else {
		pssm = directional_lights.data[0].shadow_matrix4 * v;
		z_range = directional_lights.data[0].shadow_z_range.w;
	}
	pssm /= pssm.w;
	float depth = textureLod(directional_shadow_atlas, pssm.xy, 0.0).r;
	float shadow = exp(min(0.0, pssm.z - depth) * z_range * 10.0);
	shadow = mix(shadow, 1.0, smoothstep(directional_lights.data[0].fade_from, directional_lights.data[0].fade_to, v.z));
	return mix(1.0 - directional_lights.data[0].shadow_opacity, 1.0, shadow);
}

vec3 ambient_light(vec3 p_dir) {
	// The environment's ambient light from a direction (as the scene shader and DDGI take it).
	if (params.sky_params.x == 0.0) {
		return vec3(0.0);
	}
	vec3 ambient = params.ambient_color.rgb;
	if (params.sky_params.y != 0.0) {
		vec2 uv = vec3_to_oct_with_border(mat3(params.sky_xform) * p_dir, params.radiance_border.xy);
#if defined(FELLOW_RADIANCE_ARRAY)
		vec3 sky = textureLod(radiance_octmap, vec3(uv, params.sky_params.w), 0.0).rgb;
#else
		vec3 sky = textureLod(radiance_octmap, uv, params.sky_params.w).rgb;
#endif
		ambient = mix(ambient, sky * params.sky_params.z, params.ambient_color.w);
	}
	return ambient;
}

void main() {
	ivec2 hp = ivec2(gl_GlobalInvocationID.xy) + params.rect.xy / 2;
	ivec2 hend = (params.rect.xy + params.rect.zw + 1) / 2;
	if (any(greaterThanEqual(hp, min(hend, params.sizes.xy)))) {
		return;
	}
	ivec2 fp = min(hp * 2, params.sizes.zw - 1);
	float depth = texelFetch(depth_buffer, fp, 0).r;
	vec2 uv = (vec2(fp) + 0.5) / vec2(params.sizes.zw);
	vec3 cam = params.camera_position.xyz;
	vec4 wp = params.inv_view_projection * vec4(uv * 2.0 - 1.0, depth == 0.0 ? 0.5 : depth * 2.0 - 1.0, 1.0);
	vec3 surf = wp.xyz / wp.w;
	vec3 dir = normalize(surf - cam);
	float t_scene = depth == 0.0 ? 1e9 : length(surf - cam);
	imageStore(trace_depth_out, hp, vec4(depth == 0.0 ? 1e9 : t_scene));

	float cos_sun = dot(dir, params.sun_direction.xyz);
	// The march's start, dithered per pixel and frame (interleaved gradient noise; TAA averages it).
	float jitter = fract(52.9829189 * fract(dot(vec2(hp) + float(params.counts.z % 64) * vec2(5.588238, 5.588238), vec2(0.06711056, 0.00583715))));
	vec3 radiance = vec3(0.0);
	float transmittance = 1.0;
	// The ambient light the smoke scatters: mostly from above (the sky), some from the sides and the ground.
	vec3 ambient = vec3(0.0);
	bool ambient_done = false;
	for (int i = 0; i < params.counts.x && transmittance > 0.003; i++) {
		Volume v = params.volumes[i];
		vec3 n = vec3(v.size.xyz);
		vec3 o = to_grid(i, cam);
		vec3 d = dir_to_grid(i, dir); // grid units per metre
		vec2 t = slab(o, d, n);
		t.y = min(t.y, t_scene);
		if (t.x >= t.y) {
			continue;
		}
		float ds = v.emission.z;
		int steps = min(int(ceil((t.y - t.x) / ds)), params.counts.y);
		ds = (t.y - t.x) / float(max(steps, 1));
		float g = v.emission.w;
		vec3 albedo = v.albedo_density.rgb;
		float density = v.albedo_density.w;
		vec3 sun = params.sun_illuminance.rgb * params.sun_direction.w;
		ivec3 nbrick = ivec3(textureSize(sampler3D(coarse[i], linear_clamp), 0));
		float s = t.x + ds * jitter;
		for (int k = 0; k < steps && transmittance > 0.003; k++) {
			vec3 gp = o + d * s;
			ivec3 b = clamp(ivec3(gp * 0.25), ivec3(0), nbrick - 1);
			vec2 cm = texelFetch(sampler3D(coarse[i], linear_clamp), b, 0).xy;
			if (cm.x * density < 1e-4 && cm.y < 300.0) {
				s += ds;
				continue;
			}
			if (!ambient_done) {
				ambient = ambient_light(vec3(0.0, 1.0, 0.0)) * 0.3 + ambient_light(vec3(0.0, -1.0, 0.0)) * 0.1 +
						(ambient_light(vec3(1.0, 0.0, 0.0)) + ambient_light(vec3(-1.0, 0.0, 0.0)) + ambient_light(vec3(0.0, 0.0, 1.0)) + ambient_light(vec3(0.0, 0.0, -1.0))) * 0.15;
				ambient_done = true;
			}
			vec4 sc = textureLod(sampler3D(scalars[i], linear_clamp), gp / n, 0.0);
			float sigma_t = sc.x * density;
			vec3 eps = sc.y > 300.0 ? emission_rgb(i, sc.y) : vec3(0.0);
			if (sigma_t > 1e-5 || dot(eps, vec3(1.0)) > 0.0) {
				vec3 in_scatter = vec3(0.0);
				if (sigma_t > 1e-5) {
					// Sun: Wrenninge's multiple-scattering approximation, four octaves.
					float tau_l = textureLod(sampler3D(light_tex[i], linear_clamp), gp / n, 0.0).r * v.misc.y;
					float a = 1.0;
					float b = 1.0;
					float c = 1.0;
					float ms = 0.0;
					for (int o8 = 0; o8 < 4; o8++) {
						ms += a * henyey_greenstein(cos_sun, g * c) * exp(-tau_l * b);
						a *= 0.5;
						b *= 0.3;
						c *= 0.5;
					}
					in_scatter = sun * (ms * sun_shadow(cam + dir * s));
					in_scatter += ambient * v.misc.x * (0.35 + 0.65 * exp(-tau_l * 0.3));
					if (v.fire_light_position.w > 0.0) {
						vec3 wpos = cam + dir * s;
						vec3 dl = v.fire_light_position.xyz - wpos;
						in_scatter += v.fire_light_intensity.rgb / (4.0 * 3.14159265 * (dot(dl, dl) + 0.25));
					}
					in_scatter *= albedo * sigma_t;
				}
				vec3 source = in_scatter + eps * params.camera_position.w;
				float tr = exp(-sigma_t * ds);
				float w = sigma_t > 1e-5 ? (1.0 - tr) / sigma_t : ds;
				radiance += transmittance * source * w;
				transmittance *= tr;
			}
			s += ds;
		}
	}
	imageStore(trace_out, hp, vec4(radiance, transmittance));
}
#endif

#ifdef MODE_COMPOSITE
layout(set = 0, binding = 1) uniform sampler2D trace_tex; // nearest
layout(set = 0, binding = 2) uniform sampler2D trace_depth; // nearest
layout(set = 0, binding = 4) uniform sampler2D depth_buffer;
layout(rgba16f, set = 0, binding = 5) uniform restrict image2D color_image;

void main() {
	ivec2 pos = ivec2(gl_GlobalInvocationID.xy) + params.rect.xy;
	if (any(greaterThanEqual(pos, params.rect.xy + params.rect.zw)) || any(greaterThanEqual(pos, params.sizes.zw))) {
		return;
	}
	float depth = texelFetch(depth_buffer, pos, 0).r;
	float dist = 1e9;
	if (depth != 0.0) {
		vec2 uv = (vec2(pos) + 0.5) / vec2(params.sizes.zw);
		vec4 wp = params.inv_view_projection * vec4(uv * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
		dist = length(wp.xyz / wp.w - params.camera_position.xyz);
	}
	// The four nearest half-resolution samples (within the traced rectangle), weighted bilinearly and by depth.
	vec2 h = (vec2(pos) + 0.5) * 0.5 - 0.5;
	ivec2 b0 = ivec2(floor(h));
	vec2 f = h - vec2(b0);
	ivec2 lo = params.rect.xy / 2;
	ivec2 hi = min((params.rect.xy + params.rect.zw + 1) / 2, params.sizes.xy) - 1;
	vec4 sum = vec4(0.0);
	float wsum = 0.0;
	vec4 best = vec4(0.0, 0.0, 0.0, 1.0);
	float best_dz = 1e30;
	float tolerance = 0.05 * min(dist, 1e6) + 0.05;
	for (int k = 0; k < 4; k++) {
		ivec2 o = ivec2(k & 1, k >> 1);
		ivec2 q = clamp(b0 + o, lo, hi);
		vec4 v = texelFetch(trace_tex, q, 0);
		float qd = texelFetch(trace_depth, q, 0).r;
		float dz = abs(min(qd, 1e6) - min(dist, 1e6));
		float wb = (o.x == 1 ? f.x : 1.0 - f.x) * (o.y == 1 ? f.y : 1.0 - f.y);
		float w = wb * exp(-dz / tolerance);
		sum += v * w;
		wsum += w;
		if (dz < best_dz) {
			best_dz = dz;
			best = v;
		}
	}
	vec4 g = wsum > 1e-4 ? sum / wsum : best;
	if (g.a > 0.9999 && dot(g.rgb, vec3(1.0)) <= 0.0) {
		return;
	}
	vec4 c = imageLoad(color_image, pos);
	c.rgb = c.rgb * g.a + g.rgb;
	imageStore(color_image, pos, c);
}
#endif

/* clang-format on */
