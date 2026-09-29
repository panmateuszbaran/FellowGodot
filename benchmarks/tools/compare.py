#!/usr/bin/env python3
"""Pairwise image comparison: STOCK vs FELLOW (and each vs REFERENCE when available).

  python benchmarks/tools/compare.py --a <stock_dir> --b <fellow_dir> [--ref <ref_dir>] --out <dir> [--pattern "*.png"]

Images are matched by file name. Reference images are matched by camera suffix (see --ref-map).
Writes <out>/metrics.json and <out>/diff/<name>_diff.png (|stock - fellow| luma x gain, inferno colormap),
plus <out>/diff/<name>_vsref_{a,b}.png when a reference is given.
"""

import argparse
import fnmatch
import json
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(__file__))
import imageutil as iu  # noqa: E402


def load_display(path: str, exposure: float) -> np.ndarray:
    img = iu.load_rgb(path)
    if path.lower().endswith(".exr"):
        return iu.tonemap_display(img, exposure)
    return img


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--a", required=True, help="directory A (stock)")
    ap.add_argument("--b", default=None, help="directory B (fellow)")
    ap.add_argument("--ref", default=None, help="reference directory")
    ap.add_argument("--ref-prefix", default="", help="reference file = <ref-prefix><camera><ref-suffix>")
    ap.add_argument("--ref-suffix", default=".png")
    ap.add_argument("--cam-split", default="_", help="camera name = file stem after the first '<scene>_<WxH>_' prefix")
    ap.add_argument("--pattern", default="*.png")
    ap.add_argument("--out", required=True)
    ap.add_argument("--gain", type=float, default=4.0)
    ap.add_argument("--exposure", type=float, default=1.0, help="exposure for HDR (EXR) display transform")
    ap.add_argument("--lpips", action="store_true")
    a = ap.parse_args()

    files = sorted(f for f in os.listdir(a.a) if fnmatch.fnmatch(f, a.pattern))
    os.makedirs(os.path.join(a.out, "diff"), exist_ok=True)
    res = {"a": a.a, "b": a.b, "ref": a.ref, "metric_notes": "PSNR on display sRGB [0,1]; SSIM on luma (gaussian sigma 1.5). HDR inputs pass through the same ACES-fit display transform.", "images": {}}
    for f in files:
        stem = os.path.splitext(f)[0]
        ia = load_display(os.path.join(a.a, f), a.exposure)
        entry = {}
        if a.b and os.path.exists(os.path.join(a.b, f)):
            ib = load_display(os.path.join(a.b, f), a.exposure)
            entry["a_vs_b"] = {"psnr": iu.psnr(ia, ib), "ssim": iu.ssim(ia, ib), "max_abs_diff": float(np.abs(ia - ib).max()), "mean_abs_diff": float(np.abs(ia - ib).mean())}
            if a.lpips:
                entry["a_vs_b"]["lpips"] = iu.lpips(ia, ib)
            iu.save_rgb(os.path.join(a.out, "diff", stem + "_diff.png"), iu.diff_heatmap(ia, ib, a.gain))
        if a.ref:
            parts = stem.split("_")
            cam = "_".join(parts[2:]) if len(parts) > 2 else stem
            rp = os.path.join(a.ref, a.ref_prefix + cam + a.ref_suffix)
            if os.path.exists(rp):
                ir = load_display(rp, a.exposure)
                if ir.shape == ia.shape:
                    entry["reference"] = os.path.relpath(rp, a.out)
                    entry["a_vs_ref"] = {"psnr": iu.psnr(ia, ir), "ssim": iu.ssim(ia, ir)}
                    iu.save_rgb(os.path.join(a.out, "diff", stem + "_vsref_a.png"), iu.diff_heatmap(ia, ir, a.gain))
                    if a.b and os.path.exists(os.path.join(a.b, f)):
                        entry["b_vs_ref"] = {"psnr": iu.psnr(ib, ir), "ssim": iu.ssim(ib, ir)}
                        iu.save_rgb(os.path.join(a.out, "diff", stem + "_vsref_b.png"), iu.diff_heatmap(ib, ir, a.gain))
                    if a.lpips:
                        entry["a_vs_ref"]["lpips"] = iu.lpips(ia, ir)
                        if "b_vs_ref" in entry:
                            entry["b_vs_ref"]["lpips"] = iu.lpips(ib, ir)
        res["images"][f] = entry
        print(f, json.dumps({k: v for k, v in entry.items() if k != "reference"}))
    with open(os.path.join(a.out, "metrics.json"), "w", encoding="utf-8") as fh:
        json.dump(res, fh, indent=1)
    return 0


if __name__ == "__main__":
    sys.exit(main())
