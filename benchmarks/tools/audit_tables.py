#!/usr/bin/env python3
"""Markdown tables for docs/fellow_renderer_audit.md, generated from benchmark result JSONs (no hand-copied numbers).

  python benchmarks/tools/audit_tables.py --perf results/godot-4.7.2_perf.json --profile results/godot-4.7.2_profile.json
         [--noaa results/godot-4.7.2_noaa_profile.json] [--build stock] > audit_tables.md

Sections: frame summary (perf session), per-pass GPU breakdown (profile session, GPU timestamps), and, with --noaa,
the cost of the TAA path (motion vectors + TAA resolve) = preset high minus preset high_noaa, per pass.
"""

import argparse
import json
import sys

SCENES = ["pbr", "sponza", "bistro_exterior", "bistro_interior"]
TITLE = {"pbr": "PBR Lab", "sponza": "Sponza", "bistro_exterior": "Bistro ext.", "bistro_interior": "Bistro int."}
PASSES = [
    "Render Depth Pre-Pass", "Render Directional/SpotLight Shadows", "Render OmniLight Shadows", "Render Opaque Pass",
    "Render Sky", "Render 3D Transparent Pass", "Process SSAO", "Process SSIL", "Process SSR", "Prepare Depth for SSAO/SSIL",
    "Copy Final Framebuffer (SSIL/SSR)", "Render Fog", "Integrate Fog", "TAA", "Auto exposure", "Glow", "Tonemap",
]


def val(x):
    return x["mean"] if isinstance(x, dict) else x


def entry(sess, scene, res, build):
    return sess["results"].get(f"{scene}|{res}|{build}")


def frame_summary(perf, build):
    out = ["| Scene | Res | Frame ms | GPU ms | Render-thread CPU ms | p99 ms | 1% low FPS | Avg FPS | Draw calls | Primitives | VRAM MB | Runs | CV |",
           "|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|"]
    for scene in SCENES:
        for res in ["1920x1080", "2560x1440"]:
            e = entry(perf, scene, res, build)
            if not e or not e.get("aggregate", {}).get("runs_valid"):
                continue
            a = e["aggregate"]
            out.append(f"| {TITLE[scene]} | {res} | {a['frame_ms_run_means']['mean']:.2f} | {a['gpu_ms_run_means']['mean']:.2f} | "
                       f"{a['render_cpu_ms_run_means']['mean']:.2f} | {a['frame_ms_pooled']['p99']:.2f} | {a['fps_1pct_low_pooled']:.0f} | "
                       f"{a['avg_fps_run']['mean']:.0f} | {a['draw_calls_run_means']['mean']:.0f} | {a['primitives_run_means']['mean'] / 1e6:.2f} M | "
                       f"{a['video_mem_peak_bytes'] / 2**20:.0f} | {a['runs_valid']} | {100 * a['frame_ms_run_means']['cv']:.1f}% |")
    return "\n".join(out)


def pass_table(prof, build, res):
    cols = [s for s in SCENES if entry(prof, s, res, build)]
    tot = {s: val(entry(prof, s, res, build)["aggregate"]["gpu_passes_ms"]["__total__"]) for s in cols}
    out = [f"| Pass ({res}, GPU ms and share of frame) | " + " | ".join(TITLE[s] for s in cols) + " |", "|---|" + "---:|" * len(cols)]
    for p in PASSES:
        cells, any_ = [], False
        for s in cols:
            g = entry(prof, s, res, build)["aggregate"]["gpu_passes_ms"].get(p)
            if g is None:
                cells.append("–")
                continue
            v = val(g)
            any_ = True
            cells.append(f"{v:.2f} ({100 * v / tot[s]:.0f}%)")
        if any_:
            out.append(f"| {p} | " + " | ".join(cells) + " |")
    out.append("| **sum of timestamped passes** | " + " | ".join(f"**{tot[s]:.2f}**" for s in cols) + " |")
    return "\n".join(out)


def taa_path_cost(prof, noaa, build):
    out = ["| Scene | Res | GPU ms high (TAA) | GPU ms high_noaa | Δ total | Δ depth pre-pass (motion vectors) | TAA pass | Δ other |",
           "|---|---|---:|---:|---:|---:|---:|---:|"]
    for scene in SCENES:
        for res in ["1920x1080", "2560x1440"]:
            a, b = entry(prof, scene, res, build), entry(noaa, scene, res, build)
            if not a or not b:
                continue
            ga, gb = a["aggregate"]["gpu_passes_ms"], b["aggregate"]["gpu_passes_ms"]
            ta, tb = val(ga["__total__"]), val(gb["__total__"])
            pre = val(ga.get("Render Depth Pre-Pass", 0)) - val(gb.get("Render Depth Pre-Pass", 0))
            taa = val(ga.get("TAA", 0))
            out.append(f"| {TITLE[scene]} | {res} | {ta:.2f} | {tb:.2f} | {ta - tb:+.2f} ({100 * (ta - tb) / tb:+.0f}%) | {pre:+.2f} | {taa:.2f} | {ta - tb - pre - taa:+.2f} |")
    return "\n".join(out)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--perf", required=True)
    ap.add_argument("--profile", required=True)
    ap.add_argument("--noaa", default=None)
    ap.add_argument("--build", default="stock")
    a = ap.parse_args()
    perf = json.load(open(a.perf, encoding="utf-8"))
    prof = json.load(open(a.profile, encoding="utf-8"))
    print("### Frame summary (" + perf["session"] + ", build " + a.build + ")\n")
    print(frame_summary(perf, a.build) + "\n")
    for res in ["1920x1080", "2560x1440"]:
        print(f"### Per-pass GPU time, {res} (" + prof["session"] + ")\n")
        print(pass_table(prof, a.build, res) + "\n")
    if a.noaa:
        noaa = json.load(open(a.noaa, encoding="utf-8"))
        print("### Cost of the TAA path: preset `high` minus `high_noaa` (" + noaa["session"] + ")\n")
        print(taa_path_cost(prof, noaa, a.build) + "\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
