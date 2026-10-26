#!/usr/bin/env python3
"""NumPy reference of FellowEngine's burning solids (fellow_burn_sim.glsl, FellowBurnable3D; decision 0120).

A burnable object is a box of voxels around its mesh (cubic voxels of h metres), each with
  T  the temperature above ambient (K),
  w  the volume fraction of virgin wood, c of char, a of ash (w + c + a <= 1; the rest is air).
Per step (dt of burning time: the physics tick times `burn_speed`), in this order (one GPU pass, BURN_HEAT, then
BURN_SETTLE):
  1. conduction between neighbouring voxels (conductivity of the mixture, the faces' harmonic mean; air does not
     conduct), explicit;
  2. exchange with the gas at the exposed faces (a face is exposed by how little solid and ash is beyond it):
     convection and radiation to the gas temperature outside the surface (the hottest of three samples along the
     outward direction: the flame in front of it) or the surface's own flame (thinner than a voxel: over the voxels
     that released volatiles the step before, beside them and up to `flame_reach` above, as much as their flux per
     area covers it), semi-implicit (stable at any step);
  3. pyrolysis above `pyrolysis_temperature`: wood -> char (`char_volume` of its volume), endothermic, the volatiles
     (1 - `char_yield` of the mass) released as the gas's fuel at the surface;
  4. char oxidation (glowing) above `glow_temperature` where the char is exposed: char -> ash (`ash_volume` of its
     volume), exothermic;
  5. ash settles: ash with no wood or char left in its voxel falls into the voxel below (along the voxel axis
     nearest to the world's down) while that has room, down to the object's lowest layer.
The gas sees per voxel: the fuel released (units per second per m^3), the surface temperature and exposure (its air
next to a hot surface heats), the solid fraction (w + c: an obstacle).
"""

import numpy as np

f32 = np.float32
T_AMBIENT = f32(293.0)
SIGMA = f32(5.670374e-8)


class Material:
	"""Wood (softwood); SI units; temperatures in K above ambient (293 K)."""

	def __init__(self):
		self.density = f32(500.0)  # kg/m^3
		self.specific_heat = f32(1700.0)  # J/(kg K)
		self.conductivity = f32(0.15)  # W/(m K)
		self.char_conductivity = f32(0.08)
		self.ash_conductivity = f32(0.04)
		self.char_density = f32(125.0)  # kg/m^3 of char volume
		self.char_specific_heat = f32(1000.0)
		self.ash_density = f32(60.0)
		self.ash_specific_heat = f32(800.0)
		self.pyrolysis_temperature = f32(280.0)  # onset (573 K); full rate 120 K above
		self.pyrolysis_rate = f32(0.05)  # 1/s at full rate
		self.pyrolysis_heat = f32(2.0e5)  # J per kg of wood pyrolysed (absorbed)
		self.char_yield = f32(0.25)  # mass fraction of the wood left as char
		self.char_volume = f32(0.85)  # volume of char per volume of wood pyrolysed
		self.glow_temperature = f32(380.0)  # char oxidation onset (673 K); full rate 200 K above
		self.glow_rate = f32(0.02)  # 1/s at full rate, exposed char
		self.glow_heat = f32(4.0e6)  # J per kg of char oxidised kept by the solid
		self.ash_volume = f32(0.35)  # volume of ash per volume of char oxidised
		self.convection = f32(20.0)  # W/(m^2 K)
		self.emissivity = f32(0.9)
		self.fuel_per_kg = f32(40.0)  # the gas's fuel units per kg of volatiles
		self.settle_rate = f32(2.0)  # 1/s
		self.flame_temperature = f32(0.0)  # K above ambient: a surface in its own flame exchanges with it (0: off)
		self.flame_flux = f32(0.005)  # kg/(m^2 s) of volatiles for a surface fully in its flame
		self.flame_reach = 0  # voxels: the flame over the voxels releasing volatiles and this far above them


def smoothstep(e0, e1, x):
	t = np.clip((x - e0) / (e1 - e0), 0.0, 1.0).astype(f32)
	return (t * t * (f32(3.0) - f32(2.0) * t)).astype(f32)


def neighbour(a, axis, d, fill):
	"""a shifted so that out[i] = a[i + d] along axis; fill outside the grid."""
	out = np.roll(a, -d, axis=axis)
	sl = [slice(None)] * 3
	sl[axis] = -1 if d > 0 else 0
	out[tuple(sl)] = fill
	return out


def shift(a, axis, d, fill):
	"""a shifted so that out[i] = a[i + d] along axis (any d); fill outside the grid."""
	out = np.full_like(a, fill)
	n = a.shape[axis]
	if abs(d) >= n:
		return out
	src = [slice(None)] * a.ndim
	dst = [slice(None)] * a.ndim
	if d >= 0:
		src[axis] = slice(d, n)
		dst[axis] = slice(0, n - d)
	else:
		src[axis] = slice(0, n + d)
		dst[axis] = slice(-d, n)
	out[tuple(dst)] = a[tuple(src)]
	return out


class Burnable:
	def __init__(self, occupancy, h, mat=None):
		self.n = occupancy.shape
		self.h = f32(h)
		self.mat = mat or Material()
		self.T = np.zeros(self.n, f32)
		self.w = occupancy.astype(f32)
		self.c = np.zeros(self.n, f32)
		self.a = np.zeros(self.n, f32)
		self.fuel = np.zeros(self.n, f32)  # released this step, units / (s m^3)
		self.volatiles = np.zeros(self.n, f32)  # released this step, kg / (s m^3)
		self.volatiles_prev = np.zeros(self.n, f32)  # the step before's (the flame rising from them heats this step)
		self.time = 0.0

	def heat_capacity(self):
		m = self.mat
		cv = (m.density * m.specific_heat * self.w + m.char_density * m.char_specific_heat * self.c + m.ash_density * m.ash_specific_heat * self.a).astype(f32)
		return np.maximum(cv, f32(0.05) * m.density * m.specific_heat).astype(f32)

	def exposure(self):
		"""Per voxel with material: the sum over its six faces of how open they are (1 - solid beyond - 0.6 ash
		beyond); outside the grid open."""
		solid = (self.w + self.c).astype(f32)
		e = np.zeros(self.n, f32)
		for axis in range(3):
			for d in (-1, 1):
				s = neighbour(solid, axis, d, f32(0.0))
				a = neighbour(self.a, axis, d, f32(0.0))
				e = (e + np.clip(f32(1.0) - s - f32(0.6) * a, 0.0, 1.0)).astype(f32)
		fill = (solid + self.a).astype(f32)
		return np.where(fill >= f32(1e-3), e, f32(0.0)).astype(f32)

	def outward(self):
		"""The direction out of the material: minus the solid's gradient (central, clamped), unit or zero."""
		solid = (self.w + self.c).astype(f32)
		g = []
		for axis in range(3):
			g.append((neighbour(solid, axis, 1, f32(0.0)) - neighbour(solid, axis, -1, f32(0.0))).astype(f32))
		g = -np.stack(g, axis=-1)
		l = np.sqrt((g * g).sum(-1)).astype(f32)
		return np.where(l[..., None] > f32(1e-4), g / np.maximum(l, f32(1e-4))[..., None], f32(0.0)).astype(f32)

	def step(self, dt, gas_temperature, settle=(1, -1, 0), sample_distance=1.5):
		"""dt: burning time (s). gas_temperature(positions (..., 3) in voxel units) -> K above ambient. settle: the
		ash's axis, direction and floor; sample_distance: how far outside the surface the gas is sampled (voxels)."""
		m = self.mat
		h = self.h
		w, c, a, T = self.w, self.c, self.a, self.T
		cv = self.heat_capacity()
		# 1. conduction
		k = (m.conductivity * w + m.char_conductivity * c + m.ash_conductivity * a).astype(f32)
		lap = np.zeros(self.n, f32)
		for axis in range(3):
			for d in (-1, 1):
				kn = neighbour(k, axis, d, f32(0.0))
				tn = neighbour(T, axis, d, f32(0.0))
				kf = np.where((k > 0) & (kn > 0), f32(2.0) * k * kn / np.maximum(k + kn, f32(1e-12)), f32(0.0)).astype(f32)
				lap = (lap + kf * (tn - T)).astype(f32)
		T = (T + dt * lap / (cv * h * h)).astype(f32)
		# 2. exchange with the gas at the exposed faces (semi-implicit)
		e = self.exposure()
		n = self.outward()
		idx = np.stack(np.meshgrid(*[np.arange(v) for v in self.n], indexing="ij"), axis=-1).astype(f32) + f32(0.5)
		sd = f32(sample_distance)
		tg = np.maximum(gas_temperature(idx + n * (f32(0.5) * sd)), np.maximum(gas_temperature(idx + n * sd), gas_temperature(idx + n * (f32(1.5) * sd)))).astype(f32)
		if float(m.flame_temperature) > 0.0:
			# The surface's own flame (thinner than a voxel): where the voxel or those below it (up to flame_reach)
			# released volatiles the step before, the surface exchanges with a flame of flame_temperature, as much as
			# their flux (per area) covers it.
			axis, sign = settle[0], -settle[1]  # up
			# The flame over the voxel: the volatiles of the voxels below it (up to flame_reach) and beside them (one
			# voxel across: the flame's width, how it creeps sideways).
			side = np.zeros(self.n, f32)
			o1, o2 = [x for x in range(3) if x != axis]
			for d1 in (-1, 0, 1):
				for d2 in (-1, 0, 1):
					side = np.maximum(side, shift(shift(self.volatiles_prev, o1, d1, f32(0.0)), o2, d2, f32(0.0)))
			q = side.copy()
			for kk in range(1, int(m.flame_reach) + 1):
				q = np.maximum(q, shift(side, axis, -sign * kk, f32(0.0)))
			cover = np.clip(q * h / m.flame_flux, 0.0, 1.0).astype(f32)
			tg = np.maximum(tg, cover * m.flame_temperature).astype(f32)
		ta = T + T_AMBIENT
		tga = tg + T_AMBIENT
		hr = (m.emissivity * SIGMA * (tga * tga + ta * ta) * (tga + ta)).astype(f32)
		coef = ((m.convection + hr) * e / (cv * h)).astype(f32)
		T = ((T + dt * coef * tg) / (f32(1.0) + dt * coef)).astype(f32)
		# 3. pyrolysis
		rp = (m.pyrolysis_rate * smoothstep(m.pyrolysis_temperature, m.pyrolysis_temperature + f32(120.0), T)).astype(f32)
		dw = (w * (f32(1.0) - np.exp(-rp * dt))).astype(f32)
		w = (w - dw).astype(f32)
		c = (c + dw * m.char_volume).astype(f32)
		T = (T - dw * m.density * m.pyrolysis_heat / cv).astype(f32)
		self.volatiles = (dw * m.density * (f32(1.0) - m.char_yield) / f32(dt)).astype(f32)
		self.fuel = (self.volatiles * m.fuel_per_kg).astype(f32)
		# 4. char oxidation where exposed
		ro = (m.glow_rate * smoothstep(m.glow_temperature, m.glow_temperature + f32(200.0), T) * np.minimum(e, f32(1.0))).astype(f32)
		dc = (c * (f32(1.0) - np.exp(-ro * dt))).astype(f32)
		c = (c - dc).astype(f32)
		a = (a + dc * m.ash_volume).astype(f32)
		T = (T + dc * m.char_density * m.glow_heat / cv).astype(f32)
		# empty voxels hold the gas's temperature
		fill = (w + c + a).astype(f32)
		T = np.where(fill < f32(1e-3), tg, T).astype(f32)
		self.w, self.c, self.a, self.T = w, c, a, T
		self.settle(dt, *settle)
		self.volatiles_prev = self.volatiles.astype(np.float16).astype(f32)  # the GPU keeps it in half precision
		self.time += float(dt)

	def settle(self, dt, axis=1, sign=-1, floor=0):
		"""Ash moves along `axis` in the direction `sign` (the voxel axis nearest to the world's down) while the voxel
		there has room, not beyond layer `floor` (the object's lowest along it)."""
		m = self.mat
		w, c, a = self.w, self.c, self.a
		fill = (w + c + a).astype(f32)
		rate = f32(1.0 - np.exp(-float(m.settle_rate) * float(dt)))
		shape = [1, 1, 1]
		shape[axis] = self.n[axis]
		along = np.arange(self.n[axis]).reshape(shape)
		ok = np.broadcast_to((along >= floor) if sign < 0 else (along <= floor), self.n)
		fill_b = neighbour(fill, axis, sign, f32(1.0))
		ok_b = neighbour(ok.astype(np.uint8), axis, sign, 0) > 0
		cap_below = np.where(ok_b, np.clip(f32(1.0) - fill_b, 0.0, 1.0), f32(0.0)).astype(f32)
		movable = (w + c) < f32(0.05)
		out = np.where(movable, np.minimum(a * rate, cap_below), f32(0.0)).astype(f32)
		inflow = neighbour(out, axis, -sign, f32(0.0))
		self.a = (a - out + inflow).astype(f32)

	def totals(self):
		v = float(self.h) ** 3
		return {"wood": float(self.w.sum()) * v, "char": float(self.c.sum()) * v, "ash": float(self.a.sum()) * v, "T max": float(self.T.max())}


def log_occupancy(length=0.8, radius=0.1, h=0.025, margin=1):
	"""A horizontal log along x."""
	nx = int(round(length / h)) + 2 * margin
	nr = int(np.ceil(2 * radius / h)) + 2 * margin
	ny = nz = nr
	ii, jj, kk = np.meshgrid(np.arange(nx), np.arange(ny), np.arange(nz), indexing="ij")
	x = (ii + 0.5) * h - margin * h
	y = (jj + 0.5) * h - ny * h * 0.5
	z = (kk + 0.5) * h - nz * h * 0.5
	return (x >= 0) & (x <= length) & (y * y + z * z <= radius * radius)


if __name__ == "__main__":
	import time
	occ = log_occupancy()
	b = Burnable(occ, 0.025)
	n = np.array(b.n, f32)

	def flame(p):
		# a fire under the log's middle: 900 K above ambient in a blob below and around it
		cx = n[0] * 0.5
		d = np.sqrt(((p[..., 0] - cx) / (n[0] * 0.2)) ** 2 + ((p[..., 1] - n[1] * 0.1) / (n[1] * 0.6)) ** 2 + ((p[..., 2] - n[2] * 0.5) / (n[2] * 0.7)) ** 2)
		return (f32(900.0) * np.exp(-d * d)).astype(f32)

	t0 = time.time()
	dt = 0.5
	for i in range(1200):
		b.step(dt, flame if i < 240 else (lambda p: np.zeros(p.shape[:-1], f32)))
		if (i + 1) % 60 == 0:
			t = b.totals()
			print("t %5.0f s  wood %.4f  char %.4f  ash %.4f m^3  T max %6.0f K  fuel %.3f" % (b.time, t["wood"], t["char"], t["ash"], t["T max"] + 293, float(b.fuel.sum()) * 0.025 ** 3), flush=True)
	print("%.1f s" % (time.time() - t0))
