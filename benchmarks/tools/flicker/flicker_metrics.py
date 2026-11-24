#!/usr/bin/env python3
"""Frame-to-frame flicker of rendered sequences (milestone 188): showcase/tools/flicker_look.gd's frames, one directory
per configuration (<root>/<config>/f%04d.png), the first --still frames from a camera held still, the rest from a camera
moving steadily.

The measure is the second temporal difference of each pixel's luminance, |I[t+1] - 2 I[t] + I[t-1]| (8-bit levels): a
still image or one moving steadily scores about 0, a pixel that changes back and forth scores high. Averaged over the
buildings (the upper part of the frame without the bright sky: --rows, --sky), and the share of those pixels above 3
levels. Writes <out>/flicker.txt, <out>/flicker.json and a map per configuration (<out>/map_<config>.png: the still
frames' flicker in red over the dimmed picture, half size).

    python flicker_metrics.py <root> --still 24 --out <dir> [--configs base,no_soft,...] [--rows 0.61] [--sky 225]
"""
import argparse
import json
import os

import numpy as np
from PIL import Image


def load(d, a, b):
    return np.stack([np.asarray(Image.open(os.path.join(d, "f%04d.png" % i)).convert("L"), dtype=np.float32) for i in range(a, b)])


def second_difference(frames):
    return np.abs(frames[2:] - 2.0 * frames[1:-1] + frames[:-2]).mean(axis=0)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("root")
    ap.add_argument("--still", type=int, required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--configs", default="")
    ap.add_argument("--rows", type=float, default=0.61, help="the share of the frame's rows from the top measured")
    ap.add_argument("--sky", type=float, default=225.0, help="pixels this bright in the first frame are left out")
    a = ap.parse_args()
    configs = [c for c in a.configs.split(",") if c] or sorted(d for d in os.listdir(a.root) if os.path.isdir(os.path.join(a.root, d)))
    os.makedirs(a.out, exist_ok=True)
    mask = None
    results = {}
    lines = []
    for c in configs:
        d = os.path.join(a.root, c)
        if not os.path.isdir(d):
            continue
        n = len([f for f in os.listdir(d) if f.startswith("f") and f.endswith(".png")])
        if n < a.still + 3:
            lines.append("%-16s %d frames: too few" % (c, n))
            continue
        frames = load(d, 0, n)
        if mask is None:
            mask = np.zeros(frames.shape[1:], bool)
            mask[: int(frames.shape[1] * a.rows), :] = True
            mask &= frames[0] < a.sky
        still = second_difference(frames[: a.still])
        moving = second_difference(frames[a.still :])
        r = {
            "still_mean": float(still[mask].mean()),
            "still_above3": float((still[mask] > 3.0).mean() * 100.0),
            "moving_mean": float(moving[mask].mean()),
            "moving_above3": float((moving[mask] > 3.0).mean() * 100.0),
            "frames": n,
        }
        results[c] = r
        lines.append("%-16s still: d2 %.3f, >3 %5.2f%% | moving: d2 %.3f, >3 %5.2f%%" % (c, r["still_mean"], r["still_above3"], r["moving_mean"], r["moving_above3"]))
        pic = np.asarray(Image.open(os.path.join(d, "f%04d.png" % (a.still // 2))).convert("RGB")).astype(np.float32) * 0.35
        pic[..., 0] = np.maximum(pic[..., 0], np.clip(still * 20.0, 0, 255))
        pic[..., 1] = np.maximum(pic[..., 1], np.clip(still * 6.0, 0, 255))
        im = Image.fromarray(pic.astype(np.uint8))
        im.resize((im.width // 2, im.height // 2), Image.BILINEAR).save(os.path.join(a.out, "map_%s.png" % c))
    with open(os.path.join(a.out, "flicker.txt"), "w") as f:
        f.write("\n".join(lines) + "\n")
    with open(os.path.join(a.out, "flicker.json"), "w") as f:
        json.dump(results, f, indent=1)
    print("\n".join(lines))


if __name__ == "__main__":
    main()
