"""Compares the PNGs of <dir>/a and <dir>/b (same relative paths): differing pixels, max |d|, PSNR, bounding box."""
import glob
import math
import os
import sys

import numpy as np
from PIL import Image

out = sys.argv[1]
rows = []
for f in sorted(glob.glob(os.path.join(out, "a", "**", "*.png"), recursive=True)):
    g = os.path.join(out, "b", os.path.relpath(f, os.path.join(out, "a")))
    if not os.path.exists(g):
        print("missing", g)
        continue
    a = np.asarray(Image.open(f)).astype(int)[..., :3]
    b = np.asarray(Image.open(g)).astype(int)[..., :3]
    d = np.abs(a - b).max(-1)
    mse = ((a - b) ** 2).mean()
    ys, xs = np.nonzero(d > 0)
    bbox = (int(xs.min()), int(xs.max()), int(ys.min()), int(ys.max())) if len(xs) else ()
    rows.append((os.path.basename(f), int((d > 0).sum()), int(d.max()), round(10 * math.log10(255 * 255 / mse), 2) if mse > 0 else 99.0, bbox))
print("images", len(rows), "differing", sum(1 for r in rows if r[1]), "px total", sum(r[1] for r in rows), "max |d|", max([r[2] for r in rows] or [0]))
for r in rows:
    if r[1]:
        print("  ", r)
