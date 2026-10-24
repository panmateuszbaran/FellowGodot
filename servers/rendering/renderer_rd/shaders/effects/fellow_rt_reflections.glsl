#[compute]

#version 460

#VERSION_DEFINES

#if defined(MODE_TRACE)
#extension GL_EXT_ray_query : require
#if defined(FELLOW_POSITION_FETCH)
#extension GL_EXT_ray_tracing_position_fetch : require
#endif
#endif

// FellowEngine ray-traced reflections (docs/decisions/0084-rt-reflections.md). Where the environment enables SSR and
// rendering/fellow/raytracing/reflections is on, per pixel of the depth pre-pass up to max_roughness, a ray in the
// direction the surface's GGX lobe reflects the view into (importance-sampled visible normals) against the frame's ray
// tracing scene (decision 0080). Hits are lit as DDGI's (decision 0083): albedo x (the sun with a shadow ray + the DDGI
// volume's irradiance, or the ambient light) + emission; misses see the sky. The opaque colour passes read the result in
// place of the reflection probes' and the sky's indirect specular light.
//
// MODE_TRACE: the ray (RGB the radiance it brings, A 1 where traced, its hit distance in the history's z).
// MODE_TEMPORAL: blended with the previous frames at the reprojected position (fewer frames for smooth surfaces).
// MODE_DENOISE: a depth- and normal-aware blur whose radius grows with roughness, into the buffer the colour pass reads.

#include "../oct_inc.glsl"
#include "../fellow_ddgi_inc.glsl"

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

struct FellowRTInstance {
	vec4 albedo; // rgb: linear albedo, w: 1 if double-sided
	vec4 emission; // rgb: emission in the scene's units, w: metallic (no diffuse reflection)
};

layout(set = 0, binding = 0, std140) uniform Params {
	mat4 inv_projection; // NDC (x, y, depth * 2 - 1) to view
	mat4 view_to_world;
	mat4 world_to_prev_clip;
	FellowDDGIVolume volume; // base_cell.w 0: no DDGI
	vec4 sun_direction; // w 1 if there is one
	vec4 sun_color; // w 1 if shadowed
	vec4 ambient_color; // rgb, w sky contribution
	vec4 sky_params; // x ambient on, y sky radiance on, z radiance factor, w radiance's last level
	mat4 sky_xform;
	vec4 radiance_border;
	ivec2 size;
	uint frame;
	float max_roughness;
	float ray_length;
	uint history_valid;
	float interleave_roughness; // rougher surfaces: one pixel of each 2 x 2 traced per frame (traced_offset())
	uint pad0;
	FellowDDGIVolume outer; // DDGI's coarser cascade (base_cell.w 0: none)
}
params;

ivec2 traced_offset() {
	const ivec2 order[4] = ivec2[](ivec2(0, 0), ivec2(1, 1), ivec2(1, 0), ivec2(0, 1));
	return order[params.frame & 3u];
}

layout(set = 0, binding = 1) uniform sampler2D depth_buffer;
layout(set = 0, binding = 2) uniform sampler2D normal_roughness_buffer;

vec3 view_position(ivec2 p_pos, float p_depth) {
	vec2 uv = (vec2(p_pos) + 0.5) / vec2(params.size);
	vec4 v = params.inv_projection * vec4(uv * 2.0 - 1.0, p_depth * 2.0 - 1.0, 1.0);
	return v.xyz / v.w;
}

// The normal-roughness buffer as the scene shader writes it (normal_roughness_compatibility()).
vec4 normal_roughness(ivec2 p_pos) {
	vec4 nr = texelFetch(normal_roughness_buffer, p_pos, 0);
	float roughness = nr.w;
	if (roughness > 0.5) {
		roughness = 1.0 - roughness;
	}
	roughness /= (127.0 / 255.0);
	return vec4(normalize(nr.xyz * 2.0 - 1.0), roughness);
}

#if defined(MODE_TRACE)

layout(set = 0, binding = 3) uniform accelerationStructureEXT tlas;
layout(rgba16f, set = 0, binding = 4) uniform restrict writeonly image2D raw_reflection;
layout(set = 0, binding = 5) uniform texture2D irradiance_atlas;
layout(set = 0, binding = 6) uniform texture2D distance_atlas;
layout(set = 0, binding = 7) uniform sampler linear_sampler;
layout(set = 0, binding = 8, std430) restrict readonly buffer Instances {
	FellowRTInstance data[];
}
instances;
#if defined(FELLOW_RADIANCE_ARRAY)
layout(set = 0, binding = 9) uniform texture2DArray radiance_octmap;
#else
layout(set = 0, binding = 9) uniform texture2D radiance_octmap;
#endif
layout(set = 0, binding = 10) uniform texture2D outer_irradiance_atlas;
layout(set = 0, binding = 11) uniform texture2D outer_distance_atlas;

uint hash(uint p_x) {
	p_x ^= p_x >> 16;
	p_x *= 0x7feb352du;
	p_x ^= p_x >> 15;
	p_x *= 0x846ca68bu;
	p_x ^= p_x >> 16;
	return p_x;
}

vec3 environment(vec3 p_dir, float p_level) {
	if (params.sky_params.x == 0.0) {
		return vec3(0.0);
	}
	vec3 ambient = params.ambient_color.rgb;
	if (params.sky_params.y != 0.0) {
		vec2 uv = vec3_to_oct_with_border(mat3(params.sky_xform) * p_dir, params.radiance_border.xy);
#if defined(FELLOW_RADIANCE_ARRAY)
		vec3 sky = textureLod(sampler2DArray(radiance_octmap, linear_sampler), vec3(uv, floor(p_level + 0.5)), 0.0).rgb;
#else
		vec3 sky = textureLod(sampler2D(radiance_octmap, linear_sampler), uv, p_level).rgb;
#endif
		// Reflections see the sky itself (not the ambient light's colour): its radiance, at the lobe's blur.
		return sky * params.sky_params.z;
	}
	return ambient;
}

// GGX visible normal sampling (Heitz 2018), in the frame where the normal is +Z.
vec3 sample_ggx_vndf(vec3 p_v, float p_alpha, vec2 p_u) {
	vec3 vh = normalize(vec3(p_alpha * p_v.x, p_alpha * p_v.y, p_v.z));
	float lensq = vh.x * vh.x + vh.y * vh.y;
	vec3 t1 = lensq > 0.0 ? vec3(-vh.y, vh.x, 0.0) * inversesqrt(lensq) : vec3(1.0, 0.0, 0.0);
	vec3 t2 = cross(vh, t1);
	float r = sqrt(p_u.x);
	float phi = 6.28318530718 * p_u.y;
	float s1 = r * cos(phi);
	float s2 = r * sin(phi);
	float s = 0.5 * (1.0 + vh.z);
	s2 = (1.0 - s) * sqrt(max(0.0, 1.0 - s1 * s1)) + s * s2;
	vec3 nh = s1 * t1 + s2 * t2 + sqrt(max(0.0, 1.0 - s1 * s1 - s2 * s2)) * vh;
	return normalize(vec3(p_alpha * nh.x, p_alpha * nh.y, max(0.0, nh.z)));
}

void trace_pixel(ivec2 pos, float depth, vec4 nr) {
	vec3 vp = view_position(pos, depth);
	vec3 wp = (params.view_to_world * vec4(vp, 1.0)).xyz;
	vec3 n = normalize(mat3(params.view_to_world) * nr.xyz);
	vec3 v = normalize(mat3(params.view_to_world) * -vp); // towards the eye
	float alpha = nr.w * nr.w;

	vec3 dir = reflect(-v, n);
	if (alpha > 0.0001) {
		// A basis around the normal.
		float sgn = n.z >= 0.0 ? 1.0 : -1.0;
		float a = -1.0 / (sgn + n.z);
		float b = n.x * n.y * a;
		vec3 t1 = vec3(1.0 + sgn * n.x * n.x * a, sgn * b, -sgn * n.x);
		vec3 t2 = vec3(b, sgn + n.y * n.y * a, -n.y);
		vec3 vl = vec3(dot(v, t1), dot(v, t2), dot(v, n));
		uint seed = hash(uint(pos.x) + hash(uint(pos.y) + hash(params.frame)));
		vec2 u = fract(vec2(float(seed & 0xFFFFu), float(seed >> 16)) / 65536.0 + float(params.frame) * vec2(0.7548776662, 0.5698402910));
		vec3 h = sample_ggx_vndf(vl, alpha, u);
		vec3 hw = normalize(t1 * h.x + t2 * h.y + n * h.z);
		dir = reflect(-v, hw);
		if (dot(dir, n) <= 0.0) {
			dir = reflect(-v, n); // below the surface: the mirror direction
		}
	}
	// The geometric normal from the depth buffer (the neighbour on the nearer side along each axis), as RTAO's: the origin
	// is lifted along it and the ray kept above it, so a normal map or smooth shading normals on a faceted mesh do not send
	// the ray into the surface it leaves (it would see that surface, lit, instead of what it reflects).
	vec3 dx, dy;
	{
		ivec2 lo = max(pos - 1, ivec2(0));
		ivec2 hi = min(pos + 1, params.size - 1);
		vec3 pl = view_position(ivec2(lo.x, pos.y), texelFetch(depth_buffer, ivec2(lo.x, pos.y), 0).r);
		vec3 pr = view_position(ivec2(hi.x, pos.y), texelFetch(depth_buffer, ivec2(hi.x, pos.y), 0).r);
		vec3 pd = view_position(ivec2(pos.x, lo.y), texelFetch(depth_buffer, ivec2(pos.x, lo.y), 0).r);
		vec3 pu = view_position(ivec2(pos.x, hi.y), texelFetch(depth_buffer, ivec2(pos.x, hi.y), 0).r);
		dx = abs(pr.z - vp.z) < abs(vp.z - pl.z) ? pr - vp : vp - pl;
		dy = abs(pu.z - vp.z) < abs(vp.z - pd.z) ? pu - vp : vp - pd;
	}
	vec3 gvn = cross(dy, dx);
	gvn = dot(gvn, gvn) > 1e-12 ? normalize(gvn) : nr.xyz;
	if (dot(gvn, vp) > 0.0) {
		gvn = -gvn; // facing the camera
	}
	vec3 ng = normalize(mat3(params.view_to_world) * gvn);
	float below = dot(dir, ng);
	if (below < 0.0) {
		dir = normalize(dir - 2.0 * below * ng); // mirrored above the surface
	}
	float dist = length(vp);
	vec3 origin = wp + ng * (0.003 + 0.0015 * dist);

	rayQueryEXT rq;
	rayQueryInitializeEXT(rq, tlas, gl_RayFlagsOpaqueEXT | gl_RayFlagsCullBackFacingTrianglesEXT, 0x01, origin, 0.0, dir, params.ray_length);
	while (rayQueryProceedEXT(rq)) {
	}
	vec3 radiance;
	float hit_t = params.ray_length;
	if (rayQueryGetIntersectionTypeEXT(rq, true) == gl_RayQueryCommittedIntersectionNoneEXT) {
		radiance = environment(dir, params.sky_params.w * sqrt(nr.w));
	} else {
		hit_t = rayQueryGetIntersectionTEXT(rq, true);
		FellowRTInstance inst = instances.data[rayQueryGetIntersectionInstanceCustomIndexEXT(rq, true)];
		vec3 hn;
#if defined(FELLOW_POSITION_FETCH)
		vec3 tri[3];
		rayQueryGetIntersectionTriangleVertexPositionsEXT(rq, true, tri);
		mat4x3 o2w = rayQueryGetIntersectionObjectToWorldEXT(rq, true);
		// In world space (no inverse: singular for compressed flat meshes; see fellow_ddgi.glsl).
		vec3 wn = cross(mat3(o2w) * (tri[1] - tri[0]), mat3(o2w) * (tri[2] - tri[0]));
		float wl = dot(wn, wn);
		hn = wl > 1e-20 ? wn * inversesqrt(wl) : -dir;
		if (dot(hn, dir) > 0.0) {
			hn = -hn;
		}
#else
		hn = -dir;
#endif
		vec3 hit = origin + dir * hit_t;
		vec3 light = vec3(0.0);
		if (params.sun_direction.w != 0.0) {
			float ndl = dot(hn, params.sun_direction.xyz);
			if (ndl > 0.0) {
				float visible = 1.0;
				if (params.sun_color.w != 0.0) {
					rayQueryEXT sq;
					rayQueryInitializeEXT(sq, tlas, gl_RayFlagsTerminateOnFirstHitEXT | gl_RayFlagsOpaqueEXT | gl_RayFlagsCullFrontFacingTrianglesEXT, 0x02, hit + hn * 0.01, 0.0, params.sun_direction.xyz, params.ray_length);
					while (rayQueryProceedEXT(sq)) {
					}
					visible = rayQueryGetIntersectionTypeEXT(sq, true) == gl_RayQueryCommittedIntersectionNoneEXT ? 1.0 : 0.0;
				}
				light += params.sun_color.rgb * ndl * visible;
			}
		}
		float coverage = 0.0;
		vec3 gi = fellow_ddgi_irradiance_cascades(irradiance_atlas, distance_atlas, params.volume, outer_irradiance_atlas, outer_distance_atlas, params.outer, linear_sampler, hit, hn, -dir, coverage);
		vec3 ambient = params.ambient_color.rgb;
		if (params.sky_params.y != 0.0) {
			ambient = mix(ambient, environment(hn, params.sky_params.w), params.ambient_color.w);
		}
		light += mix(params.sky_params.x != 0.0 ? ambient : vec3(0.0), gi, clamp(coverage, 0.0, 1.0));
		radiance = inst.albedo.rgb * (1.0 - inst.emission.w) * light + inst.emission.rgb;
	}
	if (any(isnan(radiance)) || any(isinf(radiance))) {
		radiance = vec3(0.0);
	}
	imageStore(raw_reflection, pos, vec4(radiance, hit_t));
}

// A thread per 2 x 2 block: its smooth pixels are traced every frame, its rough ones (interleaved) only the pixel of
// traced_offset(). (A thread per pixel left three of four lanes idle over rough surfaces.)
void main() {
	ivec2 block = ivec2(gl_GlobalInvocationID.xy);
	for (int k = 0; k < 4; k++) {
		ivec2 pos = block * 2 + ivec2(k & 1, k >> 1);
		if (any(greaterThanEqual(pos, params.size))) {
			continue;
		}
		float depth = texelFetch(depth_buffer, pos, 0).r;
		vec4 nr = normal_roughness(pos);
		if (depth == 0.0 || nr.w > params.max_roughness) {
			imageStore(raw_reflection, pos, vec4(0.0));
			continue;
		}
		if (nr.w > params.interleave_roughness && any(notEqual(pos & 1, traced_offset()))) {
			imageStore(raw_reflection, pos, vec4(0.0, 0.0, 0.0, -1.0)); // not traced this frame (A < 0)
			continue;
		}
		trace_pixel(pos, depth, nr);
	}
}

#elif defined(MODE_TEMPORAL)

layout(set = 0, binding = 3) uniform sampler2D raw_reflection;
layout(set = 0, binding = 4) uniform sampler2D history; // rgb, a: frames accumulated
layout(rgba16f, set = 0, binding = 5) uniform restrict writeonly image2D history_out;

void main() {
	ivec2 pos = ivec2(gl_GlobalInvocationID.xy);
	if (any(greaterThanEqual(pos, params.size))) {
		return;
	}
	float depth = texelFetch(depth_buffer, pos, 0).r;
	vec4 nr = normal_roughness(pos);
	if (depth == 0.0 || nr.w > params.max_roughness) {
		imageStore(history_out, pos, vec4(0.0));
		return;
	}
	vec4 raw4 = texelFetch(raw_reflection, pos, 0);
	// Not traced this frame (interleaved): the history, or where it is lost the 2 x 2 block's traced sample.
	bool fresh = raw4.a >= 0.0;
	if (!fresh) {
		raw4 = texelFetch(raw_reflection, min((pos & ~1) + traced_offset(), params.size - 1), 0);
	}
	vec3 raw = raw4.rgb;
	// Mirror-like surfaces take one sample (exact), rougher ones accumulate up to 16 frames (interleaved ones count only
	// their new samples, up to 8: 32 frames).
	float max_frames = mix(1.0, 16.0, clamp(nr.w * 4.0, 0.0, 1.0));
	if (nr.w > params.interleave_roughness) {
		max_frames = 8.0;
	}
	vec3 result = raw;
	float count = 1.0;
	if (params.history_valid != 0u && max_frames > 1.0) {
		vec3 vp = view_position(pos, depth);
		vec4 clip = params.world_to_prev_clip * (params.view_to_world * vec4(vp, 1.0));
		vec2 prev_uv = (clip.xy / clip.w) * 0.5 + 0.5;
		if (clip.w > 0.0 && all(greaterThanEqual(prev_uv, vec2(0.0))) && all(lessThanEqual(prev_uv, vec2(1.0)))) {
			vec4 h = textureLod(history, prev_uv, 0.0);
			if (h.a > 0.0) {
				if (fresh) {
					float n = min(h.a, max_frames - 1.0);
					result = mix(h.rgb, raw, 1.0 / (n + 1.0));
					count = n + 1.0;
				} else {
					result = h.rgb;
					count = h.a;
				}
			}
		}
	}
	imageStore(history_out, pos, vec4(result, count));
}

#else // MODE_DENOISE

layout(set = 0, binding = 3) uniform sampler2D accumulated;
layout(rgba16f, set = 0, binding = 4) uniform restrict writeonly image2D dest;

void main() {
	ivec2 pos = ivec2(gl_GlobalInvocationID.xy);
	if (any(greaterThanEqual(pos, params.size))) {
		return;
	}
	float depth = texelFetch(depth_buffer, pos, 0).r;
	vec4 nr = normal_roughness(pos);
	if (depth == 0.0 || nr.w > params.max_roughness) {
		imageStore(dest, pos, vec4(0.0));
		return;
	}
	vec4 center = texelFetch(accumulated, pos, 0);
	int radius = int(clamp(nr.w * 8.0, 0.0, 3.0));
	vec3 sum = center.rgb;
	float wsum = 1.0;
	float z = -view_position(pos, depth).z;
	for (int y = -radius; y <= radius; y++) {
		for (int x = -radius; x <= radius; x++) {
			if (x == 0 && y == 0) {
				continue;
			}
			ivec2 q = clamp(pos + ivec2(x, y), ivec2(0), params.size - 1);
			float qd = texelFetch(depth_buffer, q, 0).r;
			if (qd == 0.0) {
				continue;
			}
			vec4 qnr = normal_roughness(q);
			float w = exp(-abs(-view_position(q, qd).z - z) / (0.01 * z + 0.001) * 2.0);
			w *= pow(max(dot(qnr.xyz, nr.xyz), 0.0), 32.0);
			w *= exp(-abs(qnr.w - nr.w) * 10.0);
			sum += texelFetch(accumulated, q, 0).rgb * w;
			wsum += w;
		}
	}
	// Fades out towards max_roughness, where the reflection probes and the sky take over.
	float fade = 1.0 - smoothstep(params.max_roughness * 0.8, params.max_roughness, nr.w);
	imageStore(dest, pos, vec4(sum / wsum, fade));
}

#endif
