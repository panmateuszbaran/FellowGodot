#!/usr/bin/env python3
"""FellowEngine's clouds measured (milestone 182, decision 0324) from showcase/tools/sky_look.gd's pictures: the same
views with the clouds and without them (post/clouds/enabled false), the clouds being where the two differ.

* the satellite view (20 km up, straight down): the clouds' outlines' fractal dimension from their areas and perimeters
  (each cloud not cut by the picture's edge: perimeter ~ area^(D/2); Lovejoy 1982, Science 216, measured D = 1.35 for
  clouds and rain areas from 1 to 1.2 million km^2 — a smooth outline is 1, a dough-like blob little more);
* the views from below (zenith, up, side, away, sun): how sharp the clouds' edges are (the picture's gradient along
  their outlines, 8-bit levels a pixel: a soft, out-of-focus edge is a gentle one), their inner texture (the luminance's
  deviation from its 9-pixel mean inside them, levels), and their luminance (5th, 50th and 95th percentiles) against
  the sky's behind them.

    python benchmarks/tools/sky/sky_metrics.py <clouds dir> <clear dir> [--json out.json]
"""

import json
import os
import sys

import numpy as np
from PIL import Image
from scipy import ndimage

LUMA = np.array([0.2126, 0.7152, 0.0722])


def load(d, v):
    p = os.path.join(d, v + ".png")
    if not os.path.exists(p):
        return None
    return np.asarray(Image.open(p).convert("RGB")).astype(np.float64)


def fractal_dimension(mask, min_area=20):
    lab, n = ndimage.label(mask)
    areas, perims = [], []
    h, w = mask.shape
    for i, sl in enumerate(ndimage.find_objects(lab), 1):
        if sl is None:
            continue
        if sl[0].start == 0 or sl[1].start == 0 or sl[0].stop == h or sl[1].stop == w:
            continue
        m = lab[sl] == i
        a = int(m.sum())
        if a < min_area:
            continue
        pm = np.pad(m, 1)
        p = int((pm[1:, :] != pm[:-1, :]).sum() + (pm[:, 1:] != pm[:, :-1]).sum())
        areas.append(a)
        perims.append(p)
    if len(areas) < 5:
        return None, len(areas)
    la, lp = np.log(areas), np.log(perims)
    slope = np.polyfit(la, lp, 1)[0]
    return float(2.0 * slope), len(areas)


def from_below(cl, clear):
    diff = np.abs(cl - clear).max(axis=2)
    mask = diff > 6.0
    lum = cl @ LUMA
    sky = clear @ LUMA
    if mask.sum() < 100:
        return None
    # The edges: the picture's gradient on the clouds' outlines (the mask's boundary pixels).
    gy, gx = np.gradient(lum)
    g = np.hypot(gx, gy)
    edge = mask & ~ndimage.binary_erosion(mask)
    sharpness = float(np.median(g[edge])) if edge.sum() > 20 else float("nan")
    # The texture inside: the luminance's deviation from its 9-pixel mean, away from the edges.
    hp = lum - ndimage.uniform_filter(lum, 9)
    core = ndimage.binary_erosion(mask, iterations=6)
    texture = float(np.std(hp[core])) if core.sum() > 50 else float("nan")
    p5, p50, p95 = np.percentile(lum[mask], [5, 50, 95])
    return {
        "cover": float(mask.mean()),
        "edge_gradient": sharpness,
        "texture_levels": texture,
        "cloud_p5_p50_p95": [float(p5), float(p50), float(p95)],
        "sky_median": float(np.median(sky[mask])),
    }


def main():
    a, b = sys.argv[1], sys.argv[2]
    out = {}
    sat, sat0 = load(a, "sat"), load(b, "sat")
    if sat is not None and sat0 is not None:
        mask = np.abs(sat - sat0).max(axis=2) > 12.0
        d, n = fractal_dimension(mask)
        out["sat"] = {"cover": float(mask.mean()), "clouds": n, "fractal_dimension": d}
        print(
            "satellite: cover %.2f, %d clouds, outlines' fractal dimension %s (Lovejoy: 1.35)"
            % (mask.mean(), n, "%.3f" % d if d else "-")
        )
    for v in ("zenith", "up", "side", "away", "sun"):
        cl, clear = load(a, v), load(b, v)
        if cl is None or clear is None:
            continue
        r = from_below(cl, clear)
        if r is None:
            continue
        out[v] = r
        print(
            "%-6s cover %.2f, edges %.2f levels/px, texture %.2f levels, cloud luminance p5/p50/p95 %.0f/%.0f/%.0f, the sky behind %.0f"
            % (v, r["cover"], r["edge_gradient"], r["texture_levels"], *r["cloud_p5_p50_p95"], r["sky_median"])
        )
    if "--json" in sys.argv:
        json.dump(out, open(sys.argv[sys.argv.index("--json") + 1], "w"), indent=1)


if __name__ == "__main__":
    main()
