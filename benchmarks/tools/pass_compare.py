#!/usr/bin/env python3
"""Per-pass GPU time of several variants of one profile session (GPU timestamps, --profile runs).

  python benchmarks/tools/pass_compare.py results/<session>.json <variant,variant,...> [--res 1920x1080]
                                          [--passes "Shadow|Opaque|TAA"] [--md out.md]

One table per scene: rows = timestamped passes (only those whose mean is >= 0.01 ms in some variant, or that match
--passes), columns = variants with the delta vs the first variant. The first variant is the reference (usually stock).
Per-pass means are over all measured frames of the run (flythrough and fixed cameras), as recorded by the runner.
"""

import argparse
import json
import re
import sys

TITLE = {"pbr": "PBR Lab", "sponza": "Sponza", "bistro_exterior": "Bistro exterior", "bistro_interior": "Bistro interior"}


def val(x):
    return x["mean"] if isinstance(x, dict) else x


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("session_json")
    ap.add_argument("variants")
    ap.add_argument("--res", default="1920x1080")
    ap.add_argument("--passes", default=None, help="regex: always show matching passes")
    ap.add_argument("--min", type=float, default=0.01)
    ap.add_argument("--md", default=None)
    a = ap.parse_args()
    with open(a.session_json, encoding="utf-8") as f:
        sess = json.load(f)
    variants = a.variants.split(",")
    rx = re.compile(a.passes) if a.passes else None
    lines = []
    scenes = []
    for key in sess["results"]:
        sc, res, _ = key.split("|")
        if res == a.res and sc not in scenes:
            scenes.append(sc)
    for sc in scenes:
        data = {}
        for v in variants:
            e = sess["results"].get(f"{sc}|{a.res}|{v}")
            if e and e.get("aggregate", {}).get("gpu_passes_ms"):
                data[v] = {p: val(x) for p, x in e["aggregate"]["gpu_passes_ms"].items()}
        if not data:
            continue
        ref = variants[0]
        order = []
        for v in variants:
            for p in data.get(v, {}):
                if p not in order:
                    order.append(p)
        lines.append(f"#### {TITLE.get(sc, sc)} ({a.res}), GPU ms per pass\n")
        lines.append("| pass | " + " | ".join(variants) + " |")
        lines.append("|---|" + "---:|" * len(variants))
        for p in order:
            if p == "__total__":
                continue
            vals = [data.get(v, {}).get(p) for v in variants]
            if not (any(x is not None and x >= a.min for x in vals) or (rx and rx.search(p))):
                continue
            cells = []
            for v, x in zip(variants, vals):
                if x is None:
                    cells.append("–")
                elif v == ref or data.get(ref, {}).get(p) is None:
                    cells.append(f"{x:.3f}")
                else:
                    r = data[ref][p]
                    cells.append(f"{x:.3f} ({x - r:+.3f})")
            lines.append(f"| {p} | " + " | ".join(cells) + " |")
        tots = [data.get(v, {}).get("__total__") for v in variants]
        cells = []
        for v, x in zip(variants, tots):
            if x is None:
                cells.append("–")
            elif v == ref or tots[0] is None:
                cells.append(f"**{x:.3f}**")
            else:
                cells.append(f"**{x:.3f} ({100 * (x - tots[0]) / tots[0]:+.1f} %)**")
        lines.append("| **sum of timestamped passes** | " + " | ".join(cells) + " |")
        lines.append("")
    text = "\n".join(lines)
    print(text)
    if a.md:
        with open(a.md, "w", encoding="utf-8") as f:
            f.write(text + "\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
