#[compute]

#version 450

#VERSION_DEFINES

// FellowEngine volumetric clouds (docs/decisions/0109-volumetric-clouds.md), after Schneider's Nubis (Horizon Zero
// Dawn, SIGGRAPH 2015/2017) and Hillaire's physically based clouds (SIGGRAPH 2016): a layer of clouds in a spherical
// shell over the ground, its density from tileable 3D noise (Perlin-Worley for the shapes, Worley for the eroded
// edges) under a coverage, lit by the sun (Beer-Lambert with Wrenninge's multiple-scattering octaves and a dual-lobe
// Henyey-Greenstein phase) and an ambient term from the sky.
//
// MODE_NOISE_SHAPE: the 128^3 shape noise (r: Perlin-Worley, gba: Worley octaves).
// MODE_NOISE_DETAIL: the 32^3 detail noise (rgb: Worley octaves).
// MODE_TRACE: the clouds seen from the camera, at half resolution, one ray per pixel (rgb: the in-scattered light, a:
// the transmittance). With the sky atmosphere (decision 0108) the sun reaches each sample through the atmosphere's
// transmittance (orange at sunset) and the sky's light comes from its sky-view table.
// MODE_TEMPORAL: this frame's trace blended with the previous result at the reprojected position.
// MODE_COMPOSITE: the clouds over the sky's pixels of the colour buffer (bilinear from half resolution).

#if defined(MODE_NOISE_SHAPE) || defined(MODE_NOISE_DETAIL)
layout(local_size_x = 4, local_size_y = 4, local_size_z = 4) in;
#else
layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;
#endif

layout(set = 0, binding = 0, std140) uniform Params {
	mat4 inv_view_projection; // NDC (x, y, depth * 2 - 1) to world
	mat4 prev_view_projection; // world to the previous frame's clip (the reprojection of the clouds' far points)
	vec4 camera_position; // world, metres; w: the ground's height
	vec4 sun_direction; // world, towards the sun; w: the time (s)
	vec4 sun_color; // the sun's illuminance (colour * energy)
	vec4 ambient_color; // the sky's light on the clouds' tops (without the atmosphere); w: the ground's albedo
	vec4 layer; // bottom altitude, thickness (km), coverage, density
	vec4 shape; // shape scale (1 / km), detail scale (1 / km), wind x, wind z (km / s)
	vec4 misc; // max distance (km), fade distance (km), history blend, frame
	ivec4 sizes; // trace w, h; screen w, h
	ivec4 flags; // x: 1 the atmosphere's tables are bound (the sun through the atmosphere, the sky's light); y: 1 interleaved
	// (each frame one pixel of every 2 x 2 block traced, decision 0115); z: this frame's pixel in the block (0-3)
}
params;

#define PLANET_RADIUS 6360.0
#define EXTINCTION 40.0 // per km at density 1 (clouds' extinction coefficient, about 0.04 per metre)
#define WEATHER_SCALE 0.4 // the weather's frequency relative to the shapes'

float hash13(vec3 p) {
	p = fract(p * 0.1031);
	p += dot(p, p.zyx + 31.32);
	return fract((p.x + p.y) * p.z);
}

vec3 hash33(vec3 p) {
	p = fract(p * vec3(0.1031, 0.1030, 0.0973));
	p += dot(p, p.yxz + 33.33);
	return fract((p.xxy + p.yxx) * p.zyx);
}

float remap(float v, float lo, float hi, float new_lo, float new_hi) {
	return new_lo + (clamp(v, lo, hi) - lo) / max(hi - lo, 1e-5) * (new_hi - new_lo);
}

#if defined(MODE_NOISE_SHAPE) || defined(MODE_NOISE_DETAIL)
layout(rgba8, set = 1, binding = 0) uniform restrict writeonly image3D noise_dest;

// Tileable Worley noise: 1 - the distance to the nearest feature point of a grid of p_cells cells per period.
float worley(vec3 p_uvw, float p_cells) {
	vec3 p = p_uvw * p_cells;
	vec3 cell = floor(p);
	float best = 1e9;
	for (int z = -1; z <= 1; z++) {
		for (int y = -1; y <= 1; y++) {
			for (int x = -1; x <= 1; x++) {
				vec3 c = cell + vec3(x, y, z);
				vec3 wrapped = mod(c, p_cells);
				vec3 feature = c + hash33(wrapped * 17.0 + 1.0);
				vec3 d = feature - p;
				best = min(best, dot(d, d));
			}
		}
	}
	return 1.0 - clamp(sqrt(best), 0.0, 1.0);
}

// Tileable gradient (Perlin) noise with period p_period, in [0, 1].
vec3 grad3(vec3 p_cell, float p_period) {
	vec3 h = hash33(mod(p_cell, p_period) * 13.0 + 7.0) * 2.0 - 1.0;
	return normalize(h + 1e-4);
}

float perlin(vec3 p_uvw, float p_period) {
	vec3 p = p_uvw * p_period;
	vec3 i = floor(p);
	vec3 f = p - i;
	vec3 u = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);
	float n000 = dot(grad3(i + vec3(0, 0, 0), p_period), f - vec3(0, 0, 0));
	float n100 = dot(grad3(i + vec3(1, 0, 0), p_period), f - vec3(1, 0, 0));
	float n010 = dot(grad3(i + vec3(0, 1, 0), p_period), f - vec3(0, 1, 0));
	float n110 = dot(grad3(i + vec3(1, 1, 0), p_period), f - vec3(1, 1, 0));
	float n001 = dot(grad3(i + vec3(0, 0, 1), p_period), f - vec3(0, 0, 1));
	float n101 = dot(grad3(i + vec3(1, 0, 1), p_period), f - vec3(1, 0, 1));
	float n011 = dot(grad3(i + vec3(0, 1, 1), p_period), f - vec3(0, 1, 1));
	float n111 = dot(grad3(i + vec3(1, 1, 1), p_period), f - vec3(1, 1, 1));
	float n = mix(mix(mix(n000, n100, u.x), mix(n010, n110, u.x), u.y), mix(mix(n001, n101, u.x), mix(n011, n111, u.x), u.y), u.z);
	return clamp(n * 0.9 + 0.5, 0.0, 1.0);
}

void main() {
	ivec3 pos = ivec3(gl_GlobalInvocationID.xyz);
	ivec3 size = imageSize(noise_dest);
	if (any(greaterThanEqual(pos, size))) {
		return;
	}
	vec3 uvw = (vec3(pos) + 0.5) / vec3(size);
#ifdef MODE_NOISE_SHAPE
	// The octaves' sums stretched to about [0, 1] (their spread is a fifth of it), the Perlin noise then dilated by the
	// Worley noise (Schneider's Perlin-Worley: billowy shapes).
	float p = perlin(uvw, 4.0) * 0.5 + perlin(uvw, 8.0) * 0.3 + perlin(uvw, 16.0) * 0.2;
	p = clamp((p - 0.5) * 2.2 + 0.5, 0.0, 1.0);
	float w1 = worley(uvw, 4.0);
	float w2 = worley(uvw, 8.0);
	float w3 = worley(uvw, 16.0);
	float w4 = worley(uvw, 32.0);
	float wfbm = clamp((w1 * 0.625 + w2 * 0.25 + w3 * 0.125 - 0.5) * 2.0 + 0.5, 0.0, 1.0);
	float perlin_worley = remap(p, 0.0, 1.0, wfbm, 1.0);
	imageStore(noise_dest, pos, vec4(perlin_worley, w2 * 0.625 + w3 * 0.25 + w4 * 0.125, w3 * 0.625 + w4 * 0.375, w4));
#else
	float w1 = worley(uvw, 4.0);
	float w2 = worley(uvw, 8.0);
	float w3 = worley(uvw, 16.0);
	imageStore(noise_dest, pos, vec4(w1, w2, w3, 1.0));
#endif
}
#endif

#if defined(MODE_TRACE)
layout(set = 0, binding = 1) uniform sampler3D shape_noise;
layout(set = 0, binding = 2) uniform sampler3D detail_noise;
layout(set = 0, binding = 3) uniform sampler2D atmosphere_transmittance;
layout(set = 0, binding = 4) uniform sampler2D atmosphere_sky_view;
layout(set = 0, binding = 5) uniform sampler2D depth_buffer;
layout(rgba16f, set = 1, binding = 0) uniform restrict writeonly image2D trace_dest;

#define ATMOSPHERE_TOP 6460.0

vec2 transmittance_uv(float r, float mu) {
	float H = sqrt(max(0.0, ATMOSPHERE_TOP * ATMOSPHERE_TOP - PLANET_RADIUS * PLANET_RADIUS));
	float rho = sqrt(max(0.0, r * r - PLANET_RADIUS * PLANET_RADIUS));
	float discriminant = r * r * (mu * mu - 1.0) + ATMOSPHERE_TOP * ATMOSPHERE_TOP;
	float d = max(0.0, -r * mu + sqrt(max(discriminant, 0.0)));
	float d_min = ATMOSPHERE_TOP - r;
	float d_max = rho + H;
	return vec2((d - d_min) / max(d_max - d_min, 1e-6), rho / H);
}

float ray_sphere_far(vec3 o, vec3 d, float r) {
	float b = dot(o, d);
	float c = dot(o, o) - r * r;
	float disc = b * b - c;
	if (disc < 0.0) {
		return -1.0;
	}
	return -b + sqrt(disc);
}

float ray_sphere_near(vec3 o, vec3 d, float r) {
	float b = dot(o, d);
	float c = dot(o, o) - r * r;
	float disc = b * b - c;
	if (disc < 0.0) {
		return -1.0;
	}
	return -b - sqrt(disc);
}

// Density at p (km, planet-centred); p_detail: with the eroded edges (the light samples skip them).
float cloud_density(vec3 p, bool p_detail) {
	float r = length(p);
	float h = (r - PLANET_RADIUS - params.layer.x) / params.layer.y; // 0 at the bottom, 1 at the top
	if (h < 0.0 || h > 1.0) {
		return 0.0;
	}
	vec3 wind = vec3(params.shape.z, 0.0, params.shape.w) * params.sun_direction.w;
	vec3 q = p + wind;
	// The weather: where the clouds stand, a 2D field (the shape noise's Perlin-Worley, about 16 km across its period)
	// spread to about [0, 1] (its mean 0.70 and deviation 0.14); the clouds cover where it exceeds 1 - coverage, the
	// more the further (local coverage, from 0 at their rims to 1 in their middles).
	float w = textureLod(shape_noise, vec3(q.x, 0.0, q.z) * params.shape.x * WEATHER_SCALE, 0.0).r;
	float wn = clamp((w - 0.70) * (1.0 / (0.14 * 3.46)) + 0.5, 0.0, 1.0);
	float local_coverage = clamp((wn - (1.0 - params.layer.z)) * 2.0, 0.0, 1.0);
	if (local_coverage <= 0.0) {
		return 0.0;
	}
	vec4 s = textureLod(shape_noise, q * params.shape.x, 0.0);
	float fbm = s.g * 0.625 + s.b * 0.25 + s.a * 0.125;
	float base = remap(s.r, -(1.0 - fbm), 1.0, 0.0, 1.0);
	// Flat bottoms; the tops rounded and the higher the denser the clouds stand (cumulus).
	float top = 0.3 + 0.7 * local_coverage;
	float profile = smoothstep(0.0, 0.08, h) * smoothstep(top, top * 0.3, h);
	float c = remap(base * profile, 1.0 - local_coverage, 1.0, 0.0, 1.0) * local_coverage;
	if (c <= 0.0) {
		return 0.0;
	}
	if (p_detail) {
		// The edges eroded by the detail noise (wispy at the bottoms, billowy at the tops).
		vec3 dn = textureLod(detail_noise, q * params.shape.y, 0.0).rgb;
		float dfbm = dn.r * 0.625 + dn.g * 0.25 + dn.b * 0.125;
		float erosion = mix(dfbm, 1.0 - dfbm, clamp(h * 4.0, 0.0, 1.0));
		c = remap(c, erosion * 0.3, 1.0, 0.0, 1.0);
	}
	return max(c, 0.0) * params.layer.w;
}

float hg(float g, float cos_theta) {
	float g2 = g * g;
	return (1.0 - g2) / (4.0 * 3.14159265 * pow(max(1.0 + g2 - 2.0 * g * cos_theta, 1e-4), 1.5));
}

ivec2 interleave_offset() {
	// The 2 x 2 block's pixels in a diagonal order (0, 0), (1, 1), (1, 0), (0, 1).
	int k = params.flags.z & 3;
	return ivec2((k == 1 || k == 2) ? 1 : 0, (k == 1 || k == 3) ? 1 : 0);
}

void main() {
	ivec2 pos = ivec2(gl_GlobalInvocationID.xy);
	if (params.flags.y != 0) {
		pos = pos * 2 + interleave_offset(); // one thread per 2 x 2 block
	}
	if (any(greaterThanEqual(pos, params.sizes.xy))) {
		return;
	}
	// Only where the composite can show clouds: a sky pixel among the full-resolution pixels whose bilinear sample
	// reads this one (its own 2 x 2 and a pixel around them). Under surfaces (most of an interior, a street) nothing
	// is traced.
	{
		bool sky = false;
		ivec2 base = pos * 2 - 1;
		ivec2 last = params.sizes.zw - 1;
		for (int y = 0; y < 4 && !sky; y++) {
			for (int x = 0; x < 4; x++) {
				if (texelFetch(depth_buffer, clamp(base + ivec2(x, y), ivec2(0), last), 0).r == 0.0) {
					sky = true;
					break;
				}
			}
		}
		if (!sky) {
			imageStore(trace_dest, pos, vec4(0.0, 0.0, 0.0, 1.0));
			return;
		}
	}
	vec2 uv = (vec2(pos) + 0.5) / vec2(params.sizes.xy);
	vec4 wp = params.inv_view_projection * vec4(uv * 2.0 - 1.0, 0.5, 1.0);
	vec3 d = normalize(wp.xyz / wp.w - params.camera_position.xyz);
	float cam_h = max(params.camera_position.y - params.camera_position.w, 0.0) * 0.001;
	vec3 o = vec3(0.0, PLANET_RADIUS + cam_h, 0.0) + vec3(params.camera_position.x, 0.0, params.camera_position.z) * 0.001;
	float r_bottom = PLANET_RADIUS + params.layer.x;
	float r_top = r_bottom + params.layer.y;
	float t0, t1;
	float r_o = length(o);
	if (r_o < r_bottom) {
		t0 = ray_sphere_far(o, d, r_bottom);
		t1 = ray_sphere_far(o, d, r_top);
		// Below the horizon the ray meets the ground first.
		float tg = ray_sphere_near(o, d, PLANET_RADIUS);
		if (tg > 0.0) {
			imageStore(trace_dest, pos, vec4(0.0, 0.0, 0.0, 1.0));
			return;
		}
	} else if (r_o < r_top) {
		t0 = 0.0;
		float tb = ray_sphere_near(o, d, r_bottom);
		t1 = tb > 0.0 ? tb : ray_sphere_far(o, d, r_top);
	} else {
		t0 = ray_sphere_near(o, d, r_top);
		t1 = ray_sphere_near(o, d, r_bottom);
		if (t1 < 0.0) {
			t1 = ray_sphere_far(o, d, r_top);
		}
	}
	if (t0 < 0.0 || t1 <= t0 || t0 > params.misc.x) {
		imageStore(trace_dest, pos, vec4(0.0, 0.0, 0.0, 1.0));
		return;
	}
	t1 = min(t1, params.misc.x);
	const int STEPS = 64;
	float dt = (t1 - t0) / float(STEPS);
	float jitter = fract(52.9829189 * fract(dot(vec2(pos) + 5.588238 * float(int(params.misc.w) & 63), vec2(0.06711056, 0.00583715))));
	vec3 sun = params.sun_direction.xyz;
	float cos_theta = dot(d, sun);
	bool atmosphere = params.flags.x != 0;
	vec3 sky_top = params.ambient_color.rgb;
	if (atmosphere) {
		// The sky's light from above (its zenith and mid-elevation radiance, over the upper hemisphere).
		vec3 zenith = textureLod(atmosphere_sky_view, vec2(0.5, 0.02), 0.0).rgb;
		vec3 mid = textureLod(atmosphere_sky_view, vec2(0.25, 0.3), 0.0).rgb;
		sky_top = (zenith + mid) * 0.5 * 3.14159265 * params.sun_color.rgb;
	}
	vec3 L = vec3(0.0);
	float T = 1.0;
	float first_hit = -1.0;
	for (int i = 0; i < STEPS; i++) {
		float t = t0 + (float(i) + jitter) * dt;
		vec3 p = o + d * t;
		float density = cloud_density(p, true);
		if (density <= 0.0) {
			continue;
		}
		if (first_hit < 0.0) {
			first_hit = t;
		}
		float sigma = density * EXTINCTION;
		// Light march towards the sun: six samples over about 2 km (the shape alone).
		float od = 0.0;
		float ls = 0.05;
		vec3 lp = p;
		for (int j = 0; j < 6; j++) {
			lp += sun * ls;
			od += cloud_density(lp, false) * EXTINCTION * ls;
			ls *= 1.8;
		}
		// Multiple scattering (Wrenninge: four octaves, each with less extinction and a flatter phase; the light of the
		// later ones reaches deep into the clouds, their undersides grey rather than black).
		vec3 sun_at_p = params.sun_color.rgb;
		if (atmosphere) {
			float rp = length(p);
			sun_at_p *= textureLod(atmosphere_transmittance, transmittance_uv(rp, dot(p / rp, sun)), 0.0).rgb;
		}
		vec3 sun_light = vec3(0.0);
		float a = 1.0;
		float b = 1.0;
		float c = 1.0;
		for (int k = 0; k < 4; k++) {
			float phase = mix(hg(0.8 * c, cos_theta), hg(-0.3 * c, cos_theta), 0.5);
			sun_light += a * phase * exp(-od * b) * sun_at_p;
			a *= 0.6;
			b *= 0.3;
			c *= 0.5;
		}
		// The sky's light from above (less at the bottoms) and the sunlit ground's from below (ambient_color.w: the
		// ground's albedo), scattered isotropically.
		float hf = clamp((length(p) - r_bottom) / params.layer.y, 0.0, 1.0);
		vec3 ground = sun_at_p * (params.ambient_color.w * max(sun.y, 0.0));
		vec3 ambient = (sky_top * mix(0.5, 1.0, hf) + ground * (1.0 - hf)) * (1.0 / (4.0 * 3.14159265));
		vec3 S = (sun_light + ambient) * sigma;
		float step_t = exp(-sigma * dt);
		L += T * (S - S * step_t) / max(sigma, 1e-5);
		T *= step_t;
		if (T < 0.01) {
			T = 0.0;
			break;
		}
	}
	// Distant clouds fade into the sky (a stand-in for the aerial perspective over tens of kilometres).
	if (first_hit > 0.0) {
		float fade = exp(-first_hit / params.misc.y);
		L *= fade;
		T = mix(1.0, T, fade);
	}
	imageStore(trace_dest, pos, vec4(L, T));
}
#endif

#if defined(MODE_TEMPORAL)
layout(set = 0, binding = 1) uniform sampler2D trace_tex;
layout(set = 0, binding = 2) uniform sampler2D history_tex;
layout(rgba16f, set = 1, binding = 0) uniform restrict writeonly image2D history_dest;

void main() {
	ivec2 pos = ivec2(gl_GlobalInvocationID.xy);
	if (any(greaterThanEqual(pos, params.sizes.xy))) {
		return;
	}
	vec2 uv = (vec2(pos) + 0.5) / vec2(params.sizes.xy);
	vec4 cur = texelFetch(trace_tex, pos, 0);
	// Interleaved (decision 0115): one pixel of each 2 x 2 block was traced this frame; the others hold traces of up to
	// three frames ago, at the screen positions of then. A pixel not traced follows its reprojected history, clamped to
	// this frame's traces around it (one to four in the 3 x 3), or, without history (uncovered by the camera's motion),
	// takes their mean.
	bool interleaved = params.flags.y != 0;
	ivec2 o = ivec2(0);
	bool fresh = true;
	if (interleaved) {
		int k = params.flags.z & 3;
		o = ivec2((k == 1 || k == 2) ? 1 : 0, (k == 1 || k == 3) ? 1 : 0);
		fresh = all(equal(pos & 1, o));
	}
	vec4 lo = cur;
	vec4 hi = cur;
	vec4 sum = vec4(0.0);
	float count = 0.0;
	bool first = interleaved;
	for (int y = -1; y <= 1; y++) {
		for (int x = -1; x <= 1; x++) {
			ivec2 q = clamp(pos + ivec2(x, y), ivec2(0), params.sizes.xy - 1);
			if (interleaved && !all(equal(q & 1, o))) {
				continue;
			}
			vec4 n = texelFetch(trace_tex, q, 0);
			if (first) {
				lo = n;
				hi = n;
				first = false;
			}
			lo = min(lo, n);
			hi = max(hi, n);
			sum += n;
			count += 1.0;
		}
	}
	vec4 result = (fresh || count == 0.0) ? cur : sum / count;
	// The clouds' far point reprojected: rotation and slow translation (the clouds are kilometres away).
	vec4 wp = params.inv_view_projection * vec4(uv * 2.0 - 1.0, 0.5, 1.0);
	vec3 dir = normalize(wp.xyz / wp.w - params.camera_position.xyz);
	vec4 prev_clip = params.prev_view_projection * vec4(params.camera_position.xyz + dir * 5000.0, 1.0);
	vec2 prev_uv = (prev_clip.xy / prev_clip.w) * 0.5 + 0.5;
	if (params.misc.z > 0.0 && prev_clip.w > 0.0 && all(greaterThanEqual(prev_uv, vec2(0.0))) && all(lessThanEqual(prev_uv, vec2(1.0)))) {
		// Clamped to this frame's neighbourhood (moving clouds, uncovered sky).
		vec4 hist = clamp(textureLod(history_tex, prev_uv, 0.0), lo, hi);
		// A fresh trace each fourth frame when interleaved: a lighter history keeps about the same response time.
		float blend = interleaved ? 0.7 : params.misc.z;
		result = fresh ? mix(cur, hist, blend) : hist;
	}
	imageStore(history_dest, pos, result);
}
#endif

#if defined(MODE_COMPOSITE)
layout(set = 0, binding = 1) uniform sampler2D clouds_tex;
layout(set = 0, binding = 2) uniform sampler2D depth_buffer;
layout(rgba16f, set = 1, binding = 0) uniform restrict image2D color_image;

void main() {
	ivec2 pos = ivec2(gl_GlobalInvocationID.xy);
	if (any(greaterThanEqual(pos, params.sizes.zw))) {
		return;
	}
	if (texelFetch(depth_buffer, pos, 0).r != 0.0) {
		return; // a surface in front of the sky (the clouds are kilometres away)
	}
	vec2 uv = (vec2(pos) + 0.5) / vec2(params.sizes.zw);
	vec4 c = textureLod(clouds_tex, uv, 0.0);
	vec4 col = imageLoad(color_image, pos);
	col.rgb = col.rgb * c.a + c.rgb;
	imageStore(color_image, pos, col);
}
#endif
