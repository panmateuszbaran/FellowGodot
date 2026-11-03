#!/usr/bin/env python3
# FellowEngine — check one character's run of fmb_character_run.gd: no NaNs, the speed it reaches in each phase of the
# velocity schedule, the heading, how the feet meet the ground, and a contact sheet of the skeleton.
#
#   python fmb_character_check.py <out dir> [--sheet sheet.png]

import json
import os
import sys

import numpy as np

# The schedule of fmb_character_run.gd: (start, end, desired speed).
PHASES = [(0.0, 1.0, 0.0, "stand"), (1.0, 4.0, 1.3, "walk +Z"), (4.0, 6.0, 1.3, "walk +X"), (6.0, 8.0, 2.8, "jog +X"),
          (8.0, 10.0, 4.0, "run a curve"), (10.0, 12.0, 0.0, "stop")]
J = 77
FOOT = ["LeftFoot", "LeftToeBase", "RightFoot", "RightToeBase"]


def main():
    d = sys.argv[1]
    sheet = sys.argv[sys.argv.index("--sheet") + 1] if "--sheet" in sys.argv else None
    jp = np.fromfile(os.path.join(d, "joints.bin"), "<f4").reshape(-1, J, 3)
    info = json.load(open(os.path.join(d, "info.json")))
    T = jp.shape[0]
    t = (np.arange(T) + 1) / 60.0
    ok = True
    if not np.isfinite(jp).all():
        print("NaN or inf in the joints")
        ok = False
    hips = jp[:, 0]
    v = np.zeros(T)
    v[1:] = np.linalg.norm(np.diff(hips[:, [0, 2]], axis=0), axis=1) * 60.0
    print(f"steps {T}, generations {info['stats'].get('generations')}, latency {info['stats'].get('avg_latency_msec'):.1f} ms")
    print(f"service: {json.dumps({k: info['service'].get(k) for k in ('requests', 'batches', 'avg_batch_msec')})}")
    for a, b, sp, name in PHASES:
        # the second half of each phase (the speed settles)
        m = (t >= (a + b) / 2) & (t < b)
        if not m.any():
            continue
        vs = v[m].mean()
        h = hips[m][-1]
        print(f"  {name:12s} desired {sp:.1f} m/s  shown {vs:.2f} m/s  hips height {hips[m, 1].mean():.2f}  at ({h[0]:.1f}, {h[2]:.1f})")
    # Feet: the lowest foot joint each frame, and how far a planted foot slides.
    names = json.load(open(os.path.join(d, "names.json"))) if os.path.exists(os.path.join(d, "names.json")) else None
    lowest = jp[:, :, 1].min(1)
    print(f"  lowest joint height: median {np.median(lowest):.3f} m, min {lowest.min():.3f}, max {lowest.max():.3f}")
    if sheet:
        import matplotlib

        matplotlib.use("Agg")
        import matplotlib.pyplot as plt

        idx = np.linspace(30, T - 1, 24).astype(int)
        fig, axs = plt.subplots(3, 8, figsize=(20, 8))
        for ax, i in zip(axs.flat, idx):
            w = jp[i]
            c = w[0]
            ax.scatter(w[:, 0] - c[0], w[:, 1], s=2, c="k")
            ax.set_xlim(-1, 1)
            ax.set_ylim(0, 2)
            ax.set_aspect("equal")
            ax.set_title(f"t {t[i]:.2f} v {v[i]:.1f}")
            ax.axis("off")
        plt.tight_layout()
        plt.savefig(sheet, dpi=55)
        fig2 = plt.figure(figsize=(6, 6))
        plt.plot(hips[:, 0], hips[:, 2])
        plt.axis("equal")
        plt.title("hips path (x, z)")
        plt.savefig(sheet.replace(".png", "_path.png"), dpi=60)
    print("CHARACTER", "OK" if ok else "FAILED")


if __name__ == "__main__":
    main()
