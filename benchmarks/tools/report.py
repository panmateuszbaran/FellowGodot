#!/usr/bin/env python3
"""Generate the Stock Godot vs FellowEngine report (HTML + Markdown + machine-readable data).

  python benchmarks/tools/report.py --baseline godot-4.7.2 --ab <tag> [--fluid <tag>] [--out reports/<date>_<commit>]

Inputs (all produced by run_suite.py / run_benchmarks.py):
  results/<baseline>_perf.json, _profile.json                       stock baseline aggregates
  results/raw/<baseline>_capture/stock/<scene>/*.png|exr             stock captures
  results/raw/<baseline>_capture_pbrref/stock/pbr/*.exr              PBR lab, reference lighting, linear EXR
  results/raw/<baseline>_pbrref_export/cycles_*.exr                  Cycles reference (benchmarks/reference)
  results/raw/<baseline>_reference*/stock/<scene>/*_ref-*.png        accumulated AA ground truth
  results/<ab>_perf.json, raw/<ab>_capture/{stock,fellow}/, raw/<ab>_temporal/{stock,fellow}/
  results/<fluid>_*.json                                             fluid lab sessions (FellowEngine only)
Outputs: <out>/index.html, summary.md, hardware.json, settings.json, performance.csv, performance.json,
         screenshots/{stock,fellow,reference,diff}/, graphs/, temporal/.
"""

from __future__ import annotations

import argparse
import csv
import datetime
import glob
import html
import json
import math
import os
import shutil
import subprocess
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(__file__))
import imageutil as iu  # noqa: E402

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
RES = os.path.join(ROOT, "benchmarks", "results")
RAW = os.path.join(RES, "raw")
SCENE_ORDER = ["pbr", "sponza", "bistro_exterior", "bistro_interior", "fluid"]
SCENE_TITLE = {"pbr": "PBR Validation Lab", "sponza": "Intel Sponza", "bistro_exterior": "Bistro Exterior", "bistro_interior": "Bistro Interior", "fluid": "Fluid Lab"}


def load_json(p):
    if p and os.path.exists(p):
        with open(p, encoding="utf-8") as f:
            return json.load(f)
    return None


def git(*a):
    try:
        return subprocess.run(["git", "-C", ROOT, *a], capture_output=True, text=True, timeout=30).stdout.strip()
    except Exception:  # noqa: BLE001
        return ""


def fmt(v, nd=3):
    if v is None or (isinstance(v, float) and (math.isnan(v) or math.isinf(v))):
        return "–"
    return f"{v:.{nd}f}"


def pct_delta(new, old):
    if new is None or old in (None, 0):
        return None
    return (new - old) / old * 100.0


# --------------------------------------------------------------------------- performance tables

def perf_rows(session_json, label_override=None):
    rows = []
    if not session_json:
        return rows
    for key, e in session_json["results"].items():
        a = e.get("aggregate") or {}
        if not a.get("runs_valid"):
            rows.append({"scene": e["scene"], "resolution": e["resolution"], "build": e["build"], "valid": False})
            continue
        tel = [r.get("_gpu_telemetry", {}) for r in e["runs"] if r.get("valid")]
        clk = [t["clocks.gr"]["mean"] for t in tel if "clocks.gr" in t]
        tmp = [t["temperature.gpu"]["mean"] for t in tel if "temperature.gpu" in t]
        row = {
            "scene": e["scene"], "resolution": e["resolution"], "build": label_override or e["build"], "fellow": e.get("fellow", ""),
            "valid": True, "runs": a["runs_valid"], "noisy": a.get("noisy", False),
            "frame_ms": a["frame_ms_run_means"]["mean"], "frame_ms_std": a["frame_ms_run_means"]["std"], "frame_ms_cv": a["frame_ms_run_means"]["cv"],
            "gpu_ms": a["gpu_ms_run_means"]["mean"], "gpu_ms_cv": a["gpu_ms_run_means"]["cv"],
            "cpu_ms": a["render_cpu_ms_run_means"]["mean"], "setup_ms": a["setup_cpu_ms_run_means"]["mean"],
            "p50": a["frame_ms_pooled"]["p50"], "p90": a["frame_ms_pooled"]["p90"], "p95": a["frame_ms_pooled"]["p95"], "p99": a["frame_ms_pooled"]["p99"], "p99_9": a["frame_ms_pooled"]["p99_9"],
            "gpu_p95": a["gpu_ms_pooled"]["p95"], "gpu_p99": a["gpu_ms_pooled"]["p99"],
            "fps": a["avg_fps_run"]["mean"], "fps_1pct_low": a["fps_1pct_low_pooled"],
            "draw_calls": a["draw_calls_run_means"]["mean"], "primitives": a["primitives_run_means"]["mean"], "objects": a["objects_run_means"]["mean"],
            "stutters": a["stutter_frames_total"], "frames": a["frames_total"],
            "pipeline_compiles": sum(a["pipeline_compilations_during_measurement"].values()),
            "vram_mb": a["video_mem_peak_bytes"] / 2**20, "tex_mb": a["texture_mem_peak_bytes"] / 2**20,
            "ram_mb": a["process_peak_working_set_bytes"] / 2**20, "load_ms": a["load_time_ms"]["mean"],
            "gpu_clock": float(np.mean(clk)) if clk else None, "gpu_temp": float(np.mean(tmp)) if tmp else None,
            "gpu_passes": a.get("gpu_passes_ms"), "per_camera": a.get("per_camera"),
        }
        rows.append(row)
    rows.sort(key=lambda r: (SCENE_ORDER.index(r["scene"]) if r["scene"] in SCENE_ORDER else 99, r["resolution"], r["build"]))
    return rows


def perf_table_html(rows, compare=True):
    head = ["Scene", "Res", "Build", "Runs", "Frame ms (CV)", "GPU ms", "Render CPU ms", "p50", "p95", "p99", "Avg FPS", "1% low", "Draw calls", "Primitives", "VRAM MB", "Stutters", "GPU MHz / °C"]
    if compare:
        head.insert(6, "Δ frame vs stock")
    out = ["<table class='perf'><thead><tr>" + "".join(f"<th>{h}</th>" for h in head) + "</tr></thead><tbody>"]
    stock = {(r["scene"], r["resolution"]): r for r in rows if r["build"] == "stock" and r.get("valid")}
    for r in rows:
        if not r.get("valid"):
            out.append(f"<tr><td>{SCENE_TITLE.get(r['scene'], r['scene'])}</td><td>{r['resolution']}</td><td>{r['build']}</td><td colspan='{len(head) - 3}'>no valid run</td></tr>")
            continue
        noisy = " <span class='badge warn'>noisy</span>" if r["noisy"] else ""
        cells = [SCENE_TITLE.get(r["scene"], r["scene"]), r["resolution"], html.escape(r["build"] + (f" [{r['fellow']}]" if r.get("fellow") else "")), str(r["runs"]),
                 f"{fmt(r['frame_ms'])} ({fmt(r['frame_ms_cv'] * 100, 1)}%){noisy}"]
        if compare:
            s = stock.get((r["scene"], r["resolution"]))
            d = pct_delta(r["frame_ms"], s["frame_ms"]) if s and r["build"] != "stock" else None
            cls = "" if d is None else ("good" if d < -2 else ("bad" if d > 2 else "neutral"))
            cells.append(f"<span class='{cls}'>{'' if d is None else f'{d:+.1f}%'}</span>")
        cells += [fmt(r["gpu_ms"]), fmt(r["cpu_ms"]), fmt(r["p50"]), fmt(r["p95"]), fmt(r["p99"]), fmt(r["fps"], 1), fmt(r["fps_1pct_low"], 1),
                  fmt(r["draw_calls"], 0), f"{r['primitives'] / 1e6:.2f} M", fmt(r["vram_mb"], 0), str(r["stutters"]),
                  f"{fmt(r['gpu_clock'], 0)} / {fmt(r['gpu_temp'], 0)}"]
        out.append("<tr>" + "".join(f"<td>{c}</td>" for c in cells) + "</tr>")
    out.append("</tbody></table>")
    return "\n".join(out)


def perf_table_md(rows):
    lines = ["| Scene | Res | Build | GPU ms | Frame ms | CPU ms | p95 ms | p99 ms | VRAM MB | CV | Runs |", "|---|---|---|---:|---:|---:|---:|---:|---:|---:|---:|"]
    for r in rows:
        if not r.get("valid"):
            lines.append(f"| {SCENE_TITLE.get(r['scene'], r['scene'])} | {r['resolution']} | {r['build']} | – | – | – | – | – | – | – | 0 |")
            continue
        lines.append(f"| {SCENE_TITLE.get(r['scene'], r['scene'])} | {r['resolution']} | {r['build']}{' ['+r['fellow']+']' if r.get('fellow') else ''} | {fmt(r['gpu_ms'])} | {fmt(r['frame_ms'])} | {fmt(r['cpu_ms'])} | {fmt(r['p95'])} | {fmt(r['p99'])} | {fmt(r['vram_mb'], 0)} | {fmt(r['frame_ms_cv'] * 100, 1)}%{' (noisy)' if r['noisy'] else ''} | {r['runs']} |")
    return "\n".join(lines)


# --------------------------------------------------------------------------- graphs

def plot_frame_distributions(session_json, out_png, title):
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    if not session_json:
        return None
    keys = sorted(session_json["results"].keys())
    scenes = sorted({session_json["results"][k]["scene"] for k in keys}, key=lambda s: SCENE_ORDER.index(s) if s in SCENE_ORDER else 99)
    fig, axs = plt.subplots(1, len(scenes), figsize=(4.2 * len(scenes), 3.4), squeeze=False)
    for ax, sc in zip(axs[0], scenes):
        for k in keys:
            e = session_json["results"][k]
            if e["scene"] != sc:
                continue
            vals = []
            for r in e["runs"]:
                jf = os.path.join(ROOT, r.get("_json", ""))
                d = load_json(jf) if r.get("valid") else None
                if d:
                    vals += d["frames"]["frame_ms"]
            if not vals:
                continue
            v = np.sort(np.asarray(vals))
            ax.plot(v, np.linspace(0, 1, len(v)), label=f"{e['build']} {e['resolution']}")
        ax.set_title(SCENE_TITLE.get(sc, sc), fontsize=10)
        ax.set_xlabel("frame time (ms)")
        ax.grid(alpha=0.3)
        ax.legend(fontsize=7)
    axs[0][0].set_ylabel("fraction of frames ≤ x")
    fig.suptitle(title, fontsize=11)
    fig.tight_layout()
    fig.savefig(out_png, dpi=110)
    plt.close(fig)
    return out_png


def plot_gpu_passes(rows, out_png, title):
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    rr = [r for r in rows if r.get("valid") and r.get("gpu_passes")]
    if not rr:
        return None
    names = set()
    for r in rr:
        names |= {k for k, v in r["gpu_passes"].items() if k != "__total__" and v > 0.02}
    names = sorted(names, key=lambda n: -max(r["gpu_passes"].get(n, 0) for r in rr))[:14]
    fig, ax = plt.subplots(figsize=(12, 0.45 * len(rr) + 1.8))
    labels = [f"{SCENE_TITLE.get(r['scene'], r['scene'])} {r['resolution']} {r['build']}" for r in rr]
    left = np.zeros(len(rr))
    cmap = plt.get_cmap("tab20")
    for i, n in enumerate(names):
        vals = np.array([r["gpu_passes"].get(n, 0.0) for r in rr])
        ax.barh(labels, vals, left=left, color=cmap(i % 20), label=n)
        left += vals
    ax.set_xlabel("GPU ms (engine RENDER_TIMESTAMP markers, profile runs)")
    ax.legend(fontsize=7, ncol=2, loc="lower right")
    ax.set_title(title, fontsize=11)
    ax.invert_yaxis()
    fig.tight_layout()
    fig.savefig(out_png, dpi=110)
    plt.close(fig)
    return out_png


# --------------------------------------------------------------------------- images

def downscale_copy(src, dst, width=1280):
    from PIL import Image

    os.makedirs(os.path.dirname(dst), exist_ok=True)
    im = Image.open(src).convert("RGB")
    if im.width > width:
        im = im.resize((width, round(im.height * width / im.width)), Image.LANCZOS)
    im.save(dst, optimize=True)
    return dst


def slider_html(a_rel, b_rel, label_a, label_b, uid):
    return f"""<div class="slider" id="s{uid}">
  <img src="{a_rel}" alt="{html.escape(label_a)}" loading="lazy">
  <div class="top" style="clip-path: inset(0 50% 0 0)"><img src="{b_rel}" alt="{html.escape(label_b)}" loading="lazy"></div>
  <div class="bar" style="left:50%"></div>
  <span class="lab l">{html.escape(label_b)}</span><span class="lab r">{html.escape(label_a)}</span>
  <input type="range" min="0" max="100" value="50" aria-label="before/after">
</div>"""


def image_section(out_dir, scene, pairs, uid0=0):
    """pairs: list of dict(name, a (stock path), b (fellow path or None), ref (path or None), metrics)."""
    parts = []
    uid = uid0
    for p in pairs:
        uid += 1
        base = f"{scene}_{p['name']}"
        a_rel = os.path.relpath(downscale_copy(p["a"], os.path.join(out_dir, "screenshots", "stock", base + ".png")), out_dir).replace("\\", "/")
        cells = []
        if p.get("b"):
            b_rel = os.path.relpath(downscale_copy(p["b"], os.path.join(out_dir, "screenshots", "fellow", base + ".png")), out_dir).replace("\\", "/")
            cells.append(slider_html(a_rel, b_rel, "Godot stock", "FellowEngine", uid))
        else:
            cells.append(f"<img class='full' src='{a_rel}' loading='lazy'>")
        side = []
        if p.get("diff"):
            d_rel = os.path.relpath(downscale_copy(p["diff"], os.path.join(out_dir, "screenshots", "diff", base + "_diff.png"), 640), out_dir).replace("\\", "/")
            side.append(f"<figure><img src='{d_rel}' loading='lazy'><figcaption>|stock − fellow| ×4</figcaption></figure>")
        if p.get("ref"):
            r_rel = os.path.relpath(downscale_copy(p["ref"], os.path.join(out_dir, "screenshots", "reference", base + "_ref.png"), 640), out_dir).replace("\\", "/")
            side.append(f"<figure><img src='{r_rel}' loading='lazy'><figcaption>{html.escape(p.get('ref_label', 'reference'))}</figcaption></figure>")
        m = p.get("metrics") or {}
        mt = "".join(f"<tr><td>{html.escape(k)}</td><td>{html.escape(v)}</td></tr>" for k, v in m.items())
        parts.append(f"""<div class="shot"><h4>{html.escape(p['name'])} <small>{html.escape(p.get('purpose', ''))}</small></h4>
<div class="shotgrid"><div>{''.join(cells)}</div><div class="side">{''.join(side)}<table class="metrics">{mt}</table></div></div></div>""")
    return "\n".join(parts), uid


def image_metrics(a, b=None, ref=None):
    ia = iu.load_rgb(a)
    m = {}
    if b:
        ib = iu.load_rgb(b)
        m["stock vs fellow PSNR / SSIM"] = f"{iu.psnr(ia, ib):.2f} dB / {iu.ssim(ia, ib):.4f}"
    if ref:
        ir = iu.load_rgb(ref)
        if ir.shape == ia.shape:
            m["stock vs ref PSNR / SSIM"] = f"{iu.psnr(ia, ir):.2f} dB / {iu.ssim(ia, ir):.4f}"
            if b:
                m["fellow vs ref PSNR / SSIM"] = f"{iu.psnr(ib, ir):.2f} dB / {iu.ssim(ib, ir):.4f}"
    return m


CSS = """
:root { --bg:#fbfbfa; --fg:#1d1d1f; --muted:#6b6b70; --line:#e2e2e0; --card:#ffffff; --good:#1a7f37; --bad:#c62828; --warn:#a15c00; --accent:#3b5bdb; }
@media (prefers-color-scheme: dark) { :root:not([data-theme="light"]) { --bg:#141416; --fg:#ececef; --muted:#9a9aa2; --line:#2c2c31; --card:#1c1c20; --good:#5bd07b; --bad:#ff7b72; --warn:#e3a33c; --accent:#8ea2ff; } }
body { background:var(--bg); color:var(--fg); font:14px/1.5 -apple-system, "Segoe UI", Roboto, sans-serif; margin:0; }
main { max-width:1480px; margin:0 auto; padding:24px 16px 64px; }
h1 { font-size:24px; margin:0 0 4px; } h2 { font-size:19px; margin:36px 0 8px; border-bottom:1px solid var(--line); padding-bottom:4px; }
h3 { font-size:16px; margin:24px 0 6px; } h4 { font-size:14px; margin:14px 0 6px; } small { color:var(--muted); font-weight:normal; }
.muted { color:var(--muted); } code { font-size:12px; }
table { border-collapse:collapse; font-variant-numeric:tabular-nums; } td, th { border-bottom:1px solid var(--line); padding:4px 8px; text-align:right; white-space:nowrap; }
th { color:var(--muted); font-weight:600; font-size:12px; } td:first-child, th:first-child, td:nth-child(2), td:nth-child(3) { text-align:left; }
.tablewrap { overflow-x:auto; } .good { color:var(--good); font-weight:600; } .bad { color:var(--bad); font-weight:600; } .neutral { color:var(--muted); }
.badge { font-size:11px; padding:1px 6px; border-radius:8px; border:1px solid currentColor; } .badge.warn { color:var(--warn); }
.card { background:var(--card); border:1px solid var(--line); border-radius:8px; padding:12px 16px; margin:12px 0; }
.shot { margin:18px 0; } .shotgrid { display:grid; grid-template-columns: minmax(0,3fr) minmax(0,1.3fr); gap:12px; align-items:start; }
@media (max-width: 900px) { .shotgrid { grid-template-columns: 1fr; } }
.side figure { margin:0 0 8px; } .side img, img.full { width:100%; display:block; border-radius:4px; } figcaption { font-size:12px; color:var(--muted); }
.slider { position:relative; width:100%; overflow:hidden; border-radius:4px; user-select:none; }
.slider img { display:block; width:100%; } .slider .top { position:absolute; inset:0; } .slider .top img { width:100%; }
.slider .bar { position:absolute; top:0; bottom:0; width:2px; background:#fff; box-shadow:0 0 3px #000; pointer-events:none; }
.slider input { position:absolute; inset:0; width:100%; height:100%; opacity:0; cursor:ew-resize; margin:0; }
.slider .lab { position:absolute; top:6px; font-size:11px; background:rgba(0,0,0,.55); color:#fff; padding:1px 6px; border-radius:3px; pointer-events:none; }
.slider .lab.l { left:6px; } .slider .lab.r { right:6px; }
.metrics td { font-size:12px; } img.graph { max-width:100%; }
"""

JS = """
document.querySelectorAll('.slider').forEach(function(s){
  var r = s.querySelector('input'), top = s.querySelector('.top'), bar = s.querySelector('.bar');
  r.addEventListener('input', function(){ top.style.clipPath = 'inset(0 ' + (100 - r.value) + '% 0 0)'; bar.style.left = r.value + '%'; });
});
"""


# --------------------------------------------------------------------------- main

def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--baseline", default="godot-4.7.2")
    ap.add_argument("--ab", default=None, help="tag of the stock-vs-fellow session set")
    ap.add_argument("--ab-label", default="FellowEngine")
    ap.add_argument("--fluid", default=None, help="tag of the fluid sessions")
    ap.add_argument("--notes", default=None, help="markdown file with analysis text to embed")
    ap.add_argument("--out", default=None)
    ap.add_argument("--res", default="1920x1080", help="resolution used for image sections")
    a = ap.parse_args()

    commit = git("rev-parse", "--short=10", "HEAD") or "nocommit"
    out = a.out or os.path.join(ROOT, "reports", f"{datetime.date.today().isoformat()}_{commit}")
    if os.path.exists(out):
        shutil.rmtree(out)
    for d in ["screenshots/stock", "screenshots/fellow", "screenshots/reference", "screenshots/diff", "graphs", "temporal"]:
        os.makedirs(os.path.join(out, d), exist_ok=True)

    base_perf = load_json(os.path.join(RES, f"{a.baseline}_perf.json"))
    base_prof = load_json(os.path.join(RES, f"{a.baseline}_profile.json"))
    ab_perf = load_json(os.path.join(RES, f"{a.ab}_perf.json")) if a.ab else None
    ab_prof = load_json(os.path.join(RES, f"{a.ab}_profile.json")) if a.ab else None
    fl_perf = load_json(os.path.join(RES, f"{a.fluid}_perf.json")) if a.fluid else None

    hw = (ab_perf or base_perf or {}).get("hardware", {})
    with open(os.path.join(out, "hardware.json"), "w", encoding="utf-8") as f:
        json.dump(hw, f, indent=2)
    settings = {}
    for sess in [base_perf, ab_perf, fl_perf]:
        if sess:
            for k, e in sess["results"].items():
                if e.get("effective_settings"):
                    settings[f"{sess['session']}|{k}"] = {"preset": e.get("preset"), "fellow": e.get("fellow"), "fellow_state": e.get("fellow_state"), "effective": e.get("effective_settings")}
    with open(os.path.join(out, "settings.json"), "w", encoding="utf-8") as f:
        json.dump(settings, f, indent=1, default=str)

    base_rows = perf_rows(base_perf)
    ab_rows = perf_rows(ab_perf)
    fl_rows = perf_rows(fl_perf)
    prof_rows = perf_rows(base_prof) + perf_rows(ab_prof)
    allrows = [dict(r, session="baseline") for r in base_rows] + [dict(r, session="ab") for r in ab_rows] + [dict(r, session="fluid") for r in fl_rows]
    with open(os.path.join(out, "performance.json"), "w", encoding="utf-8") as f:
        json.dump({"baseline": base_rows, "ab": ab_rows, "fluid": fl_rows, "profile": prof_rows}, f, indent=1, default=str)
    keys = ["session", "scene", "resolution", "build", "fellow", "runs", "noisy", "gpu_ms", "gpu_ms_cv", "frame_ms", "frame_ms_std", "frame_ms_cv", "cpu_ms", "setup_ms", "p50", "p90", "p95", "p99", "p99_9",
            "gpu_p95", "gpu_p99", "fps", "fps_1pct_low", "draw_calls", "primitives", "objects", "stutters", "frames", "pipeline_compiles", "vram_mb", "tex_mb", "ram_mb", "load_ms", "gpu_clock", "gpu_temp"]
    with open(os.path.join(out, "performance.csv"), "w", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(f, fieldnames=keys, extrasaction="ignore")
        w.writeheader()
        for r in allrows:
            if r.get("valid"):
                w.writerow(r)

    g1 = plot_frame_distributions(base_perf, os.path.join(out, "graphs", "baseline_frametime_cdf.png"), "Stock Godot 4.7.2 frame-time distribution")
    g2 = plot_frame_distributions(ab_perf, os.path.join(out, "graphs", "ab_frametime_cdf.png"), f"Stock vs {a.ab_label} frame-time distribution") if ab_perf else None
    g3 = plot_gpu_passes(prof_rows, os.path.join(out, "graphs", "gpu_passes.png"), "Per-pass GPU time (profile runs)")

    notes = open(a.notes, encoding="utf-8").read() if a.notes and os.path.exists(a.notes) else ""

    # ---------- image sections
    sections = []
    uid = 0
    cap_base = os.path.join(RAW, f"{a.baseline}_capture", "stock")
    cap_ab_stock = os.path.join(RAW, f"{a.ab}_capture", "stock") if a.ab else None
    cap_ab_fellow = os.path.join(RAW, f"{a.ab}_capture", "fellow") if a.ab else None
    ref_dirs = [os.path.join(RAW, f"{a.baseline}_reference", "stock"), os.path.join(RAW, f"{a.baseline}_reference_ext", "stock")]
    for scene in SCENE_ORDER:
        pairs = []
        src_stock = cap_ab_stock if cap_ab_stock and os.path.isdir(os.path.join(cap_ab_stock, scene)) else cap_base
        for f in sorted(glob.glob(os.path.join(src_stock, scene, f"{scene}_{a.res}_*.png"))):
            name = os.path.basename(f)[len(f"{scene}_{a.res}_"):-4]
            if name.startswith("path"):
                continue
            b = os.path.join(cap_ab_fellow, scene, os.path.basename(f)) if cap_ab_fellow else None
            b = b if b and os.path.exists(b) else None
            ref = None
            for rd in ref_dirs:
                cand = os.path.join(rd, scene, f"{scene}_{a.res}_ref-cam-{name}.png")
                if os.path.exists(cand):
                    ref = cand
            diffp = None
            if b:
                diffp = os.path.join(out, "screenshots", "diff", f"{scene}_{name}_fulldiff.png")
                iu.save_rgb(diffp, iu.diff_heatmap(iu.load_rgb(f), iu.load_rgb(b), 4.0))
            pairs.append({"name": name, "a": f, "b": b, "ref": ref, "ref_label": "accumulated ground truth (2x2 SSAA + 8x MSAA + 16 jittered frames)", "diff": diffp,
                          "metrics": image_metrics(f, b, ref)})
        if pairs:
            body, uid = image_section(out, scene, pairs, uid)
            sections.append(f"<h3>{SCENE_TITLE[scene]}</h3>{body}")

    # Cycles reference for the PBR lab (linear EXR, identical display transform on both sides)
    cyc_html = ""
    exp_dir = os.path.join(RAW, f"{a.baseline}_pbrref_export")
    gd_dir = os.path.join(RAW, f"{a.baseline}_capture_pbrref", "stock", "pbr")
    cyc = sorted(glob.glob(os.path.join(exp_dir, "cycles_*.exr")))
    if cyc:
        rows = []
        for c in cyc:
            cam = os.path.basename(c)[len("cycles_"):-4]
            g = os.path.join(gd_dir, f"pbr_{a.res}_{cam}.exr")
            if not os.path.exists(g):
                continue
            hc = iu.load_exr(c)
            hg = iu.load_rgb(g)
            if hc.shape != hg.shape:
                continue
            dc = iu.tonemap_display(hc)
            dg = iu.tonemap_display(hg)
            pc = os.path.join(out, "screenshots", "reference", f"pbr_{cam}_cycles.png")
            pg = os.path.join(out, "screenshots", "stock", f"pbr_{cam}_godot_linear.png")
            pd = os.path.join(out, "screenshots", "diff", f"pbr_{cam}_vs_cycles.png")
            iu.save_rgb(pc, dc)
            iu.save_rgb(pg, dg)
            iu.save_rgb(pd, iu.diff_heatmap(dg, dc, 4.0))
            lum_ratio = float(np.median(iu.luma(hg)[iu.luma(hc) > 1e-3] / iu.luma(hc)[iu.luma(hc) > 1e-3]))
            uid += 1
            rows.append(f"""<div class="shot"><h4>{cam} <small>Godot stock (linear EXR, pre-tonemap) vs Cycles, same ACES-fit display transform</small></h4>
<div class="shotgrid"><div>{slider_html(os.path.relpath(pc, out).replace(os.sep, '/'), os.path.relpath(pg, out).replace(os.sep, '/'), 'Cycles', 'Godot stock', uid)}</div>
<div class="side"><figure><img src="{os.path.relpath(pd, out).replace(os.sep, '/')}"><figcaption>|Godot − Cycles| ×4</figcaption></figure>
<table class="metrics"><tr><td>PSNR / SSIM</td><td>{iu.psnr(dg, dc):.2f} dB / {iu.ssim(dg, dc):.4f}</td></tr><tr><td>median luminance ratio Godot/Cycles</td><td>{lum_ratio:.3f}</td></tr></table></div></div></div>""")
        if rows:
            cyc_html = "<h3>PBR Validation Lab vs Cycles path-traced reference</h3><p class='muted'>Reference lighting variant (uniform sky + sun only). Expected, non-bug differences: Cycles has multi-bounce GI and emissive light transport; Godot's preset uses only sky ambient + SSAO. See benchmarks/reference/pbr_lab_cycles.py.</p>" + "\n".join(rows)

    # Temporal results
    temporal_html = ""
    tjs = sorted(glob.glob(os.path.join(RAW, f"{a.ab}_temporal_analysis", "temporal_*.json"))) if a.ab else []
    if tjs:
        trows = []
        for tj in tjs:
            d = load_json(tj)
            seq = d["sequence"]
            for ext in [".png", ".webp"]:
                src = tj.replace(".json", ext)
                if os.path.exists(src):
                    shutil.copy(src, os.path.join(out, "temporal", os.path.basename(src)))
            res_rows = "".join(f"<tr><td>{html.escape(lab)}</td>" + "".join(f"<td>{fmt(v['aggregate'].get(k), 4)}</td>" for k in ["err_motion", "err_static", "flicker", "psnr", "ssim_every4"]) + "</tr>" for lab, v in d["results"].items())
            trows.append(f"""<h4>{html.escape(seq)}</h4><table><tr><th>build</th><th>error in motion regions</th><th>error static</th><th>flicker vs GT</th><th>PSNR</th><th>SSIM</th></tr>{res_rows}</table>
<p><img class="graph" src="temporal/{os.path.basename(tj).replace('.json', '.png')}"></p>
{'<p><img class="graph" src="temporal/' + os.path.basename(tj).replace('.json', '.webp') + '"><br><span class="muted">stock | fellow | ground truth | stock error | fellow error</span></p>' if os.path.exists(tj.replace('.json', '.webp')) else ''}""")
        temporal_html = "<h2>Temporal stability (sequences vs accumulated ground truth)</h2><p class='muted'>Lower is better for all error columns. Errors are mean absolute luma differences (0..1 display scale).</p>" + "\n".join(trows)

    fluid_html = ""
    if fl_perf:
        fr = []
        for k, e in fl_perf["results"].items():
            jruns = [load_json(os.path.join(ROOT, r["_json"])) for r in e["runs"] if r.get("valid")]
            ex = [j.get("extra_metrics", {}) for j in jruns if j]
            sim = np.mean([x["fluid_sim_gpu_ms"]["mean"] for x in ex if x.get("fluid_sim_gpu_ms", {}).get("n")]) if ex else float("nan")
            surf = np.mean([x["fluid_surface_gpu_ms"]["mean"] for x in ex if x.get("fluid_surface_gpu_ms", {}).get("n")]) if ex else float("nan")
            info = jruns[0]["scene_desc"]["info"] if jruns else {}
            agg = e.get("aggregate", {})
            fr.append((info.get("simulation_resolution", "?"), info.get("water_visible"), e["resolution"], sim, surf, agg.get("gpu_ms_run_means", {}).get("mean"), agg.get("frame_ms_run_means", {}).get("mean"), info.get("simulation_vram_bytes", 0) / 2**20, agg.get("runs_valid", 0)))
        fr.sort(key=lambda t: (t[2], int(t[0]) if str(t[0]).isdigit() else 0, not t[1]))
        body = "".join(f"<tr><td>{t[0]}²</td><td>{'yes' if t[1] else 'no'}</td><td>{t[2]}</td><td>{fmt(t[3])}</td><td>{fmt(t[4])}</td><td>{fmt(t[5])}</td><td>{fmt(t[6])}</td><td>{fmt(t[7], 1)}</td><td>{t[8]}</td></tr>" for t in fr)
        fluid_html = f"""<h2>Fluid Lab (FellowEngine GPU shallow water)</h2>
<p class="muted">Simulation and surface-generation GPU times come from RenderingDevice timestamps inside the module; the viewport GPU time excludes them.
Water rendering cost = viewport GPU time with water visible − with water hidden (simulation running in both).</p>
<div class="tablewrap"><table><tr><th>Grid</th><th>Water drawn</th><th>Res</th><th>Sim GPU ms</th><th>Surface GPU ms</th><th>Viewport GPU ms</th><th>Frame ms</th><th>Sim VRAM MB</th><th>Runs</th></tr>{body}</table></div>"""

    hw_rows = ""
    try:
        cpu = json.loads(hw.get("cpu", "{}")) if isinstance(hw.get("cpu"), str) else hw.get("cpu", {})
    except Exception:  # noqa: BLE001
        cpu = {}
    gpu = (hw.get("gpu") or [{}])[0]
    hw_items = {
        "CPU": cpu.get("Name", "?") if isinstance(cpu, dict) else str(cpu),
        "GPU": f"{gpu.get('name', '?')} ({gpu.get('memory.total', '?')} MiB, driver {gpu.get('driver_version', '?')}, power limit {gpu.get('power.limit', '?')} W)",
        "OS": hw.get("os_caption", ""), "Display": hw.get("display", ""), "Power": hw.get("power", ""),
        "Engine commit": f"{hw.get('engine', {}).get('describe', '')} on {hw.get('engine', {}).get('branch', '')}",
        "Compiler": json.dumps(hw.get("compiler", {})), "Build flags": hw.get("build_flags", ""),
    }
    hw_rows = "".join(f"<tr><td>{html.escape(k)}</td><td style='text-align:left;white-space:normal'>{html.escape(str(v))}</td></tr>" for k, v in hw_items.items())

    def md_to_html(md):
        """Tiny markdown subset for the embedded analysis notes: headings, bullets, paragraphs, bold, code."""
        import re
        outl = []
        in_list = False
        for line in md.splitlines():
            s_ = html.escape(line)
            s_ = re.sub(r"\*\*(.+?)\*\*", r"<b>\1</b>", s_)
            s_ = re.sub(r"`(.+?)`", r"<code>\1</code>", s_)
            is_item = line.startswith("- ")
            if in_list and not is_item:
                outl.append("</ul>")
                in_list = False
            if is_item:
                if not in_list:
                    outl.append("<ul>")
                    in_list = True
                outl.append(f"<li>{s_[2:]}</li>")
            elif line.startswith("### "):
                outl.append(f"<h3>{s_[4:]}</h3>")
            elif line.startswith("## "):
                outl.append(f"<h2>{s_[3:]}</h2>")
            elif line.startswith("|"):
                outl.append(f"<pre>{s_}</pre>")
            elif line.strip():
                outl.append(f"<p>{s_}</p>")
        if in_list:
            outl.append("</ul>")
        return "\n".join(outl)

    page = f"""<!doctype html><html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1">
<title>Stock vs FellowEngine</title><style>{CSS}</style></head><body><main>
<h1>Stock Godot 4.7.2 vs FellowEngine</h1>
<p class="muted">Generated {datetime.datetime.now().strftime('%Y-%m-%d %H:%M')} · commit {html.escape(commit)} · baseline <code>{html.escape(a.baseline)}</code>{' · A/B <code>' + html.escape(a.ab) + '</code>' if a.ab else ''}</p>
{('<div class="card">' + md_to_html(notes) + '</div>') if notes else ''}
<h2>Summary: stock vs {html.escape(a.ab_label)} (interleaved ABBA runs)</h2>
<div class="tablewrap">{perf_table_html(ab_rows) if ab_rows else '<p>No A/B session.</p>'}</div>
{f'<p><img class="graph" src="graphs/{os.path.basename(g2)}"></p>' if g2 else ''}
<h2>Immutable stock baseline (Godot 4.7.2)</h2>
<div class="tablewrap">{perf_table_html(base_rows, compare=False)}</div>
{f'<p><img class="graph" src="graphs/{os.path.basename(g1)}"></p>' if g1 else ''}
{f'<h3>Per-pass GPU timings</h3><p class="muted">Profile runs with engine timestamps enabled (timestamps serialize the render graph; totals are therefore slightly higher than normal runs).</p><p><img class="graph" src="graphs/{os.path.basename(g3)}"></p>' if g3 else ''}
<h2>Hardware &amp; configuration</h2><table>{hw_rows}</table>
<p class="muted">Full hardware record: <a href="hardware.json">hardware.json</a> · every applied renderer setting: <a href="settings.json">settings.json</a> · raw numbers: <a href="performance.csv">performance.csv</a>, <a href="performance.json">performance.json</a></p>
{fluid_html}
{temporal_html}
<h2>Visual comparison ({html.escape(a.res)})</h2>
<p class="muted">Drag the slider: left = FellowEngine, right = Godot stock. Diff heatmaps show |stock − fellow| luma ×4 (black = identical). PNG captures are lossless; shown downscaled.</p>
{cyc_html}
{''.join(sections)}
</main><script>{JS}</script></body></html>"""
    with open(os.path.join(out, "index.html"), "w", encoding="utf-8") as f:
        f.write(page)

    md = [f"# Stock Godot 4.7.2 vs FellowEngine — report {datetime.date.today().isoformat()} ({commit})", ""]
    if notes:
        md += [notes, ""]
    md += ["## Stock vs FellowEngine (A/B)", "", perf_table_md(ab_rows) if ab_rows else "_no A/B session_", "", "## Immutable stock baseline", "", perf_table_md(base_rows), ""]
    if fl_rows:
        md += ["## Fluid Lab", "", "See index.html (fluid section).", ""]
    md += ["Hardware: see hardware.json. Settings: see settings.json.", ""]
    with open(os.path.join(out, "summary.md"), "w", encoding="utf-8") as f:
        f.write("\n".join(md))
    latest = os.path.join(ROOT, "reports", "latest")
    if os.path.exists(latest):
        shutil.rmtree(latest)
    shutil.copytree(out, latest)
    print("report:", out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
