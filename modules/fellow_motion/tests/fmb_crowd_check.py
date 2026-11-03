#!/usr/bin/env python3
# FellowEngine — the crowd run of fmb_crowd_run.gd: does anyone sink (hips below 0.75 m, head below 1.2 m)?
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
    a = np.fromfile(os.path.join(d, "crowd.bin"), "<f4").reshape(-1, n, 2)
    hips, head = a[..., 0], a[..., 1]
    low = (hips < 0.75) | (head < 1.2)
    print(f"steps {a.shape[0]}, characters {n}, service {info['service']}")
    for c in range(n):
        print(f"  character {c}: hips min {hips[:, c].min():.2f} mean {hips[:, c].mean():.2f}, head min {head[:, c].min():.2f},"
              f" low {low[:, c].mean() * 100:.1f} % of the steps")
    bad = low.mean()
    print("CROWD", "OK" if bad < 0.01 else f"SINKS ({bad * 100:.1f} % of character-steps low)")


if __name__ == "__main__":
    main()
