#[compute]

#version 460

#VERSION_DEFINES

#if defined(MODE_TRACE) || defined(MODE_REFERENCE)
#extension GL_EXT_ray_query : require
#if defined(FELLOW_POSITION_FETCH)
#extension GL_EXT_ray_tracing_position_fetch : require
#endif
#endif

// FellowEngine dynamic diffuse global illumination (docs/decisions/0083-ddgi.md), after Majercik et al., "Dynamic
// Diffuse Global Illumination with Ray-Traced Irradiance Fields" (JCGT 2019). The probes of a volume around the camera
// trace rays against the frame's ray tracing scene (decision 0080) every frame; the hits are lit by the sun (with a
// shadow ray), by the volume itself (the previous frame's irradiance: multiple bounces) and by their emission; misses
// see the environment's ambient light (the sky). The opaque and transparent colour passes read the probes in place of
// the environment's ambient light (scene shader, fellow_ddgi_inc.glsl).
//
// A frame updates the probes whose index is the frame's phase modulo the update interval (setting gi_update_interval).
// MODE_TRACE: one invocation per ray of those probes: its radiance (RGB) and distance (A; negative for a back face).
// MODE_DISTANCE: one workgroup per probe updated (16 x 16 texels): the rays' distances around each texel's direction
// (mean, mean square), blended with the previous values; the probe's relocation (gi_probe_relocation).
// MODE_IRRADIANCE: one workgroup per probe (8 x 8 texels): for those updated, the rays' cosine-weighted mean radiance,
// blended, the probe's state (active: not inside geometry) in A, a slot whose cell changed starting over; the others
// only check their cell: one the volume moved to a new cell is marked inactive (not read) until its update.
// MODE_SCREEN: per pixel of the depth pre-pass, the volume's irradiance and coverage at its position and normal (the
// opaque colour passes read it instead of looking the probes up: no extra registers in the scene shader).
// MODE_REFERENCE (debug/raytracing_gi_reference): in place of MODE_SCREEN, per pixel a path traced from its position
// (cosine-weighted, the sun sampled at every hit, up to n bounces), accumulated while the camera stands still: the
// irradiance the probes approximate, for comparisons.

#include "../oct_inc.glsl"
#include "../fellow_ddgi_inc.glsl"

struct FellowRTInstance {
	vec4 albedo; // rgb: linear albedo, w: 1 if double-sided
	vec4 emission; // rgb: emission in the scene's units, w: metallic (no diffuse reflection)
};

layout(set = 0, binding = 0, std140) uniform Params {
	FellowDDGIVolume volume;
	mat4 ray_rotation; // this frame's random rotation of the rays' directions
	vec4 sun_direction; // xyz towards the light (world), w 1 if there is one
	vec4 sun_color; // rgb in the scene's units (energy, exposure), w 1 if it casts shadows
	vec4 ambient_color; // rgb the environment's ambient colour and energy, w the sky contribution
	vec4 sky_params; // x 1: ambient light, y 1: from the sky's radiance, z the radiance's factor (IBL exposure, energy), w the radiance's level
	mat4 sky_xform; // world directions to the sky's
	vec4 radiance_border; // x border, y 1 - 2 border
	vec4 trace; // x the rays' length, y energy kept per bounce, z irradiance hysteresis, w distance hysteresis
	ivec4 frame; // x frame, y update interval, z this frame's phase (probes with index % interval == phase are updated), w 1: reference restarts
	mat4 inv_projection; // MODE_SCREEN: NDC (x, y, depth * 2 - 1) to view
	mat4 view_to_world;
	ivec4 screen; // xy the screen pass's size, z the reference's bounces
}
params;

#if !defined(MODE_SCREEN) && !defined(MODE_REFERENCE)
layout(rgba16f, set = 0, binding = 1) uniform restrict image2D ray_data; // x ray, y probe (linear slot index)
#endif

vec3 fellow_ddgi_ray_direction(int p_index, int p_count) {
	// Spherical Fibonacci, rotated.
	const float golden = 2.39996322972865332; // pi * (3 - sqrt(5))
	float z = 1.0 - (2.0 * float(p_index) + 1.0) / float(p_count);
	float r = sqrt(max(0.0, 1.0 - z * z));
	float phi = float(p_index) * golden;
	return normalize(mat3(params.ray_rotation) * vec3(r * cos(phi), r * sin(phi), z));
}

ivec3 fellow_ddgi_slot_of_index(int p_index) {
	ivec3 c = params.volume.counts.xyz;
	return ivec3(p_index % c.x, (p_index / c.x) % c.y, p_index / (c.x * c.y));
}

// The probe updated by row / workgroup p_row this frame.
int fellow_ddgi_updated_probe(int p_row) {
	return p_row * params.frame.y + params.frame.z;
}

ivec3 fellow_ddgi_cell_of_slot(ivec3 p_slot) {
	// The cell in [base, base + counts) whose slot this is.
	ivec3 c = params.volume.counts.xyz;
	ivec3 b = params.volume.base_cell.xyz;
	return b + ((p_slot - b) % c + c) % c;
}

#if defined(MODE_TRACE)

layout(local_size_x = 64, local_size_y = 1, local_size_z = 1) in;

layout(set = 0, binding = 2) uniform accelerationStructureEXT tlas;
layout(set = 0, binding = 3) uniform texture2D irradiance_atlas;
layout(set = 0, binding = 4) uniform texture2D distance_atlas;
layout(set = 0, binding = 5) uniform sampler linear_sampler;
layout(set = 0, binding = 6, std430) restrict readonly buffer Instances {
	FellowRTInstance data[];
}
instances;
#if defined(FELLOW_RADIANCE_ARRAY)
layout(set = 0, binding = 7) uniform texture2DArray radiance_octmap;
#else
layout(set = 0, binding = 7) uniform texture2D radiance_octmap;
#endif

vec3 fellow_ddgi_ambient(vec3 p_dir) {
	// What the environment's ambient light gives in a direction (the scene shader's ambient, per direction).
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

void main() {
	int ray = int(gl_GlobalInvocationID.x);
	int probe = fellow_ddgi_updated_probe(int(gl_GlobalInvocationID.y));
	int rays = params.volume.counts.w;
	ivec3 counts = params.volume.counts.xyz;
	if (ray >= rays || probe >= counts.x * counts.y * counts.z) {
		return;
	}
	ivec3 slot = fellow_ddgi_slot_of_index(probe);
	vec3 origin = vec3(fellow_ddgi_cell_of_slot(slot)) * params.volume.spacing.x + fellow_ddgi_probe_offset(distance_atlas, linear_sampler, slot, counts);
	vec3 dir = fellow_ddgi_ray_direction(ray, rays);

	rayQueryEXT rq;
	rayQueryInitializeEXT(rq, tlas, gl_RayFlagsOpaqueEXT, 0x01, origin, 0.0, dir, params.trace.x);
	while (rayQueryProceedEXT(rq)) {
	}
	if (rayQueryGetIntersectionTypeEXT(rq, true) == gl_RayQueryCommittedIntersectionNoneEXT) {
		imageStore(ray_data, ivec2(ray, probe), vec4(fellow_ddgi_ambient(dir), 1e4));
		return;
	}
	float t = rayQueryGetIntersectionTEXT(rq, true);
	FellowRTInstance inst = instances.data[rayQueryGetIntersectionInstanceCustomIndexEXT(rq, true)];
	bool front = rayQueryGetIntersectionFrontFaceEXT(rq, true);
	if (!front && inst.albedo.w == 0.0) {
		// A back face: the probe sees the inside of a closed mesh (DDGI's convention: a shortened negative distance).
		imageStore(ray_data, ivec2(ray, probe), vec4(0.0, 0.0, 0.0, -t * 0.2));
		return;
	}
	// The back face of a double-sided surface is a surface (it is lit below), but it also counts towards telling a probe
	// inside a double-sided mesh (walls and columns modelled double-sided): its distance is stored negative too, with its
	// radiance (the irradiance pass adds the radiance of negative rays that have any).
	float stored_t = front ? t : -t;
	vec3 n;
#if defined(FELLOW_POSITION_FETCH)
	vec3 tri[3];
	rayQueryGetIntersectionTriangleVertexPositionsEXT(rq, true, tri);
	mat4x3 o2w = rayQueryGetIntersectionObjectToWorldEXT(rq, true);
	// The triangle in world space, then its normal: no inverse of the instance transform, which is singular for a
	// compressed flat mesh (its box, and so the transform's scale, is 0 thick: Sponza's floors). A degenerate triangle
	// takes the reversed ray (a NaN here spread through the probes' bounces: black areas in Sponza on the RTX).
	vec3 wn = cross(mat3(o2w) * (tri[1] - tri[0]), mat3(o2w) * (tri[2] - tri[0]));
	float wl = dot(wn, wn);
	n = wl > 1e-20 ? wn * inversesqrt(wl) : -dir;
	if (dot(n, dir) > 0.0) {
		n = -n;
	}
#else
	n = -dir;
#endif
	vec3 hit = origin + dir * t;
	vec3 light = vec3(0.0);
	if (params.sun_direction.w != 0.0) {
		float ndl = dot(n, params.sun_direction.xyz);
		if (ndl > 0.0) {
			float visible = 1.0;
			if (params.sun_color.w != 0.0) {
				rayQueryEXT sq;
				rayQueryInitializeEXT(sq, tlas, gl_RayFlagsTerminateOnFirstHitEXT | gl_RayFlagsOpaqueEXT | gl_RayFlagsCullFrontFacingTrianglesEXT, 0x02, hit + n * 0.01, 0.0, params.sun_direction.xyz, params.trace.x);
				while (rayQueryProceedEXT(sq)) {
				}
				visible = rayQueryGetIntersectionTypeEXT(sq, true) == gl_RayQueryCommittedIntersectionNoneEXT ? 1.0 : 0.0;
			}
			light += params.sun_color.rgb * ndl * visible;
		}
	}
	// Further bounces: the volume's irradiance at the hit (the previous frame's), slightly attenuated.
	float coverage;
	vec3 bounce = fellow_ddgi_irradiance(irradiance_atlas, distance_atlas, linear_sampler, params.volume, hit, n, -dir, coverage);
	light += mix(fellow_ddgi_ambient(n) * 0.5, bounce, clamp(coverage, 0.0, 1.0)) * params.trace.y;
	vec3 radiance = inst.albedo.rgb * (1.0 - inst.emission.w) * light + inst.emission.rgb;
	if (any(isnan(radiance)) || any(isinf(radiance))) {
		radiance = vec3(0.0); // never into the probes: it would spread through every bounce
	}
	if (stored_t < 0.0) {
		radiance = max(radiance, vec3(1e-4)); // marks it as a lit back face (not a single-sided one; a half float keeps 1e-4)
	}
	imageStore(ray_data, ivec2(ray, probe), vec4(radiance, stored_t));
}

#elif defined(MODE_DISTANCE)

layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;

layout(rg16f, set = 0, binding = 2) uniform restrict image2D distance_atlas;
layout(set = 0, binding = 3, std430) restrict readonly buffer ProbeCells {
	ivec4 data[];
}
probe_cells;

// Probe relocation (after RTXGI): from the rays the probe traced, a new offset — through the nearest back face where it
// is inside geometry (a quarter of its rays or more see back faces), away from the nearest surface where that is closer
// than a fifth of the spacing — kept within 0.45 spacings of the cell's position (otherwise the offset stays).
vec3 fellow_ddgi_relocate(vec3 p_offset, int p_probe, int p_rays) {
	float spacing = params.volume.spacing.x;
	float min_front = 0.2 * spacing;
	int backs = 0;
	float closest_back = 1e9;
	vec3 closest_back_dir = vec3(0.0);
	float closest_front = 1e9;
	vec3 closest_front_dir = vec3(0.0);
	float farthest_front = 0.0;
	vec3 farthest_front_dir = vec3(0.0);
	for (int r = 0; r < p_rays; r++) {
		vec4 rd = imageLoad(ray_data, ivec2(r, p_probe));
		vec3 dir = fellow_ddgi_ray_direction(r, p_rays);
		if (rd.a < 0.0) {
			backs++;
			float d = all(equal(rd.rgb, vec3(0.0))) ? -rd.a * 5.0 : -rd.a; // single-sided back faces are stored shortened
			if (d < closest_back) {
				closest_back = d;
				closest_back_dir = dir;
			}
		} else {
			if (rd.a < closest_front) {
				closest_front = rd.a;
				closest_front_dir = dir;
			}
			if (rd.a > farthest_front) {
				farthest_front = rd.a;
				farthest_front_dir = dir;
			}
		}
	}
	vec3 move = vec3(0.0);
	if (float(backs) >= 0.25 * float(p_rays)) {
		move = closest_back_dir * (closest_back + min_front * 0.5);
	} else if (closest_front < min_front) {
		// Along the most open direction when it leads away from the nearest surface, else straight away from it.
		vec3 away = dot(farthest_front_dir, closest_front_dir) <= 0.0 ? farthest_front_dir : -closest_front_dir;
		move = away * min(min_front - closest_front, farthest_front * 0.5);
	}
	vec3 next = p_offset + move;
	return all(lessThanEqual(abs(next), vec3(0.45 * spacing))) ? next : p_offset;
}

void main() {
	int probe = fellow_ddgi_updated_probe(int(gl_WorkGroupID.x));
	ivec3 counts = params.volume.counts.xyz;
	if (probe >= counts.x * counts.y * counts.z) {
		return;
	}
	ivec3 slot = fellow_ddgi_slot_of_index(probe);
	ivec3 cell = fellow_ddgi_cell_of_slot(slot);
	ivec2 texel = ivec2(gl_LocalInvocationID.xy);
	bool same = probe_cells.data[probe].xyz == cell && probe_cells.data[probe].w != 0;
	if (texel == ivec2(0)) {
		// The offset for the probe's next update (this one's rays were traced from the current one). A slot that changed
		// cell starts at the cell's position.
		ivec2 oat = fellow_ddgi_offset_texel(slot, counts);
		vec3 offset = vec3(0.0);
		if (same && params.volume.shading.y != 0.0) {
			offset = vec3(imageLoad(distance_atlas, oat).rg, imageLoad(distance_atlas, oat + ivec2(1, 0)).r);
			offset = fellow_ddgi_relocate(offset, probe, params.volume.counts.w);
		}
		imageStore(distance_atlas, oat, vec4(offset.xy, 0.0, 0.0));
		imageStore(distance_atlas, oat + ivec2(1, 0), vec4(offset.z, 0.0, 0.0, 0.0));
	}
	const float tile = float(FELLOW_DDGI_DISTANCE_TILE);
	vec3 dir = oct_to_vec3_with_border((vec2(texel) + 0.5) / tile, (tile - 2.0) / tile);
	int rays = params.volume.counts.w;
	float max_distance = params.volume.spacing.x * 1.5;
	vec2 sum = vec2(0.0);
	float wsum = 0.0;
	for (int r = 0; r < rays; r++) {
		float w = max(0.0, dot(dir, fellow_ddgi_ray_direction(r, rays)));
		w = pow(w, 50.0);
		if (w <= 0.0) {
			continue;
		}
		float d = min(abs(imageLoad(ray_data, ivec2(r, probe)).a), max_distance);
		sum += vec2(d, d * d) * w;
		wsum += w;
	}
	vec2 result = wsum > 0.0 ? sum / wsum : vec2(max_distance, max_distance * max_distance);
	ivec2 at = fellow_ddgi_tile_origin(slot, params.volume.counts.xyz, FELLOW_DDGI_DISTANCE_TILE) + texel;
	if (same) {
		result = mix(result, imageLoad(distance_atlas, at).rg, params.trace.w);
	}
	imageStore(distance_atlas, at, vec4(result, 0.0, 0.0));
}

#elif defined(MODE_SCREEN)

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

layout(set = 0, binding = 1) uniform sampler2D depth_buffer;
layout(set = 0, binding = 2) uniform sampler2D normal_roughness_buffer;
layout(set = 0, binding = 3) uniform texture2D irradiance_atlas;
layout(set = 0, binding = 4) uniform texture2D distance_atlas;
layout(set = 0, binding = 5) uniform sampler linear_sampler;
layout(rgba16f, set = 0, binding = 6) uniform restrict writeonly image2D dest;

void main() {
	ivec2 pos = ivec2(gl_GlobalInvocationID.xy);
	if (any(greaterThanEqual(pos, params.screen.xy))) {
		return;
	}
	float depth = texelFetch(depth_buffer, pos, 0).r;
	if (depth == 0.0) {
		imageStore(dest, pos, vec4(0.0));
		return;
	}
	vec2 uv = (vec2(pos) + 0.5) / vec2(params.screen.xy);
	vec4 v = params.inv_projection * vec4(uv * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
	vec3 vp = v.xyz / v.w;
	vec3 wp = (params.view_to_world * vec4(vp, 1.0)).xyz;
	// The normal as the scene shader writes it (normal_roughness_compatibility()).
	vec3 vn = normalize(texelFetch(normal_roughness_buffer, pos, 0).xyz * 2.0 - 1.0);
	vec3 wn = normalize(mat3(params.view_to_world) * vn);
	vec3 weye = normalize(mat3(params.view_to_world) * -normalize(vp));
	float coverage;
	vec3 gi = fellow_ddgi_irradiance(irradiance_atlas, distance_atlas, linear_sampler, params.volume, wp, wn, weye, coverage);
	imageStore(dest, pos, vec4(gi, clamp(coverage, 0.0, 1.0)));
}

#elif defined(MODE_REFERENCE)

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

layout(set = 0, binding = 1) uniform sampler2D depth_buffer;
layout(set = 0, binding = 2) uniform sampler2D normal_roughness_buffer;
layout(set = 0, binding = 3) uniform accelerationStructureEXT tlas;
layout(set = 0, binding = 4) uniform sampler linear_sampler;
layout(set = 0, binding = 5, std430) restrict readonly buffer Instances {
	FellowRTInstance data[];
}
instances;
#if defined(FELLOW_RADIANCE_ARRAY)
layout(set = 0, binding = 6) uniform texture2DArray radiance_octmap;
#else
layout(set = 0, binding = 6) uniform texture2D radiance_octmap;
#endif
layout(rgba32f, set = 0, binding = 7) uniform restrict image2D accumulation; // rgb: the paths' sum, a: their count
layout(rgba16f, set = 0, binding = 8) uniform restrict writeonly image2D dest;

uint hash(uint p_x) {
	p_x ^= p_x >> 16;
	p_x *= 0x7feb352du;
	p_x ^= p_x >> 15;
	p_x *= 0x846ca68bu;
	p_x ^= p_x >> 16;
	return p_x;
}

// As fellow_ddgi_ambient() of MODE_TRACE: what a ray that leaves the scene brings.
vec3 environment_ambient(vec3 p_dir) {
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

vec3 view_position(ivec2 p_pos, float p_depth) {
	vec2 uv = (vec2(p_pos) + 0.5) / vec2(params.screen.xy);
	vec4 v = params.inv_projection * vec4(uv * 2.0 - 1.0, p_depth * 2.0 - 1.0, 1.0);
	return v.xyz / v.w;
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

void main() {
	ivec2 pos = ivec2(gl_GlobalInvocationID.xy);
	if (any(greaterThanEqual(pos, params.screen.xy))) {
		return;
	}
	float depth = texelFetch(depth_buffer, pos, 0).r;
	if (depth == 0.0) {
		imageStore(dest, pos, vec4(0.0));
		return;
	}
	vec3 vp = view_position(pos, depth);
	vec3 wp = (params.view_to_world * vec4(vp, 1.0)).xyz;
	vec3 n = normalize(mat3(params.view_to_world) * normalize(texelFetch(normal_roughness_buffer, pos, 0).xyz * 2.0 - 1.0));
	// The geometric normal from the depth buffer (as RTAO's): the origin lifted along it, the paths kept above it.
	vec3 dx, dy;
	{
		ivec2 lo = max(pos - 1, ivec2(0));
		ivec2 hi = min(pos + 1, params.screen.xy - 1);
		vec3 pl = view_position(ivec2(lo.x, pos.y), texelFetch(depth_buffer, ivec2(lo.x, pos.y), 0).r);
		vec3 pr = view_position(ivec2(hi.x, pos.y), texelFetch(depth_buffer, ivec2(hi.x, pos.y), 0).r);
		vec3 pd = view_position(ivec2(pos.x, lo.y), texelFetch(depth_buffer, ivec2(pos.x, lo.y), 0).r);
		vec3 pu = view_position(ivec2(pos.x, hi.y), texelFetch(depth_buffer, ivec2(pos.x, hi.y), 0).r);
		dx = abs(pr.z - vp.z) < abs(vp.z - pl.z) ? pr - vp : vp - pl;
		dy = abs(pu.z - vp.z) < abs(vp.z - pd.z) ? pu - vp : vp - pd;
	}
	vec3 gvn = cross(dy, dx);
	gvn = dot(gvn, gvn) > 1e-12 ? normalize(gvn) : -normalize(vp);
	if (dot(gvn, vp) > 0.0) {
		gvn = -gvn;
	}
	vec3 ng = normalize(mat3(params.view_to_world) * gvn);
	vec3 origin = wp + ng * (0.003 + 0.0015 * length(vp));
	uint seed = hash(uint(pos.x) + hash(uint(pos.y) + hash(uint(params.frame.x))));

	vec3 radiance = vec3(0.0);
	vec3 throughput = vec3(1.0);
	int bounces = max(params.screen.z, 1);
	for (int b = 0; b < bounces; b++) {
		vec2 u = vec2(float(seed & 0xFFFFu), float(seed >> 16)) / 65536.0;
		seed = hash(seed + 0x9e3779b9u);
		vec3 dir = cosine_direction(n, u);
		float below = dot(dir, ng);
		if (below < 0.0) {
			dir = normalize(dir - 2.0 * below * ng); // mirrored above the surface
		}
		rayQueryEXT rq;
		rayQueryInitializeEXT(rq, tlas, gl_RayFlagsOpaqueEXT, 0x01, origin, 0.0, dir, params.trace.x);
		while (rayQueryProceedEXT(rq)) {
		}
		if (rayQueryGetIntersectionTypeEXT(rq, true) == gl_RayQueryCommittedIntersectionNoneEXT) {
			radiance += throughput * environment_ambient(dir);
			break;
		}
		float t = rayQueryGetIntersectionTEXT(rq, true);
		FellowRTInstance inst = instances.data[rayQueryGetIntersectionInstanceCustomIndexEXT(rq, true)];
		if (!rayQueryGetIntersectionFrontFaceEXT(rq, true) && inst.albedo.w == 0.0) {
			break; // the inside of a closed mesh: no light
		}
		vec3 hn;
#if defined(FELLOW_POSITION_FETCH)
		vec3 tri[3];
		rayQueryGetIntersectionTriangleVertexPositionsEXT(rq, true, tri);
		mat4x3 o2w = rayQueryGetIntersectionObjectToWorldEXT(rq, true);
		vec3 wn = cross(mat3(o2w) * (tri[1] - tri[0]), mat3(o2w) * (tri[2] - tri[0]));
		float wl = dot(wn, wn);
		hn = wl > 1e-20 ? wn * inversesqrt(wl) : -dir;
		if (dot(hn, dir) > 0.0) {
			hn = -hn;
		}
#else
		hn = -dir;
#endif
		vec3 hit = origin + dir * t;
		radiance += throughput * inst.emission.rgb;
		vec3 albedo = inst.albedo.rgb * (1.0 - inst.emission.w);
		if (params.sun_direction.w != 0.0) {
			float ndl = dot(hn, params.sun_direction.xyz);
			if (ndl > 0.0) {
				float visible = 1.0;
				if (params.sun_color.w != 0.0) {
					rayQueryEXT sq;
					rayQueryInitializeEXT(sq, tlas, gl_RayFlagsTerminateOnFirstHitEXT | gl_RayFlagsOpaqueEXT | gl_RayFlagsCullFrontFacingTrianglesEXT, 0x02, hit + hn * 0.01, 0.0, params.sun_direction.xyz, params.trace.x);
					while (rayQueryProceedEXT(sq)) {
					}
					visible = rayQueryGetIntersectionTypeEXT(sq, true) == gl_RayQueryCommittedIntersectionNoneEXT ? 1.0 : 0.0;
				}
				radiance += throughput * albedo * params.sun_color.rgb * ndl * visible;
			}
		}
		throughput *= albedo;
		if (max(throughput.r, max(throughput.g, throughput.b)) < 0.001) {
			break;
		}
		origin = hit + hn * 0.01;
		n = hn;
		ng = hn;
	}
	if (any(isnan(radiance)) || any(isinf(radiance))) {
		radiance = vec3(0.0);
	}
	vec4 acc = params.frame.w != 0 ? vec4(0.0) : imageLoad(accumulation, pos);
	acc += vec4(radiance, 1.0);
	imageStore(accumulation, pos, acc);
	imageStore(dest, pos, vec4(acc.rgb / acc.a, 1.0));
}

#else // MODE_IRRADIANCE

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

layout(rgba16f, set = 0, binding = 2) uniform restrict image2D irradiance_atlas;
layout(set = 0, binding = 3, std430) restrict buffer ProbeCells {
	ivec4 data[];
}
probe_cells;

void main() {
	int probe = int(gl_WorkGroupID.x);
	ivec3 slot = fellow_ddgi_slot_of_index(probe);
	ivec3 cell = fellow_ddgi_cell_of_slot(slot);
	ivec2 texel = ivec2(gl_LocalInvocationID.xy);
	uint index = gl_LocalInvocationIndex;
	ivec2 at = fellow_ddgi_tile_origin(slot, params.volume.counts.xyz, FELLOW_DDGI_IRRADIANCE_TILE) + texel;
	if (probe % params.frame.y != params.frame.z) {
		// Not updated this frame. If the volume moved this slot to a new cell, its light is the old cell's: inactive
		// until its update (which then starts over).
		ivec4 known = probe_cells.data[probe];
		if (known.w != 0 && known.xyz != cell) {
			imageStore(irradiance_atlas, at, vec4(imageLoad(irradiance_atlas, at).rgb, 0.0));
			barrier();
			if (index == 0u) {
				probe_cells.data[probe] = ivec4(known.xyz, 0);
			}
		}
		return;
	}
	const float tile = float(FELLOW_DDGI_IRRADIANCE_TILE);
	vec3 dir = oct_to_vec3_with_border((vec2(texel) + 0.5) / tile, (tile - 2.0) / tile);
	int rays = params.volume.counts.w;
	vec3 sum = vec3(0.0);
	float wsum = 0.0;
	uint backfaces = 0u; // the same for every texel of the probe (they read the same rays)
	for (int r = 0; r < rays; r++) {
		vec4 rd = imageLoad(ray_data, ivec2(r, probe));
		if (rd.a < 0.0) {
			backfaces++;
			if (all(equal(rd.rgb, vec3(0.0)))) {
				continue; // a single-sided back face: no light
			}
		}
		float w = max(0.0, dot(dir, fellow_ddgi_ray_direction(r, rays)));
		sum += rd.rgb * w;
		wsum += w;
	}
	vec3 irradiance = wsum > 0.0 ? sum / wsum : vec3(0.0);
	// Inside geometry: a quarter of the rays or more see back faces (the probe is skipped by the lookups).
	float probe_active = float(backfaces) < 0.25 * float(rays) ? 1.0 : 0.0;
	bool same = probe_cells.data[probe].xyz == cell && probe_cells.data[probe].w != 0;
	if (same) {
		vec3 previous = imageLoad(irradiance_atlas, at).rgb;
		if (!any(isnan(previous)) && !any(isinf(previous))) {
			irradiance = mix(irradiance, previous, params.trace.z);
		}
	}
	imageStore(irradiance_atlas, at, vec4(irradiance, probe_active));
	barrier();
	if (index == 0u) {
		probe_cells.data[probe] = ivec4(cell, 1);
	}
}

#endif
