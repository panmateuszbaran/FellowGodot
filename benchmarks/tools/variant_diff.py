#!/usr/bin/env python3
"""Pixel-level comparison of two build variants of one session (same frames rendered by both).

  python benchmarks/tools/variant_diff.py <session> <variant_a> <variant_b> [--scenes pbr,sponza] [--json out.json]

Pairs results/raw/<session>/<a>/<scene>/<file>.png with .../<b>/<scene>/<file>.png (same file name after the
variant prefix is stripped) and reports per sequence / camera: mean and min PSNR, max absolute difference (8-bit),
fraction of pixels differing by more than 2 and 8 levels. Used to verify that an optimisation is image-identical
(e.g. the directional shadow cache against its lattice-only reference).
"""

import argparse
import glob
import json
import os
import re
import sys

import numpy as np
from PIL import Image

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))


def load(p):
    return np.asarray(Image.open(p).convert("RGB"), dtype=np.int16)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("session")
    ap.add_argument("a")
    ap.add_argument("b")
    ap.add_argument("--scenes", default="")
    ap.add_argument("--json", default=None)
    args = ap.parse_args()
    raw = os.path.join(ROOT, "benchmarks", "results", "raw", args.session)
    scenes = args.scenes.split(",") if args.scenes else sorted(os.listdir(os.path.join(raw, args.a)))
    report = {}
    for sc in scenes:
        da = os.path.join(raw, args.a, sc)
        db = os.path.join(raw, args.b, sc)
        if not (os.path.isdir(da) and os.path.isdir(db)):
            continue
        # file names start with <scene>_<WxH>_<build>..., strip the build label for matching
        # (only that component: a short label such as "a" also occurs inside scene and sequence names)
        def key(p, label):
            name = os.path.basename(p)
            for sep in ("_", "."):
                tag = "_" + label + sep
                if tag in name:
                    return name.replace(tag, sep, 1)
            return name
        ka = {key(p, args.a): p for p in glob.glob(os.path.join(da, "*.png"))}
        kb = {key(p, args.b): p for p in glob.glob(os.path.join(db, "*.png"))}
        common = sorted(set(ka) & set(kb))
        groups = {}
        for k in common:
            m = re.search(r"(seq-[^_]+|cam-[^_.]+)", k)
            g = m.group(1) if m else "other"
            A = load(ka[k])
            B = load(kb[k])
            if A.shape != B.shape:
                continue
            d = np.abs(A - B)
            mse = float(np.mean((A - B).astype(np.float64) ** 2))
            psnr = 99.0 if mse == 0 else 10 * np.log10(255.0 ** 2 / mse)
            dm = d.max(axis=2)
            e = groups.setdefault(g, {"frames": 0, "psnr": [], "max": 0, "gt2": [], "gt8": []})
            e["frames"] += 1
            e["psnr"].append(psnr)
            e["max"] = max(e["max"], int(d.max()))
            e["gt2"].append(float(np.mean(dm > 2)))
            e["gt8"].append(float(np.mean(dm > 8)))
        out = {}
        for g, e in groups.items():
            out[g] = {
                "frames": e["frames"],
                "psnr_mean": float(np.mean(e["psnr"])),
                "psnr_min": float(np.min(e["psnr"])),
                "max_abs": e["max"],
                "frac_gt2_mean": float(np.mean(e["gt2"])),
                "frac_gt8_max": float(np.max(e["gt8"])),
            }
        report[sc] = out
        for g, o in sorted(out.items()):
            print(f"{sc:16s} {g:22s} n={o['frames']:3d} PSNR mean {o['psnr_mean']:6.2f} min {o['psnr_min']:6.2f} dB  max|d| {o['max_abs']:3d}  >2: {o['frac_gt2_mean']*100:6.3f}%  >8 (worst frame): {o['frac_gt8_max']*100:6.3f}%")
    if args.json:
        with open(args.json, "w", encoding="utf-8") as f:
            json.dump(report, f, indent=1)
    return 0


if __name__ == "__main__":
    sys.exit(main())
