"""Compare two Movie Maker PNG sequences frame by frame (FellowEngine, decision 0072).

Usage: python movie_diff.py <dir a> <dir b>
Prints the number of frames compared, the frames that differ, the largest per-channel difference and the first
differing frame.
"""

import glob
import os
import sys

import numpy as np
from PIL import Image


def main() -> int:
    a_dir, b_dir = sys.argv[1], sys.argv[2]
    names = sorted(os.path.basename(p) for p in glob.glob(os.path.join(a_dir, "*.png")))
    common = [n for n in names if os.path.exists(os.path.join(b_dir, n))]
    differing = 0
    max_d = 0
    first = None
    for n in common:
        a = np.asarray(Image.open(os.path.join(a_dir, n)).convert("RGB"), dtype=np.int16)
        b = np.asarray(Image.open(os.path.join(b_dir, n)).convert("RGB"), dtype=np.int16)
        if a.shape != b.shape:
            print(f"{n}: size {a.shape} against {b.shape}")
            differing += 1
            continue
        d = int(np.abs(a - b).max())
        if d:
            differing += 1
            max_d = max(max_d, d)
            if first is None:
                first = n
    print(f"frames {len(common)} (a {len(names)}) differing {differing} max |d| {max_d} first {first}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
