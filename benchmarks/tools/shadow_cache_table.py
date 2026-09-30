#!/usr/bin/env python3
"""Directional shadow cache analysis of a perf session (FellowEngine counters recorded per frame).

  python benchmarks/tools/shadow_cache_table.py results/<session>.json [--cache fl_cache] [--ref stock] [--md out.md]

Per scene / resolution:
  * flythrough (moving camera): mean frame / GPU / render-thread CPU ms and shadow draw calls of the reference and the
    cache variant, delta, fraction of frames that re-rendered a cascade cache, GPU ms of frames with and without a
    rebuild, p99 frame ms (stability);
  * fixed cameras (--percam runs): mean GPU ms per camera for reference and cache variant;
  * counters: cascades served from the cache / rebuilt / rendered stock, dynamic casters drawn per frame.
"""

import argparse
import json
import os
import statistics
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))


def load_run(r):
    if not r.get("valid") or not r.get("_json"):
        return None
    p = os.path.join(ROOT, *r["_json"].replace("\\", "/").split("/"))
    if not os.path.exists(p):
        return None
    with open(p, encoding="utf-8") as f:
        return json.load(f)


def mean(v):
    v = [x for x in v if x is not None]
    return statistics.fmean(v) if v else None


def fmt(v, n=2):
    return "–" if v is None else f"{v:.{n}f}"


def pct(a, b):
    if a is None or b is None or a == 0:
        return "–"
    return f"{(b - a) / a * 100:+.1f} %"


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("session")
    ap.add_argument("--cache", default="fl_cache")
    ap.add_argument("--ref", default="stock")
    ap.add_argument("--md", default=None)
    a = ap.parse_args()
    with open(a.session, encoding="utf-8") as f:
        s = json.load(f)
    res = s["results"]
    keys = sorted({(e["scene"], e["resolution"]) for e in res.values()}, key=lambda k: (k[0], k[1]))
    lines = []
    lines.append("### Flythrough (moving camera)\n")
    lines.append(f"| Scene | Res | frame ms {a.ref} → {a.cache} | GPU ms | render CPU ms | shadow draws | frames with a rebuild | GPU ms rebuild / other frames | p99 frame ms |")
    lines.append("|---|---|---:|---:|---:|---:|---:|---:|---:|")
    cam_lines = ["### Fixed cameras (GPU ms, mean of runs)\n", f"| Scene | Res | camera | {a.ref} | {a.cache} | Δ |", "|---|---|---|---:|---:|---:|"]
    cnt_lines = ["### Cache counters (per measured frame)\n", "| Scene | Res | cached | rebuilt | stock/fallback | dynamic casters |", "|---|---|---:|---:|---:|---:|"]
    for sc, rs in keys:
        e_ref = res.get(f"{sc}|{rs}|{a.ref}")
        e_c = res.get(f"{sc}|{rs}|{a.cache}")
        if not e_ref or not e_c:
            continue
        runs_ref = [x for x in (load_run(r) for r in e_ref.get("runs", [])) if x]
        runs_c = [x for x in (load_run(r) for r in e_c.get("runs", [])) if x]
        if not runs_ref or not runs_c:
            continue

        def m(runs, key):
            return mean([x["summary"][key]["mean"] for x in runs if key in x.get("summary", {})])

        def p99(runs):
            return mean([x["summary"]["frame_ms"]["p99"] for x in runs])

        reb_frac, gpu_reb, gpu_oth = [], [], []
        cnt = {"cached": [], "rebuilt": [], "other": [], "dyn": []}
        for x in runs_c:
            fr = x.get("frames", {})
            fel = fr.get("fellow", {})
            rb = fel.get("shadow_cache_cascades_rebuilt")
            gpu = fr.get("gpu_ms")
            if rb and gpu and len(rb) == len(gpu):
                # counters are sampled right after the frame; with a separate render thread they can lag one frame
                flags = [v > 0 for v in rb]
                reb_frac.append(sum(flags) / len(flags))
                gr = [g for g, fl in zip(gpu, flags) if fl]
                go = [g for g, fl in zip(gpu, flags) if not fl]
                if gr:
                    gpu_reb.append(statistics.fmean(gr))
                if go:
                    gpu_oth.append(statistics.fmean(go))
            fs = x.get("fellow_stats", {})
            n = len(fr.get("frame_ms", [])) or 1
            if fs:
                cnt["cached"].append(fs.get("shadow_cache_cascades_cached", 0) / n)
                cnt["rebuilt"].append(fs.get("shadow_cache_cascades_rebuilt", 0) / n)
                cnt["other"].append((fs.get("shadow_cache_cascades_fallback", 0) + fs.get("shadow_cache_cascades_stock", 0)) / n)
                cnt["dyn"].append(fs.get("shadow_cache_dynamic_casters", 0) / n)
        fr_ref, fr_c = m(runs_ref, "frame_ms"), m(runs_c, "frame_ms")
        g_ref, g_c = m(runs_ref, "gpu_ms"), m(runs_c, "gpu_ms")
        c_ref, c_c = m(runs_ref, "render_cpu_ms"), m(runs_c, "render_cpu_ms")
        sd_ref, sd_c = m(runs_ref, "shadow_draw_calls"), m(runs_c, "shadow_draw_calls")
        lines.append(f"| {sc} | {rs} | {fmt(fr_ref)} → {fmt(fr_c)} ({pct(fr_ref, fr_c)}) | {fmt(g_ref)} → {fmt(g_c)} ({pct(g_ref, g_c)}) | "
                     f"{fmt(c_ref)} → {fmt(c_c)} ({pct(c_ref, c_c)}) | {fmt(sd_ref, 0)} → {fmt(sd_c, 0)} | "
                     f"{fmt(mean(reb_frac) and mean(reb_frac) * 100, 1)} % | {fmt(mean(gpu_reb))} / {fmt(mean(gpu_oth))} | {fmt(p99(runs_ref))} → {fmt(p99(runs_c))} |")
        cnt_lines.append(f"| {sc} | {rs} | {fmt(mean(cnt['cached']))} | {fmt(mean(cnt['rebuilt']), 3)} | {fmt(mean(cnt['other']), 3)} | {fmt(mean(cnt['dyn']), 1)} |")
        cams = sorted(set().union(*[set(x.get("per_camera", {}).keys()) for x in runs_ref + runs_c]))
        for cam in cams:
            gr = mean([x["per_camera"][cam]["summary"]["gpu_ms"]["mean"] for x in runs_ref if cam in x.get("per_camera", {})])
            gc = mean([x["per_camera"][cam]["summary"]["gpu_ms"]["mean"] for x in runs_c if cam in x.get("per_camera", {})])
            cam_lines.append(f"| {sc} | {rs} | {cam} | {fmt(gr)} | {fmt(gc)} | {pct(gr, gc)} |")
    out = "\n".join(lines + [""] + cam_lines + [""] + cnt_lines) + "\n"
    print(out)
    if a.md:
        with open(a.md, "w", encoding="utf-8") as f:
            f.write(out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
