#!/usr/bin/env python3
"""Compare FellowGasVolume3D's GPU state (benchmarks/project/bench/gas_check.gd's dump) with the NumPy reference.

  python3 gas_compare.py <dump dir>

The dump must be in high precision (RGBA32F). Prints, per field, the relative L2 difference, the largest difference
and the fields' scale, and the residual (|div u - expansion|) of both after their last projection."""

import json
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gas_reference as gr  # noqa: E402

f32 = np.float32


def load(path, shape, channels, dtype=np.float32):
	a = np.fromfile(path, dtype=dtype)
	nx, ny, nz = shape
	a = a.reshape(nz, ny, nx, channels) if channels > 1 else a.reshape(nz, ny, nx)
	return np.ascontiguousarray(np.moveaxis(a, [0, 1, 2], [2, 1, 0]))  # -> [x, y, z, ...]


def make_params(info):
	n = tuple(info["grid"])
	p = gr.Params(n, info["dx"])
	p.iterations = info["iterations"]
	p.multigrid = info.get("multigrid", False)
	p.multigrid_cycles = info.get("cycles", 2)
	p.sparse = info.get("sparse", False)
	p.sparse_margin = info.get("margin", 2)
	p.maccormack = info["maccormack"]
	case = info["case"]
	e = {"centre": (n[0] * 0.5, 2.5, n[2] * 0.5), "radius": n[0] * 0.12, "fuel": 3.0, "temperature": 900.0, "smoke": 0.2}
	if case == "closed":
		p.closed = [True] * 6
		e = dict(e, fuel=0.0, temperature=400.0, smoke=1.0)
	elif case == "wind":
		p.wind = np.array([1.0, 0.0, 0.0], f32)
	p.emitters.append(e)
	return p


def make_state(p, info):
	st = gr.State(p)
	if info["case"] == "sphere":
		# gas_check.gd's static sphere: r 0.5 m at (0, 2.4, 0) -> grid (16, 24, 16), 5 cells.
		n = p.n
		c = np.array([n[0] * 0.5, 2.4 / float(p.dx), n[2] * 0.5], f32)
		d = np.linalg.norm(st.cell_g - c, axis=-1)
		st.solid = d <= f32(0.5) / p.dx
	return st


def report(name, gpu, ref):
	d = gpu.astype(np.float64) - ref.astype(np.float64)
	scale = np.sqrt((ref.astype(np.float64) ** 2).mean()) + 1e-12
	rel = np.sqrt((d ** 2).mean()) / scale
	print(f"  {name:12s} rel L2 {rel:.2e}  max |d| {np.abs(d).max():.3e}  (rms of the reference {scale:.3e}, max {np.abs(ref).max():.3e})")
	return rel


def main():
	d = sys.argv[1]
	info = json.load(open(os.path.join(d, "info.json")))
	assert info["high_precision"], "the dump must be in high precision"
	n = tuple(info["grid"])
	nv = (n[0] + 1, n[1] + 1, n[2] + 1)
	p = make_params(info)
	st = make_state(p, info)
	for i in range(info["steps"]):
		if info["case"] == "shift" and i == 20:
			st.shift((2, 0, -1))  # gas_check.gd moves the world-space volume by (0.2, 0, -0.1) m after 20 steps
		st.step()
	s_gpu = load(os.path.join(d, "scalars.bin"), n, 4)
	v_gpu = load(os.path.join(d, "velocity.bin"), nv, 4)[..., :3]
	q_gpu = load(os.path.join(d, "pressure.bin"), n, 1)
	r_gpu = load(os.path.join(d, "residual.bin"), n, 1)
	solid_gpu = load(os.path.join(d, "solid.bin"), n, 1, np.uint8)
	print(f"  solid cells  GPU {int((solid_gpu != 0).sum())} reference {int(st.solid.sum())}, differing {int(((solid_gpu != 0) != st.solid).sum())}")
	solver = f"multigrid x{p.multigrid_cycles}" if p.multigrid else f"SOR {info['iterations']}"
	sparse = f", sparse (margin {p.sparse_margin})" if p.sparse else ""
	print(f"{d}: {info['case']}, {n}, {info['steps']} steps, MacCormack {info['maccormack']}, {solver}{sparse}")
	worst = 0.0
	for c, name in enumerate(["smoke", "temperature", "fuel", "burn rate"]):
		worst = max(worst, report(name, s_gpu[..., c], st.s[..., c]))
	for c, name in enumerate(["velocity x", "velocity y", "velocity z"]):
		# The component's own faces (the texels past its last face repeat it).
		sl = [slice(0, n[0]), slice(0, n[1]), slice(0, n[2])]
		sl[c] = slice(0, n[c] + 1)
		worst = max(worst, report(name, v_gpu[tuple(sl) + (c,)], st.vel[tuple(sl) + (c,)]))
	report("pressure", q_gpu, st.pressure)
	rmax, rmean = st.residual()
	print(f"  residual     GPU max {np.abs(r_gpu).max():.4f} mean {np.abs(r_gpu).mean():.5f}   reference max {rmax:.4f} mean {rmean:.5f}  (1/s)")
	print(f"  totals       smoke GPU {s_gpu[..., 0].sum():.4f} ref {st.s[..., 0].sum():.4f}; fuel GPU {s_gpu[..., 2].sum():.4f} ref {st.s[..., 2].sum():.4f}")
	print(f"WORST_REL {worst:.3e}")


if __name__ == "__main__":
	main()
