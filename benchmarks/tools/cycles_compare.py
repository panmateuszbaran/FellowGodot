#!/usr/bin/env python3
"""PBR lab captures (lighting=ref, linear EXR) of several variants against the Cycles path-traced reference.

  python benchmarks/tools/cycles_compare.py <capture_session> <variant,variant,...> [--baseline godot-4.7.2] [--json out.json]

Capture with: run_benchmarks.py --scene pbr --mode capture --res 1920x1080 --extra="--hdr=1 --lighting=ref"
Both sides go through the same ACES-fit display transform (imageutil.tonemap_display). Reports per camera PSNR / SSIM /
LPIPS of each variant vs Cycles, plus the same metrics restricted to "shadow-edge" pixels: pixels where the Cycles
luminance gradient is strong and the pixel is darker than its 15x15 neighbourhood maximum by more than 30 % (the
penumbra regions, where directional shadow filtering differs). Global differences are dominated by what Godot's preset
does not simulate (multi-bounce GI), so compare variants with each other, not with 0.
"""

import argparse
import glob
import json
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(__file__))
import imageutil as iu  # noqa: E402

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
RAW = os.path.join(ROOT, "benchmarks", "results", "raw")


def max_filter(img, r):
    out = img.copy()
    for dy in range(-r, r + 1):
        out = np.maximum(out, np.roll(img, dy, axis=0))
    tmp = out.copy()
    for dx in range(-r, r + 1):
        out = np.maximum(out, np.roll(tmp, dx, axis=1))
    return out


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("session")
    ap.add_argument("variants")
    ap.add_argument("--baseline", default="godot-4.7.2")
    ap.add_argument("--res", default="1920x1080")
    ap.add_argument("--json", default=None)
    a = ap.parse_args()
    exp_dir = os.path.join(RAW, f"{a.baseline}_pbrref_export")
    variants = a.variants.split(",")
    report = {}
    for c in sorted(glob.glob(os.path.join(exp_dir, "cycles_*.exr"))):
        cam = os.path.basename(c)[len("cycles_"):-4]
        hc = iu.load_exr(c)
        dc = iu.tonemap_display(hc)
        lc = iu.luma(dc)
        gy, gx = np.gradient(lc)
        grad = np.hypot(gx, gy)
        mx = max_filter(lc, 7)
        edge = (grad > 0.02) & (lc < 0.7 * mx)
        row = {"edge_fraction": float(edge.mean())}
        for v in variants:
            g = os.path.join(RAW, a.session, v, "pbr", f"pbr_{a.res}_{cam}.exr")
            if not os.path.exists(g):
                continue
            dg = iu.tonemap_display(iu.load_rgb(g))
            if dg.shape != dc.shape:
                continue
            e = np.abs(iu.luma(dg) - lc)
            lp = iu.lpips(dg, dc)
            row[v] = {"psnr": iu.psnr(dg, dc), "ssim": iu.ssim(dg, dc), "lpips": lp,
                      "edge_mae": float(e[edge].mean()) if edge.any() else None, "mae": float(e.mean())}
        report[cam] = row
        cells = []
        for v in variants:
            r = row.get(v)
            if r:
                cells.append(f"{v}: {r['psnr']:.2f} dB / {r['ssim']:.4f} / LPIPS {r['lpips'] if r['lpips'] is None else round(r['lpips'], 4)} / edge MAE {r['edge_mae']:.4f}")
        print(f"{cam:14s} edges {row['edge_fraction']*100:5.2f}% | " + " | ".join(cells))
    if a.json:
        with open(a.json, "w", encoding="utf-8") as f:
            json.dump(report, f, indent=1)
    return 0


if __name__ == "__main__":
    sys.exit(main())
