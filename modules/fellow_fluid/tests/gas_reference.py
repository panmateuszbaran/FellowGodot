#!/usr/bin/env python3
"""NumPy reference of FellowEngine's GPU gas solver (fellow_gas.glsl, FellowGasVolume3D).

The kernels below are the GPU's, one function per compute pass, in the same order and with the same formulas, in
float32 (the GPU's arithmetic; its state textures are RGBA16F unless the volume's `high_precision` is set, then
RGBA32F like here). Used to check the GPU on lavapipe (tests/gas_compare.py) and to choose the solver's defaults.

Grid (decision 0117):
  * cells (i, j, k), 0 <= i < nx ..., centres at grid position (i + 0.5, j + 0.5, k + 0.5); one cell = dx metres;
  * scalars S[i, j, k] = (smoke, temperature above ambient (K), fuel, burn rate (fuel / s)) at the centres;
  * staggered (MAC) velocity in one (nx + 1) x (ny + 1) x (nz + 1) texture: u at x faces (i, j + .5, k + .5),
    v at y faces (i + .5, j, k + .5), w at z faces (i + .5, j + .5, k); the texels past a component's last face
    (u at j = ny or k = nz, ...) repeat the last face (each pass computes the clamped face), so the texture's
    clamp-to-edge trilinear sampling extrapolates with a zero gradient;
  * pressure (times dt / rho) at the centres, warm-started from the last step, solved by red-black SOR;
  * the domain's six faces are closed (a wall), open (pressure 0 outside, ambient air flows in) or, for an open face
    the wind blows in through, an inflow at the wind's velocity.
"""

import numpy as np

f32 = np.float32
G = f32(9.81)
T_AMBIENT = f32(293.0)

# Face indices of the closed / open masks.
NEG_X, POS_X, NEG_Y, POS_Y, NEG_Z, POS_Z = range(6)


class Params:
	def __init__(self, n=(32, 48, 32), dx=0.1):
		self.n = tuple(int(v) for v in n)
		self.dx = f32(dx)
		self.dt = f32(1.0 / 60.0)
		self.up = np.array([0.0, 1.0, 0.0], f32)  # world up in grid axes
		self.closed = [False, False, True, False, False, False]  # the floor (-y) closed
		self.wind = np.array([0.0, 0.0, 0.0], f32)  # m/s, grid axes
		self.buoyancy = f32(1.0)  # x physical (g * T / (T_ambient + T))
		self.smoke_weight = f32(0.0)  # m/s^2 per unit smoke, downwards
		self.vorticity = f32(0.3)  # confinement epsilon
		self.smoke_dissipation = f32(0.05)  # 1/s
		self.cooling = f32(1.2)  # 1/s
		self.ignition = f32(250.0)  # K above ambient
		self.burn_rate = f32(6.0)  # 1/s (fraction of the fuel burnt per second when hot)
		self.heat = f32(1400.0)  # K per unit fuel burnt
		self.soot = f32(0.6)  # smoke per unit fuel burnt
		self.expansion = f32(0.4)  # divergence (1/s) per unit fuel burnt per second
		self.iterations = 20
		self.omega = f32(1.6)
		self.multigrid = True  # the pressure by multigrid V-cycles (else SOR)
		self.multigrid_cycles = 2
		self.maccormack = True
		self.max_speed_cells = f32(6.0)  # |u| <= this many cells per step
		self.emitters = []  # dicts: centre (grid), radius (grid), smoke, temperature, fuel (per second), velocity


def trilinear(tex, t):
	"""Clamp-to-edge trilinear sample of tex (X, Y, Z[, C]) at texel coordinates t (..., 3); texel centres at integers."""
	size = np.array(tex.shape[:3])
	t0 = np.floor(t)
	f = (t - t0).astype(f32)
	i0 = t0.astype(np.int64)
	i1 = i0 + 1
	i0 = np.clip(i0, 0, size - 1)
	i1 = np.clip(i1, 0, size - 1)
	out = None
	for cx in (0, 1):
		ix = i1[..., 0] if cx else i0[..., 0]
		wx = f[..., 0] if cx else (f32(1.0) - f[..., 0])
		for cy in (0, 1):
			iy = i1[..., 1] if cy else i0[..., 1]
			wy = f[..., 1] if cy else (f32(1.0) - f[..., 1])
			for cz in (0, 1):
				iz = i1[..., 2] if cz else i0[..., 2]
				wz = f[..., 2] if cz else (f32(1.0) - f[..., 2])
				w = (wx * wy * wz).astype(f32)
				v = tex[ix, iy, iz]
				if v.ndim > w.ndim:
					w = w[..., None]
				term = (v * w).astype(f32)
				out = term if out is None else (out + term).astype(f32)
	return out


def corners_minmax(tex, t):
	size = np.array(tex.shape[:3])
	i0 = np.clip(np.floor(t).astype(np.int64), 0, size - 1)
	i1 = np.clip(np.floor(t).astype(np.int64) + 1, 0, size - 1)
	lo = None
	hi = None
	for cx in (0, 1):
		ix = i1[..., 0] if cx else i0[..., 0]
		for cy in (0, 1):
			iy = i1[..., 1] if cy else i0[..., 1]
			for cz in (0, 1):
				iz = i1[..., 2] if cz else i0[..., 2]
				v = tex[ix, iy, iz]
				lo = v if lo is None else np.minimum(lo, v)
				hi = v if hi is None else np.maximum(hi, v)
	return lo, hi


# Texel offsets of the three velocity components' lattices: texel coordinate = grid position - offset.
VEL_OFFSET = [np.array([0.0, 0.5, 0.5], f32), np.array([0.5, 0.0, 0.5], f32), np.array([0.5, 0.5, 0.0], f32)]


def sample_velocity(vel, g):
	"""vel: (nx+1, ny+1, nz+1, 3); g (..., 3) grid positions; returns (..., 3)."""
	return np.stack([trilinear(vel[..., c], g - VEL_OFFSET[c]) for c in range(3)], axis=-1)


class State:
	def __init__(self, p):
		nx, ny, nz = p.n
		self.p = p
		self.s = np.zeros((nx, ny, nz, 4), f32)
		self.vel = np.zeros((nx + 1, ny + 1, nz + 1, 3), f32)
		self.pressure = np.zeros((nx, ny, nz), f32)
		self.solid = np.zeros((nx, ny, nz), bool)
		self.solid_vel = np.zeros((nx, ny, nz, 3), f32)
		self.time = 0.0
		self.steps = 0
		gi = np.arange(nx, dtype=f32) + f32(0.5)
		gj = np.arange(ny, dtype=f32) + f32(0.5)
		gk = np.arange(nz, dtype=f32) + f32(0.5)
		self.cell_g = np.stack(np.meshgrid(gi, gj, gk, indexing="ij"), axis=-1).astype(f32)
		# Face positions (clamped faces for the texels past the last face), per component.
		self.face_g = []
		for c in range(3):
			idx = [np.arange(nx + 1), np.arange(ny + 1), np.arange(nz + 1)]
			pos = []
			for a in range(3):
				if a == c:
					pos.append(idx[a].astype(f32))
				else:
					pos.append(np.minimum(idx[a], p.n[a] - 1).astype(f32) + f32(0.5))
			self.face_g.append(np.stack(np.meshgrid(*pos, indexing="ij"), axis=-1).astype(f32))

	# ---------------------------------------------------------------- helpers
	def outside_open(self, g):
		"""True where g left the domain through a face that is not closed (ambient air there)."""
		p = self.p
		out = np.zeros(g.shape[:-1], bool)
		for a in range(3):
			if not p.closed[2 * a]:
				out |= g[..., a] < 0.0
			if not p.closed[2 * a + 1]:
				out |= g[..., a] > f32(p.n[a])
		return out

	def face_kind(self, a, side):
		"""0 closed, 1 open, 2 inflow (open, the wind blowing in)."""
		p = self.p
		if p.closed[2 * a + side]:
			return 0
		normal_out = -1.0 if side == 0 else 1.0
		return 2 if p.wind[a] * normal_out < -1e-6 else 1

	# ---------------------------------------------------------------- passes
	def emit(self):
		p = self.p
		s = self.s
		for e in p.emitters:
			d = np.linalg.norm(self.cell_g - np.array(e["centre"], f32), axis=-1).astype(f32)
			w = np.clip(f32(e["radius"]) + f32(0.5) - d, 0.0, 1.0).astype(f32)
			s[..., 0] += w * f32(e.get("smoke", 0.0)) * p.dt
			s[..., 2] += w * f32(e.get("fuel", 0.0)) * p.dt
			s[..., 1] = np.maximum(s[..., 1], w * f32(e.get("temperature", 0.0)))
		s[self.solid] = 0.0

	def react(self, s):
		p = self.p
		# The factors per step as the CPU computes them (double precision, then float), FellowGas::_run_step().
		burn = f32(1.0 - np.exp(-float(p.burn_rate) * float(p.dt)))
		keep_smoke = f32(np.exp(-float(p.smoke_dissipation) * float(p.dt)))
		keep_temp = f32(np.exp(-float(p.cooling) * float(p.dt)))
		smoke, temp, fuel = s[..., 0], s[..., 1], s[..., 2]
		hot = temp > p.ignition
		burnt = np.where(hot, fuel * burn, f32(0.0)).astype(f32)
		fuel = fuel - burnt
		temp = temp + p.heat * burnt
		smoke = smoke + p.soot * burnt
		flame = (burnt / p.dt).astype(f32)
		smoke = (smoke * keep_smoke).astype(f32)
		temp = (temp * keep_temp).astype(f32)
		out = np.stack([np.maximum(smoke, 0), np.maximum(temp, 0), np.maximum(fuel, 0), flame], axis=-1).astype(f32)
		out[self.solid] = 0.0
		return out

	def advect_scalars(self):
		p = self.p
		g = self.cell_g
		u = sample_velocity(self.vel, g)
		gb = (g - p.dt * u / p.dx).astype(f32)
		out_b = self.outside_open(gb)
		hat = trilinear(self.s, gb - f32(0.5))
		hat[out_b] = 0.0
		if p.maccormack:
			gf = (g + p.dt * u / p.dx).astype(f32)
			tilde = trilinear(hat, gf - f32(0.5))
			new = (hat + f32(0.5) * (self.s - tilde)).astype(f32)
			lo, hi = corners_minmax(self.s, gb - f32(0.5))
			new = np.clip(new, lo, hi)
			new[out_b] = hat[out_b]
		else:
			new = hat
		self.s = self.react(new)

	def advect_velocity(self):
		p = self.p
		new = np.zeros_like(self.vel)
		hats = []
		gbs = []
		for c in range(3):
			gf_ = self.face_g[c]
			u = sample_velocity(self.vel, gf_)
			gb = (gf_ - p.dt * u / p.dx).astype(f32)
			hats.append(trilinear(self.vel[..., c], gb - VEL_OFFSET[c]))
			gbs.append((gb, u))
		hat = np.stack(hats, axis=-1)
		for c in range(3):
			gb, u = gbs[c]
			if p.maccormack:
				gfw = (self.face_g[c] + p.dt * u / p.dx).astype(f32)
				tilde = trilinear(hat[..., c], gfw - VEL_OFFSET[c])
				v = (hat[..., c] + f32(0.5) * (self.vel[..., c] - tilde)).astype(f32)
				lo, hi = corners_minmax(self.vel[..., c], gb - VEL_OFFSET[c])
				new[..., c] = np.clip(v, lo, hi)
			else:
				new[..., c] = hat[..., c]
		self.vel = new

	def cell_velocity(self):
		v = self.vel
		nx, ny, nz = self.p.n
		uc = f32(0.5) * (v[0:nx, 0:ny, 0:nz, 0] + v[1:nx + 1, 0:ny, 0:nz, 0])
		vc = f32(0.5) * (v[0:nx, 0:ny, 0:nz, 1] + v[0:nx, 1:ny + 1, 0:nz, 1])
		wc = f32(0.5) * (v[0:nx, 0:ny, 0:nz, 2] + v[0:nx, 0:ny, 1:nz + 1, 2])
		return np.stack([uc, vc, wc], axis=-1).astype(f32)

	@staticmethod
	def central(a, axis, dx):
		"""Central difference with clamped neighbours (the GPU's clamped texel fetches)."""
		n = a.shape[axis]
		ip = np.minimum(np.arange(n) + 1, n - 1)
		im = np.maximum(np.arange(n) - 1, 0)
		return ((np.take(a, ip, axis=axis) - np.take(a, im, axis=axis)) / (f32(2.0) * dx)).astype(f32)

	def cell_forces(self):
		p = self.p
		uc = self.cell_velocity()
		dx = p.dx
		wx = self.central(uc[..., 2], 1, dx) - self.central(uc[..., 1], 2, dx)
		wy = self.central(uc[..., 0], 2, dx) - self.central(uc[..., 2], 0, dx)
		wz = self.central(uc[..., 1], 0, dx) - self.central(uc[..., 0], 1, dx)
		w = np.stack([wx, wy, wz], axis=-1).astype(f32)
		wl = np.sqrt((w * w).sum(-1)).astype(f32)
		eta = np.stack([self.central(wl, a, dx) for a in range(3)], axis=-1).astype(f32)
		nrm = (eta / (np.sqrt((eta * eta).sum(-1))[..., None] + f32(1e-5))).astype(f32)
		f = (p.vorticity * dx * np.cross(nrm, w)).astype(f32)
		temp = self.s[..., 1]
		smoke = self.s[..., 0]
		a_up = (p.buoyancy * G * temp / (T_AMBIENT + temp) - p.smoke_weight * smoke).astype(f32)
		f = (f + a_up[..., None] * p.up).astype(f32)
		f[self.solid] = 0.0
		return f

	def apply_forces(self, f):
		p = self.p
		n = p.n
		for c in range(3):
			# Face (clamped index) between cell a = idx - 1 and b = idx along axis c.
			idx = [np.arange(n[0] + 1), np.arange(n[1] + 1), np.arange(n[2] + 1)]
			for a in range(3):
				if a != c:
					idx[a] = np.minimum(idx[a], n[a] - 1)
			ib = [idx[0], idx[1], idx[2]]
			ia = list(ib)
			ia[c] = np.maximum(idx[c] - 1, 0)
			ib[c] = np.minimum(idx[c], n[c] - 1)
			fa = f[np.ix_(ia[0], ia[1], ia[2])][..., c]
			fb = f[np.ix_(ib[0], ib[1], ib[2])][..., c]
			face = (f32(0.5) * (fa + fb)).astype(f32)
			self.vel[..., c] = (self.vel[..., c] + p.dt * face).astype(f32)

	def face_neighbour_cells(self, c):
		"""For component c's texels: the indices of the cells on either side (clamped) and boundary flags."""
		n = self.p.n
		idx = [np.arange(n[0] + 1), np.arange(n[1] + 1), np.arange(n[2] + 1)]
		for a in range(3):
			if a != c:
				idx[a] = np.minimum(idx[a], n[a] - 1)
		grid = np.meshgrid(*idx, indexing="ij")
		ia = list(grid)
		ib = list(grid)
		ia[c] = np.maximum(grid[c] - 1, 0)
		ib[c] = np.minimum(grid[c], n[c] - 1)
		low = grid[c] == 0
		high = grid[c] == n[c]
		return ia, ib, low, high

	def boundary_velocity(self, c, side):
		kind = self.face_kind(c, side)
		return kind, (self.p.wind[c] if kind == 2 else f32(0.0))

	def divergence(self):
		p = self.p
		nx, ny, nz = p.n
		v = self.vel
		div = np.zeros(p.n, f32)
		for c in range(3):
			sl_lo = [slice(0, nx), slice(0, ny), slice(0, nz)]
			sl_hi = list(sl_lo)
			sl_hi[c] = slice(1, p.n[c] + 1)
			lo = v[tuple(sl_lo) + (c,)].copy()
			hi = v[tuple(sl_hi) + (c,)].copy()
			# Faces against solid neighbours take the solid's velocity.
			nb_lo = np.roll(self.solid, 1, axis=c)
			nb_hi = np.roll(self.solid, -1, axis=c)
			sv_lo = np.roll(self.solid_vel[..., c], 1, axis=c)
			sv_hi = np.roll(self.solid_vel[..., c], -1, axis=c)
			first = [slice(None)] * 3
			first[c] = 0
			last = [slice(None)] * 3
			last[c] = p.n[c] - 1
			nb_lo[tuple(first)] = False
			nb_hi[tuple(last)] = False
			lo = np.where(nb_lo, sv_lo, lo)
			hi = np.where(nb_hi, sv_hi, hi)
			kind, bv = self.boundary_velocity(c, 0)
			if kind != 1:
				lo[tuple(first)] = bv
			kind, bv = self.boundary_velocity(c, 1)
			if kind != 1:
				hi[tuple(last)] = bv
			div = (div + (hi - lo) / p.dx).astype(f32)
		rhs = (div - p.expansion * self.s[..., 3]).astype(f32)
		rhs[self.solid] = 0.0
		return rhs

	def neighbour_terms(self, q, solid=None):
		"""Sum of the included neighbours' q and their count (solid and closed / inflow faces excluded, open faces
		counted with q = 0), on the grid or on a multigrid level (q's shape)."""
		solid = self.solid if solid is None else solid
		n = q.shape
		tot = np.zeros(n, f32)
		cnt = np.zeros(n, f32)
		for c in range(3):
			for side in (0, 1):
				shift = 1 if side == 0 else -1
				qn = np.roll(q, shift, axis=c)
				sn = np.roll(solid, shift, axis=c)
				edge = [slice(None)] * 3
				edge[c] = 0 if side == 0 else n[c] - 1
				inc = ~sn
				inc[tuple(edge)] = False
				tot = (tot + np.where(inc, qn, f32(0.0))).astype(f32)
				cnt = cnt + inc
				if self.face_kind(c, side) == 1:
					e = np.zeros(n, bool)
					e[tuple(edge)] = True
					cnt = cnt + e  # an open face: q = 0 outside
		return tot, cnt

	def smooth(self, q, rhs, solid, dx, omega, iterations):
		"""Red-black Gauss-Seidel / SOR sweeps on a level (the GPU's PRESSURE and MG_COARSE passes)."""
		ii, jj, kk = np.meshgrid(*[np.arange(v) for v in q.shape], indexing="ij")
		colour = (ii + jj + kk) & 1
		dx2 = (dx * dx).astype(f32)
		for _ in range(iterations):
			for col in (0, 1):
				tot, cnt = self.neighbour_terms(q, solid)
				gs = np.where(cnt > 0, (tot - dx2 * rhs) / np.maximum(cnt, 1), f32(0.0)).astype(f32)
				new = ((f32(1.0) - omega) * q + omega * gs).astype(f32)
				q = np.where((colour == col) & ~solid, new, q).astype(f32)
				q[solid & (colour == col)] = 0.0
		return q

	def multigrid_levels(self):
		sizes = []
		s = self.p.n
		while min(s) > 4:
			s = tuple((v + 1) // 2 for v in s)
			sizes.append(s)
		return sizes

	def restrict(self, q, rhs, solid, dx, coarse):
		"""MG_RESTRICT: the children's residual averaged over the fluid ones; a coarse cell is solid where all are."""
		tot, cnt = self.neighbour_terms(q, solid)
		dx2 = (dx * dx).astype(f32)
		r = (rhs - (tot - cnt * q) / dx2).astype(f32)
		n = q.shape
		pad = [(0, 2 * coarse[a] - n[a]) for a in range(3)]
		rp = np.pad(np.where(solid, f32(0.0), r), pad)
		fp = np.pad(~solid, pad, constant_values=False)
		rs = np.zeros(coarse, f32)
		fs = np.zeros(coarse, f32)
		for k in range(8):
			ox, oy, oz = k & 1, (k >> 1) & 1, (k >> 2) & 1
			rs = (rs + rp[ox::2, oy::2, oz::2]).astype(f32)
			fs = fs + fp[ox::2, oy::2, oz::2]
		rc = np.where(fs > 0, rs / np.maximum(fs, 1), f32(0.0)).astype(f32)
		return rc, fs == 0

	@staticmethod
	def prolong(qc, n, solid):
		"""MG_PROLONG: the coarse correction interpolated trilinearly (3/4, 1/4 per axis), added to fluid cells."""
		m = np.array(qc.shape)
		ii, jj, kk = np.meshgrid(np.arange(n[0]), np.arange(n[1]), np.arange(n[2]), indexing="ij")
		c = np.stack([ii, jj, kk], axis=-1)
		par = c // 2
		nb = np.clip(par + (c & 1) * 2 - 1, 0, m - 1)
		e = np.zeros(n, f32)
		for k in range(8):
			sel = [(k >> a) & 1 for a in range(3)]
			q = [nb[..., a] if sel[a] else par[..., a] for a in range(3)]
			w = f32((0.25 if sel[0] else 0.75) * (0.25 if sel[1] else 0.75))
			w = f32(w * f32(0.25 if sel[2] else 0.75))
			e = (e + w * qc[q[0], q[1], q[2]]).astype(f32)
		return np.where(solid, f32(0.0), e).astype(f32)

	def pressure_solve_multigrid(self, rhs):
		p = self.p
		omega = f32(1.15)
		sizes = self.multigrid_levels()
		L = len(sizes)
		q0 = self.pressure
		for _ in range(p.multigrid_cycles):
			qs = [q0] + [None] * L
			rs = [rhs] + [None] * L
			ss = [self.solid] + [None] * L
			for level in range(L):
				dxl = f32(float(p.dx) * (1 << level))
				qs[level] = self.smooth(qs[level], rs[level], ss[level], dxl, omega, 2)
				rs[level + 1], ss[level + 1] = self.restrict(qs[level], rs[level], ss[level], dxl, sizes[level])
				qs[level + 1] = np.zeros(sizes[level], f32)
			dxc = f32(float(p.dx) * (1 << L))
			qs[L] = self.smooth(qs[L], rs[L], ss[L], dxc, omega, 20)
			for level in range(L - 1, -1, -1):
				dxl = f32(float(p.dx) * (1 << level))
				n = qs[level].shape
				qs[level] = (qs[level] + self.prolong(qs[level + 1], n, ss[level])).astype(f32)
				qs[level] = self.smooth(qs[level], rs[level], ss[level], dxl, omega, 2)
			q0 = qs[0]
		q0[self.solid] = 0.0
		self.pressure = q0

	def pressure_solve(self, rhs, iterations=None, omega=None):
		if self.p.multigrid and iterations is None and omega is None and self.multigrid_levels():
			return self.pressure_solve_multigrid(rhs)
		p = self.p
		iterations = p.iterations if iterations is None else iterations
		omega = p.omega if omega is None else omega
		q = self.pressure
		nx, ny, nz = p.n
		ii, jj, kk = np.meshgrid(np.arange(nx), np.arange(ny), np.arange(nz), indexing="ij")
		colour = (ii + jj + kk) & 1
		dx2 = (p.dx * p.dx).astype(f32)
		for _ in range(iterations):
			for col in (0, 1):
				tot, cnt = self.neighbour_terms(q)
				gs = np.where(cnt > 0, (tot - dx2 * rhs) / np.maximum(cnt, 1), f32(0.0)).astype(f32)
				new = ((f32(1.0) - omega) * q + omega * gs).astype(f32)
				m = (colour == col) & ~self.solid
				q = np.where(m, new, q).astype(f32)
		q[self.solid] = 0.0
		self.pressure = q

	def project(self):
		p = self.p
		q = self.pressure
		maxs = p.max_speed_cells * p.dx / p.dt
		for c in range(3):
			ia, ib, low, high = self.face_neighbour_cells(c)
			qa = q[ia[0], ia[1], ia[2]]
			qb = q[ib[0], ib[1], ib[2]]
			sa = self.solid[ia[0], ia[1], ia[2]]
			sb = self.solid[ib[0], ib[1], ib[2]]
			qa = np.where(low, f32(0.0), qa)  # outside an open face: q = 0
			qb = np.where(high, f32(0.0), qb)
			sa = np.where(low, False, sa)
			sb = np.where(high, False, sb)
			v = (self.vel[..., c] - (qb - qa) / p.dx).astype(f32)
			sva = self.solid_vel[ia[0], ia[1], ia[2], c]
			svb = self.solid_vel[ib[0], ib[1], ib[2], c]
			solid_v = np.where(sa & sb, f32(0.5) * (sva + svb), np.where(sa, sva, svb)).astype(f32)
			v = np.where(sa | sb, solid_v, v)
			for side, mask in ((0, low), (1, high)):
				kind, bv = self.boundary_velocity(c, side)
				if kind != 1:
					v = np.where(mask, bv, v)
			self.vel[..., c] = np.clip(v, -maxs, maxs).astype(f32)

	def step(self):
		self.emit()
		self.advect_scalars()
		self.advect_velocity()
		f = self.cell_forces()
		self.apply_forces(f)
		rhs = self.divergence()
		self.pressure_solve(rhs)
		self.project()
		self.time += float(self.p.dt)
		self.steps += 1

	def residual(self):
		"""max |div u - expansion| over fluid cells after projection (1/s)."""
		r = self.divergence()
		r[self.solid] = 0.0
		return float(np.abs(r).max()), float(np.abs(r).mean())


def campfire(n=(32, 48, 32), dx=0.08):
	p = Params(n, dx)
	cx = n[0] * 0.5
	cz = n[2] * 0.5
	p.emitters.append({"centre": (cx, 2.5, cz), "radius": n[0] * 0.12, "fuel": 3.0, "temperature": 900.0, "smoke": 0.2})
	return p


if __name__ == "__main__":
	import argparse
	import time

	ap = argparse.ArgumentParser()
	ap.add_argument("--steps", type=int, default=120)
	ap.add_argument("--n", default="32,48,32")
	ap.add_argument("--iterations", type=int, default=20)
	ap.add_argument("--omega", type=float, default=1.6)
	ap.add_argument("--sl", action="store_true")
	args = ap.parse_args()
	n = tuple(int(v) for v in args.n.split(","))
	p = campfire(n)
	p.iterations = args.iterations
	p.omega = f32(args.omega)
	p.maccormack = not args.sl
	st = State(p)
	t0 = time.time()
	for i in range(args.steps):
		st.step()
		if (i + 1) % 20 == 0:
			mx, mean = st.residual()
			s = st.s
			print(f"step {i + 1}: smoke {s[..., 0].sum():.2f} T max {s[..., 1].max():.0f} fuel {s[..., 2].sum():.2f} flame {s[..., 3].sum():.1f} |u| max {np.abs(st.vel).max():.2f} m/s residual max {mx:.3f} mean {mean:.4f} ({time.time() - t0:.1f} s)")
