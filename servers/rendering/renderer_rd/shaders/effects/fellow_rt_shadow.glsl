#[compute]

#version 460

#VERSION_DEFINES

#if defined(MODE_TRACE)
#extension GL_EXT_ray_query : require
#endif

// FellowEngine ray-traced sun shadows (docs/decisions/0082-rt-sun-shadows.md). For the first directional light, per
// pixel of the depth pre-pass, rays towards the light (spread over its angular size: soft shadows) against the frame's
// ray tracing scene (decision 0080, its shadow casters). The opaque colour pass reads the result in place of the shadow
// map lookup where it is valid (within half the ray tracing radius of the camera); elsewhere the shadow map is used.
//
// MODE_TRACE: the rays; x the fraction that reach the light, y 1 where the result is valid.
// MODE_TEMPORAL: (soft shadows only) the trace blended with the previous frames' result at the reprojected position.
// MODE_DENOISE: (soft shadows only) a depth-aware 3 x 3 blur; hard shadows are written as traced.

#define MAX_HISTORY 12.0

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

layout(set = 0, binding = 0, std140) uniform Params {
	mat4 inv_projection; // NDC (x, y, depth * 2 - 1) to view
	mat4 view_to_world;
	mat4 world_to_prev_clip;
	vec4 light_direction; // xyz: world space, towards the light; w: tan of the light's angular radius (0: hard)
	ivec2 size;
	uint frame;
	uint ray_count;
	float max_distance; // valid within this distance of the camera
	float ray_length;
	uint history_valid;
	uint missing_casters; // 1: shadow casters not in the scene (MultiMesh, particles): lit pixels are the shadow map's
}
params;

layout(set = 0, binding = 1) uniform sampler2D depth_buffer;

vec3 view_position(ivec2 p_pos, float p_depth) {
	vec2 uv = (vec2(p_pos) + 0.5) / vec2(params.size);
	vec4 v = params.inv_projection * vec4(uv * 2.0 - 1.0, p_depth * 2.0 - 1.0, 1.0);
	return v.xyz / v.w;
}


#if defined(MODE_TRACE)

layout(set = 0, binding = 3) uniform accelerationStructureEXT tlas;
layout(rg16f, set = 0, binding = 4) uniform restrict writeonly image2D raw_shadow;

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
	if (depth == 0.0) {
		imageStore(raw_shadow, pos, vec4(1.0, 0.0, 0.0, 0.0)); // the sky: the shadow map path (never read)
		return;
	}
	vec3 vp = view_position(pos, depth);
	float dist = length(vp);
	if (dist > params.max_distance) {
		imageStore(raw_shadow, pos, vec4(1.0, 0.0, 0.0, 0.0));
		return;
	}
	vec3 wp = (params.view_to_world * vec4(vp, 1.0)).xyz;
	// The geometric normal from the depth buffer (as RTAO): the origin lifted along it.
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
	gvn = dot(gvn, gvn) > 1e-12 ? normalize(gvn) : -normalize(vp);
	if (dot(gvn, vp) > 0.0) {
		gvn = -gvn;
	}
	vec3 ng = normalize(mat3(params.view_to_world) * gvn);
	vec3 l = params.light_direction.xyz;
	if (dot(ng, l) <= 0.0) {
		// Facing away from the light by the depth buffer's normal: no ray (the lifted origin would start inside the
		// surface). The shadow map decides: that normal is unreliable on alpha-tested foliage and at silhouettes (where it
		// had darkened lit leaves), and a surface that truly faces away is unlit by N.L anyway.
		imageStore(raw_shadow, pos, vec4(1.0, 0.0, 0.0, 0.0));
		return;
	}
	vec3 origin = wp + ng * (0.003 + 0.0015 * dist);

	// A basis around the light direction for the cone samples.
	float s = l.z >= 0.0 ? 1.0 : -1.0;
	float a = -1.0 / (s + l.z);
	float b = l.x * l.y * a;
	vec3 t1 = vec3(1.0 + s * l.x * l.x * a, s * b, -s * l.x);
	vec3 t2 = vec3(b, s + l.y * l.y * a, -l.y);
	uint seed = hash(uint(pos.x) + hash(uint(pos.y) + hash(params.frame)));
	vec2 jitter = vec2(float(seed & 0xFFFFu), float(seed >> 16)) / 65536.0;

	float lit = 0.0;
	bool alpha_only = false;
	uint rays = params.light_direction.w > 0.0 ? params.ray_count : 1u;
	for (uint i = 0u; i < rays; i++) {
		vec3 dir = l;
		if (params.light_direction.w > 0.0) {
			float k = float(params.frame * rays + i);
			vec2 u = fract(jitter + k * vec2(0.7548776662, 0.5698402910));
			// Uniform over the light's disc (its tangent-plane radius at distance 1).
			float r = sqrt(u.x) * params.light_direction.w;
			float phi = 6.28318530718 * u.y;
			dir = normalize(l + t1 * (r * cos(phi)) + t2 * (r * sin(phi)));
		}
		rayQueryEXT rq;
		// Front faces culled: the ray keeps the faces that face the light, as a shadow map rendered from the light does
		// (a receiver inside a closed mesh is shadowed by its top; the receiver's own lit faces never occlude it).
		// Alpha-tested casters (mask 8, non-opaque) report candidates instead of hits: their alpha is not tested here,
		// so a ray that meets one and no opaque caster leaves the pixel to the shadow map (which tests it).
		rayQueryInitializeEXT(rq, tlas, gl_RayFlagsTerminateOnFirstHitEXT | gl_RayFlagsCullFrontFacingTrianglesEXT, 0x02 | 0x08, origin, 0.0, dir, params.ray_length);
		bool alpha_candidate = false;
		while (rayQueryProceedEXT(rq)) {
			alpha_candidate = true; // not confirmed: traversal goes on for an opaque caster
		}
		if (rayQueryGetIntersectionTypeEXT(rq, true) == gl_RayQueryCommittedIntersectionNoneEXT) {
			if (alpha_candidate) {
				alpha_only = true;
			} else {
				lit += 1.0;
			}
		}
	}
	if (alpha_only || (params.missing_casters != 0u && lit > 0.0)) {
		imageStore(raw_shadow, pos, vec4(1.0, 0.0, 0.0, 0.0)); // invalid: the shadow map's
		return;
	}
	imageStore(raw_shadow, pos, vec4(lit / float(rays), 1.0, 0.0, 0.0));
}

#elif defined(MODE_TEMPORAL)

layout(set = 0, binding = 3) uniform sampler2D raw_shadow;
layout(set = 0, binding = 4) uniform sampler2D history; // x: shadow, y: frames accumulated, z: the view depth it is for
layout(rgba16f, set = 0, binding = 5) uniform restrict writeonly image2D history_out;

void main() {
	ivec2 pos = ivec2(gl_GlobalInvocationID.xy);
	if (any(greaterThanEqual(pos, params.size))) {
		return;
	}
	vec2 raw = texelFetch(raw_shadow, pos, 0).xy;
	float depth = texelFetch(depth_buffer, pos, 0).r;
	if (depth == 0.0 || raw.y < 0.5 || params.light_direction.w == 0.0) {
		// Hard shadows: one exact ray, nothing to accumulate.
		if (raw.y > 0.5) {
			imageStore(history_out, pos, vec4(raw.x, 1.0, -view_position(pos, depth).z, 1.0));
			return;
		}
		imageStore(history_out, pos, vec4(raw.x, 1.0, 0.0, raw.y));
		return;
	}
	vec3 vp = view_position(pos, depth);
	float shadow = raw.x;
	float count = 1.0;
	if (params.history_valid != 0u) {
		vec3 wp = (params.view_to_world * vec4(vp, 1.0)).xyz;
		vec4 clip = params.world_to_prev_clip * vec4(wp, 1.0);
		vec2 prev_uv = (clip.xy / clip.w) * 0.5 + 0.5;
		if (clip.w > 0.0 && all(greaterThanEqual(prev_uv, vec2(0.0))) && all(lessThanEqual(prev_uv, vec2(1.0)))) {
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
				if (h.w > 0.5 && abs(h.z - expected) <= 0.03 * expected + 0.01) {
					sum += h.xy * w;
					wsum += w;
				}
			}
			if (wsum > 0.001) {
				vec2 h = sum / wsum;
				float n = min(h.y, MAX_HISTORY - 1.0);
				shadow = mix(h.x, shadow, 1.0 / (n + 1.0));
				count = n + 1.0;
			}
		}
	}
	imageStore(history_out, pos, vec4(shadow, count, -vp.z, 1.0));
}

#else // MODE_DENOISE

layout(set = 0, binding = 3) uniform sampler2D accumulated;
layout(rg8, set = 0, binding = 4) uniform restrict writeonly image2D dest;

void main() {
	ivec2 pos = ivec2(gl_GlobalInvocationID.xy);
	if (any(greaterThanEqual(pos, params.size))) {
		return;
	}
	vec4 center = texelFetch(accumulated, pos, 0);
	if (center.w < 0.5) {
		imageStore(dest, pos, vec4(1.0, 0.0, 0.0, 0.0));
		return;
	}
	if (params.light_direction.w == 0.0) {
		imageStore(dest, pos, vec4(center.x, 1.0, 0.0, 0.0)); // hard shadows: as traced
		return;
	}
	float z = center.z;
	float sum = 0.0;
	float wsum = 0.0;
	for (int y = -1; y <= 1; y++) {
		for (int x = -1; x <= 1; x++) {
			ivec2 q = clamp(pos + ivec2(x, y), ivec2(0), params.size - 1);
			vec4 s = texelFetch(accumulated, q, 0);
			if (s.w < 0.5) {
				continue;
			}
			float w = (x == 0 && y == 0) ? 1.0 : 0.5 / sqrt(center.y);
			w *= exp(-abs(s.z - z) / (0.01 * z + 0.001) * 2.0);
			sum += s.x * w;
			wsum += w;
		}
	}
	imageStore(dest, pos, vec4(wsum > 0.0 ? sum / wsum : center.x, 1.0, 0.0, 0.0));
}

#endif
