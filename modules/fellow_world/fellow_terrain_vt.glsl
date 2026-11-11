#[compute]

#version 450

#VERSION_DEFINES

// FellowEngine: a FellowTerrain3D's virtual texture pages baked (milestone 70, decision 0178). One page per z of the
// dispatch: its PADDED x PADDED texels (the page and BORDER texels around it, from the material beyond the page's
// edges) evaluated as the terrain's shader does a fragment — the splat weights, each layer's albedo, height and normal
// from the top or three projections, the height blend, the macro variation — with each texel's footprint on the
// ground as the gradients, and written into the page's slot of the atlas: the albedo in sRGB with the roughness, the
// normal in the terrain's space (x and z). With out_size, one level of the terrain's overview instead: out_size texels
// a side, no border, into the images bound (that level's views).

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

#define PAGE 128
#define BORDER 4
#define PADDED (PAGE + 2 * BORDER)
#define LAYERS 9

layout(rgba8, set = 0, binding = 0) uniform restrict writeonly image2D atlas_albedo;
layout(rgba8, set = 0, binding = 1) uniform restrict writeonly image2D atlas_normal;
layout(set = 0, binding = 2) uniform sampler map_sampler; // clamped, linear, mipmaps
layout(set = 0, binding = 3) uniform sampler layer_sampler; // repeated, linear, mipmaps
layout(set = 0, binding = 4) uniform texture2D heightmap;
layout(set = 0, binding = 5) uniform texture2D normalmap;
layout(set = 0, binding = 6) uniform texture2D splat_0;
layout(set = 0, binding = 7) uniform texture2D splat_1;
layout(set = 0, binding = 8) uniform texture2D albedo_tex[LAYERS]; // sRGB views: linear texels
layout(set = 0, binding = 9) uniform texture2D normal_tex[LAYERS];

layout(set = 0, binding = 10, std430) restrict readonly buffer Params {
	vec4 layer_color[LAYERS];
	vec4 layer_params[LAYERS]; // 1 / texture size (m), roughness, normal strength, triplanar
	vec4 layer_flags[LAYERS]; // albedo texture, normal texture, height in the albedo's alpha
	ivec4 jobs[]; // slot x, slot y, mip, page x | page z << 16
}
params;

layout(push_constant, std430) uniform Push {
	vec2 map_texels;
	vec2 world_offset;
	float spacing;
	float texels_per_metre;
	float height_blend;
	float macro_variation;
	int job_base;
	int out_size;
	int pad0;
	int pad1;
}
pc;

float hash12(vec2 p) {
	vec3 p3 = fract(vec3(p.xyx) * 0.1031);
	p3 += dot(p3, p3.yzx + 33.33);
	return fract((p3.x + p3.y) * p3.z);
}

float value_noise(vec2 p) {
	vec2 i = floor(p);
	vec2 f = fract(p);
	vec2 u = f * f * (3.0 - 2.0 * f);
	return mix(mix(hash12(i), hash12(i + vec2(1.0, 0.0)), u.x), mix(hash12(i + vec2(0.0, 1.0)), hash12(i + vec2(1.0, 1.0)), u.x), u.y);
}

vec2 unpack_normal(vec4 t, float strength) {
	return (t.xy * 2.0 - 1.0) * strength;
}

vec4 sample_albedo(int i, vec2 u, vec2 gx, vec2 gy) {
	return textureGrad(sampler2D(albedo_tex[i], layer_sampler), u, gx, gy);
}

vec4 sample_normal(int i, vec2 u, vec2 gx, vec2 gy) {
	return textureGrad(sampler2D(normal_tex[i], layer_sampler), u, gx, gy);
}

// FellowTerrain3D's layer_#() (the same arithmetic).
void layer(int i, vec3 pl, vec3 dpdx, vec3 dpdy, vec3 nw, vec3 blend, out vec4 r_albedo, out vec3 r_normal) {
	float s = params.layer_params[i].x;
	float strength = params.layer_params[i].z;
	vec4 f = params.layer_flags[i];
	if (params.layer_params[i].w > 0.5) {
		vec3 sgn = sign(nw + vec3(1e-6));
		vec4 a = vec4(0.0);
		vec3 n = vec3(0.0);
		float wsum = 0.0;
		if (blend.x > 0.01) {
			vec2 u = vec2(pl.z * sgn.x, -pl.y) * s;
			vec2 gx = vec2(dpdx.z * sgn.x, -dpdx.y) * s;
			vec2 gy = vec2(dpdy.z * sgn.x, -dpdy.y) * s;
			a += (f.x > 0.5 ? sample_albedo(i, u, gx, gy) : vec4(1.0)) * blend.x;
			vec2 t = f.y > 0.5 ? unpack_normal(sample_normal(i, u, gx, gy), strength) : vec2(0.0);
			t.x *= sgn.x;
			n += vec3(nw.x, t.y + nw.y, t.x + nw.z) * blend.x;
			wsum += blend.x;
		}
		if (blend.y > 0.01) {
			vec2 u = vec2(pl.x, -pl.z) * s;
			vec2 gx = vec2(dpdx.x, -dpdx.z) * s;
			vec2 gy = vec2(dpdy.x, -dpdy.z) * s;
			a += (f.x > 0.5 ? sample_albedo(i, u, gx, gy) : vec4(1.0)) * blend.y;
			vec2 t = f.y > 0.5 ? unpack_normal(sample_normal(i, u, gx, gy), strength) : vec2(0.0);
			n += vec3(t.x + nw.x, nw.y, t.y + nw.z) * blend.y;
			wsum += blend.y;
		}
		if (blend.z > 0.01) {
			vec2 u = vec2(-pl.x * sgn.z, -pl.y) * s;
			vec2 gx = vec2(-dpdx.x * sgn.z, -dpdx.y) * s;
			vec2 gy = vec2(-dpdy.x * sgn.z, -dpdy.y) * s;
			a += (f.x > 0.5 ? sample_albedo(i, u, gx, gy) : vec4(1.0)) * blend.z;
			vec2 t = f.y > 0.5 ? unpack_normal(sample_normal(i, u, gx, gy), strength) : vec2(0.0);
			t.x *= -sgn.z;
			n += vec3(t.x + nw.x, t.y + nw.y, nw.z) * blend.z;
			wsum += blend.z;
		}
		r_albedo = a / max(wsum, 1e-5);
		r_normal = normalize(n + nw * 1e-5);
	} else {
		vec2 u = vec2(pl.x, -pl.z) * s;
		vec2 gx = vec2(dpdx.x, -dpdx.z) * s;
		vec2 gy = vec2(dpdy.x, -dpdy.z) * s;
		r_albedo = f.x > 0.5 ? sample_albedo(i, u, gx, gy) : vec4(1.0);
		vec2 t = f.y > 0.5 ? unpack_normal(sample_normal(i, u, gx, gy), strength) : vec2(0.0);
		r_normal = normalize(vec3(t.x + nw.x, nw.y, t.y + nw.z));
	}
	if (f.z < 0.5) {
		r_albedo.a = 0.5;
	}
}

vec3 linear_to_srgb(vec3 c) {
	c = clamp(c, vec3(0.0), vec3(1.0));
	return mix(c * 12.92, 1.055 * pow(c, vec3(1.0 / 2.4)) - 0.055, step(vec3(0.0031308), c));
}

void main() {
	ivec2 t = ivec2(gl_GlobalInvocationID.xy);
	int size = pc.out_size > 0 ? pc.out_size : PADDED;
	if (t.x >= size || t.y >= size) {
		return;
	}
	ivec4 job = params.jobs[pc.job_base + int(gl_GlobalInvocationID.z)];
	int mip = job.z;
	ivec2 page = ivec2(job.w & 0xFFFF, job.w >> 16);
	float texel = exp2(float(mip)) / pc.texels_per_metre; // metres
	// The texel's centre on the map (terrain-local metres; the border's from beyond the page's edges).
	vec2 p = (vec2(page * PAGE) + vec2(pc.out_size > 0 ? t : t - BORDER) + 0.5) * texel;
	vec2 uv = (p / pc.spacing + 0.5) / pc.map_texels;
	// The maps at the texel's footprint.
	float map_lod = max(0.0, log2(texel / pc.spacing));
	float h = textureLod(sampler2D(heightmap, map_sampler), uv, map_lod).r;
	vec2 n2 = textureLod(sampler2D(normalmap, map_sampler), uv, map_lod).rg * 2.0 - 1.0;
	vec3 nw = normalize(vec3(n2.x, sqrt(max(0.0, 1.0 - dot(n2, n2))), n2.y));
	vec4 s0 = textureLod(sampler2D(splat_0, map_sampler), uv, map_lod);
	vec4 s1 = textureLod(sampler2D(splat_1, map_sampler), uv, map_lod);
	float w[LAYERS];
	w[1] = s0.r;
	w[2] = s0.g;
	w[3] = s0.b;
	w[4] = s0.a;
	w[5] = s1.r;
	w[6] = s1.g;
	w[7] = s1.b;
	w[8] = s1.a;
	w[0] = max(0.0, 1.0 - (s0.r + s0.g + s0.b + s0.a + s1.r + s1.g + s1.b + s1.a));
	// A texel's step along x and z on the ground (its slope included): the gradients a fragment as large would have.
	float ny = max(nw.y, 0.05);
	vec3 dpdx = vec3(texel, -nw.x / ny * texel, 0.0);
	vec3 dpdy = vec3(0.0, -nw.z / ny * texel, texel);
	vec3 blend = pow(abs(nw), vec3(4.0));
	blend /= max(dot(blend, vec3(1.0)), 1e-5);
	vec3 pw = vec3(p.x, h, p.y) + vec3(pc.world_offset.x, 0.0, pc.world_offset.y);

	vec4 alb[LAYERS];
	vec3 nrm[LAYERS];
	float v[LAYERS];
	float top = -1e9;
	for (int i = 0; i < LAYERS; i++) {
		alb[i] = vec4(0.0);
		nrm[i] = nw;
		v[i] = -1e9;
		if (w[i] > 0.002) {
			layer(i, pw, dpdx, dpdy, nw, blend, alb[i], nrm[i]);
			v[i] = w[i] + alb[i].a * pc.height_blend;
			top = max(top, v[i]);
		}
	}
	vec3 albedo = vec3(0.0);
	vec3 n = vec3(0.0);
	float rough = 0.0;
	float total = 0.0;
	for (int i = 0; i < LAYERS; i++) {
		float b = v[i] > -1e8 ? (pc.height_blend > 0.0 ? max(v[i] - (top - 0.15), 0.0) * w[i] : w[i]) : 0.0;
		albedo += alb[i].rgb * params.layer_color[i].rgb * b;
		n += nrm[i] * b;
		rough += params.layer_params[i].y * b;
		total += b;
	}
	total = max(total, 1e-5);
	albedo /= total;
	rough /= total;
	float mv = value_noise(pw.xz * 0.013) * 0.6 + value_noise(pw.xz * 0.071) * 0.4;
	albedo *= mix(1.0, 0.7 + 0.6 * mv, pc.macro_variation);
	n = normalize(n + vec3(0.0, 1e-5, 0.0));

	ivec2 at = pc.out_size > 0 ? t : job.xy * PADDED + t;
	imageStore(atlas_albedo, at, vec4(linear_to_srgb(albedo), clamp(rough, 0.0, 1.0)));
	imageStore(atlas_normal, at, vec4(n.x * 0.5 + 0.5, n.z * 0.5 + 0.5, 0.0, 1.0));
}
