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
//   COARSE      per 4 x 4 x 4 brick the largest smoke density, temperature and fuel (empty space skipped by the
//               renderer; the sparse bricks' sources) and per workgroup the fire's emission (the fire light, read
//               back asynchronously).
//
// Sparse bricks (params.sparse.x): SPARSE_SOURCE marks the 4^3 bricks with smoke, heat or fuel or an emitter,
// SPARSE_ACTIVATE the bricks within the margin of a source (A: their cells are simulated) and within one brick more
// (F: their velocity is), gives what leaves them ambient values (no smoke, the wind, zero pressure) and lists them;
// the passes then run over the lists (one workgroup per brick, indirect dispatches), the pressure outside A zero.
//
// The grid (tests/gas_reference.py is this file in NumPy): cells (i, j, k) with centres at grid position
// (i + 0.5, j + 0.5, k + 0.5), dx metres each. Scalars at the centres: x smoke, y temperature above ambient (K),
// z fuel, w burn rate (fuel / s). The velocity is staggered (MAC) in one (nx + 1) x (ny + 1) x (nz + 1) texture: x at
// the x faces (i, j + .5, k + .5), y at the y faces, z at the z faces; the texels past a component's last face repeat
// it (each pass computes the clamped face), so clamp-to-edge sampling extends the field with a zero gradient.

#ifdef MODE_MG_TAIL
#define TAIL_THREADS 256
layout(local_size_x = TAIL_THREADS, local_size_y = 1, local_size_z = 1) in;
#else
layout(local_size_x = 4, local_size_y = 4, local_size_z = 4) in;
#endif

#ifdef HIGH_PRECISION
#define FMT rgba32f
#else
#define FMT rgba16f
#endif

#define MAX_EMITTERS 16
#define MAX_OBSTACLES 16
#define MAX_MG_LEVELS 12
#define MAX_BURNABLES 8 // burning objects coupled with a volume (FellowBurnable3D, decision 0120)
#define BURN_AIR_MAX 350.0 // K above ambient: the most a hot surface heats the air of a cell next to it
#define ZERO_CELL 255u // a coarse level's cell (solid texture) whose children are all outside the simulated bricks
// Sparse bricks: a brick is a source where its largest smoke, temperature (K) or fuel is above these.
#define SPARSE_SMOKE 1e-3
#define SPARSE_TEMPERATURE 1.0
#define SPARSE_FUEL 1e-3

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
	vec4 misc; // x time (s), y dx^2, z ambient temperature (K), w the flame's temperature limit (K above ambient; 0 none)
	vec4 emission; // the fire light's emission: x intensity, y cell volume (m^3), z colour temperature scale, w unused
	ivec4 sparse; // x 1: sparse bricks, y the F list's start in the brick list, z the margin (bricks), w 1: every brick counts as simulated before (ACTIVATE)
	Emitter emitters[MAX_EMITTERS];
	Obstacle obstacles[MAX_OBSTACLES];
	// The multigrid's coarser levels (index 1 = the first coarser, 0 unused), all in one atlas texture per field:
	ivec4 mg_levels[MAX_MG_LEVELS]; // xyz size, w x offset in the atlas
	vec4 mg_open_lo[MAX_MG_LEVELS]; // xyz the open faces' weights on the low sides, w dx^2 of the level
	vec4 mg_open_hi[MAX_MG_LEVELS]; // xyz on the high sides
	// Burning objects (their INJECT textures: per cell of a region of this grid, x fuel released (per s per m^3),
	// y the surface's temperature, z its exposure, w the solid fraction).
	ivec4 burn_count; // x
	ivec4 burn_origin[MAX_BURNABLES]; // the region's first cell
	ivec4 burn_size[MAX_BURNABLES];
	vec4 burn_params[MAX_BURNABLES]; // x smoke per unit of fuel released, y the air's heating rate (1/s) at full exposure,
	// z the fuel burning where it is released, w the flicker of the fuel released (0 none)
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

#if defined(MODE_OBSTACLES) || defined(MODE_EMIT) || defined(MODE_ADVECT_S1) || defined(MODE_ADVECT_S2) || defined(MODE_ADVECT_V1) || defined(MODE_ADVECT_V2) || defined(MODE_CURL) || defined(MODE_CELL_FORCE) || defined(MODE_FACE_FORCE) || defined(MODE_DIVERGENCE) || defined(MODE_PRESSURE) || defined(MODE_MG_PROLONG) || defined(MODE_PROJECT)
#define SPARSE_PASS
// The simulated bricks (SPARSE_ACTIVATE): A from 0, F from params.sparse.y; x | y << 10 | z << 20 each.
layout(set = 0, binding = 14, std430) restrict readonly buffer Bricks {
	uint list[];
}
bricks;

// The cell (texel) a workgroup's invocation handles in the listed brick (lists from p_start: A 0, F params.sparse.y,
// the first coarser level's bricks 2 * params.sparse.y).
ivec3 sparse_id_from(uint p_start) {
	uint e = bricks.list[p_start + gl_WorkGroupID.x];
	return ivec3(int(e & 1023u), int((e >> 10u) & 1023u), int(e >> 20u)) * 4 + ivec3(gl_LocalInvocationID);
}

ivec3 sparse_id(bool p_faces) {
	return sparse_id_from(p_faces ? uint(params.sparse.y) : 0u);
}

// PRESSURE, PROLONG: push.offsets.z 1 the grid's A, 2 the first coarser level's bricks.
uint level_list_start(int p_which) {
	return p_which == 1 ? 0u : 2u * uint(params.sparse.y);
}

ivec3 cell_id(bool p_faces) {
	return params.sparse.x != 0 ? sparse_id(p_faces) : ivec3(gl_GlobalInvocationID);
}
#endif

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

#if defined(MODE_OBSTACLES) || defined(MODE_EMIT) || defined(MODE_SPARSE_SOURCE)
layout(set = 0, binding = 16) uniform sampler3D burn_inject[MAX_BURNABLES]; // texelFetch

// Burnable p_b's injection at cell p_c, or zero outside its region.
vec4 burn_at(int p_b, ivec3 p_c) {
	ivec3 q = p_c - params.burn_origin[p_b].xyz;
	if (any(lessThan(q, ivec3(0))) || any(greaterThanEqual(q, params.burn_size[p_b].xyz))) {
		return vec4(0.0);
	}
	return texelFetch(burn_inject[p_b], q, 0);
}
#endif

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

#ifdef MODE_SHIFT
// The box moved by whole cells (a volume in world space following its node): the state shifted by the offset into the
// other buffers; the cells and faces coming in get fresh air (the wind's velocity).
layout(push_constant, std430) uniform Push {
	ivec4 size; // xyz: the offset (cells): new cell c holds old cell c + offset
	vec4 level; // unused
	ivec4 coarse_size; // unused
	vec4 open_lo; // unused
	vec4 open_hi; // unused
	ivec4 offsets; // unused
}
push;

layout(FMT, set = 0, binding = 1) uniform restrict readonly image3D scalars_in;
layout(FMT, set = 0, binding = 2) uniform restrict readonly image3D vel_in;
layout(FMT, set = 0, binding = 3) uniform restrict writeonly image3D scalars_out;
layout(FMT, set = 0, binding = 4) uniform restrict writeonly image3D vel_out;

void main() {
	ivec3 t = ivec3(gl_GlobalInvocationID);
	if (any(greaterThan(t, N()))) {
		return;
	}
	ivec3 src = t + push.size.xyz;
	if (all(lessThan(t, N()))) {
		bool inside = all(greaterThanEqual(src, ivec3(0))) && all(lessThan(src, N()));
		imageStore(scalars_out, t, inside ? imageLoad(scalars_in, src) : vec4(0.0));
	}
	bool inside_v = all(greaterThanEqual(src, ivec3(0))) && all(lessThanEqual(src, N()));
	imageStore(vel_out, t, inside_v ? imageLoad(vel_in, src) : vec4(params.wind.xyz, 0.0));
}
#endif

#ifdef MODE_OBSTACLES
layout(r8ui, set = 0, binding = 1) uniform restrict writeonly uimage3D solid_out;

void main() {
	ivec3 c = cell_id(true);
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
	// Burning objects: their solid (wood and char) where it fills half the cell (still: ids past the obstacles').
	for (int b = 0; b < params.burn_count.x; b++) {
		if (burn_at(b, c).w > 0.5) {
			id = uint(MAX_OBSTACLES + 1 + b);
		}
	}
	imageStore(solid_out, c, uvec4(id));
}
#endif

#ifdef MODE_EMIT
layout(FMT, set = 0, binding = 1) uniform restrict image3D scalars;
layout(r8ui, set = 0, binding = 2) uniform restrict readonly uimage3D solid;

void main() {
	ivec3 c = cell_id(false);
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
	// Burning objects: the air next to a hot surface heated towards its temperature (by its exposure); the volatiles
	// they release (the fuel, some smoke), a part of which (burn_params.z) burns where it is released when the
	// surface or the air there is above the ignition temperature (the flame attached to the surface: the cells are
	// coarser than its stand-off), no hotter than the flame's limit.
	for (int b = 0; b < params.burn_count.x; b++) {
		vec4 v = burn_at(b, c);
		if (v.x > 0.0 || v.y > s.y) {
			// The air next to the hot surface is heated towards its temperature, but its cell no hotter than 350 K above
			// ambient: the hot layer on a surface is far thinner than a cell, and hot air without a flame does not glow
			// (the embers' heat glowed as an orange fog over the ground). Above the ignition temperature still.
			float air = min(v.y, BURN_AIR_MAX);
			if (air > s.y) {
				s.y += (air - s.y) * (1.0 - exp(-params.burn_params[b].y * v.z * dt()));
			}
			float released = v.x * dt();
			if (params.burn_params[b].w > 0.0) {
				// Flicker (as an emitter's): value noise of a 15 cm period drifting upwards through the fuel released.
				float n = value_noise(g * (sqrt(params.misc.y) / 0.15) - params.up.xyz * (params.misc.x * 2.0), uint(b) * 104729u + 31u);
				released *= max(1.0 + params.burn_params[b].w * (2.0 * n - 1.0), 0.0);
			}
			float now = max(v.y, s.y) > params.decay.z ? released * params.burn_params[b].z : 0.0;
			if (params.misc.w > 0.0 && params.combustion.x > 0.0) {
				now = min(now, max(params.misc.w - s.y, 0.0) / params.combustion.x);
			}
			s.z += released - now;
			s.y += params.combustion.x * now;
			s.x += released * params.burn_params[b].x + params.combustion.y * now;
			changed = true;
		}
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
	if (params.misc.w > 0.0 && params.combustion.x > 0.0) {
		// No hotter than the limit (the flame's temperature): the fuel left burns further on, where it is cooler.
		burnt = min(burnt, max(params.misc.w - p_s.y, 0.0) / params.combustion.x);
	}
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
	ivec3 c = cell_id(params.flags.w != 0); // MacCormack: the hat over F
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
	ivec3 c = cell_id(false);
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
	ivec3 t = cell_id(true);
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
	ivec3 t = cell_id(true);
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
	ivec3 c = cell_id(true);
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
	ivec3 c = cell_id(true);
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
	ivec3 t = cell_id(true);
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
	ivec3 c = cell_id(false);
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

#if defined(MODE_PRESSURE) || defined(MODE_MG_RESTRICT) || defined(MODE_MG_PROLONG) || defined(MODE_MG_COARSE) || defined(MODE_MG_TAIL)
// The pressure solve's levels: level 0 is the grid, each coarser level half its size (rounded up), its cells solid where
// all their children are (decision 0117: SOR, or a multigrid V-cycle with red-black Gauss-Seidel smoothing).
layout(push_constant, std430) uniform Push {
	ivec4 size; // this level's size (RESTRICT, PROLONG: the finer level's); w: the red-black colour (PRESSURE)
	vec4 level; // x dx^2 of this level (RESTRICT, PROLONG: the finer level's), y SOR omega, z iterations (COARSE), w unused
	ivec4 coarse_size; // RESTRICT, PROLONG: the coarser level's size
	vec4 open_lo; // xyz: an open face's weight in the stencil on the low sides (this level's; RESTRICT: the finer's)
	vec4 open_hi; // xyz: on the high sides (1 on the grid; on the coarser levels 1 / the distance in cells to the zero)
	ivec4 offsets; // x: this level's x offset in the levels' atlas (RESTRICT, PROLONG: the finer level's), y: the coarser
	// level's (RESTRICT, PROLONG); the grid itself (level 0) has its own textures, offset 0. z: sparse bricks, 1 the grid
	// (PRESSURE, PROLONG over A's list; RESTRICT: a child outside A is a zero cell), 2 the first coarser level
	// (PRESSURE, PROLONG over its bricks with a cell of A's below).
	// TAIL: size.x the first level it solves, size.y the coarsest; level.y omega, level.z the coarsest's iterations.
}
push;

ivec3 LN() {
	return push.size.xyz;
}

ivec3 OFF() {
	return ivec3(push.offsets.x, 0, 0);
}

#ifndef MODE_MG_TAIL
// The neighbours' sum and weights' sum of the Poisson stencil at a cell of a level (solid neighbours and closed or
// inflow faces left out, an open face counted at the level's weight with zero pressure outside).
void stencil(ivec3 p_c, out float r_sum, out float r_count);
#endif
#endif

#if defined(MODE_PRESSURE) || defined(MODE_MG_COARSE)
layout(r32f, set = 0, binding = 1) uniform restrict readonly image3D rhs;
#ifdef MODE_MG_COARSE
layout(r32f, set = 0, binding = 4) uniform restrict coherent image3D pressure;
#else
layout(r32f, set = 0, binding = 4) uniform restrict image3D pressure;
#endif
layout(r8ui, set = 0, binding = 5) uniform restrict readonly uimage3D solid;
#endif

#ifdef MODE_MG_RESTRICT
layout(r32f, set = 0, binding = 1) uniform restrict readonly image3D fine_rhs;
layout(r32f, set = 0, binding = 3) uniform restrict writeonly image3D coarse_rhs;
layout(r32f, set = 0, binding = 4) uniform restrict readonly image3D pressure; // the finer level's
layout(r8ui, set = 0, binding = 5) uniform restrict readonly uimage3D solid; // the finer level's
layout(r32f, set = 0, binding = 6) uniform restrict writeonly image3D coarse_pressure_out; // zeroed
layout(r8ui, set = 0, binding = 7) uniform restrict writeonly uimage3D coarse_solid;
layout(r8ui, set = 0, binding = 13) uniform restrict readonly uimage3D brick_mask; // sparse (offsets.z): bit 0 A
#endif

#ifdef MODE_MG_PROLONG
layout(r32f, set = 0, binding = 2) uniform restrict readonly image3D coarse_pressure_in;
layout(r32f, set = 0, binding = 4) uniform restrict image3D pressure; // the finer level's
layout(r8ui, set = 0, binding = 5) uniform restrict readonly uimage3D solid; // the finer level's
#endif

#if defined(MODE_PRESSURE) || defined(MODE_MG_RESTRICT) || defined(MODE_MG_PROLONG) || defined(MODE_MG_COARSE)
void stencil(ivec3 p_c, out float r_sum, out float r_count) {
	float sum = 0.0;
	float count = 0.0;
	const ivec3 o = OFF();
	for (int a = 0; a < 3; a++) {
		ivec3 e = unit(a);
		if (icomp(p_c, a) > 0) {
			uint sn = imageLoad(solid, o + p_c - e).r;
			if (sn == 0u) {
				sum += imageLoad(pressure, o + p_c - e).r;
				count += 1.0;
			} else if (sn == ZERO_CELL) {
				count += 1.0; // a zero cell (sparse: outside the simulated bricks)
			}
		} else if (face_kind(2 * a) == 1) {
			count += comp(push.open_lo.xyz, a); // open: zero pressure outside
		}
		if (icomp(p_c, a) < icomp(LN(), a) - 1) {
			uint sn = imageLoad(solid, o + p_c + e).r;
			if (sn == 0u) {
				sum += imageLoad(pressure, o + p_c + e).r;
				count += 1.0;
			} else if (sn == ZERO_CELL) {
				count += 1.0;
			}
		} else if (face_kind(2 * a + 1) == 1) {
			count += comp(push.open_hi.xyz, a);
		}
	}
	r_sum = sum;
	r_count = count;
}
#endif

#if defined(MODE_PRESSURE) || defined(MODE_MG_COARSE)
void relax(ivec3 p_c) {
	const ivec3 g = OFF() + p_c;
	if (imageLoad(solid, g).r != 0u) {
		imageStore(pressure, g, vec4(0.0));
		return;
	}
	float sum;
	float count;
	stencil(p_c, sum, count);
	float q = imageLoad(pressure, g).r;
	float gs = count > 0.0 ? (sum - push.level.x * imageLoad(rhs, g).r) / count : 0.0;
	q = (1.0 - push.level.y) * q + push.level.y * gs;
	imageStore(pressure, g, vec4(q));
}
#endif

#ifdef MODE_PRESSURE
// One colour of a red-black sweep (SOR at level 0, Gauss-Seidel at the multigrid's levels).
void main() {
	ivec3 c;
	if (push.offsets.z != 0) {
		// Sparse bricks (the grid, the first coarser level): one workgroup per listed brick, the other colour's
		// invocations idle.
		c = sparse_id_from(level_list_start(push.offsets.z));
		if (((c.x + c.y + c.z) & 1) != push.size.w) {
			return;
		}
	} else {
		ivec3 t = ivec3(gl_GlobalInvocationID);
		c = ivec3(2 * t.x + ((t.y + t.z + push.size.w) & 1), t.y, t.z);
	}
	if (any(greaterThanEqual(c, LN()))) {
		return;
	}
	relax(c);
}
#endif

#ifdef MODE_MG_COARSE
// The coarsest level, solved by one workgroup: red-black Gauss-Seidel sweeps with barriers between the colours.
void main() {
	ivec3 n = LN();
	int total = n.x * n.y * n.z;
	int tid = int(gl_LocalInvocationIndex);
	int iterations = int(push.level.z);
	for (int it = 0; it < iterations; it++) {
		for (int colour = 0; colour < 2; colour++) {
			for (int i = tid; i < total; i += 64) {
				ivec3 c = ivec3(i % n.x, (i / n.x) % n.y, i / (n.x * n.y));
				if (((c.x + c.y + c.z) & 1) == colour) {
					relax(c);
				}
			}
			memoryBarrierImage();
			barrier();
		}
	}
}
#endif

#ifdef MODE_MG_RESTRICT
// Per coarse cell: the residual of its fine children (rhs - the stencil's Laplacian), averaged over those that are
// fluid; the coarse correction starts at zero; the coarse cell is solid where all its children are.
void main() {
	ivec3 cc = ivec3(gl_GlobalInvocationID);
	if (any(greaterThanEqual(cc, push.coarse_size.xyz))) {
		return;
	}
	float sum = 0.0;
	float fluid = 0.0;
	bool zero = false;
	const ivec3 o = OFF();
	for (int k = 0; k < 8; k++) {
		ivec3 c = cc * 2 + ivec3(k & 1, (k >> 1) & 1, (k >> 2) & 1);
		if (any(greaterThanEqual(c, LN()))) {
			continue;
		}
		uint sc = imageLoad(solid, o + c).r;
		if (sc != 0u) {
			zero = zero || sc == ZERO_CELL;
			continue;
		}
		if (push.offsets.z != 0 && (imageLoad(brick_mask, c / 4).r & 1u) == 0u) {
			zero = true; // outside the simulated bricks
			continue;
		}
		float s;
		float count;
		stencil(c, s, count);
		float q = imageLoad(pressure, o + c).r;
		sum += imageLoad(fine_rhs, o + c).r - (s - count * q) / push.level.x;
		fluid += 1.0;
	}
	const ivec3 gc = ivec3(push.offsets.y, 0, 0) + cc;
	imageStore(coarse_rhs, gc, vec4(fluid > 0.0 ? sum / fluid : 0.0));
	imageStore(coarse_pressure_out, gc, vec4(0.0));
	// A zero cell where any child is one: the zero pressure outside the simulated bricks kept on every level (a level
	// without it would be a closed box's singular problem, its right-hand side incompatible: the V-cycle diverged).
	imageStore(coarse_solid, gc, uvec4(zero ? ZERO_CELL : (fluid > 0.0 ? 0u : 1u)));
}
#endif

#ifdef MODE_MG_PROLONG
// Per fine cell: the coarse correction interpolated trilinearly (cell centres: 3/4 and 1/4 per axis) and added.
void main() {
	ivec3 c = push.offsets.z != 0 ? sparse_id_from(level_list_start(push.offsets.z)) : ivec3(gl_GlobalInvocationID);
	const ivec3 g = OFF() + c;
	if (any(greaterThanEqual(c, LN())) || imageLoad(solid, g).r != 0u) {
		return;
	}
	ivec3 m = push.coarse_size.xyz - 1;
	ivec3 par = c / 2;
	ivec3 nb = clamp(par + ivec3(((c & 1) * 2) - 1), ivec3(0), m);
	const ivec3 oc = ivec3(push.offsets.y, 0, 0);
	float e = 0.0;
	for (int k = 0; k < 8; k++) {
		ivec3 sel = ivec3(k & 1, (k >> 1) & 1, (k >> 2) & 1);
		ivec3 q = ivec3(sel.x != 0 ? nb.x : par.x, sel.y != 0 ? nb.y : par.y, sel.z != 0 ? nb.z : par.z);
		float w = (sel.x != 0 ? 0.25 : 0.75) * (sel.y != 0 ? 0.25 : 0.75) * (sel.z != 0 ? 0.25 : 0.75);
		e += w * imageLoad(coarse_pressure_in, oc + q).r;
	}
	imageStore(pressure, g, vec4(imageLoad(pressure, g).r + e));
}
#endif

#ifdef MODE_MG_TAIL
// The multigrid's small levels in one workgroup, from level push.size.x down to the coarsest (push.size.y) and back up:
// the per-level passes' operations (PRESSURE's red-black sweeps, MG_RESTRICT, MG_COARSE's sweeps, MG_PROLONG) in the
// same order on the same cells, a barrier after each (dozens of small dispatches, each waiting for the last, in one).
// The levels are in the atlas (params.mg_levels: sizes and x offsets).
layout(r32f, set = 0, binding = 1) uniform restrict coherent image3D t_rhs;
layout(r32f, set = 0, binding = 4) uniform restrict coherent image3D t_pressure;
layout(r8ui, set = 0, binding = 5) uniform restrict coherent uimage3D t_solid;

void tail_sync() {
	memoryBarrierImage();
	barrier();
}

ivec3 tail_size(int p_l) {
	return params.mg_levels[p_l].xyz;
}

ivec3 tail_offset(int p_l) {
	return ivec3(params.mg_levels[p_l].w, 0, 0);
}

// stencil() on level p_l.
void tail_stencil(int p_l, ivec3 p_c, out float r_sum, out float r_count) {
	const ivec3 n = tail_size(p_l);
	const ivec3 o = tail_offset(p_l);
	float sum = 0.0;
	float count = 0.0;
	for (int a = 0; a < 3; a++) {
		ivec3 e = unit(a);
		if (icomp(p_c, a) > 0) {
			uint sn = imageLoad(t_solid, o + p_c - e).r;
			if (sn == 0u) {
				sum += imageLoad(t_pressure, o + p_c - e).r;
				count += 1.0;
			} else if (sn == ZERO_CELL) {
				count += 1.0;
			}
		} else if (face_kind(2 * a) == 1) {
			count += comp(params.mg_open_lo[p_l].xyz, a);
		}
		if (icomp(p_c, a) < icomp(n, a) - 1) {
			uint sn = imageLoad(t_solid, o + p_c + e).r;
			if (sn == 0u) {
				sum += imageLoad(t_pressure, o + p_c + e).r;
				count += 1.0;
			} else if (sn == ZERO_CELL) {
				count += 1.0;
			}
		} else if (face_kind(2 * a + 1) == 1) {
			count += comp(params.mg_open_hi[p_l].xyz, a);
		}
	}
	r_sum = sum;
	r_count = count;
}

// PRESSURE's red-black sweeps on level p_l.
void tail_smooth(int p_l, int p_iterations, float p_omega) {
	const ivec3 n = tail_size(p_l);
	const ivec3 o = tail_offset(p_l);
	const float dx2 = params.mg_open_lo[p_l].w;
	const int hx = (n.x + 1) / 2;
	const int total = hx * n.y * n.z;
	for (int it = 0; it < p_iterations; it++) {
		for (int colour = 0; colour < 2; colour++) {
			for (int i = int(gl_LocalInvocationIndex); i < total; i += TAIL_THREADS) {
				ivec3 t = ivec3(i % hx, (i / hx) % n.y, i / (hx * n.y));
				ivec3 c = ivec3(2 * t.x + ((t.y + t.z + colour) & 1), t.y, t.z);
				if (c.x >= n.x) {
					continue;
				}
				const ivec3 g = o + c;
				if (imageLoad(t_solid, g).r != 0u) {
					imageStore(t_pressure, g, vec4(0.0));
					continue;
				}
				float sum;
				float count;
				tail_stencil(p_l, c, sum, count);
				float q = imageLoad(t_pressure, g).r;
				float gs = count > 0.0 ? (sum - dx2 * imageLoad(t_rhs, g).r) / count : 0.0;
				q = (1.0 - p_omega) * q + p_omega * gs;
				imageStore(t_pressure, g, vec4(q));
			}
			tail_sync();
		}
	}
}

// MG_RESTRICT from level p_l to p_l + 1.
void tail_restrict(int p_l) {
	const ivec3 n = tail_size(p_l);
	const ivec3 o = tail_offset(p_l);
	const ivec3 nc = tail_size(p_l + 1);
	const ivec3 oc = tail_offset(p_l + 1);
	const float dx2 = params.mg_open_lo[p_l].w;
	const int total = nc.x * nc.y * nc.z;
	for (int i = int(gl_LocalInvocationIndex); i < total; i += TAIL_THREADS) {
		ivec3 cc = ivec3(i % nc.x, (i / nc.x) % nc.y, i / (nc.x * nc.y));
		float sum = 0.0;
		float fluid = 0.0;
		bool zero = false;
		for (int k = 0; k < 8; k++) {
			ivec3 c = cc * 2 + ivec3(k & 1, (k >> 1) & 1, (k >> 2) & 1);
			if (any(greaterThanEqual(c, n))) {
				continue;
			}
			uint sc = imageLoad(t_solid, o + c).r;
			if (sc != 0u) {
				zero = zero || sc == ZERO_CELL;
				continue;
			}
			float s;
			float count;
			tail_stencil(p_l, c, s, count);
			float q = imageLoad(t_pressure, o + c).r;
			sum += imageLoad(t_rhs, o + c).r - (s - count * q) / dx2;
			fluid += 1.0;
		}
		imageStore(t_rhs, oc + cc, vec4(fluid > 0.0 ? sum / fluid : 0.0));
		imageStore(t_pressure, oc + cc, vec4(0.0));
		imageStore(t_solid, oc + cc, uvec4(zero ? ZERO_CELL : (fluid > 0.0 ? 0u : 1u)));
	}
	tail_sync();
}

// MG_PROLONG from level p_l + 1 to p_l.
void tail_prolong(int p_l) {
	const ivec3 n = tail_size(p_l);
	const ivec3 o = tail_offset(p_l);
	const ivec3 m = tail_size(p_l + 1) - 1;
	const ivec3 oc = tail_offset(p_l + 1);
	const int total = n.x * n.y * n.z;
	for (int i = int(gl_LocalInvocationIndex); i < total; i += TAIL_THREADS) {
		ivec3 c = ivec3(i % n.x, (i / n.x) % n.y, i / (n.x * n.y));
		const ivec3 g = o + c;
		if (imageLoad(t_solid, g).r != 0u) {
			continue;
		}
		ivec3 par = c / 2;
		ivec3 nb = clamp(par + ivec3(((c & 1) * 2) - 1), ivec3(0), m);
		float e = 0.0;
		for (int k = 0; k < 8; k++) {
			ivec3 sel = ivec3(k & 1, (k >> 1) & 1, (k >> 2) & 1);
			ivec3 q = ivec3(sel.x != 0 ? nb.x : par.x, sel.y != 0 ? nb.y : par.y, sel.z != 0 ? nb.z : par.z);
			float w = (sel.x != 0 ? 0.25 : 0.75) * (sel.y != 0 ? 0.25 : 0.75) * (sel.z != 0 ? 0.25 : 0.75);
			e += w * imageLoad(t_pressure, oc + q).r;
		}
		imageStore(t_pressure, g, vec4(imageLoad(t_pressure, g).r + e));
	}
	tail_sync();
}

void main() {
	const int first = push.size.x;
	const int last = push.size.y;
	const float omega = push.level.y;
	for (int l = first; l < last; l++) {
		tail_smooth(l, 2, omega);
		tail_restrict(l);
	}
	tail_smooth(last, int(push.level.z), omega);
	for (int l = last - 1; l >= first; l--) {
		tail_prolong(l);
		tail_smooth(l, 2, omega);
	}
}
#endif

#ifdef MODE_PROJECT
layout(r32f, set = 0, binding = 1) uniform restrict readonly image3D pressure;
layout(FMT, set = 0, binding = 4) uniform restrict image3D vel;
layout(r8ui, set = 0, binding = 5) uniform restrict readonly uimage3D solid;

void main() {
	ivec3 t = cell_id(true);
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
layout(push_constant, std430) uniform Push {
	ivec4 size; // x 1: every brick (sparse: after a shift), else only A's (the others keep their zeros)
	vec4 level; // unused
	ivec4 coarse_size; // unused
	vec4 open_lo; // unused
	vec4 open_hi; // unused
	ivec4 offsets; // unused
}
push;

layout(FMT, set = 0, binding = 1) uniform restrict readonly image3D scalars;
layout(rgba16f, set = 0, binding = 4) uniform restrict writeonly image3D coarse; // x smoke, y temperature, z fuel
layout(r8ui, set = 0, binding = 13) uniform restrict readonly uimage3D brick_mask; // sparse: bit 0 A

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
	bool simulated = params.sparse.x == 0 || push.size.x != 0 || (all(lessThan(b, nb)) && (imageLoad(brick_mask, b).r & 1u) != 0u);
	if (all(lessThan(b, nb)) && simulated) {
		// The brick's largest values over its cells and their neighbours (what trilinear sampling inside it reads).
		ivec3 lo = max(b * 4 - 1, ivec3(0));
		ivec3 hi = min(b * 4 + 4, N() - 1);
		vec3 m = vec3(0.0);
		for (int z = lo.z; z <= hi.z; z++) {
			for (int y = lo.y; y <= hi.y; y++) {
				for (int x = lo.x; x <= hi.x; x++) {
					ivec3 c = ivec3(x, y, z);
					vec4 s = imageLoad(scalars, c);
					m = max(m, s.xyz);
					if (all(greaterThanEqual(c, b * 4)) && all(lessThan(c, b * 4 + 4)) && s.y > 300.0) {
						vec3 e = emission_rgb(s.y) * params.emission.y;
						float l = dot(e, vec3(0.2126, 0.7152, 0.0722));
						sum += vec4(l, l * (vec3(c) + 0.5));
						rgb += e;
					}
				}
			}
		}
		imageStore(coarse, b, vec4(m, 0.0));
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

#ifdef MODE_SPARSE_SOURCE
// Per brick: a source where its largest smoke, temperature or fuel (COARSE's, the last step's) is above the
// thresholds or an emitter (its bounding sphere and the one-cell ramp) reaches it.
layout(rgba16f, set = 0, binding = 1) uniform restrict readonly image3D coarse;
layout(r8ui, set = 0, binding = 2) uniform restrict writeonly uimage3D source;

void main() {
	ivec3 b = ivec3(gl_GlobalInvocationID);
	if (any(greaterThanEqual(b, (N() + 3) / 4))) {
		return;
	}
	vec4 m = imageLoad(coarse, b);
	bool src = m.x > SPARSE_SMOKE || m.y > SPARSE_TEMPERATURE || m.z > SPARSE_FUEL;
	vec3 lo = vec3(b * 4);
	vec3 hi = lo + 4.0;
	for (int e = 0; e < params.size.w && !src; e++) {
		Emitter em = params.emitters[e];
		int shape = int(em.shape.x);
		float r = shape == 1 ? length(vec3(em.rot0.w, em.rot1.w, em.rot2.w)) : (shape == 2 ? length(vec2(em.centre_radius.w, em.rot0.w)) : em.centre_radius.w);
		vec3 d = max(max(lo - em.centre_radius.xyz, em.centre_radius.xyz - hi), vec3(0.0));
		src = length(d) <= r + 1.5;
	}
	// Burning objects releasing fuel or hotter than the threshold in the brick's cells.
	for (int k = 0; k < params.burn_count.x && !src; k++) {
		ivec3 o = params.burn_origin[k].xyz;
		ivec3 e = o + params.burn_size[k].xyz;
		if (any(greaterThanEqual(b * 4, e)) || any(lessThanEqual(b * 4 + 4, o))) {
			continue;
		}
		for (int i = 0; i < 64 && !src; i++) {
			vec4 v = burn_at(k, b * 4 + ivec3(i & 3, (i >> 2) & 3, i >> 4));
			src = v.x > 0.0 || v.y > SPARSE_TEMPERATURE;
		}
	}
	imageStore(source, b, uvec4(src ? 1u : 0u));
}
#endif

#ifdef MODE_SPARSE_ACTIVATE
// Per brick (of the velocity's texels: one more layer where N % 4 == 0): A within params.sparse.z bricks of a source
// (cells only), F within one more; what leaves F gets the ambient velocity (the wind) and no solid, what leaves A no
// smoke and zero pressure; both listed for the passes' indirect dispatches.
layout(r8ui, set = 0, binding = 1) uniform restrict readonly uimage3D source;
layout(r8ui, set = 0, binding = 2) uniform restrict uimage3D brick_mask; // bit 0 A, bit 1 F
layout(FMT, set = 0, binding = 3) uniform restrict writeonly image3D scalars0;
layout(FMT, set = 0, binding = 4) uniform restrict writeonly image3D scalars1;
layout(FMT, set = 0, binding = 5) uniform restrict writeonly image3D vel0;
layout(FMT, set = 0, binding = 6) uniform restrict writeonly image3D vel1;
layout(FMT, set = 0, binding = 7) uniform restrict writeonly image3D scalars_hat;
layout(FMT, set = 0, binding = 8) uniform restrict writeonly image3D vel_hat;
layout(r32f, set = 0, binding = 9) uniform restrict writeonly image3D pressure;
layout(r8ui, set = 0, binding = 11) uniform restrict writeonly uimage3D solid;
layout(rgba16f, set = 0, binding = 12) uniform restrict writeonly image3D coarse;
layout(set = 0, binding = 14, std430) restrict writeonly buffer Bricks {
	uint list[];
}
bricks;
layout(set = 0, binding = 15, std430) restrict buffer Args {
	uint args[]; // the indirect dispatches: A's workgroups, 1, 1, F's, 1, 1
}
dispatch;

// The workgroup's 4^3 bricks and the sources within margin + 1 of them (at most 4 + 2 * 5 per axis), in shared memory.
#define ACT_TILE 14
shared uint s_source[ACT_TILE * ACT_TILE * ACT_TILE];

void main() {
	const ivec3 nb = (N() + 3) / 4;
	const int d = params.sparse.z;
	const int r = d + 1;
	const int w = 4 + 2 * r;
	const ivec3 base = ivec3(gl_WorkGroupID) * 4 - r;
	for (int i = int(gl_LocalInvocationIndex); i < w * w * w; i += 64) {
		ivec3 q = base + ivec3(i % w, (i / w) % w, i / (w * w));
		bool inside = all(greaterThanEqual(q, ivec3(0))) && all(lessThan(q, nb));
		s_source[i] = inside ? imageLoad(source, q).r : 0u;
	}
	barrier();
	ivec3 b = ivec3(gl_GlobalInvocationID);
	if (any(greaterThanEqual(b, (N() + 4) / 4))) {
		return;
	}
	bool a = false;
	bool f = false;
	const ivec3 l = ivec3(gl_LocalInvocationID) + r; // b in the tile
	for (int z = -r; z <= r; z++) {
		for (int y = -r; y <= r; y++) {
			for (int x = -r; x <= r; x++) {
				ivec3 q = l + ivec3(x, y, z);
				if (s_source[q.x + w * (q.y + w * q.z)] != 0u) {
					f = true;
					a = a || max(abs(x), max(abs(y), abs(z))) <= d;
				}
			}
		}
	}
	a = a && all(lessThan(b, nb));
	uint old = imageLoad(brick_mask, b).r;
	// After a shift or dense steps (params.sparse.w) every brick outside A and F is cleared by SPARSE_CLEAR, one
	// invocation per texel; otherwise the few bricks that left them here.
	bool prev_a = params.sparse.w == 0 && (old & 1u) != 0u;
	bool prev_f = params.sparse.w == 0 && (old & 2u) != 0u;
	if (prev_f && !f) {
		for (int z = 0; z < 4; z++) {
			for (int y = 0; y < 4; y++) {
				for (int x = 0; x < 4; x++) {
					ivec3 t = b * 4 + ivec3(x, y, z);
					if (any(greaterThan(t, N()))) {
						continue;
					}
					imageStore(vel0, t, vec4(params.wind.xyz, 0.0));
					imageStore(vel1, t, vec4(params.wind.xyz, 0.0));
					imageStore(vel_hat, t, vec4(0.0));
					if (all(lessThan(t, N()))) {
						imageStore(scalars_hat, t, vec4(0.0));
						imageStore(solid, t, uvec4(0u));
					}
				}
			}
		}
	}
	if (prev_a && !a && all(lessThan(b, nb))) {
		for (int z = 0; z < 4; z++) {
			for (int y = 0; y < 4; y++) {
				for (int x = 0; x < 4; x++) {
					ivec3 c = b * 4 + ivec3(x, y, z);
					if (any(greaterThanEqual(c, N()))) {
						continue;
					}
					imageStore(scalars0, c, vec4(0.0));
					imageStore(scalars1, c, vec4(0.0));
					imageStore(pressure, c, vec4(0.0)); // (the divergence outside A is never read)
				}
			}
		}
		imageStore(coarse, b, vec4(0.0));
	}
	imageStore(brick_mask, b, uvec4((a ? 1u : 0u) | (f ? 2u : 0u)));
	uint packed_b = uint(b.x) | (uint(b.y) << 10u) | (uint(b.z) << 20u);
	if (a) {
		bricks.list[atomicAdd(dispatch.args[0], 1u)] = packed_b;
	}
	if (f) {
		bricks.list[uint(params.sparse.y) + atomicAdd(dispatch.args[3], 1u)] = packed_b;
	}
}
#endif

#ifdef MODE_SPARSE_LIST1
// Per brick of the first coarser level (4^3 of its cells, 2^3 of the grid's bricks): listed where one of the grid's
// bricks below is in A (the others' cells are all zero cells: their pressure stays 0, MG_RESTRICT writes them).
layout(r8ui, set = 0, binding = 2) uniform restrict readonly uimage3D brick_mask;
layout(set = 0, binding = 14, std430) restrict writeonly buffer Bricks {
	uint list[];
}
bricks;
layout(set = 0, binding = 15, std430) restrict buffer Args {
	uint args[]; // ..., the first coarser level's workgroups at 6, 1, 1
}
dispatch;

void main() {
	ivec3 b = ivec3(gl_GlobalInvocationID);
	const ivec3 n1 = (N() + 1) / 2;
	if (any(greaterThanEqual(b, (n1 + 3) / 4))) {
		return;
	}
	const ivec3 nb = (N() + 3) / 4;
	bool any_a = false;
	for (int k = 0; k < 8; k++) {
		ivec3 q = b * 2 + ivec3(k & 1, (k >> 1) & 1, (k >> 2) & 1);
		if (all(lessThan(q, nb)) && (imageLoad(brick_mask, q).r & 1u) != 0u) {
			any_a = true;
		}
	}
	if (any_a) {
		bricks.list[2u * uint(params.sparse.y) + atomicAdd(dispatch.args[6], 1u)] = uint(b.x) | (uint(b.y) << 10u) | (uint(b.z) << 20u);
	}
}
#endif

#ifdef MODE_SPARSE_CLEAR
// After a shift or dense steps: every texel outside F gets the ambient velocity and no solid, zero hats; every cell
// outside A no smoke and zero pressure, every brick outside A zero maxima (one invocation per velocity texel).
layout(r8ui, set = 0, binding = 2) uniform restrict readonly uimage3D brick_mask;
layout(FMT, set = 0, binding = 3) uniform restrict writeonly image3D scalars0;
layout(FMT, set = 0, binding = 4) uniform restrict writeonly image3D scalars1;
layout(FMT, set = 0, binding = 5) uniform restrict writeonly image3D vel0;
layout(FMT, set = 0, binding = 6) uniform restrict writeonly image3D vel1;
layout(FMT, set = 0, binding = 7) uniform restrict writeonly image3D scalars_hat;
layout(FMT, set = 0, binding = 8) uniform restrict writeonly image3D vel_hat;
layout(r32f, set = 0, binding = 9) uniform restrict writeonly image3D pressure;
layout(r8ui, set = 0, binding = 11) uniform restrict writeonly uimage3D solid;
layout(rgba16f, set = 0, binding = 12) uniform restrict writeonly image3D coarse;

void main() {
	ivec3 t = ivec3(gl_GlobalInvocationID);
	if (any(greaterThan(t, N()))) {
		return;
	}
	ivec3 b = t / 4;
	uint m = imageLoad(brick_mask, b).r;
	bool cell = all(lessThan(t, N()));
	if ((m & 2u) == 0u) {
		imageStore(vel0, t, vec4(params.wind.xyz, 0.0));
		imageStore(vel1, t, vec4(params.wind.xyz, 0.0));
		imageStore(vel_hat, t, vec4(0.0));
		if (cell) {
			imageStore(scalars_hat, t, vec4(0.0));
			imageStore(solid, t, uvec4(0u));
		}
	}
	if ((m & 1u) == 0u && cell) {
		imageStore(scalars0, t, vec4(0.0));
		imageStore(scalars1, t, vec4(0.0));
		imageStore(pressure, t, vec4(0.0));
		if (all(equal(t & 3, ivec3(0)))) {
			imageStore(coarse, b, vec4(0.0));
		}
	}
}
#endif

#ifdef MODE_EXPORT
// The velocity at the cell centres for GPU particles (a GPUParticlesAttractorVectorField3D over the box): their
// vector field reads 2 * texel - 1 as the force's direction and length, so the texel is v / scale mapped to [0, 1].
layout(push_constant, std430) uniform Push {
	ivec4 size; // unused
	vec4 level; // x: 1 / the velocity mapped to 1 (m/s)
	ivec4 coarse_size; // unused
	vec4 open_lo; // unused
	vec4 open_hi; // unused
	ivec4 offsets; // unused
}
push;

layout(FMT, set = 0, binding = 1) uniform restrict readonly image3D vel;
layout(rgba16f, set = 0, binding = 4) uniform restrict writeonly image3D dest;

void main() {
	ivec3 c = ivec3(gl_GlobalInvocationID);
	if (any(greaterThanEqual(c, N()))) {
		return;
	}
	vec3 a = imageLoad(vel, c).xyz;
	vec3 v = 0.5 * vec3(a.x + imageLoad(vel, c + ivec3(1, 0, 0)).x, a.y + imageLoad(vel, c + ivec3(0, 1, 0)).y, a.z + imageLoad(vel, c + ivec3(0, 0, 1)).z);
	imageStore(dest, c, vec4(clamp(v * push.level.x, vec3(-1.0), vec3(1.0)) * 0.5 + 0.5, 1.0));
}
#endif

/* clang-format on */
