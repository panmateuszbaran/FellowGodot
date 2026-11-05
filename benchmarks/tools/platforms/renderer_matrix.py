#!/usr/bin/env python3
"""FellowEngine platform checks (milestone 49): renderer_matrix.sh for any OS (Windows with a GPU, too). Every bench
lab with each of Godot's renderers, one capture each; a summary of exit status, seconds, errors, the image's luma mean
and spread and, against a reference run (another build's out dir), the mean and largest luma difference.

  renderer_matrix.py --bin <godot> --out <dir> [--project benchmarks/project] [--renderers forward_plus,mobile,
      gl_compatibility] [--cases pbr:pbr_overview,...] [--res 1280x720] [--settle 60] [--reference <dir>]
      [--profile <FELLOW_VULKAN_PROFILE>] [--timeout 900]

Writes <out>/<renderer>/<case>/ (the capture), <out>/<renderer>/<case>.log and <out>/summary.tsv."""

import argparse
import os
import re
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from image_stats import read_png  # noqa: E402

CASES = (
    "pbr:pbr_overview fluid:fluid_cam_01 stress:stress_overview lights:lights_street blend:blend_front city:city_aerial "
    "foliage:foliage_meadow crowd:crowd_aerial particles:particles_aerial physics:physics_overview fire:fire_close "
    "burn:burn_top water:water_shore rain:rain_street wind:wind_meadow matching:matching_aerial "
    "streaming:street_start world:street rain:rain_street:--weather=storm rain:rain_street:--weather=snow"
).split()
DRIVERS = {"forward_plus": "vulkan", "mobile": "vulkan", "gl_compatibility": "opengl3"}
ERROR_RE = re.compile(r"ERROR|SCRIPT ERROR|Assertion|Segmentation|signal [0-9]+|Aborted|CrashHandler")


def luma(path):
    w, h, bpp, rows = read_png(path)
    out = []
    for line in rows:
        out.append([0.2126 * line[x] + 0.7152 * line[x + 1] + 0.0722 * line[x + 2] for x in range(0, w * bpp, bpp)])
    return out


def stats(lum):
    n = sum(len(r) for r in lum)
    total = sum(sum(r) for r in lum)
    total2 = sum(sum(v * v for v in r) for r in lum)
    mean = total / n
    return mean, max(total2 / n - mean * mean, 0.0) ** 0.5


def diff(a, b):
    if len(a) != len(b) or len(a[0]) != len(b[0]):
        return None
    total, worst, n = 0.0, 0.0, 0
    for ra, rb in zip(a, b):
        for va, vb in zip(ra, rb):
            d = abs(va - vb)
            total += d
            worst = max(worst, d)
            n += 1
    return total / n, worst


def find_png(d):
    if not os.path.isdir(d):
        return None
    for name in sorted(os.listdir(d)):
        if name.endswith(".png"):
            return os.path.join(d, name)
    return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--bin", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--project", default=os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "project"))
    ap.add_argument("--renderers", default="forward_plus,mobile,gl_compatibility")
    ap.add_argument("--cases", default=",".join(CASES))
    ap.add_argument("--res", default="1280x720")
    ap.add_argument("--settle", default="60")
    ap.add_argument("--reference", default="")
    ap.add_argument("--profile", default="")
    ap.add_argument("--timeout", type=int, default=900)
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    env = dict(os.environ)
    if a.profile:
        env["FELLOW_VULKAN_PROFILE"] = a.profile
    summary = open(os.path.join(a.out, "summary.tsv"), "w", encoding="utf-8")
    summary.write("renderer\tcase\texit\tseconds\terrors\tunique_errors\tmean\tspread\tdiff_mean\tdiff_max\n")
    for r in a.renderers.split(","):
        for c in a.cases.split(","):
            parts = c.split(":", 2)
            scene, cam = parts[0], parts[1]
            extra = parts[2].split() if len(parts) > 2 else []
            name = scene + ("_" + re.sub(r"[^a-z0-9]+", "_", " ".join(extra)).strip("_") if extra else "")
            d = os.path.join(a.out, r, name)
            os.makedirs(d, exist_ok=True)
            cmd = [a.bin, "--path", a.project, "--rendering-method", r, "--rendering-driver", DRIVERS[r], "--resolution",
                   a.res, "--fixed-fps", "60", "--", "--scene=" + scene, "--mode=capture", "--cameras=" + cam,
                   "--settle=" + a.settle, "--res=" + a.res, "--out=" + os.path.abspath(d)] + extra
            t0 = time.time()
            try:
                p = subprocess.run(cmd, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=a.timeout)
                status, log = p.returncode, p.stdout.decode("utf-8", "replace")
            except subprocess.TimeoutExpired as e:
                status, log = "timeout", (e.stdout or b"").decode("utf-8", "replace")
            seconds = int(time.time() - t0)
            with open(d + ".log", "w", encoding="utf-8") as f:
                f.write(log)
            lines = [line for line in log.splitlines() if ERROR_RE.search(line)]
            unique = sorted(set(re.sub(r"[0-9]+(\.[0-9]+)?", "N", line) for line in lines))
            with open(d + ".errors.txt", "w", encoding="utf-8") as f:
                f.write("\n".join(unique))
            png = find_png(d)
            mean = spread = dmean = dmax = "-"
            if png:
                lum = luma(png)
                m, s = stats(lum)
                mean, spread = "%.1f" % m, "%.1f" % s
                ref = find_png(os.path.join(a.reference, r, name)) if a.reference else None
                if ref:
                    dd = diff(lum, luma(ref))
                    if dd:
                        dmean, dmax = "%.2f" % dd[0], "%.0f" % dd[1]
            row = "\t".join(str(v) for v in (r, name, status, seconds, len(lines), len(unique), mean, spread, dmean, dmax))
            print(row, flush=True)
            summary.write(row + "\n")
            summary.flush()


if __name__ == "__main__":
    main()
