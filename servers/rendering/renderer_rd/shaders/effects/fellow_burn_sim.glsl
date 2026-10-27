#[compute]

#version 450

#VERSION_DEFINES

// FellowEngine burning solids (docs/decisions/0120-burning.md, FellowBurnable3D; tests/burn_reference.py is this file
// in NumPy). A burnable object is a box of cubic voxels around its mesh, each with x the temperature above ambient
// (K), y the volume fraction of virgin wood, z of char, w of ash (the rest air). Per step (burning time):
//   IGNITE  (when asked) the voxels within a sphere at least a temperature;
//   HEAT    conduction, the exchange at the exposed faces with the gas (its hottest of three samples outwards) or the
//           surface's own flame (over the voxels that released volatiles the step before, and above them), pyrolysis
//           (wood -> char, the volatiles the gas's fuel), char oxidation (char -> ash, heat); the emission texture;
//   SETTLE  the loose material (ash; with the char where a voxel has crumbled: next to no wood, and its char burned
//           three eighths away, the ash it left in it, or next to none left; the debris of a heap) falls
//           into the voxel below (along the voxel axis nearest to the world's down) while it has room, down to the
//           floor (the ground's layer, or the object's lowest), and slides down beside where it cannot (a heap no
//           steeper than a voxel per voxel), carrying its heat; the display texture;
//   INJECT  per cell of the gas grid's region around the object: the fuel released, the surface's temperature and
//           exposure, the solid fraction (the gas's EMIT and OBSTACLES read it);
//   REDUCE  per workgroup the totals (read back: the burned fraction, the node's signals).

layout(local_size_x = 4, local_size_y = 4, local_size_z = 4) in;

#define SIGMA 5.670374e-8

layout(set = 0, binding = 0, std140) uniform Params {
	ivec4 size; // nx, ny, nz; w 1: coupled with a gas volume
	vec4 grid; // x voxel size h (m), y dt (burning s), z 1 / dt, w the ash settled per step (1 - exp(-rate dt))
	vec4 densities; // x wood, y char, z ash (kg/m^3), w the smallest heat capacity (J/(m^3 K))
	vec4 heats; // x wood, y char, z ash specific heat (J/(kg K)), w pyrolysis heat (J/kg, absorbed)
	vec4 conduct; // x wood, y char, z ash conductivity (W/(m K)), w convection (W/(m^2 K))
	vec4 pyro; // x onset (K above ambient; full 120 K above), y rate (1/s), z char yield (mass), w char volume
	vec4 glow; // x onset (full 200 K above), y rate (1/s), z heat kept (J per kg of char), w ash volume
	vec4 misc; // x emissivity, y the gas's fuel per kg of volatiles, z gas sample distance (voxels), w ambient (K)
	vec4 voxel_to_gas[3]; // rows: voxel coordinates (cells) -> the gas grid's (cells)
	ivec4 gas_size;
	vec4 ignite; // xyz centre (voxels), w radius (voxels; 0: none)
	vec4 ignite_temperature; // x K above ambient
	ivec4 region_origin; // INJECT: the gas cells
	ivec4 region_size;
	vec4 gas_to_voxel[3]; // rows: the gas grid's coordinates -> voxel coordinates
	vec4 inject; // x the fuel's footprint (gas cells)
	ivec4 settle; // the ash's fall: x the axis, y its sign, z the floor (the last layer along it the ash may reach)
	vec4 flame; // the surface's own flame: x its temperature (K above ambient; 0 none), y the volatiles' flux it needs (kg/(m^2 s)), z its reach above the surface (voxels)
}
params;

ivec3 N() {
	return params.size.xyz;
}

#define BURN_DEBRIS_ASH 0.6 // ash per char, in ash volumes: a voxel with no wood whose char has burned this far (3/8 of
// it: the ash it left is 0.6 ash volumes per char left) has crumbled; the debris in a heap stays loose

// Whether a voxel's material (wood p_w, char p_c, ash p_a) is loose: crumbled (next to no wood; its char burned three
// eighths away, or next to none left), the debris of a heap; it falls (SETTLE) and is no obstacle to the gas (porous).
bool is_loose(float p_w, float p_c, float p_a) {
	return p_w < 0.05 && (p_w + p_c < 0.05 || p_a >= BURN_DEBRIS_ASH * params.glow.w * p_c);
}

bool inside(ivec3 p_c) {
	return all(greaterThanEqual(p_c, ivec3(0))) && all(lessThan(p_c, N()));
}

ivec3 unit(int p_c) {
	return ivec3(p_c == 0 ? 1 : 0, p_c == 1 ? 1 : 0, p_c == 2 ? 1 : 0);
}

/* clang-format off */

#ifdef MODE_IGNITE
layout(rgba32f, set = 0, binding = 1) uniform restrict image3D state;

void main() {
	ivec3 c = ivec3(gl_GlobalInvocationID);
	if (!inside(c)) {
		return;
	}
	if (length(vec3(c) + 0.5 - params.ignite.xyz) <= params.ignite.w) {
		vec4 s = imageLoad(state, c);
		if (s.y + s.z > 0.0) {
			s.x = max(s.x, params.ignite_temperature.x);
			imageStore(state, c, s);
		}
	}
}
#endif

#ifdef MODE_HEAT
layout(rgba32f, set = 0, binding = 1) uniform restrict readonly image3D state_in;
layout(rgba32f, set = 0, binding = 2) uniform restrict writeonly image3D state_out;
layout(rgba16f, set = 0, binding = 3) uniform restrict writeonly image3D emission; // volatiles (kg/(m^3 s)), surface T, exposure / 6, solid
layout(set = 0, binding = 4) uniform sampler3D gas_scalars; // linear, clamped (the gas's temperature in .y)
layout(rgba16f, set = 0, binding = 5) uniform restrict readonly image3D emission_prev; // the step before's

// The surface's own flame over voxel p_c: the largest volatiles' release (the step before) of the voxels below it, up
// to the flame's reach, and beside those (one voxel across: the flame's width, how it creeps sideways), in kg/(m^3 s).
float flame_source(ivec3 p_c) {
	ivec3 up = -unit(params.settle.x) * params.settle.y;
	ivec3 s1 = unit((params.settle.x + 1) % 3);
	ivec3 s2 = unit((params.settle.x + 2) % 3);
	int reach = int(params.flame.z);
	float q = 0.0;
	for (int k = 0; k <= reach; k++) {
		for (int d1 = -1; d1 <= 1; d1++) {
			for (int d2 = -1; d2 <= 1; d2++) {
				ivec3 b = p_c - up * k + s1 * d1 + s2 * d2;
				if (inside(b)) {
					q = max(q, imageLoad(emission_prev, b).x);
				}
			}
		}
	}
	return q;
}

float heat_capacity(vec4 p_s) {
	float cv = params.densities.x * params.heats.x * p_s.y + params.densities.y * params.heats.y * p_s.z + params.densities.z * params.heats.z * p_s.w;
	return max(cv, params.densities.w);
}

float conductivity(vec4 p_s) {
	return params.conduct.x * p_s.y + params.conduct.y * p_s.z + params.conduct.z * p_s.w;
}

vec4 state_at(ivec3 p_c) {
	return inside(p_c) ? imageLoad(state_in, p_c) : vec4(0.0);
}

float gas_temperature(vec3 p_v) {
	if (params.size.w == 0) {
		return 0.0;
	}
	vec4 p = vec4(p_v, 1.0);
	vec3 g = vec3(dot(params.voxel_to_gas[0], p), dot(params.voxel_to_gas[1], p), dot(params.voxel_to_gas[2], p));
	vec3 gs = vec3(params.gas_size.xyz);
	if (any(lessThan(g, vec3(0.0))) || any(greaterThan(g, gs))) {
		return 0.0;
	}
	return textureLod(gas_scalars, g / gs, 0.0).y;
}

void main() {
	ivec3 c = ivec3(gl_GlobalInvocationID);
	if (!inside(c)) {
		return;
	}
	const float h = params.grid.x;
	const float dt = params.grid.y;
	vec4 s0 = imageLoad(state_in, c);
	float T = s0.x;
	float w = s0.y;
	float ch = s0.z;
	float a = s0.w;
	float cv = heat_capacity(s0);
	// 1. Conduction (the faces' harmonic mean; air does not conduct).
	float k = conductivity(s0);
	float lap = 0.0;
	float e = 0.0; // the exposed faces (2.)
	vec3 g = vec3(0.0); // the solid's gradient
	for (int ax = 0; ax < 3; ax++) {
		for (int d = -1; d <= 1; d += 2) {
			ivec3 q = c + unit(ax) * d;
			vec4 sn = state_at(q);
			float kn = conductivity(sn);
			float kf = (k > 0.0 && kn > 0.0) ? 2.0 * k * kn / max(k + kn, 1e-12) : 0.0;
			lap += kf * (sn.x - T);
			e += clamp(1.0 - (sn.y + sn.z) - 0.6 * sn.w, 0.0, 1.0);
			g += vec3(unit(ax)) * (float(d) * (sn.y + sn.z)); // (no dynamic vector indexing: re-spirv rejects it)
		}
	}
	T = T + dt * lap / (cv * h * h);
	// 2. The exchange with the gas just outside the surface (convection and radiation, semi-implicit).
	float fill0 = w + ch + a;
	e = fill0 >= 1e-3 ? e : 0.0;
	vec3 n = -g;
	float nl = length(n);
	n = nl > 1e-4 ? n / max(nl, 1e-4) : vec3(0.0);
	// The flame's temperature: the hottest of three samples outwards (half, one and one and a half sample distances:
	// the gas cells are coarser than the voxels, the flame sheet lies somewhere in front of the surface).
	vec3 p0 = vec3(c) + 0.5;
	float tg = max(gas_temperature(p0 + n * (0.5 * params.misc.z)), max(gas_temperature(p0 + n * params.misc.z), gas_temperature(p0 + n * (1.5 * params.misc.z))));
	if (params.flame.x > 0.0) {
		// The surface's own flame (thinner than a voxel, over the voxels releasing volatiles and above them): the
		// surface exchanges with it as much as their flux covers it.
		float cover = clamp(flame_source(c) * h / params.flame.y, 0.0, 1.0);
		tg = max(tg, cover * params.flame.x);
	}
	float ta = T + params.misc.w;
	float tga = tg + params.misc.w;
	float hr = params.misc.x * SIGMA * (tga * tga + ta * ta) * (tga + ta);
	float coef = (params.conduct.w + hr) * e / (cv * h);
	T = (T + dt * coef * tg) / (1.0 + dt * coef);
	// 3. Pyrolysis: wood -> char, the volatiles the gas's fuel (and the surface's own flame).
	float rp = params.pyro.y * smoothstep(params.pyro.x, params.pyro.x + 120.0, T);
	float dw = w * (1.0 - exp(-rp * dt));
	w = w - dw;
	ch = ch + dw * params.pyro.w;
	T = T - dw * params.densities.x * params.heats.w / cv;
	float fuel = dw * params.densities.x * (1.0 - params.pyro.z) * params.grid.z; // the volatiles, kg/(m^3 s)
	// 4. Char oxidation where exposed: char -> ash, heat.
	float ro = params.glow.y * smoothstep(params.glow.x, params.glow.x + 200.0, T) * min(e, 1.0);
	float dc = ch * (1.0 - exp(-ro * dt));
	ch = ch - dc;
	a = a + dc * params.glow.w;
	T = T + dc * params.densities.y * params.glow.z / cv;
	// Empty voxels hold the gas's temperature.
	if (w + ch + a < 1e-3) {
		T = tg;
	}
	imageStore(state_out, c, vec4(T, w, ch, a));
	imageStore(emission, c, vec4(fuel, (e > 0.0 || fuel > 0.0) ? T : 0.0, e * (1.0 / 6.0), is_loose(w, ch, a) ? 0.0 : w + ch));
}
#endif

#ifdef MODE_SETTLE
layout(rgba32f, set = 0, binding = 1) uniform restrict readonly image3D state_in;
layout(rgba32f, set = 0, binding = 2) uniform restrict writeonly image3D state_out;
layout(rgba16f, set = 0, binding = 3) uniform restrict writeonly image3D display;

#define BURN_DOWN_SHARE 0.6 // of the room below, the most the voxel above may fill per step
#define BURN_SIDE_SHARE 0.1 // of the room beside-below, the most each of the four slides into it may fill

ivec3 g_dn; // down (the voxel axis nearest to the world's down)

// Whether loose material may lie in voxel p_c (inside the box, not beyond the floor).
bool settle_ok(ivec3 p_c) {
	ivec3 u = unit(params.settle.x);
	int along = u.x * p_c.x + u.y * p_c.y + u.z * p_c.z;
	return inside(p_c) && (params.settle.y < 0 ? along >= params.settle.z : along <= params.settle.z);
}

float room(ivec3 p_c) {
	if (!settle_ok(p_c)) {
		return 0.0;
	}
	vec4 s = imageLoad(state_in, p_c);
	return clamp(1.0 - (s.y + s.z + s.w), 0.0, 1.0);
}

// Whether material may slide through voxel p_c (less than half full).
bool passage(ivec3 p_c) {
	if (!inside(p_c)) {
		return false;
	}
	vec4 s = imageLoad(state_in, p_c);
	return s.y + s.z + s.w < 0.5;
}

// The loose material of a voxel (char, ash): where it has crumbled (next to no wood; next to no char, or char that has
// burned three eighths away: the ash it left in it) its char and ash.
vec2 loose(vec4 p_s) {
	return is_loose(p_s.y, p_s.z, p_s.w) ? vec2(p_s.z, p_s.w) : vec2(0.0);
}

// What voxel p_c moves this step: x down, y the rest, which may slide down beside it.
vec2 sends(ivec3 p_c, vec4 p_s) {
	vec2 l = loose(p_s);
	float m = (l.x + l.y) * params.grid.w;
	float d = min(m, BURN_DOWN_SHARE * room(p_c + g_dn));
	return vec2(d, m - d);
}

// The slide from voxel p_c (its rest p_r) through its neighbour along p_lat into the voxel below that: a heap's
// slope no steeper than a voxel per voxel.
float slide(ivec3 p_c, ivec3 p_lat, float p_r) {
	if (p_r <= 0.0 || !passage(p_c + p_lat)) {
		return 0.0;
	}
	return min(0.25 * p_r, BURN_SIDE_SHARE * room(p_c + g_dn + p_lat));
}

void main() {
	ivec3 c = ivec3(gl_GlobalInvocationID);
	if (!inside(c)) {
		return;
	}
	g_dn = unit(params.settle.x) * params.settle.y;
	ivec3 s1 = unit((params.settle.x + 1) % 3);
	ivec3 s2 = unit((params.settle.x + 2) % 3);
	ivec3 lat[4] = ivec3[4](s1, -s1, s2, -s2);
	vec4 s = imageLoad(state_in, c);
	// Out: down, then sliding beside, in the shares of its char and ash.
	vec2 l = loose(s);
	float lt = l.x + l.y;
	vec2 snd = sends(c, s);
	float out_m = snd.x;
	for (int k = 0; k < 4; k++) {
		out_m += slide(c, lat[k], snd.y);
	}
	float out_c = lt > 0.0 ? out_m * l.x / lt : 0.0;
	float out_a = out_m - out_c;
	// In: from the voxel above, and sliding from the four beside it, with their heat (the hot char keeps glowing in the
	// heap).
	float cc = params.densities.y * params.heats.y; // heat capacities per volume fraction (J/(m^3 K))
	float ca = params.densities.z * params.heats.z;
	float in_c = 0.0;
	float in_a = 0.0;
	float in_q = 0.0;
	float in_cv = 0.0;
	ivec3 u = c - g_dn;
	if (inside(u)) {
		vec4 su = imageLoad(state_in, u);
		vec2 lu = loose(su);
		float lut = lu.x + lu.y;
		if (lut > 0.0) {
			float d = sends(u, su).x;
			float fc = d * lu.x / lut;
			float fa = d * lu.y / lut;
			in_c += fc;
			in_a += fa;
			float cv = cc * fc + ca * fa;
			in_q += cv * su.x;
			in_cv += cv;
		}
	}
	for (int k = 0; k < 4; k++) {
		ivec3 q = c - g_dn - lat[k];
		if (inside(q)) {
			vec4 sq = imageLoad(state_in, q);
			vec2 lq = loose(sq);
			float lqt = lq.x + lq.y;
			if (lqt > 0.0) {
				float f = slide(q, lat[k], sends(q, sq).y);
				float fc = f * lq.x / lqt;
				float fa = f * lq.y / lqt;
				in_c += fc;
				in_a += fa;
				float cv = cc * fc + ca * fa;
				in_q += cv * sq.x;
				in_cv += cv;
			}
		}
	}
	float keep_cv = params.densities.x * params.heats.x * s.y + cc * (s.z - out_c) + ca * (s.w - out_a);
	if (in_cv > 0.0) {
		s.x = (keep_cv * s.x + in_q) / max(keep_cv + in_cv, 1e-6);
	}
	s.z = s.z - out_c + in_c;
	s.w = s.w - out_a + in_a;
	imageStore(state_out, c, s);
	imageStore(display, c, s);
}
#endif

#ifdef MODE_INJECT
layout(set = 0, binding = 1) uniform sampler3D emission; // linear, clamped
layout(rgba16f, set = 0, binding = 2) uniform restrict writeonly image3D injection;

vec3 to_voxel(vec3 p_g) {
	vec4 p = vec4(p_g, 1.0);
	return vec3(dot(params.gas_to_voxel[0], p), dot(params.gas_to_voxel[1], p), dot(params.gas_to_voxel[2], p));
}

vec4 emission_at(vec3 p_g) {
	vec3 v = to_voxel(p_g);
	vec3 n = vec3(N());
	if (any(lessThan(v, vec3(0.0))) || any(greaterThan(v, n))) {
		return vec4(0.0);
	}
	return textureLod(emission, v / n, 0.0);
}

void main() {
	ivec3 q = ivec3(gl_GlobalInvocationID);
	if (any(greaterThanEqual(q, params.region_size.xyz))) {
		return;
	}
	vec3 centre = vec3(params.region_origin.xyz + q) + 0.5;
	// The fuel and the surface over a footprint of params.inject.x cells (each voxel's release shared by the cells
	// whose footprints hold it), the solid over the cell itself.
	float fuel = 0.0;
	float tf = 0.0;
	float ts = 0.0;
	float ex = 0.0;
	for (int i = 0; i < 64; i++) {
		vec3 o = (vec3(i & 3, (i >> 2) & 3, i >> 4) + 0.5) * 0.25 - 0.5;
		vec4 e = emission_at(centre + o * params.inject.x);
		fuel += e.x;
		tf += e.x * e.y;
		ts += e.y * e.z;
		ex += e.z;
	}
	float solid = 0.0;
	for (int i = 0; i < 8; i++) {
		vec3 o = vec3(i & 1, (i >> 1) & 1, i >> 2) * 0.5 - 0.25;
		solid += emission_at(centre + o).w;
	}
	// The surface's temperature: where fuel is released the pyrolysing voxels' (the volatiles leave at it: the flame
	// attaches to the surface), elsewhere the exposed surface's.
	float t = fuel > 0.0 ? tf / fuel : (ex > 0.0 ? ts / ex : 0.0);
	imageStore(injection, q, vec4(fuel * (params.misc.y / 64.0), t, ex * (1.0 / 64.0), solid * 0.125)); // the volatiles as the gas's fuel
}
#endif

#ifdef MODE_REDUCE
layout(rgba32f, set = 0, binding = 1) uniform restrict readonly image3D state;
layout(rgba16f, set = 0, binding = 2) uniform restrict readonly image3D emission;
layout(set = 0, binding = 3, std430) restrict writeonly buffer Totals {
	vec4 data[]; // per workgroup: wood, char, ash (voxels), fuel released; the largest temperature, burning voxels
}
totals;

shared vec4 s_a[64];
shared vec4 s_b[64];

void main() {
	ivec3 c = ivec3(gl_GlobalInvocationID);
	uint li = gl_LocalInvocationIndex;
	vec4 a = vec4(0.0);
	vec4 b = vec4(0.0);
	if (inside(c)) {
		vec4 s = imageLoad(state, c);
		vec4 e = imageLoad(emission, c);
		a = vec4(s.y, s.z, s.w, e.x);
		b = vec4(s.y + s.z + s.w > 1e-3 ? s.x : 0.0, e.x > 0.0 ? 1.0 : 0.0, 0.0, 0.0);
	}
	s_a[li] = a;
	s_b[li] = b;
	barrier();
	for (uint st = 32u; st > 0u; st >>= 1u) {
		if (li < st) {
			s_a[li] += s_a[li + st];
			s_b[li] = vec4(max(s_b[li].x, s_b[li + st].x), s_b[li].y + s_b[li + st].y, 0.0, 0.0);
		}
		barrier();
	}
	if (li == 0u) {
		uvec3 groups = gl_NumWorkGroups;
		uint gi = gl_WorkGroupID.x + groups.x * (gl_WorkGroupID.y + groups.y * gl_WorkGroupID.z);
		totals.data[gi * 2u] = s_a[0];
		totals.data[gi * 2u + 1u] = s_b[0];
	}
}
#endif

/* clang-format on */
