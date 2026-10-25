#[compute]

#version 450

#VERSION_DEFINES

// FellowEngine GPU gas solver: smoke and fire (docs/decisions/0117-gas-simulation.md, FellowGasVolume3D).
//
// One compute pass per mode, in this order per step:
//   OBSTACLES   the cells covered by the obstacles (physics bodies' shapes): their index (r8ui, 0 = air);
//   EMIT        the emitters add smoke and fuel and heat the air (scalars in place);
//   ADVECT_S1   semi-Lagrangian advection of the scalars (smoke, temperature, fuel, burn rate) by the velocity;
//   ADVECT_S2   MacCormack's correction with its limiter (Selle et al. 2008), then the reaction: the fuel burns
//               where the gas is hotter than the ignition temperature (heat, soot, a burn rate for the expansion),
//               smoke dissipates, the gas cools;
//   ADVECT_V1/2 the same for the staggered velocity;
//   CURL        the vorticity at the cell centres;
//   CELL_FORCE  buoyancy (hot gas rises, smoke weighs), vorticity confinement (Fedkiw et al. 2001), turbulence;
//   FACE_FORCE  the forces on the faces; the emitters' velocity, the wind;
//   DIVERGENCE  of the velocity against the solids' and the walls' velocities, minus the combustion's expansion;
//   PRESSURE    red-black SOR on the Poisson equation, warm-started from the last step;
//   PROJECT     the pressure gradient subtracted: the velocity without divergence;
//   COARSE      per 4 x 4 x 4 brick the largest smoke density and temperature (empty space skipped by the
//               renderer) and per workgroup the fire's emission (the fire light, read back asynchronously).
//
// The grid (tests/gas_reference.py is this file in NumPy): cells (i, j, k) with centres at grid position
// (i + 0.5, j + 0.5, k + 0.5), dx metres each. Scalars at the centres: x smoke, y temperature above ambient (K),
// z fuel, w burn rate (fuel / s). The velocity is staggered (MAC) in one (nx + 1) x (ny + 1) x (nz + 1) texture: x at
// the x faces (i, j + .5, k + .5), y at the y faces, z at the z faces; the texels past a component's last face repeat
// it (each pass computes the clamped face), so clamp-to-edge sampling extends the field with a zero gradient.

layout(local_size_x = 4, local_size_y = 4, local_size_z = 4) in;

#ifdef HIGH_PRECISION
#define FMT rgba32f
#else
#define FMT rgba16f
#endif

#define MAX_EMITTERS 16
#define MAX_OBSTACLES 16

struct Emitter {
	vec4 centre_radius; // grid position; radius (cells; sphere, cylinder)
	vec4 rot0; // grid -> emitter rotation rows, w: half extents (cells; box) / half height (cylinder)
	vec4 rot1;
	vec4 rot2;
	vec4 rates; // smoke (1/s), temperature (K), fuel (1/s), noise amount
	vec4 velocity; // m/s in grid axes, w: blend per step (0..1)
	vec4 shape; // x: 0 sphere, 1 box, 2 cylinder; y: noise frequency (1/cell); z: 1 add the temperature (K/s) instead of max; w unused
};

struct Obstacle {
	vec4 centre_type; // grid position, w: 0 sphere, 1 box, 2 capsule, 3 cylinder, 4 half-space (below the y = 0 plane)
	vec4 rot0; // grid -> obstacle rotation rows, w: radius (sphere, capsule) / half extent x (box) (cells)
	vec4 rot1; // w: half extent y (box) / half height of the capsule's segment (cells)
	vec4 rot2; // w: half extent z (box)
	vec4 linear_velocity; // m/s, grid axes
	vec4 angular_velocity; // rad/s, grid axes
};

layout(set = 0, binding = 0, std140) uniform Params {
	ivec4 size; // nx, ny, nz, emitter count
	vec4 grid; // dx (m), dt (s), 1 / dx, max speed (m/s)
	vec4 up; // world up in grid axes (unit), w: buoyancy (x physical)
	vec4 wind; // m/s, grid axes, w: relaxation per step (1 - exp(-rate dt))
	vec4 forces; // x smoke weight (m/s^2 per unit), y vorticity confinement, z turbulence (m/s^2), w turbulence frequency (1/cell)
	vec4 decay; // x smoke kept per step, y temperature kept per step, z ignition (K), w fuel burnt per step when hot (fraction)
	vec4 combustion; // x heat (K per unit fuel), y soot (smoke per unit fuel), z expansion (1/s per unit fuel per s), w SOR omega
	ivec4 flags; // x closed faces (bit per face: -x +x -y +y -z +z), y inflow faces, z obstacle count, w MacCormack
	vec4 misc; // x time (s), y dx^2, z ambient temperature (K), w unused
	vec4 emission; // the fire light's emission: x intensity, y cell volume (m^3), z colour temperature scale, w unused
	Emitter emitters[MAX_EMITTERS];
	Obstacle obstacles[MAX_OBSTACLES];
}
params;

ivec3 N() {
	return params.size.xyz;
}

float dx() {
	return params.grid.x;
}

float dt() {
	return params.grid.y;
}

// Components by a run-time index without dynamic vector indexing (re-spirv, Godot's SPIR-V optimizer, rejects
// OpVectorExtractDynamic / OpVectorInsertDynamic).
float comp(vec3 p_v, int p_c) {
	return p_c == 0 ? p_v.x : (p_c == 1 ? p_v.y : p_v.z);
}

int icomp(ivec3 p_v, int p_c) {
	return p_c == 0 ? p_v.x : (p_c == 1 ? p_v.y : p_v.z);
}

vec3 with_comp(vec3 p_v, int p_c, float p_x) {
	return vec3(p_c == 0 ? p_x : p_v.x, p_c == 1 ? p_x : p_v.y, p_c == 2 ? p_x : p_v.z);
}

ivec3 iwith_comp(ivec3 p_v, int p_c, int p_x) {
	return ivec3(p_c == 0 ? p_x : p_v.x, p_c == 1 ? p_x : p_v.y, p_c == 2 ? p_x : p_v.z);
}

ivec3 unit(int p_c) {
	return ivec3(p_c == 0 ? 1 : 0, p_c == 1 ? 1 : 0, p_c == 2 ? 1 : 0);
}

bool face_closed(int p_face) {
	return (params.flags.x & (1 << p_face)) != 0;
}

bool face_inflow(int p_face) {
	return (params.flags.y & (1 << p_face)) != 0;
}

// 0 closed, 1 open, 2 inflow (open, the wind blowing in at its velocity).
int face_kind(int p_face) {
	return face_closed(p_face) ? 0 : (face_inflow(p_face) ? 2 : 1);
}

bool outside_open(vec3 p_g) {
	for (int a = 0; a < 3; a++) {
		if (comp(p_g, a) < 0.0 && !face_closed(2 * a)) {
			return true;
		}
		if (comp(p_g, a) > float(icomp(N(), a)) && !face_closed(2 * a + 1)) {
			return true;
		}
	}
	return false;
}

// The velocity components' lattices: texel coordinate = grid position - offset.
vec3 vel_offset(int p_c) {
	return p_c == 0 ? vec3(0.0, 0.5, 0.5) : (p_c == 1 ? vec3(0.5, 0.0, 0.5) : vec3(0.5, 0.5, 0.0));
}

// The face a velocity texel holds for component c (the other axes clamped to the last cell).
vec3 face_pos(ivec3 p_t, int p_c) {
	return with_comp(vec3(min(p_t, N() - 1)) + 0.5, p_c, float(icomp(p_t, p_c)));
}

vec3 solid_velocity(uint p_id, vec3 p_g) {
	if (p_id == 0u || p_id > uint(MAX_OBSTACLES)) {
		return vec3(0.0);
	}
	Obstacle o = params.obstacles[p_id - 1u];
	vec3 r = (p_g - o.centre_type.xyz) * dx();
	return o.linear_velocity.xyz + cross(o.angular_velocity.xyz, r);
}

// Integer hash value noise (PCG): the same on every GPU.
uint pcg(uint p_v) {
	uint state = p_v * 747796405u + 2891336453u;
	uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
	return (word >> 22u) ^ word;
}

float hash3(ivec3 p_c, uint p_seed) {
	uint h = pcg(uint(p_c.x) + pcg(uint(p_c.y) + pcg(uint(p_c.z) + p_seed)));
	return float(h & 0xffffffu) * (1.0 / 16777216.0);
}

float value_noise(vec3 p_p, uint p_seed) {
	vec3 i = floor(p_p);
	vec3 f = p_p - i;
	f = f * f * (3.0 - 2.0 * f);
	ivec3 c = ivec3(i);
	float n000 = hash3(c, p_seed);
	float n100 = hash3(c + ivec3(1, 0, 0), p_seed);
	float n010 = hash3(c + ivec3(0, 1, 0), p_seed);
	float n110 = hash3(c + ivec3(1, 1, 0), p_seed);
	float n001 = hash3(c + ivec3(0, 0, 1), p_seed);
	float n101 = hash3(c + ivec3(1, 0, 1), p_seed);
	float n011 = hash3(c + ivec3(0, 1, 1), p_seed);
	float n111 = hash3(c + ivec3(1, 1, 1), p_seed);
	return mix(mix(mix(n000, n100, f.x), mix(n010, n110, f.x), f.y), mix(mix(n001, n101, f.x), mix(n011, n111, f.x), f.y), f.z);
}

#if defined(MODE_EMIT) || defined(MODE_FACE_FORCE)
// The emitter's weight at a grid position: 1 inside, 0 outside, a one-cell ramp at the edge.
float emitter_weight(int p_e, vec3 p_g) {
	Emitter e = params.emitters[p_e];
	vec3 d = p_g - e.centre_radius.xyz;
	vec3 l = vec3(dot(e.rot0.xyz, d), dot(e.rot1.xyz, d), dot(e.rot2.xyz, d));
	float dist;
	int shape = int(e.shape.x);
	if (shape == 1) {
		vec3 q = abs(l) - vec3(e.rot0.w, e.rot1.w, e.rot2.w);
		dist = max(q.x, max(q.y, q.z));
	} else if (shape == 2) {
		dist = max(length(l.xz) - e.centre_radius.w, abs(l.y) - e.rot0.w);
	} else {
		dist = length(l) - e.centre_radius.w;
	}
	return clamp(0.5 - dist, 0.0, 1.0);
}
#endif

/* clang-format off */

#ifdef MODE_OBSTACLES
layout(r8ui, set = 0, binding = 1) uniform restrict writeonly uimage3D solid_out;

void main() {
	ivec3 c = ivec3(gl_GlobalInvocationID);
	if (any(greaterThanEqual(c, N()))) {
		return;
	}
	vec3 g = vec3(c) + 0.5;
	uint id = 0u;
	for (int k = 0; k < params.flags.z; k++) {
		Obstacle o = params.obstacles[k];
		vec3 d = g - o.centre_type.xyz;
		vec3 l = vec3(dot(o.rot0.xyz, d), dot(o.rot1.xyz, d), dot(o.rot2.xyz, d));
		int type = int(o.centre_type.w);
		bool inside;
		if (type == 1) {
			inside = all(lessThanEqual(abs(l), vec3(o.rot0.w, o.rot1.w, o.rot2.w)));
		} else if (type == 2) {
			vec3 q = l;
			q.y -= clamp(q.y, -o.rot1.w, o.rot1.w);
			inside = length(q) <= o.rot0.w;
		} else if (type == 3) {
			inside = length(l.xz) <= o.rot0.w && abs(l.y) <= o.rot1.w;
		} else if (type == 4) {
			inside = l.y <= 0.0; // below a plane (the y axis its normal)
		} else {
			inside = length(l) <= o.rot0.w;
		}
		if (inside) {
			id = uint(k + 1);
		}
	}
	imageStore(solid_out, c, uvec4(id));
}
#endif

#ifdef MODE_EMIT
layout(FMT, set = 0, binding = 1) uniform restrict image3D scalars;
layout(r8ui, set = 0, binding = 2) uniform restrict readonly uimage3D solid;

void main() {
	ivec3 c = ivec3(gl_GlobalInvocationID);
	if (any(greaterThanEqual(c, N()))) {
		return;
	}
	if (imageLoad(solid, c).r != 0u) {
		imageStore(scalars, c, vec4(0.0));
		return;
	}
	vec3 g = vec3(c) + 0.5;
	vec4 s = imageLoad(scalars, c);
	bool changed = false;
	for (int e = 0; e < params.size.w; e++) {
		float w = emitter_weight(e, g);
		if (w <= 0.0) {
			continue;
		}
		Emitter em = params.emitters[e];
		if (em.rates.w > 0.0) {
			// Flicker: value noise drifting upwards through the emitter.
			float n = value_noise(g * em.shape.y - params.up.xyz * (params.misc.x * 2.0), uint(e) * 7919u + 17u);
			w *= max(1.0 + em.rates.w * (2.0 * n - 1.0), 0.0);
		}
		s.x += w * em.rates.x * dt();
		s.z += w * em.rates.z * dt();
		if (em.shape.z > 0.5) {
			s.y += w * em.rates.y * dt();
		} else {
			s.y = max(s.y, w * em.rates.y);
		}
		changed = true;
	}
	if (changed) {
		imageStore(scalars, c, s);
	}
}
#endif

#if defined(MODE_ADVECT_S1) || defined(MODE_ADVECT_S2) || defined(MODE_ADVECT_V1) || defined(MODE_ADVECT_V2)
layout(set = 0, binding = 1) uniform sampler3D field; // the field advected (scalars or velocity), linear, clamped
layout(set = 0, binding = 2) uniform sampler3D velocity; // the velocity (for the scalars: the same as field for V*)
layout(set = 0, binding = 3) uniform sampler3D hat; // S2 / V2: pass 1's result
layout(FMT, set = 0, binding = 4) uniform restrict writeonly image3D dest;
layout(r8ui, set = 0, binding = 5) uniform restrict readonly uimage3D solid;

vec3 sample_velocity(vec3 p_g) {
	vec3 inv = 1.0 / vec3(N() + 1);
	float u = textureLod(velocity, (p_g + vec3(0.5, 0.0, 0.0)) * inv, 0.0).x;
	float v = textureLod(velocity, (p_g + vec3(0.0, 0.5, 0.0)) * inv, 0.0).y;
	float w = textureLod(velocity, (p_g + vec3(0.0, 0.0, 0.5)) * inv, 0.0).z;
	return vec3(u, v, w);
}

vec4 react(vec4 p_s) {
	float burnt = p_s.y > params.decay.z ? p_s.z * params.decay.w : 0.0;
	p_s.z -= burnt;
	p_s.y += params.combustion.x * burnt;
	p_s.x += params.combustion.y * burnt;
	p_s.w = burnt / dt();
	p_s.x *= params.decay.x;
	p_s.y *= params.decay.y;
	return vec4(max(p_s.x, 0.0), max(p_s.y, 0.0), max(p_s.z, 0.0), p_s.w);
}
#endif

#ifdef MODE_ADVECT_S1
void main() {
	ivec3 c = ivec3(gl_GlobalInvocationID);
	if (any(greaterThanEqual(c, N()))) {
		return;
	}
	vec3 g = vec3(c) + 0.5;
	vec3 gb = g - sample_velocity(g) * (dt() * params.grid.z);
	vec4 r = outside_open(gb) ? vec4(0.0) : textureLod(field, gb / vec3(N()), 0.0);
	if (params.flags.w == 0) {
		// Semi-Lagrangian only: this is the step's result.
		r = imageLoad(solid, c).r != 0u ? vec4(0.0) : react(r);
	}
	imageStore(dest, c, r);
}
#endif

#ifdef MODE_ADVECT_S2
void main() {
	ivec3 c = ivec3(gl_GlobalInvocationID);
	if (any(greaterThanEqual(c, N()))) {
		return;
	}
	vec3 g = vec3(c) + 0.5;
	vec3 u = sample_velocity(g) * (dt() * params.grid.z);
	vec3 gb = g - u;
	vec4 h = texelFetch(hat, c, 0);
	vec4 r = h;
	if (!outside_open(gb)) {
		vec4 tilde = textureLod(hat, (g + u) / vec3(N()), 0.0);
		r = h + 0.5 * (texelFetch(field, c, 0) - tilde);
		// The limiter: within the eight values the back-traced point was interpolated from.
		ivec3 f = ivec3(floor(gb - 0.5));
		ivec3 i0 = clamp(f, ivec3(0), N() - 1);
		ivec3 i1 = clamp(f + 1, ivec3(0), N() - 1);
		vec4 lo = vec4(1e30);
		vec4 hi = vec4(-1e30);
		for (int k = 0; k < 8; k++) {
			ivec3 q = ivec3((k & 1) != 0 ? i1.x : i0.x, (k & 2) != 0 ? i1.y : i0.y, (k & 4) != 0 ? i1.z : i0.z);
			vec4 v = texelFetch(field, q, 0);
			lo = min(lo, v);
			hi = max(hi, v);
		}
		r = clamp(r, lo, hi);
	}
	r = imageLoad(solid, c).r != 0u ? vec4(0.0) : react(r);
	imageStore(dest, c, r);
}
#endif

#if defined(MODE_ADVECT_V1) || defined(MODE_ADVECT_V2)
float sample_component(sampler3D p_tex, vec3 p_g, int p_c) {
	return comp(textureLod(p_tex, (p_g - vel_offset(p_c) + 0.5) / vec3(N() + 1), 0.0).xyz, p_c);
}
#endif

#ifdef MODE_ADVECT_V1
void main() {
	ivec3 t = ivec3(gl_GlobalInvocationID);
	if (any(greaterThan(t, N()))) {
		return;
	}
	vec3 r = vec3(0.0);
	for (int c = 0; c < 3; c++) {
		vec3 g = face_pos(t, c);
		vec3 gb = g - sample_velocity(g) * (dt() * params.grid.z);
		r = with_comp(r, c, sample_component(field, gb, c));
	}
	imageStore(dest, t, vec4(r, 0.0));
}
#endif

#ifdef MODE_ADVECT_V2
void main() {
	ivec3 t = ivec3(gl_GlobalInvocationID);
	if (any(greaterThan(t, N()))) {
		return;
	}
	vec3 h = texelFetch(hat, t, 0).xyz;
	vec3 old = texelFetch(field, t, 0).xyz;
	vec3 r = vec3(0.0);
	for (int c = 0; c < 3; c++) {
		vec3 g = face_pos(t, c);
		vec3 u = sample_velocity(g) * (dt() * params.grid.z);
		vec3 gb = g - u;
		float tilde = sample_component(hat, g + u, c);
		float v = comp(h, c) + 0.5 * (comp(old, c) - tilde);
		vec3 tc = gb - vel_offset(c);
		ivec3 f = ivec3(floor(tc));
		ivec3 i0 = clamp(f, ivec3(0), N());
		ivec3 i1 = clamp(f + 1, ivec3(0), N());
		float lo = 1e30;
		float hi = -1e30;
		for (int k = 0; k < 8; k++) {
			ivec3 q = ivec3((k & 1) != 0 ? i1.x : i0.x, (k & 2) != 0 ? i1.y : i0.y, (k & 4) != 0 ? i1.z : i0.z);
			float x = comp(texelFetch(field, q, 0).xyz, c);
			lo = min(lo, x);
			hi = max(hi, x);
		}
		r = with_comp(r, c, clamp(v, lo, hi));
	}
	imageStore(dest, t, vec4(r, 0.0));
}
#endif

#ifdef MODE_CURL
layout(FMT, set = 0, binding = 1) uniform restrict readonly image3D vel;
layout(FMT, set = 0, binding = 4) uniform restrict writeonly image3D dest;

vec3 cell_velocity(ivec3 p_c) {
	vec3 a = imageLoad(vel, p_c).xyz;
	return 0.5 * vec3(a.x + imageLoad(vel, p_c + ivec3(1, 0, 0)).x, a.y + imageLoad(vel, p_c + ivec3(0, 1, 0)).y, a.z + imageLoad(vel, p_c + ivec3(0, 0, 1)).z);
}

void main() {
	ivec3 c = ivec3(gl_GlobalInvocationID);
	if (any(greaterThanEqual(c, N()))) {
		return;
	}
	ivec3 n = N() - 1;
	vec3 xp = cell_velocity(min(c + ivec3(1, 0, 0), n));
	vec3 xm = cell_velocity(max(c - ivec3(1, 0, 0), ivec3(0)));
	vec3 yp = cell_velocity(min(c + ivec3(0, 1, 0), n));
	vec3 ym = cell_velocity(max(c - ivec3(0, 1, 0), ivec3(0)));
	vec3 zp = cell_velocity(min(c + ivec3(0, 0, 1), n));
	vec3 zm = cell_velocity(max(c - ivec3(0, 0, 1), ivec3(0)));
	float k = 1.0 / (2.0 * dx());
	vec3 w = vec3((yp.z - ym.z) * k - (zp.y - zm.y) * k, (zp.x - zm.x) * k - (xp.z - xm.z) * k, (xp.y - xm.y) * k - (yp.x - ym.x) * k);
	imageStore(dest, c, vec4(w, length(w)));
}
#endif

#ifdef MODE_CELL_FORCE
layout(FMT, set = 0, binding = 1) uniform restrict readonly image3D curl;
layout(FMT, set = 0, binding = 2) uniform restrict readonly image3D scalars;
layout(FMT, set = 0, binding = 4) uniform restrict writeonly image3D dest;
layout(r8ui, set = 0, binding = 5) uniform restrict readonly uimage3D solid;

void main() {
	ivec3 c = ivec3(gl_GlobalInvocationID);
	if (any(greaterThanEqual(c, N()))) {
		return;
	}
	if (imageLoad(solid, c).r != 0u) {
		imageStore(dest, c, vec4(0.0));
		return;
	}
	ivec3 n = N() - 1;
	vec4 w = imageLoad(curl, c);
	float k = 1.0 / (2.0 * dx());
	vec3 eta = vec3(imageLoad(curl, min(c + ivec3(1, 0, 0), n)).w - imageLoad(curl, max(c - ivec3(1, 0, 0), ivec3(0))).w,
					   imageLoad(curl, min(c + ivec3(0, 1, 0), n)).w - imageLoad(curl, max(c - ivec3(0, 1, 0), ivec3(0))).w,
					   imageLoad(curl, min(c + ivec3(0, 0, 1), n)).w - imageLoad(curl, max(c - ivec3(0, 0, 1), ivec3(0))).w) *
			k;
	vec3 nrm = eta / (length(eta) + 1e-5);
	vec3 f = params.forces.y * dx() * cross(nrm, w.xyz);
	vec4 s = imageLoad(scalars, c);
	float a_up = params.up.w * 9.81 * s.y / (params.misc.z + s.y) - params.forces.x * s.x;
	f += a_up * params.up.xyz;
	if (params.forces.z > 0.0) {
		// Turbulence: a noise force where the gas is hot, drifting upwards with it.
		vec3 p = (vec3(c) + 0.5) * params.forces.w - params.up.xyz * (params.misc.x * 1.5);
		vec3 tn = vec3(value_noise(p, 101u), value_noise(p, 211u), value_noise(p, 307u)) * 2.0 - 1.0;
		f += tn * (params.forces.z * clamp(s.y * (1.0 / 300.0), 0.0, 1.0));
	}
	imageStore(dest, c, vec4(f, 0.0));
}
#endif

#ifdef MODE_FACE_FORCE
layout(FMT, set = 0, binding = 1) uniform restrict readonly image3D force; // per cell (in the velocity-sized texture)
layout(FMT, set = 0, binding = 4) uniform restrict image3D vel;

void main() {
	ivec3 t = ivec3(gl_GlobalInvocationID);
	if (any(greaterThan(t, N()))) {
		return;
	}
	vec4 v4 = imageLoad(vel, t);
	vec3 v = v4.xyz;
	ivec3 tc = min(t, N() - 1);
	for (int c = 0; c < 3; c++) {
		ivec3 a = iwith_comp(tc, c, max(icomp(t, c) - 1, 0));
		ivec3 b = iwith_comp(tc, c, min(icomp(t, c), icomp(N(), c) - 1));
		float x = comp(v, c) + dt() * 0.5 * (comp(imageLoad(force, a).xyz, c) + comp(imageLoad(force, b).xyz, c));
		vec3 g = face_pos(t, c);
		for (int e = 0; e < params.size.w; e++) {
			float blend = params.emitters[e].velocity.w;
			if (blend <= 0.0) {
				continue;
			}
			float w = emitter_weight(e, g) * blend;
			if (w > 0.0) {
				x += (comp(params.emitters[e].velocity.xyz, c) - x) * min(w, 1.0);
			}
		}
		x += (comp(params.wind.xyz, c) - x) * params.wind.w;
		v = with_comp(v, c, x);
	}
	imageStore(vel, t, vec4(v, v4.w));
}
#endif

#ifdef MODE_DIVERGENCE
layout(FMT, set = 0, binding = 1) uniform restrict readonly image3D vel;
layout(FMT, set = 0, binding = 2) uniform restrict readonly image3D scalars;
layout(r32f, set = 0, binding = 4) uniform restrict writeonly image3D rhs;
layout(r8ui, set = 0, binding = 5) uniform restrict readonly uimage3D solid;

void main() {
	ivec3 c = ivec3(gl_GlobalInvocationID);
	if (any(greaterThanEqual(c, N()))) {
		return;
	}
	if (imageLoad(solid, c).r != 0u) {
		imageStore(rhs, c, vec4(0.0));
		return;
	}
	vec3 lo = imageLoad(vel, c).xyz;
	vec3 hi = vec3(imageLoad(vel, c + ivec3(1, 0, 0)).x, imageLoad(vel, c + ivec3(0, 1, 0)).y, imageLoad(vel, c + ivec3(0, 0, 1)).z);
	float div = 0.0;
	for (int a = 0; a < 3; a++) {
		ivec3 e = unit(a);
		float l = comp(lo, a);
		float h = comp(hi, a);
		if (icomp(c, a) > 0) {
			uint id = imageLoad(solid, c - e).r;
			if (id != 0u) {
				l = comp(solid_velocity(id, vec3(c - e) + 0.5), a);
			}
		} else {
			int kind = face_kind(2 * a);
			if (kind != 1) {
				l = kind == 2 ? comp(params.wind.xyz, a) : 0.0;
			}
		}
		if (icomp(c, a) < icomp(N(), a) - 1) {
			uint id = imageLoad(solid, c + e).r;
			if (id != 0u) {
				h = comp(solid_velocity(id, vec3(c + e) + 0.5), a);
			}
		} else {
			int kind = face_kind(2 * a + 1);
			if (kind != 1) {
				h = kind == 2 ? comp(params.wind.xyz, a) : 0.0;
			}
		}
		div += (h - l) * params.grid.z;
	}
	float r = div - params.combustion.z * imageLoad(scalars, c).w;
	imageStore(rhs, c, vec4(r));
}
#endif

#ifdef MODE_PRESSURE
layout(push_constant, std430) uniform Push {
	int colour; // the red-black colour
	int pad0;
	int pad1;
	int pad2;
}
push;

layout(r32f, set = 0, binding = 1) uniform restrict readonly image3D rhs;
layout(r32f, set = 0, binding = 4) uniform restrict image3D pressure;
layout(r8ui, set = 0, binding = 5) uniform restrict readonly uimage3D solid;

void main() {
	ivec3 t = ivec3(gl_GlobalInvocationID);
	ivec3 c = ivec3(2 * t.x + ((t.y + t.z + push.colour) & 1), t.y, t.z);
	if (any(greaterThanEqual(c, N()))) {
		return;
	}
	if (imageLoad(solid, c).r != 0u) {
		imageStore(pressure, c, vec4(0.0));
		return;
	}
	float sum = 0.0;
	float count = 0.0;
	for (int a = 0; a < 3; a++) {
		ivec3 e = unit(a);
		if (icomp(c, a) > 0) {
			if (imageLoad(solid, c - e).r == 0u) {
				sum += imageLoad(pressure, c - e).r;
				count += 1.0;
			}
		} else if (face_kind(2 * a) == 1) {
			count += 1.0; // open: zero pressure outside
		}
		if (icomp(c, a) < icomp(N(), a) - 1) {
			if (imageLoad(solid, c + e).r == 0u) {
				sum += imageLoad(pressure, c + e).r;
				count += 1.0;
			}
		} else if (face_kind(2 * a + 1) == 1) {
			count += 1.0;
		}
	}
	float q = imageLoad(pressure, c).r;
	float gs = count > 0.0 ? (sum - params.misc.y * imageLoad(rhs, c).r) / count : 0.0;
	q = (1.0 - params.combustion.w) * q + params.combustion.w * gs;
	imageStore(pressure, c, vec4(q));
}
#endif

#ifdef MODE_PROJECT
layout(r32f, set = 0, binding = 1) uniform restrict readonly image3D pressure;
layout(FMT, set = 0, binding = 4) uniform restrict image3D vel;
layout(r8ui, set = 0, binding = 5) uniform restrict readonly uimage3D solid;

void main() {
	ivec3 t = ivec3(gl_GlobalInvocationID);
	if (any(greaterThan(t, N()))) {
		return;
	}
	vec4 v4 = imageLoad(vel, t);
	vec3 v = v4.xyz;
	ivec3 tc = min(t, N() - 1);
	for (int c = 0; c < 3; c++) {
		bool low = icomp(t, c) == 0;
		bool high = icomp(t, c) == icomp(N(), c);
		ivec3 a = iwith_comp(tc, c, max(icomp(t, c) - 1, 0));
		ivec3 b = iwith_comp(tc, c, min(icomp(t, c), icomp(N(), c) - 1));
		float qa = low ? 0.0 : imageLoad(pressure, a).r;
		float qb = high ? 0.0 : imageLoad(pressure, b).r;
		uint ida = low ? 0u : imageLoad(solid, a).r;
		uint idb = high ? 0u : imageLoad(solid, b).r;
		float x = comp(v, c) - (qb - qa) * params.grid.z;
		if (ida != 0u || idb != 0u) {
			float sva = comp(solid_velocity(ida, vec3(a) + 0.5), c);
			float svb = comp(solid_velocity(idb, vec3(b) + 0.5), c);
			x = (ida != 0u && idb != 0u) ? 0.5 * (sva + svb) : (ida != 0u ? sva : svb);
		}
		if (low && face_kind(2 * c) != 1) {
			x = face_kind(2 * c) == 2 ? comp(params.wind.xyz, c) : 0.0;
		}
		if (high && face_kind(2 * c + 1) != 1) {
			x = face_kind(2 * c + 1) == 2 ? comp(params.wind.xyz, c) : 0.0;
		}
		v = with_comp(v, c, clamp(x, -params.grid.w, params.grid.w));
	}
	imageStore(vel, t, vec4(v, v4.w));
}
#endif

#ifdef MODE_COARSE
layout(FMT, set = 0, binding = 1) uniform restrict readonly image3D scalars;
layout(rg16f, set = 0, binding = 4) uniform restrict writeonly image3D coarse;

layout(set = 0, binding = 6, std430) restrict writeonly buffer Emission {
	vec4 data[]; // per workgroup: x luminance, yzw luminance-weighted grid position; then rgb emission, w unused
}
emission_out;

// The fire's emission per metre (the renderer's fellow_gas_render.glsl, emission_rgb(), gives the same).
vec3 blackbody(float p_t) {
	// Krystek's (1985) approximation of the Planckian locus in CIE 1960 uv, to linear sRGB of luminance 1.
	float t = clamp(p_t, 1000.0, 15000.0);
	float u = (0.860117757 + 1.54118254e-4 * t + 1.28641212e-7 * t * t) / (1.0 + 8.42420235e-4 * t + 7.08145163e-7 * t * t);
	float v = (0.317398726 + 4.22806245e-5 * t + 4.20481691e-8 * t * t) / (1.0 - 2.89741816e-5 * t + 1.61456053e-7 * t * t);
	float x = 3.0 * u / (2.0 * u - 8.0 * v + 4.0);
	float y = 2.0 * v / (2.0 * u - 8.0 * v + 4.0);
	vec3 xyz = vec3(x / y, 1.0, (1.0 - x - y) / y);
	vec3 rgb = mat3(vec3(3.2404542, -0.9692660, 0.0556434), vec3(-1.5371385, 1.8760108, -0.2040259), vec3(-0.4985314, 0.0415560, 1.0572252)) * xyz;
	rgb = max(rgb, vec3(0.0));
	return rgb / max(dot(rgb, vec3(0.2126, 0.7152, 0.0722)), 1e-4);
}

vec3 emission_rgb(float p_temperature) {
	float t = params.misc.z + p_temperature;
	float k = t * (1.0 / 1500.0);
	return blackbody(t * params.emission.z) * (params.emission.x * k * k * k * k * smoothstep(700.0, 1000.0, t));
}

shared vec4 s_sum[64];
shared vec4 s_rgb[64];

void main() {
	ivec3 b = ivec3(gl_GlobalInvocationID);
	ivec3 nb = (N() + 3) / 4;
	uint li = gl_LocalInvocationIndex;
	vec4 sum = vec4(0.0);
	vec3 rgb = vec3(0.0);
	if (all(lessThan(b, nb))) {
		// The brick's largest values over its cells and their neighbours (what trilinear sampling inside it reads).
		ivec3 lo = max(b * 4 - 1, ivec3(0));
		ivec3 hi = min(b * 4 + 4, N() - 1);
		vec2 m = vec2(0.0);
		for (int z = lo.z; z <= hi.z; z++) {
			for (int y = lo.y; y <= hi.y; y++) {
				for (int x = lo.x; x <= hi.x; x++) {
					ivec3 c = ivec3(x, y, z);
					vec4 s = imageLoad(scalars, c);
					m = max(m, s.xy);
					if (all(greaterThanEqual(c, b * 4)) && all(lessThan(c, b * 4 + 4)) && s.y > 300.0) {
						vec3 e = emission_rgb(s.y) * params.emission.y;
						float l = dot(e, vec3(0.2126, 0.7152, 0.0722));
						sum += vec4(l, l * (vec3(c) + 0.5));
						rgb += e;
					}
				}
			}
		}
		imageStore(coarse, b, vec4(m, 0.0, 0.0));
	}
	s_sum[li] = sum;
	s_rgb[li] = vec4(rgb, 0.0);
	barrier();
	for (uint s = 32u; s > 0u; s >>= 1u) {
		if (li < s) {
			s_sum[li] += s_sum[li + s];
			s_rgb[li] += s_rgb[li + s];
		}
		barrier();
	}
	if (li == 0u) {
		uvec3 groups = gl_NumWorkGroups;
		uint gi = gl_WorkGroupID.x + groups.x * (gl_WorkGroupID.y + groups.y * gl_WorkGroupID.z);
		emission_out.data[gi * 2u] = s_sum[0];
		emission_out.data[gi * 2u + 1u] = s_rgb[0];
	}
}
#endif

/* clang-format on */
