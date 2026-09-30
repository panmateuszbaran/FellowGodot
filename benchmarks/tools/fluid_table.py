#!/usr/bin/env python3
"""Fluid Lab table from a perf session with variants flNNN / flNNN_hidden (see run_benchmarks.py build variants).

  python benchmarks/tools/fluid_table.py results/fellow-m1_fluid_perf.json [--md out.md]

Per grid and resolution: simulation GPU ms and surface GPU ms (module timestamps, mean over runs), viewport GPU ms and
frame ms with the water drawn and hidden, water rendering cost = viewport GPU(drawn) - viewport GPU(hidden)
(simulation runs in both), simulation VRAM, runs.
"""

import argparse
import json
import os
import statistics
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))


def run_extra(entry, key):
    vals = []
    for r in entry.get("runs", []):
        if not r.get("valid"):
            continue
        jp = os.path.join(ROOT, r["_json"]) if r.get("_json") else None
        ex = None
        if jp and os.path.exists(jp):
            with open(jp, encoding="utf-8") as f:
                ex = json.load(f).get("extra_metrics", {})
        if ex and ex.get(key, {}).get("n"):
            vals.append(ex[key]["mean"])
    return statistics.fmean(vals) if vals else None


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("session")
    ap.add_argument("--md", default=None)
    a = ap.parse_args()
    s = json.load(open(a.session, encoding="utf-8"))
    res = s["results"]
    lines = ["| Grid | Res | Sim GPU ms | Surface GPU ms | Viewport GPU ms (water drawn / hidden) | Water rendering ms | Frame ms (drawn) | Sim VRAM MB | Runs |",
             "|---|---|---:|---:|---:|---:|---:|---:|---:|"]
    stock_note = ""
    for key, e in sorted(res.items()):
        if e["build"] == "stock":
            err = next((r.get("error") for r in e.get("runs", []) if r.get("error")), "")
            stock_note = f"Stock Godot: {err or 'no valid run'}"
    grids = sorted({int(e["build"][2:].split("_")[0]) for e in res.values() if e["build"].startswith("fl")})
    for g in grids:
        for resol in ["1920x1080", "2560x1440"]:
            d = res.get(f"fluid|{resol}|fl{g}")
            h = res.get(f"fluid|{resol}|fl{g}_hidden")
            if not d:
                continue
            ad, ah = d.get("aggregate", {}), (h or {}).get("aggregate", {})
            gd = ad.get("gpu_ms_run_means", {}).get("mean")
            gh = ah.get("gpu_ms_run_means", {}).get("mean")
            sim = run_extra(d, "fluid_sim_gpu_ms")
            surf = run_extra(d, "fluid_surface_gpu_ms")
            info = (d.get("scene_desc") or {}).get("info", {})
            vram = info.get("simulation_vram_bytes", 0) / 2**20
            f = lambda v, n=3: "–" if v is None else f"{v:.{n}f}"
            lines.append(f"| {g}² | {resol} | {f(sim)} | {f(surf)} | {f(gd)} / {f(gh)} | {f(None if gd is None or gh is None else gd - gh)} | "
                         f"{f(ad.get('frame_ms_run_means', {}).get('mean'))} | {vram:.1f} | {ad.get('runs_valid', 0)} |")
    out = "\n".join(lines) + ("\n\n" + stock_note if stock_note else "")
    print(out)
    if a.md:
        open(a.md, "w", encoding="utf-8").write(out + "\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
