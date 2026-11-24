#!/usr/bin/env python3
"""Frame-to-frame flicker of rendered sequences (milestone 188): showcase/tools/flicker_look.gd's frames, one directory
per configuration (<root>/<config>/f%04d.png), the first --still frames from a camera held still, the rest from a camera
moving steadily.

The measure is the second temporal difference of each pixel's luminance, |I[t+1] - 2 I[t] + I[t-1]| (8-bit levels): a
still image or one moving steadily scores about 0, a pixel that changes back and forth scores high. Averaged over the
buildings (the upper part of the frame without the bright sky: --rows, --sky), and the share of those pixels above 3
levels; and the sharpness of a still and a moving frame (the mean |I - I blurred by a pixel|: a softer image flickers
less). Writes <out>/flicker.txt, <out>/flicker.json and a map per configuration (<out>/map_<config>.png: the still
frames' flicker in red over the dimmed picture, half size).

Under motion the second difference also counts the image's own change (a texture moving by v pixels a frame changes by
about v^2 times its second derivative), so --motion measures the moving frames motion-compensated instead: in blocks of
64 x 64 pixels the displacement between frames t-1 and t+1 (phase correlation, to a fraction of a pixel), frame t
predicted as their mean each moved halfway, and |frame t - prediction| what does not follow the motion (flicker; the
interpolation's own error is alike in every configuration). Every --motion_stride'th frame (SciPy).

    python flicker_metrics.py <root> --still 24 --out <dir> [--configs base,no_soft,...] [--rows 0.61] [--sky 225]
        [--motion] [--motion_stride 3]
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


def phase_correlation(a, b):
    """The displacement d (rows, columns) with a ~ b moved by d; the correlation's peak."""
    f = np.fft.fft2(a) * np.conj(np.fft.fft2(b))
    f /= np.abs(f) + 1e-9
    r = np.real(np.fft.ifft2(f))
    i = np.unravel_index(np.argmax(r), r.shape)
    d = []
    for ax in range(2):
        n = r.shape[ax]
        lo = list(i)
        hi = list(i)
        lo[ax] = (i[ax] - 1) % n
        hi[ax] = (i[ax] + 1) % n
        m1, c, p1 = r[tuple(lo)], r[i], r[tuple(hi)]
        den = m1 - 2.0 * c + p1
        v = i[ax] + (0.5 * (m1 - p1) / den if abs(den) > 1e-12 else 0.0)
        d.append(v - n if v > n / 2 else v)
    return np.array(d), r[i]


def motion_residual(a, b, c, mask, block=64, pad=24, acc=None):
    """|b - (a and c each moved halfway to b)| over the blocks mostly inside the mask (see the module's description);
    acc: [sum, count] full-size arrays the residual is added into (the motion map)."""
    from scipy.ndimage import shift as nd_shift

    win = np.outer(np.hanning(block), np.hanning(block))
    h, w = a.shape
    out = []
    for y in range(0, h - block, block):
        for x in range(0, w - block, block):
            if mask[y : y + block, x : x + block].mean() < 0.7 or a[y : y + block, x : x + block].std() < 2.0:
                continue
            d, peak = phase_correlation(c[y : y + block, x : x + block] * win, a[y : y + block, x : x + block] * win)
            if peak < 0.05 or np.abs(d).max() > 20:
                continue
            y0, x0 = max(0, y - pad), max(0, x - pad)
            sa = nd_shift(a[y0 : y + block + pad, x0 : x + block + pad], d / 2.0, order=3, mode="nearest")
            sc = nd_shift(c[y0 : y + block + pad, x0 : x + block + pad], -d / 2.0, order=3, mode="nearest")
            pred = 0.5 * (sa + sc)[y - y0 : y - y0 + block, x - x0 : x - x0 + block]
            r = np.abs(b[y : y + block, x : x + block] - pred)[8:-8, 8:-8]
            out.append(r.ravel())
            if acc is not None:
                acc[0][y + 8 : y + block - 8, x + 8 : x + block - 8] += r
                acc[1][y + 8 : y + block - 8, x + 8 : x + block - 8] += 1.0
    return np.concatenate(out) if out else np.zeros(0)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("root")
    ap.add_argument("--still", type=int, required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--configs", default="")
    ap.add_argument("--rows", type=float, default=0.61, help="the share of the frame's rows from the top measured")
    ap.add_argument("--sky", type=float, default=225.0, help="pixels this bright in the first frame are left out")
    ap.add_argument("--motion", action="store_true", help="the moving frames motion-compensated (SciPy)")
    ap.add_argument("--motion_stride", type=int, default=3)
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
        still = second_difference(frames[: a.still]) if a.still >= 3 else np.zeros(frames.shape[1:], np.float32)
        moving = second_difference(frames[a.still :])
        # Sharpness: the mean |I - I blurred (sigma 1 pixel)| of a still and a moving frame (a softer image flickers less).
        from scipy.ndimage import gaussian_filter

        mid_still, mid_moving = frames[a.still // 2], frames[(a.still + n) // 2]
        r = {
            "sharp_still": float(np.abs(mid_still - gaussian_filter(mid_still, 1.0))[mask].mean()),
            "sharp_moving": float(np.abs(mid_moving - gaussian_filter(mid_moving, 1.0))[mask].mean()),
            "still_mean": float(still[mask].mean()),
            "still_above3": float((still[mask] > 3.0).mean() * 100.0),
            "moving_mean": float(moving[mask].mean()),
            "moving_above3": float((moving[mask] > 3.0).mean() * 100.0),
            "frames": n,
        }
        line = "%-16s still: d2 %.3f, >3 %5.2f%% | moving: d2 %.3f, >3 %5.2f%% | sharpness %.2f / %.2f" % (c, r["still_mean"], r["still_above3"], r["moving_mean"], r["moving_above3"], r["sharp_still"], r["sharp_moving"])
        if a.motion:
            acc = [np.zeros(frames.shape[1:], np.float32), np.zeros(frames.shape[1:], np.float32)]
            res = [motion_residual(frames[t - 1], frames[t], frames[t + 1], mask, acc=acc) for t in range(a.still + 1, n - 1, a.motion_stride)]
            res = np.concatenate(res)
            # The motion map: the mean residual where it was measured, in red over the dimmed middle moving frame.
            mres = acc[0] / np.maximum(acc[1], 1.0)
            mpic = np.asarray(Image.open(os.path.join(d, "f%04d.png" % ((a.still + n) // 2))).convert("RGB")).astype(np.float32) * 0.35
            mpic[..., 0] = np.maximum(mpic[..., 0], np.clip(mres * 20.0, 0, 255))
            mpic[..., 1] = np.maximum(mpic[..., 1], np.clip(mres * 6.0, 0, 255))
            mim = Image.fromarray(mpic.astype(np.uint8))
            mim.resize((mim.width // 2, mim.height // 2), Image.BILINEAR).save(os.path.join(a.out, "map_motion_%s.png" % c))
            r["moving_mc_mean"] = float(res.mean())
            r["moving_mc_above3"] = float((res > 3.0).mean() * 100.0)
            line += " | compensated: %.3f, >3 %5.2f%%" % (r["moving_mc_mean"], r["moving_mc_above3"])
        results[c] = r
        lines.append(line)
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
