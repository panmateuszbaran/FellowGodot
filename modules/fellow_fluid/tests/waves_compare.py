#!/usr/bin/env python3
"""FellowEngine wind waves (decision 0123): the GPU's displacement against NumPy.

The initial spectrum h0 (what the CPU uploaded) is evolved to the same time with the same quantized dispersion
relation, and transformed with numpy.fft (the GPU: Stockham radix-2 in shared memory, two fields per complex
transform). Run benchmarks/project/bench/water_check.gd first:

    godot --path benchmarks/project -s res://bench/water_check.gd -- --out=/tmp/wc
    python3 modules/fellow_fluid/tests/waves_compare.py /tmp/wc
"""

import json
import sys

import numpy as np


def main(d):
    info = json.load(open(f"{d}/info.json"))
    n = info["resolution"]
    t = info["time"]
    period = info["repeat_period"]
    g = info["gravity"]
    lam = info["choppiness"]
    worst = 0.0
    for c, length in enumerate(info["lengths"]):
        h0 = np.fromfile(f"{d}/h0_{c}.bin", dtype=np.float32).reshape(n, n, 4).astype(np.float64)
        hk = h0[..., 0] + 1j * h0[..., 1]
        hm = h0[..., 2] + 1j * h0[..., 3]
        dk = 2 * np.pi / length
        idx = np.arange(n) - n // 2
        kx = idx[None, :] * dk
        kz = idx[:, None] * dk
        k = np.sqrt(kx**2 + kz**2)
        w0 = 2 * np.pi / period
        w = np.floor(np.sqrt(g * k) / w0)
        e = np.exp(1j * 2 * np.pi * np.mod(w * ((t % period) / period), 1.0))
        h = hk * e + hm * np.conj(e)
        h[k < 1e-6] = 0
        ks = np.where(k > 1e-6, k, 1)

        def field(spec):
            return (np.fft.ifft2(np.fft.ifftshift(spec)) * n * n).real

        ref = [field(1j * kx / ks * h) * lam, field(h), field(1j * kz / ks * h) * lam]
        gpu = np.fromfile(f"{d}/disp_{c}.bin", dtype=np.float16)[: n * n * 4].astype(np.float64).reshape(n, n, 4)
        for ch, name in enumerate("xyz"):
            err = np.abs(gpu[..., ch] - ref[ch]).max()
            scale = np.abs(ref[ch]).max()
            worst = max(worst, err / max(scale, 1e-9))
            print(f"cascade {c} ({length:g} m) {name}: max |GPU - NumPy| {err:.2e} m of {scale:.3f} m")
    print(f"significant wave height {info['significant_wave_height']:.3f} m; worst relative error {worst:.2e}")
    return 0 if worst < 5e-3 else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1] if len(sys.argv) > 1 else "/tmp/wc"))
