#!/usr/bin/env python3
# FellowEngine — the crowd run of fmb_crowd_run.gd: does anyone sink (hips below 0.75 m, head below 1.2 m), and what
# was the character doing in the second before it first did?
#
#   python fmb_crowd_check.py <out dir>

import json
import os
import sys

import numpy as np


def main():
    d = sys.argv[1]
    info = json.load(open(os.path.join(d, "info.json")))
    n = info["crowd"]
    a = np.fromfile(os.path.join(d, "crowd.bin"), "<f4").reshape(-1, n, len(info.get("fields", [0, 1])))
    hips, head = a[..., 0], a[..., 1]
    low = (hips < 0.75) | (head < 1.2)
    print(f"steps {a.shape[0]}, characters {n}, service {info['service']}")
    for c in range(n):
        print(f"  character {c}: hips min {hips[:, c].min():.2f} mean {hips[:, c].mean():.2f}, head min {head[:, c].min():.2f},"
              f" low {low[:, c].mean() * 100:.1f} % of the steps")
    falls = 0
    if a.shape[2] >= 6:
        for c in range(n):
            idx = np.flatnonzero(low[:, c])
            if idx.size == 0:
                continue
            s0 = idx[0]
            falls += 1
            w = a[max(0, s0 - 60):s0 + 1, c]
            print(f"  character {c} first low at step {s0} ({s0 / 60:.2f} s): the second before: state {w[:, 2].min():.0f}-{w[:, 2].max():.0f},"
                  f" target speed {w[:, 3].min():.2f}-{w[:, 3].max():.2f}, desired {w[:, 4].min():.2f}-{w[:, 4].max():.2f},"
                  f" generations {w[0, 5]:.0f} -> {w[-1, 5]:.0f}; hips {w[0, 0]:.2f} -> {w[-1, 0]:.2f}")
    if a.shape[2] >= 7:
        print("get-ups:", int(a[-1, :, 6].sum()), "by character", a[-1, :, 6].astype(int).tolist())
    # Staying down: low for more than 3 s in a row.
    stuck = 0
    for c in range(n):
        run = best = 0
        for v in low[:, c]:
            run = run + 1 if v else 0
            best = max(best, run)
        if best > 180:
            stuck += 1
            print(f"  character {c} down for {best / 60:.1f} s in a row")
    # Falls: entries into the low state after at least 1 s up.
    entries = 0
    for c in range(n):
        up = 0
        for v in low[:, c]:
            if v and up >= 60:
                entries += 1
            up = 0 if v else up + 1
    print("FALLS", entries, "characters", falls)
    print("STUCK", stuck)
    bad = low.mean()
    print("CROWD", "OK" if bad < 0.01 else f"SINKS ({bad * 100:.1f} % of character-steps low)")


if __name__ == "__main__":
    main()
