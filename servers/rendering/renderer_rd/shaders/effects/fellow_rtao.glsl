#[compute]

#version 460

#VERSION_DEFINES

#if defined(MODE_TRACE)
#extension GL_EXT_ray_query : require
#endif

// FellowEngine ray-traced ambient occlusion (docs/decisions/0081-rtao.md). Where the environment enables SSAO and
// rendering/fellow/raytracing/ambient_occlusion is on, this writes the buffer SSAO would (1: unoccluded), from rays
// against the frame's ray tracing scene (decision 0080) instead of the depth buffer: what is off screen or behind the
// visible surfaces occludes too, and nothing visible that is far behind a surface does.
//
// MODE_TRACE: per pixel, rays over the cosine-weighted hemisphere of its normal, up to the environment's SSAO radius; a
//   hit at distance t occludes by 1 - t / radius.
// MODE_TEMPORAL: the trace blended with the previous frames' result at the pixel's reprojected position (rejected
//   where the depth there does not match), up to MAX_HISTORY frames.
// MODE_DENOISE: a depth- and normal-aware 5 x 5 blur (stride 2) of the accumulated result, with the environment's
//   intensity and power, into the SSAO buffer.

#define MAX_HISTORY 24.0

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

layout(set = 0, binding = 0, std140) uniform Params {
	mat4 inv_projection; // NDC (x, y, depth * 2 - 1) to view
	mat4 view_to_world;
	mat4 world_to_prev_clip;
	ivec2 size;
	uint frame;
	uint ray_count;
	float radius;
	float intensity;
	float power;
	uint history_valid;
	uint debug; // rendering/fellow/debug/raytracing_view: 1 the scene's hit distances from the camera, 2 their match with the depth buffer
	uint pad0;
	uint pad1;
	uint pad2;
}
params;

layout(set = 0, binding = 1) uniform sampler2D depth_buffer;
layout(set = 0, binding = 2) uniform sampler2D normal_roughness_buffer;

vec3 view_position(ivec2 p_pos, float p_depth) {
	vec2 uv = (vec2(p_pos) + 0.5) / vec2(params.size);
	vec4 v = params.inv_projection * vec4(uv * 2.0 - 1.0, p_depth * 2.0 - 1.0, 1.0);
	return v.xyz / v.w;
}

vec3 view_normal(ivec2 p_pos) {
	return normalize(texelFetch(normal_roughness_buffer, p_pos, 0).xyz * 2.0 - 1.0);
}

#if defined(MODE_TRACE)

layout(set = 0, binding = 3) uniform accelerationStructureEXT tlas;
layout(r16f, set = 0, binding = 4) uniform restrict writeonly image2D raw_ao;

uint hash(uint p_x) {
	p_x ^= p_x >> 16;
	p_x *= 0x7feb352du;
	p_x ^= p_x >> 15;
	p_x *= 0x846ca68bu;
	p_x ^= p_x >> 16;
	return p_x;
}

void main() {
	ivec2 pos = ivec2(gl_GlobalInvocationID.xy);
	if (any(greaterThanEqual(pos, params.size))) {
		return;
	}
	float depth = texelFetch(depth_buffer, pos, 0).r;
	if (params.debug != 0u) {
		// The ray tracing scene seen from the camera: a primary ray through the pixel.
		vec3 vnear = view_position(pos, 1.0); // the near plane (reverse Z)
		vec3 cam = params.view_to_world[3].xyz;
		vec3 dir = normalize(mat3(params.view_to_world) * vnear);
		rayQueryEXT rq;
		rayQueryInitializeEXT(rq, tlas, gl_RayFlagsOpaqueEXT | gl_RayFlagsCullBackFacingTrianglesEXT, 0xFF, cam, 0.0, dir, 10000.0);
		while (rayQueryProceedEXT(rq)) {
		}
		float v = 1.0;
		if (rayQueryGetIntersectionTypeEXT(rq, true) != gl_RayQueryCommittedIntersectionNoneEXT) {
			float t = rayQueryGetIntersectionTEXT(rq, true);
			if (params.debug == 1u) {
				v = fract(t / 8.0) * 0.8;
			} else {
				float raster = depth == 0.0 ? 1e9 : length(view_position(pos, depth));
				v = abs(t - raster) < 0.02 * raster + 0.05 ? 0.5 : 0.0; // grey: the scenes agree, black: they do not
			}
		}
		imageStore(raw_ao, pos, vec4(v));
		return;
	}
	if (depth == 0.0) {
		imageStore(raw_ao, pos, vec4(1.0)); // nothing drawn (reverse Z): the sky
		return;
	}
	vec3 vp = view_position(pos, depth);
	vec3 wp = (params.view_to_world * vec4(vp, 1.0)).xyz;
	vec3 n = normalize(mat3(params.view_to_world) * view_normal(pos));
	// The geometric normal from the depth buffer (the neighbour on the nearer side along each axis): the ray origin is
	// lifted along it and rays are kept above it, so faceted surfaces with smooth shading normals do not hit their own
	// neighbouring facets.
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
	gvn = dot(gvn, gvn) > 1e-12 ? normalize(gvn) : view_normal(pos);
	if (dot(gvn, vp) > 0.0) {
		gvn = -gvn; // facing the camera
	}
	vec3 ng = normalize(mat3(params.view_to_world) * gvn);
	// A basis around the normal (Frisvad / Duff et al.).
	float s = n.z >= 0.0 ? 1.0 : -1.0;
	float a = -1.0 / (s + n.z);
	float b = n.x * n.y * a;
	vec3 t1 = vec3(1.0 + s * n.x * n.x * a, s * b, -s * n.x);
	vec3 t2 = vec3(b, s + n.y * n.y * a, -n.y);

	// The ray origin lifted off the surface by a little more than the depth buffer's precision at its distance.
	float dist = length(vp);
	vec3 origin = wp + ng * (0.002 + 0.0015 * dist);
	uint seed = hash(uint(pos.x) + hash(uint(pos.y) + hash(params.frame)));
	// Per-pixel offset into a 2D low-discrepancy sequence (R2), advanced every frame and ray.
	vec2 jitter = vec2(float(seed & 0xFFFFu), float(seed >> 16)) / 65536.0;

	float occlusion = 0.0;
	for (uint i = 0u; i < params.ray_count; i++) {
		float k = float(params.frame * params.ray_count + i);
		vec2 u = fract(jitter + k * vec2(0.7548776662, 0.5698402910));
		// Cosine-weighted hemisphere.
		float r = sqrt(u.x);
		float phi = 6.28318530718 * u.y;
		vec3 dir = normalize(t1 * (r * cos(phi)) + t2 * (r * sin(phi)) + n * sqrt(max(0.0, 1.0 - u.x)));
		float below = dot(dir, ng);
		if (below < 0.0) {
			dir = normalize(dir - 2.0 * below * ng); // mirrored above the surface
		}

		rayQueryEXT rq;
		rayQueryInitializeEXT(rq, tlas, gl_RayFlagsTerminateOnFirstHitEXT | gl_RayFlagsOpaqueEXT | gl_RayFlagsCullBackFacingTrianglesEXT, 0xFF, origin, 0.0, dir, params.radius);
		while (rayQueryProceedEXT(rq)) {
		}
		if (rayQueryGetIntersectionTypeEXT(rq, true) != gl_RayQueryCommittedIntersectionNoneEXT) {
			occlusion += 1.0 - rayQueryGetIntersectionTEXT(rq, true) / params.radius;
		}
	}
	imageStore(raw_ao, pos, vec4(1.0 - occlusion / float(params.ray_count)));
}

#elif defined(MODE_TEMPORAL)

layout(set = 0, binding = 3) uniform sampler2D raw_ao;
layout(set = 0, binding = 4) uniform sampler2D history; // x: AO, y: frames accumulated, z: the view depth it is for
layout(rgba16f, set = 0, binding = 5) uniform restrict writeonly image2D history_out;

void main() {
	ivec2 pos = ivec2(gl_GlobalInvocationID.xy);
	if (any(greaterThanEqual(pos, params.size))) {
		return;
	}
	float ao = texelFetch(raw_ao, pos, 0).r;
	float depth = texelFetch(depth_buffer, pos, 0).r;
	if (depth == 0.0 || params.debug != 0u) {
		imageStore(history_out, pos, vec4(ao, 1.0, 0.0, 0.0));
		return;
	}
	vec3 vp = view_position(pos, depth);
	float view_depth = -vp.z;
	float count = 1.0;
	if (params.history_valid != 0u) {
		vec3 wp = (params.view_to_world * vec4(vp, 1.0)).xyz;
		vec4 clip = params.world_to_prev_clip * vec4(wp, 1.0);
		vec2 prev_uv = (clip.xy / clip.w) * 0.5 + 0.5;
		if (clip.w > 0.0 && all(greaterThanEqual(prev_uv, vec2(0.0))) && all(lessThanEqual(prev_uv, vec2(1.0)))) {
			// The four history texels around the position, each kept only where it was at the expected depth.
			vec2 p = prev_uv * vec2(params.size) - 0.5;
			ivec2 base = ivec2(floor(p));
			vec2 f = p - vec2(base);
			float expected = clip.w;
			vec2 sum = vec2(0.0);
			float wsum = 0.0;
			for (int j = 0; j < 4; j++) {
				ivec2 o = ivec2(j & 1, j >> 1);
				ivec2 q = clamp(base + o, ivec2(0), params.size - 1);
				vec4 h = texelFetch(history, q, 0);
				float w = (o.x == 1 ? f.x : 1.0 - f.x) * (o.y == 1 ? f.y : 1.0 - f.y);
				if (abs(h.z - expected) <= 0.03 * expected + 0.01) {
					sum += h.xy * w;
					wsum += w;
				}
			}
			if (wsum > 0.001) {
				vec2 h = sum / wsum;
				float n = min(h.y, MAX_HISTORY - 1.0);
				ao = mix(h.x, ao, 1.0 / (n + 1.0));
				count = n + 1.0;
			}
		}
	}
	imageStore(history_out, pos, vec4(ao, count, view_depth, 0.0));
}

#else // MODE_DENOISE

layout(set = 0, binding = 3) uniform sampler2D accumulated;
layout(r8, set = 0, binding = 4) uniform restrict writeonly image2D dest;

void main() {
	ivec2 pos = ivec2(gl_GlobalInvocationID.xy);
	if (any(greaterThanEqual(pos, params.size))) {
		return;
	}
	vec4 center = texelFetch(accumulated, pos, 0);
	float depth = texelFetch(depth_buffer, pos, 0).r;
	if (params.debug != 0u) {
		imageStore(dest, pos, vec4(center.x));
		return;
	}
	if (depth == 0.0) {
		imageStore(dest, pos, vec4(1.0));
		return;
	}
	vec3 n = view_normal(pos);
	float z = center.z;
	// Fewer frames accumulated: a wider blur weight (the stride stays 2).
	float sum = 0.0;
	float wsum = 0.0;
	for (int y = -2; y <= 2; y++) {
		for (int x = -2; x <= 2; x++) {
			ivec2 q = clamp(pos + ivec2(x, y) * 2, ivec2(0), params.size - 1);
			vec4 s = texelFetch(accumulated, q, 0);
			float w = exp(-float(x * x + y * y) / (4.0 + 8.0 / center.y));
			w *= exp(-abs(s.z - z) / (0.02 * z + 0.001) * 2.0);
			w *= pow(max(dot(view_normal(q), n), 0.0), 8.0);
			sum += s.x * w;
			wsum += w;
		}
	}
	float ao = wsum > 0.0 ? sum / wsum : center.x;
	// The environment's intensity (2 in stock's defaults: occlusion as traced) and power.
	ao = clamp(1.0 - (1.0 - ao) * (params.intensity * 0.5), 0.0, 1.0);
	ao = pow(ao, params.power);
	imageStore(dest, pos, vec4(ao));
}

#endif
