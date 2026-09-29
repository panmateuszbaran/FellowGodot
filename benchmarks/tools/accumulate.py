#!/usr/bin/env python3
"""Average sub-pixel jittered reference renders (mode=reference) into ground-truth frames.

Input  : <dir>/<stem>_ref-<name>_jNN.png   (N jittered renders per output frame, preset reference_ssaa)
Output : <out>/<stem>_ref-<name>.png       (average in LINEAR light, re-encoded to sRGB)
The jittered inputs can be deleted afterwards with --delete-inputs (they are large and redundant).
"""

import argparse
import collections
import glob
import os
import re
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(__file__))
import imageutil as iu  # noqa: E402

PAT = re.compile(r"^(?P<base>.+_ref-.+)_j(?P<j>\d+)\.png$")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("dir")
    ap.add_argument("--out", default=None)
    ap.add_argument("--delete-inputs", action="store_true")
    a = ap.parse_args()
    out = a.out or a.dir
    groups = collections.defaultdict(list)
    for f in glob.glob(os.path.join(a.dir, "*_ref-*_j*.png")):
        m = PAT.match(os.path.basename(f))
        if m:
            groups[m.group("base")].append(f)
    os.makedirs(out, exist_ok=True)
    for base, files in sorted(groups.items()):
        acc = None
        for f in sorted(files):
            lin = iu.srgb_to_linear(iu.load_rgb(f)).astype(np.float64)
            acc = lin if acc is None else acc + lin
        avg = acc / len(files)
        iu.save_rgb(os.path.join(out, base + ".png"), iu.linear_to_srgb(avg))
        print(f"{base}: {len(files)} samples")
        if a.delete_inputs:
            for f in files:
                os.remove(f)
    return 0


if __name__ == "__main__":
    sys.exit(main())
