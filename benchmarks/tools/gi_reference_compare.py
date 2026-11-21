#!/usr/bin/env python3
"""Diffuse global illumination against the path-traced reference (milestone 167, decision 0308).

    python benchmarks/tools/gi_reference_compare.py <session dir> [--ref ref16] [arm ...]

<session dir> is a capture session of run_benchmarks.py (--mode capture --extra "--freeze_exposure=1 --hdr=1"), one
build variant per GI option and one with rendering/fellow/debug/raytracing_gi_reference=<bounces> (the reference arm,
default 'ref'). Every arm's linear EXR captures are compared with the reference's, camera by camera:

* bias: the mean log2 of the luminance ratio (stops; + brighter than the reference);
* error: the mean |log2 ratio| (stops).

Both on luminance averaged over 4 x 4 pixels (the reference's noise and TAA's differences averaged out), over the
pixels the reference lights above 1e-3 (a camera that is all darker is left out of the means). The other arms differ
from the reference only in their indirect diffuse light (the direct light, specular and the rest are the same frames),
so the ratio measures that light where it shows. Needs numpy and the OpenEXR module (pip install OpenEXR)."""
import argparse
import glob
import os
import sys

import numpy as np

try:
    import Imath
    import OpenEXR
except ImportError:
    sys.exit("needs the OpenEXR module: pip install OpenEXR")


def load_exr(path: str) -> np.ndarray:
    f = OpenEXR.InputFile(path)
    dw = f.header()["dataWindow"]
    w, h = dw.max.x - dw.min.x + 1, dw.max.y - dw.min.y + 1
    pt = Imath.PixelType(Imath.PixelType.FLOAT)
    return np.stack([np.frombuffer(f.channel(c, pt), dtype=np.float32).reshape(h, w) for c in "RGB"], -1)


def luminance_4x4(rgb: np.ndarray) -> np.ndarray:
    lum = rgb[..., 0] * 0.2126 + rgb[..., 1] * 0.7152 + rgb[..., 2] * 0.0722
    h, w = lum.shape
    return lum[: h // 4 * 4, : w // 4 * 4].reshape(h // 4, 4, w // 4, 4).mean((1, 3))


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("session")
    ap.add_argument("arms", nargs="*", help="arms to compare (default: every other directory)")
    ap.add_argument("--ref", default="ref", help="the reference arm")
    args = ap.parse_args()
    root = args.session
    arms = args.arms or sorted(d for d in os.listdir(root) if os.path.isdir(os.path.join(root, d)) and d != args.ref)
    refs = sorted(glob.glob(os.path.join(root, args.ref, "*", "*.exr")))
    if not refs:
        print(f"no reference captures in {os.path.join(root, args.ref)}")
        return 1
    print("%-40s" % "camera: bias / error (stops)" + "".join("%15s" % a[:14] for a in arms))
    totals = {a: [] for a in arms}
    for rp in refs:
        scene = os.path.basename(os.path.dirname(rp))
        name = os.path.basename(rp)
        ref = luminance_4x4(load_exr(rp))
        lit = ref > 1e-3
        line = "%-40s" % name[:-4].split("_", 2)[-1][-40:]
        for a in arms:
            ap_ = os.path.join(root, a, scene, name)
            if not os.path.exists(ap_) or lit.sum() < 100:
                line += "%15s" % "-"
                continue
            x = luminance_4x4(load_exr(ap_))
            r = np.log2((x[lit] + 1e-4) / (ref[lit] + 1e-4))
            totals[a].append((r.mean(), np.abs(r).mean()))
            line += "%15s" % ("%+.2f / %.2f" % totals[a][-1])
        print(line)
    line = "%-40s" % "mean"
    for a in arms:
        v = np.array(totals[a])
        line += "%15s" % (("%+.2f / %.2f" % (v[:, 0].mean(), v[:, 1].mean())) if len(v) else "-")
    print(line)
    return 0


if __name__ == "__main__":
    sys.exit(main())
