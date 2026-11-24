#!/usr/bin/env python3
"""Flicker under motion measured along the scene's own motion vectors (milestone 191): showcase/tools/play_look.gd
--velocity=1's frames (f%04d.png) and motion vectors (v%04d.bin: width, height as int32, then RG16F per pixel — the
previous frame's UV minus this frame's).

Each pixel of frame t is followed to frame t-1 by its motion vector and to frame t+1 by the same vector reversed (a
steady motion over three frames), and the second difference along that path, |I[t+1](p - v) - 2 I[t](p) + I[t-1](p + v)|
(8-bit luma levels), is what does not follow the motion: zero for a surface whose shading only moves, high for one
that changes back and forth (flicker). Left out: pixels whose path leaves the frame, the sky (as bright as --sky), and
disocclusions (the motion vector at the previous place differing from the pixel's by more than --disocclusion pixels).
Also the first difference |I[t](p) - I[t-1](p + v)| (it also counts slow changes and the resampling's blur).

Writes <out>/reproj.json, <out>/reproj.txt and <out>/map_reproj.png (the mean second difference in red over the dimmed
middle frame, half size).

    python reproj_flicker.py <dir> --out <dir> [--stride 2] [--sky 225] [--disocclusion 1.0] [--label base]
"""
import argparse
import json
import os

import numpy as np
from PIL import Image
from scipy.ndimage import map_coordinates


def luma(path):
    a = np.asarray(Image.open(path).convert("RGB"), dtype=np.float32)
    return 0.2126 * a[..., 0] + 0.7152 * a[..., 1] + 0.0722 * a[..., 2]


def velocity(path):
    with open(path, "rb") as f:
        w, h = np.frombuffer(f.read(8), dtype="<i4")
        v = np.frombuffer(f.read(), dtype="<f2").astype(np.float32)
    return v.reshape(h, w, 2)


def sample(img, x, y):
    return map_coordinates(img, [y, x], order=1, mode="nearest")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("dir")
    ap.add_argument("--out", required=True)
    ap.add_argument("--stride", type=int, default=2)
    ap.add_argument("--sky", type=float, default=225.0)
    ap.add_argument("--disocclusion", type=float, default=1.0, help="pixels")
    ap.add_argument("--label", default="")
    a = ap.parse_args()
    n = len([f for f in os.listdir(a.dir) if f.startswith("f") and f.endswith(".png")])
    first = luma(os.path.join(a.dir, "f0000.png"))
    h, w = first.shape
    yy, xx = np.mgrid[0:h, 0:w].astype(np.float32)
    # The vertical sign of the motion vectors (UV's y up or down): the one that predicts the second frame better.
    v1 = velocity(os.path.join(a.dir, "v0001.bin"))
    i1 = luma(os.path.join(a.dir, "f0001.png"))
    best = None
    for sy in (1.0, -1.0):
        e = np.median(np.abs(i1 - sample(first, xx + v1[..., 0] * w, yy + sy * v1[..., 1] * h)))
        if best is None or e < best[0]:
            best = (e, sy)
    sy = best[1]
    acc = np.zeros((h, w), np.float32)
    cnt = np.zeros((h, w), np.float32)
    d2_all, d1_all = [], []
    frames = {}

    def get(i):
        if i not in frames:
            frames[i] = (luma(os.path.join(a.dir, "f%04d.png" % i)), velocity(os.path.join(a.dir, "v%04d.bin" % i)))
            for k in list(frames):
                if k < i - 2:
                    del frames[k]
        return frames[i]

    for t in range(1, n - 1, a.stride):
        ip, vp = get(t - 1)
        it, vt = get(t)
        inx, _ = get(t + 1)
        dx = vt[..., 0] * w
        dy = sy * vt[..., 1] * h
        px, py = xx + dx, yy + dy
        nx, ny = xx - dx, yy - dy
        ok = (px >= 0) & (px <= w - 1) & (py >= 0) & (py <= h - 1) & (nx >= 0) & (nx <= w - 1) & (ny >= 0) & (ny <= h - 1)
        ok &= it < a.sky
        # Disocclusion: the previous frame's motion vector where the pixel came from differs from its own.
        pvx = sample(vp[..., 0], px, py) * w
        pvy = sy * sample(vp[..., 1], px, py) * h
        ok &= np.hypot(pvx - dx, pvy - dy) < a.disocclusion
        prev = sample(ip, px, py)
        nxt = sample(inx, nx, ny)
        d2 = np.abs(nxt - 2.0 * it + prev)
        d1 = np.abs(it - prev)
        d2_all.append(d2[ok])
        d1_all.append(d1[ok])
        acc[ok] += d2[ok]
        cnt[ok] += 1.0
    d2 = np.concatenate(d2_all)
    d1 = np.concatenate(d1_all)
    r = {
        "label": a.label,
        "frames": n,
        "y_sign": sy,
        "pixels": int(d2.size),
        "d2_mean": float(d2.mean()),
        "d2_above3": float((d2 > 3.0).mean() * 100.0),
        "d2_above8": float((d2 > 8.0).mean() * 100.0),
        "d1_mean": float(d1.mean()),
        "d1_above3": float((d1 > 3.0).mean() * 100.0),
    }
    os.makedirs(a.out, exist_ok=True)
    line = "%-16s along the motion: d2 %.3f, >3 %5.2f%%, >8 %5.2f%% | d1 %.3f, >3 %5.2f%% (%d pixels)" % (a.label, r["d2_mean"], r["d2_above3"], r["d2_above8"], r["d1_mean"], r["d1_above3"], r["pixels"])
    with open(os.path.join(a.out, "reproj.txt"), "w") as f:
        f.write(line + "\n")
    with open(os.path.join(a.out, "reproj.json"), "w") as f:
        json.dump(r, f, indent=1)
    m = acc / np.maximum(cnt, 1.0)
    pic = np.asarray(Image.open(os.path.join(a.dir, "f%04d.png" % (n // 2))).convert("RGB")).astype(np.float32) * 0.35
    pic[..., 0] = np.maximum(pic[..., 0], np.clip(m * 20.0, 0, 255))
    pic[..., 1] = np.maximum(pic[..., 1], np.clip(m * 6.0, 0, 255))
    im = Image.fromarray(pic.astype(np.uint8))
    im.resize((im.width // 2, im.height // 2), Image.BILINEAR).save(os.path.join(a.out, "map_reproj.png"))
    print(line)


if __name__ == "__main__":
    main()
