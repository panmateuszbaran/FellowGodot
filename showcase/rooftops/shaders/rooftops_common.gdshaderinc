// FellowEngine showcase (rooftops): noise and patterns shared by the scene's procedural materials. The models' UVs are
// in metres (box projected on walls, along the slope on roofs), so every pattern here is in metres too.

float rt_hash12(vec2 p) {
	vec3 p3 = fract(vec3(p.xyx) * 0.1031);
	p3 += dot(p3, p3.yzx + 33.33);
	return fract((p3.x + p3.y) * p3.z);
}

vec2 rt_hash22(vec2 p) {
	vec3 p3 = fract(vec3(p.xyx) * vec3(0.1031, 0.1030, 0.0973));
	p3 += dot(p3, p3.yzx + 33.33);
	return fract((p3.xx + p3.yz) * p3.zy);
}

float rt_hash13(vec3 p) {
	p = fract(p * 0.1031);
	p += dot(p, p.zyx + 31.32);
	return fract((p.x + p.y) * p.z);
}

float rt_noise(vec2 p) {
	vec2 i = floor(p);
	vec2 f = fract(p);
	vec2 u = f * f * (3.0 - 2.0 * f);
	float a = rt_hash12(i);
	float b = rt_hash12(i + vec2(1.0, 0.0));
	float c = rt_hash12(i + vec2(0.0, 1.0));
	float d = rt_hash12(i + vec2(1.0, 1.0));
	return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}

float rt_fbm(vec2 p, int octaves) {
	float s = 0.0;
	float a = 0.5;
	for (int i = 0; i < octaves; i++) {
		s += a * rt_noise(p);
		p = mat2(vec2(1.6, 1.2), vec2(-1.2, 1.6)) * p + vec2(3.1, 1.7);
		a *= 0.5;
	}
	return s;
}

// A detail's amplitude faded where the pixel covers more than its size (no shimmer far away).
float rt_detail_fade(vec2 uv, float size) {
	vec2 w = fwidth(uv);
	return clamp(1.0 - max(w.x, w.y) / size, 0.0, 1.0);
}

// A height (metres) on the surface turned into its normal: the surface gradient from the screen-space derivatives of
// the position and the height (Mikkelsen, "Bump mapping unparametrized surfaces on the GPU"), no tangents needed.
vec3 rt_bump(vec3 n, vec3 pos, float h, float strength) {
	vec3 dpdx = dFdx(pos);
	vec3 dpdy = dFdy(pos);
	float dhdx = dFdx(h) * strength;
	float dhdy = dFdy(h) * strength;
	vec3 r1 = cross(dpdy, n);
	vec3 r2 = cross(n, dpdx);
	float det = dot(dpdx, r1);
	if (abs(det) < 1e-12) {
		return n;
	}
	vec3 g = sign(det) * (dhdx * r1 + dhdy * r2);
	return normalize(abs(det) * n - g);
}

// Blocks in rows (ashlar, bricks): the block's id, the position in it (0..1) and the distance to its nearest joint (m).
// size: a block's (length, height); offset: how far each row is shifted (a fraction of the length).
vec4 rt_blocks(vec2 uv, vec2 size, float offset, out vec2 id) {
	float row = floor(uv.y / size.y);
	float shift = offset * row + rt_hash12(vec2(row, 7.0)) * 0.13;
	vec2 q = vec2(uv.x / size.x + shift, uv.y / size.y);
	id = floor(q);
	vec2 f = fract(q);
	float dx = min(f.x, 1.0 - f.x) * size.x;
	float dy = min(f.y, 1.0 - f.y) * size.y;
	return vec4(f, min(dx, dy), rt_hash12(id + vec2(row * 0.37, 3.0)));
}
