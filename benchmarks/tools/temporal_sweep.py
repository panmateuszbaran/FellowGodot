#!/usr/bin/env python3
"""Temporal metrics of several build variants of one temporal session against the accumulated ground truth.

  python benchmarks/tools/temporal_sweep.py <session> <variant,variant,...> [--out <dir>] [--md out.md]

Runs benchmarks/tools/temporal.py for every (variant, sequence) whose frames exist and prints one table per metric
(rows = sequences, columns = variants). Ground-truth sessions: godot-4.7.2_reference (PBR lab sequences) and
godot-4.7.2_reference_ext (Sponza / Bistro sequences), both from the milestone-1 baseline.
"""

import argparse
import json
import os
import subprocess
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
RAW = os.path.join(ROOT, "benchmarks", "results", "raw")
SEQS = [("pbr", s, "godot-4.7.2_reference") for s in ["camera_pan", "moving_object", "thin_dolly", "specular_orbit", "foliage_strafe"]]
SEQS += [("sponza", "sponza_pan", "godot-4.7.2_reference_ext"), ("bistro_exterior", "bistro_ext_path", "godot-4.7.2_reference_ext"),
         ("bistro_interior", "bistro_int_path", "godot-4.7.2_reference_ext")]
METRICS = [("err_motion", "error in motion regions (lower is better)", 5), ("err_static", "error in static regions", 5),
           ("flicker", "flicker vs ground truth", 5), ("psnr", "PSNR dB", 2), ("ssim_every4", "SSIM", 4)]


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("session")
    ap.add_argument("variants")
    ap.add_argument("--out", default=None)
    ap.add_argument("--md", default=None)
    a = ap.parse_args()
    variants = a.variants.split(",")
    out = a.out or os.path.join(RAW, a.session + "_analysis")
    os.makedirs(out, exist_ok=True)
    table = {}
    for sc, seq, refs in SEQS:
        ref_dir = os.path.join(RAW, refs, "stock", sc)
        for v in variants:
            test = os.path.join(RAW, a.session, v, sc)
            if not os.path.isdir(test) or not any(f"_seq-{seq}_" in f for f in os.listdir(test)):
                continue
            vout = os.path.join(out, v)
            jp = os.path.join(vout, f"temporal_{seq}.json")
            if not os.path.exists(jp):
                cmd = [sys.executable, "-u", os.path.join(ROOT, "benchmarks", "tools", "temporal.py"), "--test", test, "--labels", v,
                       "--ref", ref_dir, "--seq", seq, "--out", vout]
                subprocess.call(cmd, cwd=ROOT, creationflags=0x08000000 if os.name == "nt" else 0)
            if os.path.exists(jp):
                with open(jp, encoding="utf-8") as f:
                    table.setdefault(seq, {})[v] = json.load(f)["results"][v]["aggregate"]
    lines = []
    for key, title, nd in METRICS:
        lines.append(f"#### {title}\n")
        lines.append("| sequence | " + " | ".join(variants) + " |")
        lines.append("|---|" + "---:|" * len(variants))
        for _, seq, _ in SEQS:
            if seq not in table:
                continue
            row = []
            vals = {v: table[seq].get(v, {}).get(key) for v in variants}
            finite = [x for x in vals.values() if x is not None]
            best = (max if key in ("psnr", "ssim_every4") else min)(finite) if finite else None
            for v in variants:
                x = vals[v]
                cell = "–" if x is None else f"{x:.{nd}f}"
                if x is not None and x == best and len(finite) > 1:
                    cell = f"**{cell}**"
                row.append(cell)
            lines.append(f"| {seq} | " + " | ".join(row) + " |")
        lines.append("")
    text = "\n".join(lines)
    print(text)
    if a.md:
        with open(a.md, "w", encoding="utf-8") as f:
            f.write(text + "\n")
    with open(os.path.join(out, "summary.json"), "w", encoding="utf-8") as f:
        json.dump(table, f, indent=1)
    return 0


if __name__ == "__main__":
    sys.exit(main())
