#!/usr/bin/env python3
"""Temporal stability / ghosting analysis of frame sequences against an accumulated ground truth.

  python benchmarks/tools/temporal.py --test <dir> [--test2 <dir>] --ref <dir> --seq <name> --out <dir>
      [--labels stock,fellow] [--stem pbr_1920x1080]

Frame files: <stem>_seq-<name>_fNNN.png (mode=temporal) and <stem>_ref-seq-<name>_fNNN.png (mode=reference,
accumulated by accumulate.py). Only frames present in both are used.

Per frame t (display-referred luma, 0..1):
  err_all      mean |T_t - R_t|
  err_motion   mean |T_t - R_t| over pixels whose reference changed recently
               (max_k=1..8 |R_t - R_{t-k}| > 0.04): trails, ghosting, disocclusion, history lag
  err_static   mean |T_t - R_t| elsewhere: blur / resolve error without motion
  flicker      mean |(T_t - T_{t-1}) - (R_t - R_{t-1})|: temporal instability not present in ground truth
  psnr / ssim  vs reference
Outputs metrics JSON, a per-frame plot and an animated WebP (test | ref | 4x error) per sequence.
"""

import argparse
import glob
import json
import os
import re
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(__file__))
import imageutil as iu  # noqa: E402


def frames(d: str, pattern: str) -> dict:
    out = {}
    rx = re.compile(pattern)
    for f in glob.glob(os.path.join(d, "*.png")):
        m = rx.search(os.path.basename(f))
        if m:
            out[int(m.group(1))] = f
    return out


def analyse(test: dict, ref: dict, idx: list) -> dict:
    T = {i: iu.luma(iu.load_rgb(test[i])) for i in idx}
    R = {i: iu.luma(iu.load_rgb(ref[i])) for i in idx}
    per = []
    for n, i in enumerate(idx):
        e = np.abs(T[i] - R[i])
        prev = [R[idx[n - k]] for k in range(1, 9) if n - k >= 0]
        if prev:
            chg = np.max(np.stack([np.abs(R[i] - p) for p in prev]), axis=0) > 0.04
        else:
            chg = np.zeros_like(e, dtype=bool)
        row = {"frame": i, "err_all": float(e.mean()),
               "err_motion": float(e[chg].mean()) if chg.any() else 0.0, "motion_fraction": float(chg.mean()),
               "err_static": float(e[~chg].mean()) if (~chg).any() else 0.0,
               "psnr": iu.psnr(T[i], R[i])}
        if n > 0:
            row["flicker"] = float(np.abs((T[i] - T[idx[n - 1]]) - (R[i] - R[idx[n - 1]])).mean())
        per.append(row)
    agg = {}
    for k in ["err_all", "err_motion", "err_static", "flicker", "psnr"]:
        vals = [r[k] for r in per if k in r and np.isfinite(r[k])]
        agg[k] = float(np.mean(vals)) if vals else None
    # SSIM on a subset (every 4th frame) - it is slow at 1080p.
    ss = [iu.ssim(iu.load_rgb(test[i]), iu.load_rgb(ref[i])) for i in idx[::4]]
    agg["ssim_every4"] = float(np.mean(ss)) if ss else None
    return {"aggregate": agg, "per_frame": per}


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--test", required=True)
    ap.add_argument("--test2", default=None)
    ap.add_argument("--labels", default="stock,fellow")
    ap.add_argument("--ref", required=True)
    ap.add_argument("--seq", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--anim", action="store_true", help="write animated WebP comparison")
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    labels = a.labels.split(",")
    ref = frames(a.ref, r"_ref-seq-%s_f(\d+)\.png$" % re.escape(a.seq))
    tests = [frames(a.test, r"_seq-%s_f(\d+)\.png$" % re.escape(a.seq))]
    if a.test2:
        tests.append(frames(a.test2, r"_seq-%s_f(\d+)\.png$" % re.escape(a.seq)))
    idx = sorted(set(ref).intersection(*[set(t) for t in tests]))
    if not idx:
        print("no common frames")
        return 1
    res = {"sequence": a.seq, "frames": idx, "reference": a.ref, "results": {}}
    for lab, t in zip(labels, tests):
        res["results"][lab] = analyse(t, ref, idx)
        print(lab, json.dumps(res["results"][lab]["aggregate"]))
    with open(os.path.join(a.out, f"temporal_{a.seq}.json"), "w", encoding="utf-8") as f:
        json.dump(res, f, indent=1)
    try:
        import matplotlib
        matplotlib.use("Agg")
        import matplotlib.pyplot as plt
        fig, axs = plt.subplots(1, 3, figsize=(15, 3.6))
        for lab in res["results"]:
            pf = res["results"][lab]["per_frame"]
            axs[0].plot([r["frame"] for r in pf], [r["err_motion"] for r in pf], label=lab)
            axs[1].plot([r["frame"] for r in pf], [r["err_static"] for r in pf], label=lab)
            axs[2].plot([r["frame"] for r in pf[1:]], [r["flicker"] for r in pf[1:]], label=lab)
        for ax, t in zip(axs, ["error in motion regions (ghosting/trails)", "error in static regions (blur)", "flicker vs reference"]):
            ax.set_title(t, fontsize=10)
            ax.set_xlabel("frame")
            ax.legend()
            ax.grid(alpha=0.3)
        fig.tight_layout()
        fig.savefig(os.path.join(a.out, f"temporal_{a.seq}.png"), dpi=110)
        plt.close(fig)
    except Exception as e:  # noqa: BLE001
        print("plot failed:", e)
    if a.anim:
        from PIL import Image
        seq_imgs = []
        for i in idx:
            row = []
            for t in tests:
                row.append(iu.load_rgb(t[i]))
            r = iu.load_rgb(ref[i])
            row.append(r)
            row.append(iu.diff_heatmap(row[0], r, 4.0))
            if len(tests) > 1:
                row.append(iu.diff_heatmap(row[1], r, 4.0))
            h, w = r.shape[:2]
            small = [np.asarray(Image.fromarray((np.clip(x, 0, 1) * 255).astype(np.uint8)).resize((w // 3, h // 3), Image.BILINEAR)) for x in row]
            seq_imgs.append(Image.fromarray(np.concatenate(small, axis=1)))
        seq_imgs[0].save(os.path.join(a.out, f"temporal_{a.seq}.webp"), save_all=True, append_images=seq_imgs[1:], duration=50, loop=0, quality=85)
    return 0


if __name__ == "__main__":
    sys.exit(main())
