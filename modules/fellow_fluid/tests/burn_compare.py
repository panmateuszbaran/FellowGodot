#!/usr/bin/env python3
"""Compares benchmarks/project/bench/burn_check.gd's GPU state with burn_reference.py (decision 0120).

  python3 burn_compare.py <dir>

solo / ash: the reference runs the same steps from the GPU's initial voxels (the ignition before the first, no gas);
gas: one step from the GPU's state before it, with the gas's temperature read back (sampled as the GPU does), and the
injection into the gas's cells (INJECT) from that step's emission.
"""

import json
import sys

import numpy as np

sys.path.insert(0, __file__.rsplit("/", 1)[0])
import burn_reference as br  # noqa: E402

f32 = np.float32


def load_state(path, n):
	a = np.fromfile(path, dtype=np.float32)
	return a.reshape(n[2], n[1], n[0], 4).transpose(2, 1, 0, 3)  # x fastest -> [x, y, z, c]


def load_half(path, n):
	a = np.fromfile(path, dtype=np.float16).astype(np.float32)
	return a.reshape(n[2], n[1], n[0], 4).transpose(2, 1, 0, 3)


def material(info):
	m = br.Material()
	v = [f32(x) for x in info["material"]]
	m.density, m.char_density, m.ash_density = v[0], v[1], v[2]
	min_cv = v[3]
	m.specific_heat, m.char_specific_heat, m.ash_specific_heat, m.pyrolysis_heat = v[4], v[5], v[6], v[7]
	m.conductivity, m.char_conductivity, m.ash_conductivity, m.convection = v[8], v[9], v[10], v[11]
	m.pyrolysis_temperature, m.pyrolysis_rate, m.char_yield, m.char_volume = v[12], v[13], v[14], v[15]
	m.glow_temperature, m.glow_rate, m.glow_heat, m.ash_volume = v[16], v[17], v[18], v[19]
	m.emissivity, m.fuel_per_kg = v[20], v[21]
	m.flame_temperature = f32(info.get("flame_temperature", 0.0))
	m.flame_flux = f32(info.get("flame_flux", 0.005))
	m.flame_reach = int(round(info.get("flame_reach", 0.0)))
	assert abs(float(min_cv) - 0.05 * float(m.density) * float(m.specific_heat)) < 1e-2 * float(min_cv)
	return m, float(v[22])


def trilinear_clamped(field, p):
	"""field [x, y, z] of texel values, p (..., 3) in texel units (centres at i + 0.5): as a clamped linear sampler."""
	n = np.array(field.shape[:3])
	g = p - 0.5
	i0 = np.floor(g).astype(np.int64)
	fr = (g - i0).astype(f32)
	out = np.zeros(p.shape[:-1], f32)
	for dz in (0, 1):
		for dy in (0, 1):
			for dx in (0, 1):
				ix = np.clip(i0[..., 0] + dx, 0, n[0] - 1)
				iy = np.clip(i0[..., 1] + dy, 0, n[1] - 1)
				iz = np.clip(i0[..., 2] + dz, 0, n[2] - 1)
				w = (fr[..., 0] if dx else 1 - fr[..., 0]) * (fr[..., 1] if dy else 1 - fr[..., 1]) * (fr[..., 2] if dz else 1 - fr[..., 2])
				out = out + w * field[ix, iy, iz]
	return out.astype(f32)


def rel(a, b):
	d = float(np.sqrt(((a.astype(np.float64) - b) ** 2).sum()))
	s = float(np.sqrt((b.astype(np.float64) ** 2).sum()))
	return d / s if s > 0 else d


def report(name, gpu, ref):
	worst = 0.0
	parts = []
	for c, label in enumerate(["T", "wood", "char", "ash"]):
		r = rel(gpu[..., c], ref[..., c])
		worst = max(worst, r)
		parts.append("%s %.2e" % (label, r))
	print("%s: %s  (max |dT| %.3g K)" % (name, "  ".join(parts), float(np.abs(gpu[..., 0] - ref[..., 0]).max())))
	return worst


def ignite(b, centre, radius, temperature):
	idx = np.stack(np.meshgrid(*[np.arange(v) for v in b.n], indexing="ij"), axis=-1).astype(f32) + f32(0.5)
	d = np.sqrt(((idx - np.array(centre, f32)) ** 2).sum(-1))
	mask = (d <= radius) & ((b.w + b.c) > 0)
	b.T = np.where(mask, np.maximum(b.T, f32(temperature)), b.T).astype(f32)


def main():
	d = sys.argv[1]
	info = json.load(open(d + "/info.json"))
	n = info["grid"]
	last = info["last"]
	m, sample_distance = material(last)
	settle = (int(last["settle_axis"]), int(last["settle_sign"]), int(last["settle_floor"]))
	dt = float(last["dt"])
	h = float(last["voxel_size"])
	print("case %s  grid %s  voxel %.4f m  dt %.3f s  steps %d  settle %s  triangles %d" % (info["case"], n, h, dt, info["steps"], settle, info.get("triangles", 0)))
	worst = 0.0
	if info["case"] in ("solo", "ash"):
		s0 = load_state(d + "/initial.bin", n)
		b = br.Burnable(s0[..., 1] > 0, h, m)
		b.w = s0[..., 1].copy()
		first = info["first"]
		zero = lambda p: np.zeros(p.shape[:-1], f32)  # noqa: E731
		for i in range(int(info["steps"])):
			if i == 0 and first["ignite"][3] > 0:
				ignite(b, first["ignite"][:3], first["ignite"][3], first["ignite_temperature"])
			b.step(f32(dt), zero, settle=settle, sample_distance=sample_distance)
		gpu = load_state(d + "/state.bin", n)
		ref = np.stack([b.T, b.w, b.c, b.a], axis=-1)
		worst = report("state after %d steps" % info["steps"], gpu, ref)
		em = load_half(d + "/emission.bin", n)
		print("emission volatiles: rel %.2e (sum gpu %.4g ref %.4g)" % (rel(em[..., 0], b.volatiles), float(em[..., 0].sum()), float(b.volatiles.sum())))
		t = b.totals()
		print("reference totals: wood %.5f char %.5f ash %.5f m^3, T max %.0f K; GPU fractions wood %.4f char %.4f ash %.4f T max %.0f K" % (t["wood"], t["char"], t["ash"], t["T max"] + 293, info["wood"], info["char"], info["ash"], info["t_max"]))
	else:
		s0 = load_state(d + "/before.bin", n)
		b = br.Burnable(s0[..., 1] > 0, h, m)
		b.T, b.w, b.c, b.a = [s0[..., c].copy() for c in range(4)]
		b.volatiles_prev = load_half(d + "/before_emission.bin", n)[..., 0].copy()  # the GPU's step before (the surface's own flame)
		gn = info["gas_grid"]
		dtype = np.float32 if info.get("gas_high_precision") else np.float16
		gas = np.fromfile(d + "/gas.bin", dtype=dtype).astype(np.float32).reshape(gn[2], gn[1], gn[0], 4).transpose(2, 1, 0, 3)
		gas_t = gas[..., 1]
		v2g = np.array(last["voxel_to_gas"], np.float64).reshape(3, 4)

		def gas_temperature(p):
			g = (p.astype(np.float64) @ v2g[:, :3].T + v2g[:, 3]).astype(f32)
			inside = np.all((g >= 0) & (g <= np.array(gn, f32)), axis=-1)
			return np.where(inside, trilinear_clamped(gas_t, g), f32(0.0)).astype(f32)

		e_before = b.exposure()
		b.step(f32(dt), gas_temperature, settle=settle, sample_distance=sample_distance)
		gpu = load_state(d + "/state.bin", n)
		ref = np.stack([b.T, b.w, b.c, b.a], axis=-1)
		worst = report("one coupled step", gpu, ref)
		print("before: T max %.0f K, wood %.4f char %.4f ash %.4f (fractions of the voxels)" % (float(s0[..., 0].max()) + 293, float(s0[..., 1].sum()) / max(float((s0[..., 1] > 0).sum()), 1), float(s0[..., 2].sum()) / max(float((s0[..., 1] > 0).sum()), 1), float(s0[..., 3].sum())))
		# The emission (half precision on the GPU).
		em = load_half(d + "/emission.bin", n)
		ref_em = np.stack([b.volatiles, np.where((e_before > 0) | (b.volatiles > 0), b.T, 0), e_before / 6.0, b.w + b.c], axis=-1).astype(np.float16).astype(np.float32)
		for c, label in enumerate(["volatiles", "surface T", "exposure", "solid"]):
			r = rel(em[..., c], ref_em[..., c])
			worst = max(worst, r) if label != "surface T" else worst
			print("emission %s: rel %.2e" % (label, r))
		# INJECT: per gas cell of the region, 64 samples of the emission over the footprint, 8 of the solid.
		ro = np.array(last["region_origin"], np.int64)
		rs = np.array(last["region_size"], np.int64)
		inj = np.fromfile(d + "/inject.bin", dtype=np.float16).astype(np.float32).reshape(rs[2], rs[1], rs[0], 4).transpose(2, 1, 0, 3)
		g2v = np.linalg.inv(np.vstack([v2g, [0, 0, 0, 1]]))[:3]
		nv = np.array(n, f32)
		gpu_em = em  # INJECT sampled the GPU's emission

		def emission_at(pg):
			v = (pg.astype(np.float64) @ g2v[:, :3].T + g2v[:, 3]).astype(f32)
			inside = np.all((v >= 0) & (v <= nv), axis=-1)
			return np.stack([np.where(inside, trilinear_clamped(gpu_em[..., c], v), 0) for c in range(4)], axis=-1)

		ii = np.stack(np.meshgrid(*[np.arange(v) for v in rs], indexing="ij"), axis=-1).astype(f32)
		centre = ii + ro.astype(f32) + f32(0.5)
		fuel = np.zeros(tuple(rs), f32)
		tf = np.zeros(tuple(rs), f32)
		ts = np.zeros(tuple(rs), f32)
		ex = np.zeros(tuple(rs), f32)
		fp = float(last["footprint"])
		for i in range(64):
			o = (np.array([i & 3, (i >> 2) & 3, i >> 4], f32) + 0.5) * 0.25 - 0.5
			e = emission_at(centre + o * fp)
			fuel += e[..., 0]
			tf += e[..., 0] * e[..., 1]
			ts += e[..., 1] * e[..., 2]
			ex += e[..., 2]
		solid = np.zeros(tuple(rs), f32)
		for i in range(8):
			o = np.array([i & 1, (i >> 1) & 1, i >> 2], f32) * 0.5 - 0.25
			solid += emission_at(centre + o)[..., 3]
		t_surface = np.where(fuel > 0, tf / np.maximum(fuel, 1e-30), np.where(ex > 0, ts / np.maximum(ex, 1e-30), 0))
		ref_inj = np.stack([fuel * (m.fuel_per_kg / f32(64.0)), t_surface, ex / 64, solid / 8], axis=-1)
		for c, label in enumerate(["fuel", "surface T", "exposure", "solid"]):
			r = rel(inj[..., c], ref_inj[..., c])
			worst = max(worst, r)
			print("inject %s: rel %.2e (sum gpu %.4g ref %.4g)" % (label, r, float(inj[..., c].sum()), float(ref_inj[..., c].sum())))
		print("region %s + %s, solid cells (> 0.5) %d" % (list(ro), list(rs), int((inj[..., 3] > 0.5).sum())))
	print("WORST %.2e" % worst)


if __name__ == "__main__":
	main()
