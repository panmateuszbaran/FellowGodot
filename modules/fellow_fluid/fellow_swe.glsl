#[versions]

advect = "#define MODE_ADVECT";
height = "#define MODE_HEIGHT";
velocity = "#define MODE_VELOCITY";
surface = "#define MODE_SURFACE";

#[compute]

#version 450

#VERSION_DEFINES

// FellowEngine GPU shallow-water solver (2D, staggered MAC grid, explicit symplectic Euler).
//
// State texture (RGBA32F), one texel per cell (i, j):
//   r = h      water depth at the cell centre           (i + 0.5, j + 0.5)
//   g = u      x-velocity on the cell's +X face          (i + 1.0, j + 0.5)
//   b = v      z-velocity on the cell's +Z face          (i + 0.5, j + 1.0)
//   a = foam   foam / activity at the cell centre
// Terrain texture (R32F): bed height b(i, j). Cells with b >= solid_height are solid obstacles.
//
// Per substep:  ADVECT (semi-Lagrangian u, v, foam)  ->  HEIGHT (flux-form continuity, upwind depth,
// outflow limiter => exact mass conservation and h >= 0, impulses)  ->  VELOCITY (surface-gradient acceleration, wet/dry, walls, colliders, inflow, foam sources).
// Per frame:    SURFACE (surface height, gradient -> normals, foam) for rendering.
// All state stays on the GPU: there is no CPU readback anywhere in the loop.

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

#if defined(MODE_ADVECT) || defined(MODE_HEIGHT) || defined(MODE_VELOCITY) || defined(MODE_SURFACE)
layout(rgba32f, set = 0, binding = 0) uniform restrict readonly image2D state_in;
layout(r32f, set = 0, binding = 1) uniform restrict readonly image2D terrain;
#endif

#ifdef MODE_SURFACE
layout(rgba16f, set = 0, binding = 2) uniform restrict writeonly image2D surface_out;
// Cell-centre velocity (m/s) and depth (m) for FellowWater3D (its waves follow the flow).
layout(rgba16f, set = 0, binding = 4) uniform restrict writeonly image2D flow_out;
#else
layout(rgba32f, set = 0, binding = 2) uniform restrict writeonly image2D state_out;
#endif

#define MAX_COLLIDERS 16
#define MAX_IMPULSES 16

layout(set = 0, binding = 3, std430) restrict readonly buffer Interaction {
	vec4 collider_pos_radius[MAX_COLLIDERS]; // x, y = position in cells (local X, Z), z = centre height (m), w = radius (m)
	vec4 collider_vel[MAX_COLLIDERS]; // velocity (m/s) xyz
	vec4 impulse[MAX_IMPULSES]; // x, z in cells, radius in cells, depth change (m)
}
interaction;

layout(push_constant, std430) uniform Params {
	ivec2 size;
	float dx; // cell size (m)
	float dt; // substep (s)

	float gravity;
	float damping; // 1/s
	float solid_height;
	float foam_decay; // 1/s

	vec2 inflow_velocity; // m/s, applied in the first inflow_cells columns
	float inflow_depth; // inflow surface elevation (m); > 0 enables river mode (inflow at -X, open outflow at +X)
	int inflow_cells;

	int collider_count;
	int impulse_count;
	float max_speed; // m/s, CFL safety clamp
	float collider_drag; // 0..1 per substep
}
params;

const float EPS_H = 1e-4;

ivec2 clampc(ivec2 c) {
	return clamp(c, ivec2(0), params.size - ivec2(1));
}

vec4 S(ivec2 c) {
	return imageLoad(state_in, clampc(c));
}

float B(ivec2 c) {
	return imageLoad(terrain, clampc(c)).r;
}

bool solid(ivec2 c) {
	if (any(lessThan(c, ivec2(0))) || any(greaterThanEqual(c, params.size))) {
		return true;
	}
	return imageLoad(terrain, c).r >= params.solid_height;
}

#ifdef MODE_ADVECT
// Bilinear sample of the state at a staggered location (the caller picks the component with a swizzle).
// p is in cell units (cell centres at k + 0.5). Returning the whole vec4 avoids a runtime vector index
// (OpVectorExtractDynamic), which Godot's SPIR-V specialization pass (re-spirv) does not support.
vec4 sample_state(vec2 p, vec2 origin) {
	vec2 q = p - origin;
	vec2 f = floor(q);
	vec2 t = q - f;
	ivec2 c = ivec2(f);
	vec4 a = S(c);
	vec4 b = S(c + ivec2(1, 0));
	vec4 d = S(c + ivec2(0, 1));
	vec4 e = S(c + ivec2(1, 1));
	return mix(mix(a, b, t.x), mix(d, e, t.x), t.y);
}

void main() {
	ivec2 c = ivec2(gl_GlobalInvocationID.xy);
	if (any(greaterThanEqual(c, params.size))) {
		return;
	}
	vec4 s = S(c);
	float k = params.dt / params.dx; // cells per (m/s)

	// u face at (i + 1, j + 0.5)
	float v_at_u = 0.25 * (S(c).b + S(c + ivec2(1, 0)).b + S(c + ivec2(0, -1)).b + S(c + ivec2(1, -1)).b);
	vec2 pu = vec2(c) + vec2(1.0, 0.5) - k * vec2(s.g, v_at_u);
	float u_new = sample_state(pu, vec2(1.0, 0.5)).g;

	// v face at (i + 0.5, j + 1)
	float u_at_v = 0.25 * (S(c).g + S(c + ivec2(-1, 0)).g + S(c + ivec2(0, 1)).g + S(c + ivec2(-1, 1)).g);
	vec2 pv = vec2(c) + vec2(0.5, 1.0) - k * vec2(u_at_v, s.b);
	float v_new = sample_state(pv, vec2(0.5, 1.0)).b;

	// foam at the centre
	vec2 uc = vec2(0.5 * (s.g + S(c + ivec2(-1, 0)).g), 0.5 * (s.b + S(c + ivec2(0, -1)).b));
	vec2 pc = vec2(c) + vec2(0.5) - k * uc;
	float foam_new = sample_state(pc, vec2(0.5)).a;

	imageStore(state_out, c, vec4(s.r, u_new, v_new, foam_new));
}
#endif

#ifdef MODE_HEIGHT
// Raw flux through the +X / +Z face of cell c (upwind depth). Faces on the domain boundary carry no flux.
float flux_x(ivec2 c) {
	if (c.x < 0) {
		return 0.0;
	}
	if (c.x >= params.size.x - 1) {
		// River mode: the +X domain edge is an open outflow boundary (upwind, outgoing only).
		return params.inflow_depth > 0.0 ? max(S(c).g, 0.0) * S(c).r : 0.0;
	}
	float u = S(c).g;
	return u * (u > 0.0 ? S(c).r : S(c + ivec2(1, 0)).r);
}

float flux_z(ivec2 c) {
	if (c.y < 0 || c.y >= params.size.y - 1) {
		return 0.0;
	}
	float v = S(c).b;
	return v * (v > 0.0 ? S(c).r : S(c + ivec2(0, 1)).r);
}

// Outflow limiter: the fraction of its requested outflow a cell can actually deliver this substep
// (guarantees h >= 0 without clamping, so mass is conserved exactly).
float alpha(ivec2 c) {
	if (any(lessThan(c, ivec2(0))) || any(greaterThanEqual(c, params.size))) {
		return 1.0;
	}
	float out_flux = max(flux_x(c), 0.0) + max(flux_z(c), 0.0) + max(-flux_x(c + ivec2(-1, 0)), 0.0) + max(-flux_z(c + ivec2(0, -1)), 0.0);
	float requested = out_flux * params.dt / params.dx;
	float h = S(c).r;
	return requested > h ? h / max(requested, 1e-12) : 1.0;
}

float limited_flux_x(ivec2 c) {
	float f = flux_x(c);
	return f * (f > 0.0 ? alpha(c) : alpha(c + ivec2(1, 0)));
}

float limited_flux_z(ivec2 c) {
	float f = flux_z(c);
	return f * (f > 0.0 ? alpha(c) : alpha(c + ivec2(0, 1)));
}

void main() {
	ivec2 c = ivec2(gl_GlobalInvocationID.xy);
	if (any(greaterThanEqual(c, params.size))) {
		return;
	}
	vec4 s = S(c);
	if (solid(c)) {
		imageStore(state_out, c, vec4(0.0, 0.0, 0.0, 0.0));
		return;
	}
	float div = limited_flux_x(c) - limited_flux_x(c + ivec2(-1, 0)) + limited_flux_z(c) - limited_flux_z(c + ivec2(0, -1));
	float h = s.r - params.dt / params.dx * div;

	for (int i = 0; i < params.impulse_count; i++) {
		vec4 im = interaction.impulse[i];
		vec2 d = vec2(c) + vec2(0.5) - im.xy;
		h += im.w * exp(-dot(d, d) / max(im.z * im.z, 1e-4));
	}
	if (params.inflow_depth > 0.0 && c.x < params.inflow_cells) {
		h = max(params.inflow_depth - B(c), 0.0);
	}
	imageStore(state_out, c, vec4(max(h, 0.0), s.g, s.b, s.a));
}
#endif

#ifdef MODE_VELOCITY
// Extra hydrostatic head from colliders pushing into the water column (moves water away, mass conserving).
float collider_head(ivec2 c, float surface) {
	float head = 0.0;
	vec2 p = vec2(c) + vec2(0.5);
	for (int i = 0; i < params.collider_count; i++) {
		vec4 cp = interaction.collider_pos_radius[i];
		float d = length(p - cp.xy) * params.dx;
		if (d < cp.w) {
			float bottom = cp.z - sqrt(cp.w * cp.w - d * d);
			head += max(surface - bottom, 0.0);
		}
	}
	return head;
}

float eta(ivec2 c) {
	float h = S(c).r;
	float e = h + B(c);
	return e + collider_head(c, e);
}

float face_velocity(ivec2 c, ivec2 o, float vel) {
	ivec2 n = c + o;
	if (solid(c) || solid(n)) {
		return 0.0;
	}
	float h0 = S(c).r;
	float h1 = S(n).r;
	if (h0 <= EPS_H && h1 <= EPS_H) {
		return 0.0;
	}
	float e0 = eta(c);
	float e1 = eta(n);
	vel -= params.gravity * params.dt / params.dx * (e1 - e0);
	// Wet/dry: water cannot flow out of a dry cell or climb onto a bed higher than the source surface.
	if (h0 <= EPS_H) {
		vel = min(vel, 0.0);
	}
	if (h1 <= EPS_H) {
		vel = max(vel, 0.0);
	}
	if ((h1 <= EPS_H && B(n) > e0) || (h0 <= EPS_H && B(c) > e1)) {
		vel = 0.0;
	}
	vel *= exp(-params.damping * params.dt);
	return clamp(vel, -params.max_speed, params.max_speed);
}

void main() {
	ivec2 c = ivec2(gl_GlobalInvocationID.xy);
	if (any(greaterThanEqual(c, params.size))) {
		return;
	}
	vec4 s = S(c);
	float u = face_velocity(c, ivec2(1, 0), s.g);
	float v = face_velocity(c, ivec2(0, 1), s.b);

	// Colliders drag the water they touch (one-way coupling: collider -> water).
	vec2 pu = vec2(c) + vec2(1.0, 0.5);
	vec2 pv = vec2(c) + vec2(0.5, 1.0);
	float surface = s.r + B(c);
	for (int i = 0; i < params.collider_count; i++) {
		vec4 cp = interaction.collider_pos_radius[i];
		vec3 cv = interaction.collider_vel[i].xyz;
		if (cp.z - cp.w > surface) {
			continue;
		}
		float r_cells = cp.w / params.dx;
		if (length(pu - cp.xy) < r_cells) {
			u = mix(u, cv.x, params.collider_drag);
		}
		if (length(pv - cp.xy) < r_cells) {
			v = mix(v, cv.z, params.collider_drag);
		}
	}

	// Inflow boundary (river): prescribed velocity; outflow at +X edge: zero-gradient.
	if (params.inflow_depth > 0.0) {
		if (c.x < params.inflow_cells) {
			u = params.inflow_velocity.x;
			v = params.inflow_velocity.y;
		} else if (c.x == params.size.x - 1) {
			u = max(S(c + ivec2(-1, 0)).g, 0.0);
		}
	}

	// Foam / activity: generated by convergence, speed and steep surface gradients; decays and is advected.
	float h = s.r;
	float div = (s.g - S(c + ivec2(-1, 0)).g + s.b - S(c + ivec2(0, -1)).b) / params.dx;
	float speed = length(vec2(0.5 * (s.g + S(c + ivec2(-1, 0)).g), 0.5 * (s.b + S(c + ivec2(0, -1)).b)));
	float ex = eta(c + ivec2(1, 0)) - eta(c + ivec2(-1, 0));
	float ez = eta(c + ivec2(0, 1)) - eta(c + ivec2(0, -1));
	float steep = length(vec2(ex, ez)) / (2.0 * params.dx);
	float src = 0.6 * max(-div, 0.0) + 0.25 * max(speed - 1.5, 0.0) + 1.5 * max(steep - 0.25, 0.0);
	float foam = s.a * exp(-params.foam_decay * params.dt) + src * params.dt * step(EPS_H * 10.0, h);
	if (params.inflow_depth > 0.0 && c.x < params.inflow_cells + 2) {
		foam = 0.0; // prescribed inflow boundary: no artificial foam from the imposed velocity jump
	}
	imageStore(state_out, c, vec4(h, u, v, clamp(foam, 0.0, 1.0)));
}
#endif

#ifdef MODE_SURFACE
float eta_s(ivec2 c, float fallback) {
	vec4 s = S(c);
	if (s.r <= 1e-3 || solid(c)) {
		return fallback;
	}
	return s.r + B(c);
}

void main() {
	ivec2 c = ivec2(gl_GlobalInvocationID.xy);
	if (any(greaterThanEqual(c, params.size))) {
		return;
	}
	vec4 s = S(c);
	float b = B(c);
	bool is_solid = solid(c);
	bool wet = s.r > 1e-3 && !is_solid;
	// Dry cells: keep the surface slightly below the bed so it is hidden, avoiding shoreline cracks.
	// Solid obstacles: sink the surface below the neighbouring water level (hidden inside the obstacle mesh)
	// instead of raising it to the obstacle height, which would stretch the water mesh up the obstacle.
	float e = wet ? s.r + b : b - 0.02;
	if (is_solid) {
		float level = -1e9;
		for (int k = 0; k < 4; k++) {
			ivec2 n = c + ivec2(k == 0 ? 1 : (k == 1 ? -1 : 0), k == 2 ? 1 : (k == 3 ? -1 : 0));
			vec4 sn = S(n);
			if (sn.r > 1e-3 && !solid(n)) {
				level = max(level, sn.r + B(n));
			}
		}
		e = (level > -1e8 ? level : 0.0) - 0.5;
	}
	float ex0 = eta_s(c + ivec2(-1, 0), e);
	float ex1 = eta_s(c + ivec2(1, 0), e);
	float ez0 = eta_s(c + ivec2(0, -1), e);
	float ez1 = eta_s(c + ivec2(0, 1), e);
	vec2 grad = vec2(ex1 - ex0, ez1 - ez0) / (2.0 * params.dx);
	imageStore(surface_out, c, vec4(e, grad.x, grad.y, wet ? s.a : 0.0));
	// u lives on +X faces, v on +Z faces: the centre is the mean of a cell's two faces.
	vec2 vel = wet ? vec2(s.g + S(c + ivec2(-1, 0)).g, s.b + S(c + ivec2(0, -1)).b) * 0.5 : vec2(0.0);
	// The water level extended one cell beyond the wet ones (a dry cell takes its highest wet neighbour's), else a
	// metre below the bed: interpolated, the surface stays level up to where a terrain cuts it, instead of climbing
	// the bank cell by cell (the dry cells' e is just below their bed).
	float level = (wet || is_solid) ? e : -1e9; // solid: e is already below its wet neighbours (hidden in the obstacle)
	if (!wet && !is_solid) {
		for (int k = 0; k < 9; k++) {
			ivec2 n = c + ivec2(k % 3 - 1, k / 3 - 1);
			if (k != 4 && all(greaterThanEqual(n, ivec2(0))) && all(lessThan(n, params.size))) {
				vec4 sn = S(n);
				if (sn.r > 1e-3 && !solid(n)) {
					level = max(level, sn.r + B(n));
				}
			}
		}
		if (level < -1e8) {
			level = b - 1.0;
		}
	}
	imageStore(flow_out, c, vec4(vel, wet ? s.r : 0.0, level));
}
#endif
