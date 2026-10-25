// FellowEngine sky atmosphere (docs/decisions/0108-sky-atmosphere.md): the sky drawn from the tables FellowEngine
// computes every frame (rendering/fellow/post/atmosphere/enabled) for the sky's first light (LIGHT0): the in-scattered
// light seen from the camera (Hillaire's sky-view table, for a sun of illuminance 1) and the sun's disk through the
// atmosphere's transmittance. The project must declare three global shader parameters (Project Settings > Shader
// Globals): fellow_atmosphere_sky_view, fellow_atmosphere_transmittance (sampler2D), fellow_atmosphere_camera_height
// (float). Without FellowEngine's tables the sky is black.
shader_type sky;

global uniform sampler2D fellow_atmosphere_sky_view : filter_linear, repeat_disable;
global uniform sampler2D fellow_atmosphere_transmittance : filter_linear, repeat_disable;
// The camera's height above the ground (km), set by FellowEngine every frame: reading POSITION instead would make Godot
// redraw the sky's radiance every frame (0.25 ms on an RTX 5070 Ti laptop at 1080p).
global uniform float fellow_atmosphere_camera_height;

uniform float intensity = 1.0;
uniform float sun_disk_scale = 1.0;
// The tables are per unit illuminance; the colour passes light with LIGHT0_ENERGY * PI without physical light units
// (with them, LIGHT0_ENERGY is the illuminance): PI makes the sky as bright as the scene's sun implies.
uniform float energy_scale = 3.14159265;

const float BOTTOM_RADIUS = 6360.0;
const float TOP_RADIUS = 6460.0;

// Bruneton's transmittance mapping, as the tables' shader.
vec2 transmittance_uv(float r, float mu) {
	float H = sqrt(max(0.0, TOP_RADIUS * TOP_RADIUS - BOTTOM_RADIUS * BOTTOM_RADIUS));
	float rho = sqrt(max(0.0, r * r - BOTTOM_RADIUS * BOTTOM_RADIUS));
	float discriminant = r * r * (mu * mu - 1.0) + TOP_RADIUS * TOP_RADIUS;
	float d = max(0.0, -r * mu + sqrt(max(discriminant, 0.0)));
	float d_min = TOP_RADIUS - r;
	float d_max = rho + H;
	return vec2((d - d_min) / max(d_max - d_min, 1e-6), rho / H);
}

void sky() {
	vec3 d = EYEDIR;
	float r = clamp(BOTTOM_RADIUS + max(fellow_atmosphere_camera_height, 0.0), BOTTOM_RADIUS + 0.0005, TOP_RADIUS - 0.0005);
	float beta = acos(clamp(sqrt(max(0.0, r * r - BOTTOM_RADIUS * BOTTOM_RADIUS)) / r, -1.0, 1.0));
	float horizon_zenith = PI - beta;
	float zenith = acos(clamp(d.y, -1.0, 1.0));
	// The sky-view table's elevation mapping (denser at the horizon, which is at v = 0.5).
	float v;
	if (zenith < horizon_zenith) {
		v = (1.0 - sqrt(max(0.0, 1.0 - zenith / horizon_zenith))) * 0.5;
	} else {
		v = 0.5 + sqrt(clamp((zenith - horizon_zenith) / beta, 0.0, 1.0)) * 0.5;
	}
	float az = atan(d.z, d.x) - atan(LIGHT0_DIRECTION.z, LIGHT0_DIRECTION.x);
	az = mod(az + PI, 2.0 * PI) - PI;
	float u = az / (2.0 * PI) + 0.5;
	vec3 sun = LIGHT0_COLOR * LIGHT0_ENERGY * energy_scale;
	vec3 col = texture(fellow_atmosphere_sky_view, vec2(u, v)).rgb * sun;
	if (LIGHT0_ENABLED) {
		// The sun's disk: its illuminance over its solid angle, through the atmosphere, limb-darkened.
		float half_angle = max(LIGHT0_SIZE * 0.5 * sun_disk_scale, 0.0005);
		float cos_sun = dot(d, LIGHT0_DIRECTION);
		if (cos_sun > cos(half_angle) && zenith < horizon_zenith) {
			vec3 t = texture(fellow_atmosphere_transmittance, transmittance_uv(r, d.y)).rgb;
			float solid_angle = 2.0 * PI * (1.0 - cos(half_angle));
			float x = clamp(acos(clamp(cos_sun, -1.0, 1.0)) / half_angle, 0.0, 1.0);
			float limb = 1.0 - 0.6 * (1.0 - sqrt(max(0.0, 1.0 - x * x)));
			col += sun * t * (limb / (solid_angle * 0.8));
		}
	}
	COLOR = col * intensity;
}
