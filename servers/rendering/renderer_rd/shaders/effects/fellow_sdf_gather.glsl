#[compute]

#version 460

#VERSION_DEFINES

#if defined(FELLOW_TRACE_RT)
#extension GL_EXT_ray_query : require
#if defined(FELLOW_POSITION_FETCH)
#extension GL_EXT_ray_tracing_position_fetch : require
#endif
#endif

// FellowEngine SDF screen gather (docs/decisions/0308-sdf-screen-gather.md): SDFGI's diffuse light gathered per screen
// probe instead of interpolated from its world probes. A probe every `tile` pixels (at its tile's centre) traces `rays` cosine-distributed rays from its surface through SDFGI's distance field cascades; a hit takes the
// light of the voxel it reaches (SDFGI's direct light from every light, emission and SDFGI's own bounce, by the field's
// normal), a ray that leaves the last cascade the environment's ambient light. The mean is the irradiance (the
// "ambient light" the scene shader multiplies by albedo), accumulated over frames where the previous frame's probe at
// the reprojected point lies on the same surface.
// MODE_TRACE: one workgroup per probe, one invocation per ray. With FELLOW_TRACE_RT (raytracing/gi_screen_gather) the
// rays are traced against the ray tracing scene instead and their hits lit as DDGI's (the sun with a shadow ray, the
// probes' irradiance at the hit for the further bounces, emission): the first bounce per screen probe, DDGI's probes
// for the rest; the result goes to DDGI's screen pass texture.
// MODE_FILTER: per probe, the probes of the 5 x 5 tiles around it on its surface (their distance from its plane, their
// normal) averaged, for the resolve (the history keeps the unfiltered light).
// MODE_RESOLVE: per pixel of SDFGI's ambient buffer, the probes of the 2 x 2 tiles around it weighed by their distance
// from its plane and their normal; written in place of SDFGI's diffuse light (its reflections stay).

#include "../oct_inc.glsl"
#if defined(FELLOW_TRACE_RT)
#include "../fellow_ddgi_inc.glsl"
#endif

layout(set = 0, binding = 0, std140) uniform Params {
	mat4 inv_projection; // NDC (x, y, depth * 2 - 1) to view
	mat4 view_to_world;
	mat4 prev_world_to_clip; // the previous frame's (history reprojection)
	mat4 sky_xform; // world directions to the sky's
	vec4 sky_params; // x 1: ambient light, y 1: from the sky's radiance, z the radiance's factor, w the radiance's level
	vec4 ambient_color; // rgb the environment's ambient colour and energy, a the sky contribution
	vec4 radiance_border; // x border, y 1 - 2 border
	vec4 sdf; // x SDFGI's cascade size (cells), y its cascades, z its y scale, w steps per cascade
	vec4 light; // x the voxels' light to the scene shader's units, y energy, z the history's largest sample count, w normal bias (cascade 0 cells)
	vec4 footprint; // x a pixel's size at 1 m from the camera (metres), y the field's surface bias (cells: a hit that much short of the stored distance)
	ivec4 screen; // xy the internal size, zw SDFGI's ambient buffer size
	ivec4 probes; // xy the probe grid, z tile (pixels), w rays per probe
	ivec4 frame; // x frame, y 1: the history is valid
}
params;

layout(set = 0, binding = 1) uniform sampler2D depth_buffer;
layout(set = 0, binding = 2) uniform sampler2D normal_roughness_buffer;
layout(set = 0, binding = 7) uniform sampler linear_sampler;

uint hash(uint p_x) {
	p_x ^= p_x >> 16;
	p_x *= 0x7feb352du;
	p_x ^= p_x >> 15;
	p_x *= 0x846ca68bu;
	p_x ^= p_x >> 16;
	return p_x;
}

vec3 view_position(ivec2 p_pixel, float p_depth) {
	vec2 uv = (vec2(p_pixel) + 0.5) / vec2(params.screen.xy);
	vec4 v = params.inv_projection * vec4(uv * 2.0 - 1.0, p_depth * 2.0 - 1.0, 1.0);
	return v.xyz / v.w;
}

// The surface at an internal pixel: world position and normal (false where the sky is).
bool surface_at(ivec2 p_pixel, out vec3 r_pos, out vec3 r_normal, out float r_distance) {
	float depth = texelFetch(depth_buffer, p_pixel, 0).r;
	if (depth == 0.0) {
		return false;
	}
	vec3 vp = view_position(p_pixel, depth);
	r_distance = length(vp);
	r_pos = (params.view_to_world * vec4(vp, 1.0)).xyz;
	vec3 vn = normalize(texelFetch(normal_roughness_buffer, p_pixel, 0).xyz * 2.0 - 1.0);
	r_normal = normalize(mat3(params.view_to_world) * vn);
	return true;
}

#if defined(MODE_TRACE)

layout(local_size_x = 64, local_size_y = 1, local_size_z = 1) in;

#if !defined(FELLOW_TRACE_RT)
#define FELLOW_SDF_MAX_CASCADES 8 // as SDFGI's

layout(set = 0, binding = 3) uniform texture3D sdf_cascades[FELLOW_SDF_MAX_CASCADES];
layout(set = 0, binding = 4) uniform texture3D light_cascades[FELLOW_SDF_MAX_CASCADES];
layout(set = 0, binding = 5) uniform texture3D aniso0_cascades[FELLOW_SDF_MAX_CASCADES];
layout(set = 0, binding = 6) uniform texture3D aniso1_cascades[FELLOW_SDF_MAX_CASCADES];
#else
layout(set = 0, binding = 3) uniform accelerationStructureEXT tlas;
struct FellowRTInstance { // as fellow_ddgi.glsl's
	vec4 albedo; // rgb: linear albedo, w: 1 if double-sided
	vec4 emission; // rgb: emission in the scene's units, w: metallic
};
layout(set = 0, binding = 4, std430) restrict readonly buffer Instances {
	FellowRTInstance data[];
}
instances;
layout(set = 0, binding = 5) uniform texture2D ddgi_irradiance;
layout(set = 0, binding = 6) uniform texture2D ddgi_distance;
layout(set = 0, binding = 19) uniform texture2D ddgi_outer_irradiance;
layout(set = 0, binding = 20) uniform texture2D ddgi_outer_distance;
layout(set = 0, binding = 18, std140) uniform RTParams {
	FellowDDGIVolume volume;
	FellowDDGIVolume outer; // base_cell.w 0: none
	vec4 sun_direction; // xyz towards the light, w 1 if there is one
	vec4 sun_color; // rgb in the scene's units, w 1 if it casts shadows
	vec4 trace; // x the rays' length, y energy kept per bounce
}
rt;
#endif
#if defined(FELLOW_RADIANCE_ARRAY)
layout(set = 0, binding = 8) uniform texture2DArray radiance_octmap;
#else
layout(set = 0, binding = 8) uniform texture2D radiance_octmap;
#endif

#if !defined(FELLOW_TRACE_RT)
struct FellowSDFCascade { // as SDFGI's CascadeData
	vec3 offset; // the cell (0, 0, 0) in SDFGI's space (y scaled)
	float to_cell;
	ivec3 probe_world_offset;
	uint pad;
	vec4 pad2;
};

layout(set = 0, binding = 9, std140) uniform SDFCascades {
	FellowSDFCascade data[FELLOW_SDF_MAX_CASCADES];
}
sdf_cascades_data;
#endif

layout(rgba16f, set = 0, binding = 10) uniform restrict writeonly image2D out_irradiance; // rgb irradiance, a samples
layout(rgba32f, set = 0, binding = 11) uniform restrict writeonly image2D out_position; // xyz world, w 1: valid
layout(rgba16f, set = 0, binding = 12) uniform restrict writeonly image2D out_normal; // xyz world normal, w distance from the camera
layout(set = 0, binding = 13) uniform texture2D prev_irradiance;
layout(set = 0, binding = 14) uniform texture2D prev_position;
layout(set = 0, binding = 15) uniform texture2D prev_normal;

shared vec3 ray_sum[64];

// As DDGI's: what the environment's ambient light gives in a direction.
vec3 ambient_in(vec3 p_dir) {
	if (params.sky_params.x == 0.0) {
		return vec3(0.0);
	}
	vec3 ambient = params.ambient_color.rgb;
	if (params.sky_params.y != 0.0) {
		vec3 d = mat3(params.sky_xform) * p_dir;
		vec2 uv = vec3_to_oct_with_border(d, params.radiance_border.xy);
#if defined(FELLOW_RADIANCE_ARRAY)
		vec3 sky = textureLod(sampler2DArray(radiance_octmap, linear_sampler), vec3(uv, params.sky_params.w), 0.0).rgb;
#else
		vec3 sky = textureLod(sampler2D(radiance_octmap, linear_sampler), uv, params.sky_params.w).rgb;
#endif
		ambient = mix(ambient, sky * params.sky_params.z, params.ambient_color.w);
	}
	return ambient;
}

// A cosine-weighted direction around p_n.
vec3 cosine_direction(vec3 p_n, vec2 p_u) {
	float s = p_n.z >= 0.0 ? 1.0 : -1.0;
	float a = -1.0 / (s + p_n.z);
	float b = p_n.x * p_n.y * a;
	vec3 t1 = vec3(1.0 + s * p_n.x * p_n.x * a, s * b, -s * p_n.x);
	vec3 t2 = vec3(b, s + p_n.y * p_n.y * a, -p_n.y);
	float r = sqrt(p_u.x);
	float phi = 6.28318530718 * p_u.y;
	return normalize(t1 * (r * cos(phi)) + t2 * (r * sin(phi)) + p_n * sqrt(max(0.0, 1.0 - p_u.x)));
}

float radical_inverse(uint p_bits) {
	p_bits = (p_bits << 16u) | (p_bits >> 16u);
	p_bits = ((p_bits & 0x55555555u) << 1u) | ((p_bits & 0xAAAAAAAAu) >> 1u);
	p_bits = ((p_bits & 0x33333333u) << 2u) | ((p_bits & 0xCCCCCCCCu) >> 2u);
	p_bits = ((p_bits & 0x0F0F0F0Fu) << 4u) | ((p_bits & 0xF0F0F0F0u) >> 4u);
	p_bits = ((p_bits & 0x00FF00FFu) << 8u) | ((p_bits & 0xFF00FF00u) >> 8u);
	return float(p_bits) * 2.3283064365386963e-10;
}

#if !defined(FELLOW_TRACE_RT)
// The radiance a ray from p_origin (world) brings: the voxel it reaches through SDFGI's cascades, or the ambient light.
vec3 trace(vec3 p_origin, vec3 p_dir) {
	const float y_mult = params.sdf.z;
	const vec3 grid = vec3(params.sdf.x);
	const vec3 pos_to_uvw = 1.0 / grid;
	const int cascade_count = int(params.sdf.y);
	const int max_steps = int(params.sdf.w);
	vec3 ray_pos = vec3(p_origin.x, p_origin.y * y_mult, p_origin.z);
	vec3 ray_dir = normalize(vec3(p_dir.x, p_dir.y * y_mult, p_dir.z));
	vec3 inv_dir = 1.0 / max(abs(ray_dir), vec3(1e-6)) * mix(vec3(-1.0), vec3(1.0), greaterThanEqual(ray_dir, vec3(0.0)));
	bool hit = false;
	int hit_cascade = 0;
	vec3 uvw = vec3(0.0);
	for (int j = 0; j < cascade_count; j++) {
		vec3 pos = (ray_pos - sdf_cascades_data.data[j].offset) * sdf_cascades_data.data[j].to_cell;
		if (any(lessThan(pos, vec3(0.0))) || any(greaterThanEqual(pos, grid))) {
			continue;
		}
		vec3 t0 = -pos * inv_dir;
		vec3 t1 = (grid - pos) * inv_dir;
		vec3 tmax = max(t0, t1);
		float max_advance = min(tmax.x, min(tmax.y, tmax.z));
		float advance = 0.0;
		for (int s = 0; s < max_steps && advance < max_advance; s++) {
			uvw = (pos + ray_dir * advance) * pos_to_uvw;
			float d = texture(sampler3D(sdf_cascades[j], linear_sampler), uvw).r * 255.0 - params.footprint.y;
			if (d < 0.05) {
				hit = true;
				break;
			}
			advance += d;
		}
		advance = min(advance, max_advance);
		if (hit) {
			hit_cascade = j;
			break;
		}
		pos += ray_dir * advance;
		ray_pos = pos / sdf_cascades_data.data[j].to_cell + sdf_cascades_data.data[j].offset;
	}
	if (!hit) {
		return ambient_in(p_dir);
	}
	vec3 light = vec3(0.0);
	for (int j = 0; j < cascade_count; j++) {
		if (j == hit_cascade) { // the same texture for every invocation that reads one (as SDFGI's)
			const float e = 0.001;
			vec3 g = vec3(
					texture(sampler3D(sdf_cascades[j], linear_sampler), uvw + vec3(e, 0.0, 0.0)).r - texture(sampler3D(sdf_cascades[j], linear_sampler), uvw - vec3(e, 0.0, 0.0)).r,
					texture(sampler3D(sdf_cascades[j], linear_sampler), uvw + vec3(0.0, e, 0.0)).r - texture(sampler3D(sdf_cascades[j], linear_sampler), uvw - vec3(0.0, e, 0.0)).r,
					texture(sampler3D(sdf_cascades[j], linear_sampler), uvw + vec3(0.0, 0.0, e)).r - texture(sampler3D(sdf_cascades[j], linear_sampler), uvw - vec3(0.0, 0.0, e)).r);
			vec3 n = dot(g, g) > 1e-12 ? normalize(g) : -ray_dir;
			vec3 voxel = texture(sampler3D(light_cascades[j], linear_sampler), uvw).rgb;
			vec4 a0 = texture(sampler3D(aniso0_cascades[j], linear_sampler), uvw);
			vec3 a1 = vec3(a0.a, texture(sampler3D(aniso1_cascades[j], linear_sampler), uvw).rg);
			light = voxel * (dot(max(vec3(0.0), n * a0.rgb), vec3(1.0)) + dot(max(vec3(0.0), -n * a1), vec3(1.0)));
		}
	}
	return light * params.light.x;
}

// Where the rays start: off the surface's own voxels.
vec3 ray_origin(vec3 p_pos, vec3 p_normal, vec3 p_dir, float p_distance) {
	float cell = 1.0 / sdf_cascades_data.data[0].to_cell;
	return p_pos + p_normal * (params.light.w * cell) + p_dir * (0.5 * cell);
}
#else
// The radiance a ray from p_origin (world) brings: its hit in the ray tracing scene lit as DDGI's rays' are, or the
// ambient light.
vec3 trace(vec3 p_origin, vec3 p_dir) {
	rayQueryEXT rq;
	rayQueryInitializeEXT(rq, tlas, gl_RayFlagsOpaqueEXT, 0x01, p_origin, 0.0, p_dir, rt.trace.x);
	while (rayQueryProceedEXT(rq)) {
	}
	if (rayQueryGetIntersectionTypeEXT(rq, true) == gl_RayQueryCommittedIntersectionNoneEXT) {
		return ambient_in(p_dir);
	}
	float t = rayQueryGetIntersectionTEXT(rq, true);
	FellowRTInstance inst = instances.data[rayQueryGetIntersectionInstanceCustomIndexEXT(rq, true)];
	if (!rayQueryGetIntersectionFrontFaceEXT(rq, true) && inst.albedo.w == 0.0) {
		return vec3(0.0); // the inside of a closed mesh
	}
	vec3 n;
#if defined(FELLOW_POSITION_FETCH)
	vec3 tri[3];
	rayQueryGetIntersectionTriangleVertexPositionsEXT(rq, true, tri);
	mat4x3 o2w = rayQueryGetIntersectionObjectToWorldEXT(rq, true);
	vec3 wn = cross(mat3(o2w) * (tri[1] - tri[0]), mat3(o2w) * (tri[2] - tri[0]));
	float wl = dot(wn, wn);
	n = wl > 1e-20 ? wn * inversesqrt(wl) : -p_dir;
	if (dot(n, p_dir) > 0.0) {
		n = -n;
	}
#else
	n = -p_dir;
#endif
	vec3 hit = p_origin + p_dir * t;
	vec3 light = vec3(0.0);
	if (rt.sun_direction.w != 0.0) {
		float ndl = dot(n, rt.sun_direction.xyz);
		if (ndl > 0.0) {
			float visible = 1.0;
			if (rt.sun_color.w != 0.0) {
				rayQueryEXT sq;
				rayQueryInitializeEXT(sq, tlas, gl_RayFlagsTerminateOnFirstHitEXT | gl_RayFlagsOpaqueEXT | gl_RayFlagsCullFrontFacingTrianglesEXT, 0x02, hit + n * 0.01, 0.0, rt.sun_direction.xyz, rt.trace.x);
				while (rayQueryProceedEXT(sq)) {
				}
				visible = rayQueryGetIntersectionTypeEXT(sq, true) == gl_RayQueryCommittedIntersectionNoneEXT ? 1.0 : 0.0;
			}
			light += rt.sun_color.rgb * ndl * visible;
		}
	}
	// The further bounces: DDGI's probes at the hit.
	float coverage;
	vec3 bounce = fellow_ddgi_irradiance_cascades(ddgi_irradiance, ddgi_distance, rt.volume, ddgi_outer_irradiance, ddgi_outer_distance, rt.outer, linear_sampler, hit, n, -p_dir, coverage);
	light += mix(ambient_in(n) * 0.5, bounce, clamp(coverage, 0.0, 1.0)) * rt.trace.y;
	return inst.albedo.rgb * (1.0 - inst.emission.w) * light + inst.emission.rgb;
}

// Where the rays start: off the surface, by its distance from the camera (as DDGI's reference).
vec3 ray_origin(vec3 p_pos, vec3 p_normal, vec3 p_dir, float p_distance) {
	return p_pos + p_normal * (0.003 + 0.0015 * p_distance);
}
#endif

void main() {
	ivec2 probe = ivec2(gl_WorkGroupID.xy);
	uint ray = gl_LocalInvocationIndex;
	int tile = params.probes.z;
	int rays = params.probes.w;
	// The probe's pixel: its tile's centre, or where the sky is there, the first of the centres of its quarters that is
	// not. The same pixel every frame: a probe jittered over its tile made the resolve's interpolation shimmer (on
	// lavapipe 3.7 % of GI Lab's pixels changed by more than 2 levels from frame to frame of a still view, 0.5 % without).
	uint h = hash(uint(probe.x) * 7919u + hash(uint(probe.y) * 104729u + hash(uint(params.frame.x))));
	ivec2 base = probe * tile;
	ivec2 pixel = min(base + tile / 2, params.screen.xy - 1);
	vec3 pos, normal;
	float distance;
	bool valid = surface_at(pixel, pos, normal, distance);
	for (int q = 0; q < 4 && !valid; q++) {
		pixel = min(base + ivec2((q & 1) * 2 + 1, (q >> 1) * 2 + 1) * tile / 4, params.screen.xy - 1);
		valid = surface_at(pixel, pos, normal, distance);
	}
	if (!valid) {
		if (ray == 0u) {
			imageStore(out_irradiance, probe, vec4(0.0));
			imageStore(out_position, probe, vec4(0.0));
			imageStore(out_normal, probe, vec4(0.0, 0.0, 1.0, 0.0));
		}
		return; // the whole workgroup (uniform)
	}

	vec3 radiance = vec3(0.0);
	if (int(ray) < rays) {
		// Stratified (Hammersley) with a random shift per probe and frame; the origin lifted along the normal off the
		// voxels of its own surface.
		vec2 shift = vec2(float(h & 0xFFFFu), float(hash(h) & 0xFFFFu)) / 65536.0;
		vec2 u = fract(vec2((float(ray) + 0.5) / float(rays), radical_inverse(ray)) + shift);
		vec3 dir = cosine_direction(normal, u);
		radiance = trace(ray_origin(pos, normal, dir, distance), dir);
		if (any(isnan(radiance)) || any(isinf(radiance))) {
			radiance = vec3(0.0);
		}
	}
	ray_sum[ray] = radiance;
	barrier();
	for (uint s = 32u; s > 0u; s >>= 1u) {
		if (ray < s) {
			ray_sum[ray] += ray_sum[ray + s];
		}
		barrier();
	}
	if (ray != 0u) {
		return;
	}
	// Cosine-weighted directions: the mean radiance is the irradiance over pi (the scene shader's ambient light).
	vec3 irradiance = ray_sum[0] / float(max(rays, 1)) * params.light.y;
	float samples = 1.0;
	if (params.frame.y != 0) {
		// The previous frame's probe where this point was, if it lay on the same surface.
		vec4 clip = params.prev_world_to_clip * vec4(pos, 1.0);
		if (clip.w > 0.0) {
			vec2 uv = clip.xy / clip.w * 0.5 + 0.5;
			ivec2 prev_probe = ivec2(floor(uv * vec2(params.screen.xy) / float(tile)));
			if (all(greaterThanEqual(prev_probe, ivec2(0))) && all(lessThan(prev_probe, params.probes.xy))) {
				vec4 pp = texelFetch(sampler2D(prev_position, linear_sampler), prev_probe, 0);
				vec4 pn = texelFetch(sampler2D(prev_normal, linear_sampler), prev_probe, 0);
				float reach = params.footprint.x * distance * float(tile) * 1.5 + 0.02;
				vec3 delta = pp.xyz - pos;
				if (pp.w != 0.0 && dot(pn.xyz, normal) > 0.8 && abs(dot(delta, normal)) < reach * 0.5 && length(delta) < reach) {
					vec4 prev = texelFetch(sampler2D(prev_irradiance, linear_sampler), prev_probe, 0);
					samples = min(prev.a + 1.0, params.light.z);
					irradiance = mix(prev.rgb, irradiance, 1.0 / samples);
				}
			}
		}
	}
	imageStore(out_irradiance, probe, vec4(irradiance, samples));
	imageStore(out_position, probe, vec4(pos, 1.0));
	imageStore(out_normal, probe, vec4(normal, distance));
}

#elif defined(MODE_FILTER)

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

layout(set = 0, binding = 13) uniform texture2D probe_irradiance;
layout(set = 0, binding = 14) uniform texture2D probe_position;
layout(set = 0, binding = 15) uniform texture2D probe_normal;
layout(rgba16f, set = 0, binding = 17) uniform restrict writeonly image2D filtered_irradiance;

void main() {
	ivec2 probe = ivec2(gl_GlobalInvocationID.xy);
	if (any(greaterThanEqual(probe, params.probes.xy))) {
		return;
	}
	vec4 pp = texelFetch(sampler2D(probe_position, linear_sampler), probe, 0);
	vec4 center = texelFetch(sampler2D(probe_irradiance, linear_sampler), probe, 0);
	if (pp.w == 0.0) {
		imageStore(filtered_irradiance, probe, center);
		return;
	}
	vec4 pn = texelFetch(sampler2D(probe_normal, linear_sampler), probe, 0);
	float reach = params.footprint.x * pn.w * float(params.probes.z) + 0.02;
	vec3 sum = vec3(0.0);
	float wsum = 0.0;
	for (int y = -2; y <= 2; y++) {
		for (int x = -2; x <= 2; x++) {
			ivec2 q = probe + ivec2(x, y);
			if (any(lessThan(q, ivec2(0))) || any(greaterThanEqual(q, params.probes.xy))) {
				continue;
			}
			vec4 qp = texelFetch(sampler2D(probe_position, linear_sampler), q, 0);
			if (qp.w == 0.0) {
				continue;
			}
			vec3 qn = texelFetch(sampler2D(probe_normal, linear_sampler), q, 0).xyz;
			float plane = abs(dot(qp.xyz - pp.xyz, pn.xyz)) / reach;
			float w = max(0.0, dot(qn, pn.xyz) - 0.5) * exp(-plane * plane * 4.0) * exp(-float(x * x + y * y) / 4.5);
			vec4 qi = texelFetch(sampler2D(probe_irradiance, linear_sampler), q, 0);
			w *= qi.a; // probes with more frames behind them weigh more
			sum += qi.rgb * w;
			wsum += w;
		}
	}
	imageStore(filtered_irradiance, probe, vec4(wsum > 0.0 ? sum / wsum : center.rgb, center.a));
}

#elif defined(MODE_RESOLVE)

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

layout(set = 0, binding = 13) uniform texture2D probe_irradiance;
layout(set = 0, binding = 14) uniform texture2D probe_position;
layout(set = 0, binding = 15) uniform texture2D probe_normal;
layout(rgba16f, set = 0, binding = 16) uniform restrict image2D ambient_buffer; // SDFGI's

void main() {
	ivec2 at = ivec2(gl_GlobalInvocationID.xy);
	if (any(greaterThanEqual(at, params.screen.zw))) {
		return;
	}
	// The internal pixel of this texel of the ambient buffer (half the size with gi/use_half_resolution).
	ivec2 pixel = min(ivec2((vec2(at) + 0.5) * vec2(params.screen.xy) / vec2(params.screen.zw)), params.screen.xy - 1);
	vec3 pos, normal;
	float distance;
	if (!surface_at(pixel, pos, normal, distance)) {
		return;
	}
	int tile = params.probes.z;
	// The 2 x 2 probes around the pixel (by their tiles' centres), weighed by the bilinear weights, how far each lies from
	// the pixel's plane and how its normal agrees.
	vec2 g = (vec2(pixel) + 0.5) / float(tile) - 0.5;
	ivec2 g0 = ivec2(floor(g));
	vec2 f = g - vec2(g0);
	float reach = params.footprint.x * distance * float(tile) + 0.02;
	vec3 sum = vec3(0.0);
	float wsum = 0.0;
	for (int i = 0; i < 4; i++) {
		ivec2 o = ivec2(i & 1, i >> 1);
		ivec2 p = clamp(g0 + o, ivec2(0), params.probes.xy - 1);
		vec4 pp = texelFetch(sampler2D(probe_position, linear_sampler), p, 0);
		if (pp.w == 0.0) {
			continue;
		}
		vec3 pn = texelFetch(sampler2D(probe_normal, linear_sampler), p, 0).xyz;
		vec2 b2 = mix(1.0 - f, f, vec2(o));
		float plane = abs(dot(pp.xyz - pos, normal)) / reach;
		float w = b2.x * b2.y * max(0.0, dot(pn, normal) - 0.25) * exp(-plane * plane * 4.0);
		if (w <= 1e-5) {
			continue;
		}
		sum += texelFetch(sampler2D(probe_irradiance, linear_sampler), p, 0).rgb * w;
		wsum += w;
	}
	if (wsum <= 1e-4) {
		return; // no probe on this surface nearby: SDFGI's own light stays
	}
	vec4 sdfgi = imageLoad(ambient_buffer, at);
	float confidence = clamp(wsum * 8.0, 0.0, 1.0);
	imageStore(ambient_buffer, at, vec4(mix(sdfgi.rgb, sum / wsum, confidence), max(sdfgi.a, confidence)));
}

#endif
