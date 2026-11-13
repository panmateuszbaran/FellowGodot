#[compute]

#version 450

#VERSION_DEFINES

// FellowEngine sky atmosphere (docs/decisions/0108-sky-atmosphere.md): Hillaire's "A Scalable and Production Ready Sky
// and Atmosphere Rendering Technique" (EGSR 2020), as Unreal's SkyAtmosphere. Earth-like defaults: Rayleigh, Mie and
// ozone layers over a ground sphere. All radiance for a sun of illuminance 1 (the users of the tables multiply by the
// light's colour and energy). Distances in kilometres inside the shader, metres outside.
//
// MODE_TRANSMITTANCE: the transmittance to the atmosphere's top, by height and zenith angle (Bruneton's mapping).
// MODE_MULTISCATTER: the isotropic multiple-scattering factor psi_ms by height and sun zenith angle (eq. 10).
// MODE_SKY_VIEW: the sky's in-scattered radiance seen from the camera, by azimuth (from the sun) and elevation (a
// non-linear mapping, denser near the horizon).
// MODE_AERIAL: aerial perspective froxels over the camera's frustum: in-scattered radiance (rgb) and mean transmittance
// (a) from the camera to each slice's distance (squared distribution up to max_distance).

#ifdef MODE_AERIAL
layout(local_size_x = 4, local_size_y = 4, local_size_z = 4) in;
#else
layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;
#endif

layout(set = 0, binding = 0, std140) uniform Params {
	mat4 inv_view_projection; // NDC (x, y, depth) to the position relative to the camera (decision 0211)
	vec4 sun_direction; // world, towards the sun; w: the sun's disk angular radius
	vec4 camera_position; // world (metres); w: the ground's height (metres)
	vec4 rayleigh_scattering; // per km; w: scale height (km)
	vec4 mie_scattering; // per km; w: scale height (km)
	vec4 mie_absorption; // per km; w: phase g
	vec4 ozone_absorption; // per km; w: ozone layer centre (km)
	vec4 ground_albedo; // w: ozone layer width (km)
	vec4 radii; // bottom radius, top radius (km), aerial max distance (km), multi-scattering factor
	ivec4 sizes; // sky-view w, h; aerial w (= h), aerial depth
	vec4 sun_illuminance; // rgb: the sun's colour times its energy times the intensity; w: aerial distance scale
	ivec4 screen; // the colour buffer's size
}
params;

#define PI 3.14159265359

layout(set = 0, binding = 1) uniform sampler2D transmittance_lut;
layout(set = 0, binding = 2) uniform sampler2D multiscatter_lut;

#if defined(MODE_TRANSMITTANCE) || defined(MODE_MULTISCATTER) || defined(MODE_SKY_VIEW)
layout(rgba16f, set = 1, binding = 0) uniform restrict writeonly image2D dest;
#endif
#if defined(MODE_AERIAL)
layout(rgba16f, set = 1, binding = 0) uniform restrict writeonly image3D dest3d;
#endif
#if defined(MODE_APPLY)
layout(set = 0, binding = 3) uniform sampler3D aerial_lut;
layout(set = 0, binding = 4) uniform sampler2D depth_buffer;
layout(rgba16f, set = 1, binding = 0) uniform restrict image2D color_image;
#endif

float bottom_radius() {
	return params.radii.x;
}
float top_radius() {
	return params.radii.y;
}

struct Medium {
	vec3 scattering; // total (Rayleigh + Mie)
	vec3 extinction;
	vec3 rayleigh;
	vec3 mie;
};

Medium sample_medium(float p_height) {
	float h = max(p_height, 0.0);
	float dr = exp(-h / params.rayleigh_scattering.w);
	float dm = exp(-h / params.mie_scattering.w);
	float dozone = max(0.0, 1.0 - abs(h - params.ozone_absorption.w) / (0.5 * params.ground_albedo.w));
	Medium m;
	m.rayleigh = params.rayleigh_scattering.rgb * dr;
	m.mie = params.mie_scattering.rgb * dm;
	m.scattering = m.rayleigh + m.mie;
	m.extinction = m.scattering + params.mie_absorption.rgb * dm + params.ozone_absorption.rgb * dozone;
	return m;
}

// Distance along the ray (origin at radius |p_o| from the planet's centre) to the sphere of radius p_r; -1 if none.
float ray_sphere(vec3 p_o, vec3 p_d, float p_r) {
	float b = dot(p_o, p_d);
	float c = dot(p_o, p_o) - p_r * p_r;
	float disc = b * b - c;
	if (disc < 0.0) {
		return -1.0;
	}
	float s = sqrt(disc);
	float t0 = -b - s;
	float t1 = -b + s;
	if (t0 >= 0.0) {
		return t0;
	}
	if (t1 >= 0.0) {
		return t1;
	}
	return -1.0;
}

// Bruneton's transmittance mapping (height, view zenith cosine) <-> uv.
vec2 transmittance_uv(float p_r, float p_mu) {
	float bottom = bottom_radius();
	float top = top_radius();
	float H = sqrt(max(0.0, top * top - bottom * bottom));
	float rho = sqrt(max(0.0, p_r * p_r - bottom * bottom));
	float discriminant = p_r * p_r * (p_mu * p_mu - 1.0) + top * top;
	float d = max(0.0, -p_r * p_mu + sqrt(max(discriminant, 0.0)));
	float d_min = top - p_r;
	float d_max = rho + H;
	float x_mu = (d - d_min) / max(d_max - d_min, 1e-6);
	float x_r = rho / H;
	return vec2(x_mu, x_r);
}

void transmittance_params(vec2 p_uv, out float r_r, out float r_mu) {
	float bottom = bottom_radius();
	float top = top_radius();
	float H = sqrt(max(0.0, top * top - bottom * bottom));
	float rho = H * p_uv.y;
	r_r = sqrt(rho * rho + bottom * bottom);
	float d_min = top - r_r;
	float d_max = rho + H;
	float d = d_min + p_uv.x * (d_max - d_min);
	r_mu = d == 0.0 ? 1.0 : (H * H - rho * rho - d * d) / (2.0 * r_r * d);
	r_mu = clamp(r_mu, -1.0, 1.0);
}

vec3 get_transmittance(float p_r, float p_mu) {
	return textureLod(transmittance_lut, transmittance_uv(p_r, p_mu), 0.0).rgb;
}

vec3 get_multiscatter(float p_r, float p_mu_sun) {
	vec2 uv = vec2(clamp(p_mu_sun * 0.5 + 0.5, 0.0, 1.0), clamp((p_r - bottom_radius()) / (top_radius() - bottom_radius()), 0.0, 1.0));
	return textureLod(multiscatter_lut, uv, 0.0).rgb;
}

float rayleigh_phase(float p_cos) {
	return 3.0 / (16.0 * PI) * (1.0 + p_cos * p_cos);
}

float cornette_shanks_phase(float p_g, float p_cos) {
	float g2 = p_g * p_g;
	float k = 3.0 / (8.0 * PI) * (1.0 - g2) / (2.0 + g2);
	return k * (1.0 + p_cos * p_cos) / pow(max(1.0 + g2 - 2.0 * p_g * p_cos, 1e-4), 1.5);
}

// Single scattering (with the multiple-scattering term) along a ray from p_o (km, planet-centred) in p_d, over
// [0, p_t_max], p_steps steps. r_transmittance: the ray's transmittance.
vec3 integrate(vec3 p_o, vec3 p_d, float p_t_max, int p_steps, vec3 p_sun, bool p_multiscatter, out vec3 r_transmittance) {
	float cos_theta = dot(p_d, p_sun);
	float phase_r = rayleigh_phase(cos_theta);
	float phase_m = cornette_shanks_phase(params.mie_absorption.w, cos_theta);
	vec3 L = vec3(0.0);
	vec3 T = vec3(1.0);
	float t = 0.0;
	float dt = p_t_max / float(p_steps);
	for (int i = 0; i < p_steps; i++) {
		float t_new = (float(i) + 0.3) * dt;
		vec3 p = p_o + p_d * t_new;
		float r = length(p);
		Medium m = sample_medium(r - bottom_radius());
		vec3 up = p / r;
		float mu_sun = dot(up, p_sun);
		vec3 t_sun = get_transmittance(r, mu_sun);
		// The planet's shadow: the sun below this point's horizon.
		float earth = ray_sphere(p, p_sun, bottom_radius()) >= 0.0 ? 0.0 : 1.0;
		vec3 S = earth * t_sun * (m.rayleigh * phase_r + m.mie * phase_m);
		if (p_multiscatter) {
			S += get_multiscatter(r, mu_sun) * m.scattering * params.radii.w;
		}
		vec3 step_t = exp(-m.extinction * dt);
		// Energy-conserving integration (Hillaire 2015).
		vec3 S_int = (S - S * step_t) / max(m.extinction, vec3(1e-7));
		L += T * S_int;
		T *= step_t;
		t = t_new;
	}
	r_transmittance = T;
	return L;
}

#ifdef MODE_TRANSMITTANCE
void main() {
	ivec2 pos = ivec2(gl_GlobalInvocationID.xy);
	ivec2 size = imageSize(dest);
	if (any(greaterThanEqual(pos, size))) {
		return;
	}
	vec2 uv = (vec2(pos) + 0.5) / vec2(size);
	float r, mu;
	transmittance_params(uv, r, mu);
	vec3 o = vec3(0.0, r, 0.0);
	vec3 d = vec3(sqrt(max(0.0, 1.0 - mu * mu)), mu, 0.0);
	float t_top = ray_sphere(o, d, top_radius());
	vec3 optical = vec3(0.0);
	const int STEPS = 40;
	float dt = max(t_top, 0.0) / float(STEPS);
	for (int i = 0; i < STEPS; i++) {
		vec3 p = o + d * ((float(i) + 0.5) * dt);
		optical += sample_medium(length(p) - bottom_radius()).extinction * dt;
	}
	imageStore(dest, pos, vec4(exp(-optical), 1.0));
}
#endif

#ifdef MODE_MULTISCATTER
// Hillaire's eq. 5-10: second-order luminance and the transfer f_ms, from 64 directions (8 x 8), the ground's albedo
// included; psi_ms = L_2nd / (1 - f_ms).
void main() {
	ivec2 pos = ivec2(gl_GlobalInvocationID.xy);
	ivec2 size = imageSize(dest);
	if (any(greaterThanEqual(pos, size))) {
		return;
	}
	vec2 uv = (vec2(pos) + 0.5) / vec2(size);
	float mu_sun = uv.x * 2.0 - 1.0;
	float r = bottom_radius() + uv.y * (top_radius() - bottom_radius());
	vec3 o = vec3(0.0, r, 0.0);
	vec3 sun = vec3(sqrt(max(0.0, 1.0 - mu_sun * mu_sun)), mu_sun, 0.0);
	vec3 L2 = vec3(0.0);
	vec3 fms = vec3(0.0);
	const int N = 8;
	for (int j = 0; j < N; j++) {
		for (int i = 0; i < N; i++) {
			float theta = acos(1.0 - 2.0 * (float(j) + 0.5) / float(N));
			float phi = 2.0 * PI * (float(i) + 0.5) / float(N);
			vec3 d = vec3(sin(theta) * cos(phi), cos(theta), sin(theta) * sin(phi));
			float t_ground = ray_sphere(o, d, bottom_radius());
			float t_top = ray_sphere(o, d, top_radius());
			float t_max = t_ground >= 0.0 ? t_ground : t_top;
			if (t_max <= 0.0) {
				continue;
			}
			// L: single scattering with an isotropic phase; f: the medium's scattering transfer (unit illumination).
			vec3 T = vec3(1.0);
			vec3 L = vec3(0.0);
			vec3 f = vec3(0.0);
			const int STEPS = 20;
			float dt = t_max / float(STEPS);
			for (int k = 0; k < STEPS; k++) {
				vec3 p = o + d * ((float(k) + 0.3) * dt);
				float pr = length(p);
				Medium m = sample_medium(pr - bottom_radius());
				vec3 up = p / pr;
				float mus = dot(up, sun);
				float earth = ray_sphere(p, sun, bottom_radius()) >= 0.0 ? 0.0 : 1.0;
				vec3 t_sun = get_transmittance(pr, mus);
				vec3 step_t = exp(-m.extinction * dt);
				vec3 inv_ext = 1.0 / max(m.extinction, vec3(1e-7));
				vec3 S = earth * t_sun * m.scattering / (4.0 * PI);
				L += T * (S - S * step_t) * inv_ext;
				vec3 F = m.scattering;
				f += T * (F - F * step_t) * inv_ext;
				T *= step_t;
			}
			if (t_ground >= 0.0) {
				// The ground, lit by the sun (Lambertian albedo).
				vec3 pg = o + d * t_ground;
				vec3 up = normalize(pg);
				float mus = dot(up, sun);
				L += T * get_transmittance(bottom_radius(), mus) * clamp(mus, 0.0, 1.0) * params.ground_albedo.rgb / PI;
			}
			float w = 1.0 / float(N * N); // uniform over the sphere: the isotropic phase's 1/(4 pi) times 4 pi / (N * N)
			L2 += L * w;
			fms += f * w;
		}
	}
	vec3 psi = L2 / max(vec3(1.0) - fms, vec3(1e-3));
	imageStore(dest, pos, vec4(psi, 1.0));
}
#endif

#ifdef MODE_SKY_VIEW
// x: the azimuth from the sun's (-pi to pi); y: the view zenith angle, Hillaire's non-linear mapping (the horizon at 0.5,
// denser near it): above, zenith angle = horizon_zenith * (1 - (1 - 2v)^2); below, horizon_zenith + beta * (2v - 1)^2.
void main() {
	ivec2 pos = ivec2(gl_GlobalInvocationID.xy);
	ivec2 size = params.sizes.xy;
	if (any(greaterThanEqual(pos, size))) {
		return;
	}
	vec2 uv = (vec2(pos) + 0.5) / vec2(size);
	float r = clamp(bottom_radius() + max(params.camera_position.y - params.camera_position.w, 0.0) * 0.001, bottom_radius() + 0.0005, top_radius() - 0.0005);
	vec3 o = vec3(0.0, r, 0.0);
	float beta = acos(clamp(sqrt(max(0.0, r * r - bottom_radius() * bottom_radius())) / r, -1.0, 1.0));
	float horizon_zenith = PI - beta;
	float zenith;
	if (uv.y < 0.5) {
		float c = 1.0 - 2.0 * uv.y;
		zenith = horizon_zenith * (1.0 - c * c);
	} else {
		float c = 2.0 * uv.y - 1.0;
		zenith = horizon_zenith + beta * c * c;
	}
	float azimuth = (uv.x * 2.0 - 1.0) * PI;
	float sun_zenith = acos(clamp(params.sun_direction.y, -1.0, 1.0));
	vec3 sun = vec3(sin(sun_zenith), cos(sun_zenith), 0.0);
	vec3 d = vec3(sin(zenith) * cos(azimuth), cos(zenith), sin(zenith) * sin(azimuth));
	float t_ground = ray_sphere(o, d, bottom_radius());
	float t_top = ray_sphere(o, d, top_radius());
	float t_max = t_ground >= 0.0 ? t_ground : t_top;
	vec3 T;
	vec3 L = t_max > 0.0 ? integrate(o, d, t_max, 30, sun, true, T) : vec3(0.0);
	if (t_ground >= 0.0) {
		// The planet's ground below the horizon, lit by the sun through the atmosphere (Lambertian albedo).
		vec3 pg = o + d * t_ground;
		vec3 n = normalize(pg);
		float mu = dot(n, sun);
		L += T * get_transmittance(bottom_radius(), mu) * max(mu, 0.0) * params.ground_albedo.rgb / PI;
	}
	imageStore(dest, pos, vec4(L, 1.0));
}
#endif

#ifdef MODE_AERIAL
// Froxels over the camera's frustum: x, y the screen, z distance slices (slice z ends at max * ((z + 1) / depth)^2);
// rgb the in-scattered radiance from the camera to that distance, a the mean transmittance.
void main() {
	ivec3 pos = ivec3(gl_GlobalInvocationID.xyz);
	ivec3 size = ivec3(params.sizes.z, params.sizes.z, params.sizes.w);
	if (any(greaterThanEqual(pos, size))) {
		return;
	}
	vec2 uv = (vec2(pos.xy) + 0.5) / vec2(size.xy);
	vec4 p = params.inv_view_projection * vec4(uv * 2.0 - 1.0, 0.5, 1.0);
	vec3 d = normalize(p.xyz / p.w);
	float r = clamp(bottom_radius() + max(params.camera_position.y - params.camera_position.w, 0.0) * 0.001, bottom_radius() + 0.0005, top_radius() - 0.0005);
	vec3 o = vec3(0.0, r, 0.0);
	float s = (float(pos.z) + 1.0) / float(size.z);
	float dist = params.radii.z * s * s;
	float t_ground = ray_sphere(o, d, bottom_radius());
	if (t_ground >= 0.0) {
		dist = min(dist, t_ground);
	}
	vec3 T;
	int steps = max(2, min(16, pos.z + 2));
	vec3 L = integrate(o, d, dist, steps, params.sun_direction.xyz, true, T);
	imageStore(dest3d, pos, vec4(L, dot(T, vec3(1.0 / 3.0))));
}
#endif

#ifdef MODE_APPLY
// Aerial perspective on the opaque surfaces (before the transparent pass): the colour attenuated by the transmittance
// from the camera and the in-scattered light added, from the froxels at the pixel's distance (decision 0108).
void main() {
	ivec2 pos = ivec2(gl_GlobalInvocationID.xy);
	if (any(greaterThanEqual(pos, params.screen.xy))) {
		return;
	}
	float depth = texelFetch(depth_buffer, pos, 0).r;
	if (depth == 0.0) {
		return; // the sky
	}
	vec2 uv = (vec2(pos) + 0.5) / vec2(params.screen.xy);
	vec4 wp = params.inv_view_projection * vec4(uv * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
	float dist_km = length(wp.xyz / wp.w) * 0.001 * params.sun_illuminance.w;
	float slices = float(params.sizes.w);
	float z = sqrt(max(dist_km, 0.0) / params.radii.z) * slices - 1.0;
	float w = clamp(z + 1.0, 0.0, 1.0); // before the first slice's distance: faded in from nothing
	vec4 ap = textureLod(aerial_lut, vec3(uv, (max(z, 0.0) + 0.5) / slices), 0.0);
	vec3 inscatter = ap.rgb * w;
	float transmittance = mix(1.0, ap.a, w);
	vec4 c = imageLoad(color_image, pos);
	c.rgb = c.rgb * transmittance + inscatter * params.sun_illuminance.rgb;
	imageStore(color_image, pos, c);
}
#endif
