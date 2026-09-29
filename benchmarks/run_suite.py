#!/usr/bin/env python3
"""Runs a complete, named benchmark plan (sequence of run_benchmarks.py sessions + post-processing).

  python benchmarks/run_suite.py baseline --build stock --tag godot-4.7.2
  python benchmarks/run_suite.py ab --builds stock,fellow --fellow "postfx/taa_v2=true" --tag taa_v2

Plans
  baseline : perf (all scenes, 1080p + 1440p, >=3 runs) -> profile (per-pass GPU timings + per-camera, 1 run)
             -> captures (+ HDR EXR) -> PBR lab reference-lighting captures + Cycles export -> temporal sequences
             -> accumulated ground-truth references for the temporal sequences.
  ab       : same perf / capture / temporal steps for several builds (interleaved), no references (reuse baseline).
Session names are <tag>_<step>; everything lands in benchmarks/results/ (aggregates) and results/raw/ (raw).
"""

import argparse
import datetime
import json
import os
import subprocess
import sys
import time

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
PY = sys.executable
RUNNER = os.path.join(ROOT, "benchmarks", "run_benchmarks.py")
TOOLS = os.path.join(ROOT, "benchmarks", "tools")
SCENES = "pbr,sponza,bistro_exterior,bistro_interior"
PBR_SEQS = "moving_object,camera_pan,foliage_strafe,specular_orbit,thin_dolly"


def log(m):
    print(f"[suite {datetime.datetime.now().strftime('%H:%M:%S')}] {m}", flush=True)


def runner(*args) -> int:
    cmd = [PY, "-u", RUNNER, *args]
    log(" ".join(cmd[2:]))
    return subprocess.call(cmd, cwd=ROOT)


def label(spec: str) -> str:
    """Build label of a run_benchmarks.py build spec 'label[=dir][@toggles]'."""
    return spec.split("@", 1)[0].split("=", 1)[0].strip()


def tool(name, *args) -> int:
    cmd = [PY, "-u", os.path.join(TOOLS, name), *args]
    log(" ".join(cmd[2:]))
    return subprocess.call(cmd, cwd=ROOT)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("plan", choices=["baseline", "ab", "perf", "visual"])
    ap.add_argument("--build", default="stock")
    ap.add_argument("--builds", default=None)
    ap.add_argument("--fellow", default="")
    ap.add_argument("--tag", required=True)
    ap.add_argument("--scenes", default=SCENES)
    ap.add_argument("--res", default="1920x1080,2560x1440")
    ap.add_argument("--runs", default="3")
    ap.add_argument("--skip", default="", help="comma list of steps to skip")
    ap.add_argument("--baseline-tag", default="godot-4.7.2", help="ab plan: session tag holding the accumulated references")
    ap.add_argument("--seqs", default="pbr:moving_object,pbr:camera_pan,pbr:foliage_strafe,pbr:specular_orbit,pbr:thin_dolly,sponza:sponza_pan,bistro_exterior:bistro_ext_path,bistro_interior:bistro_int_path")
    a = ap.parse_args()
    builds = a.builds or a.build
    skip = set(a.skip.split(","))
    common = ["--builds", builds]
    if a.fellow:
        common += ["--fellow", a.fellow]
    t0 = time.time()
    steps = []
    if a.plan in ("baseline", "ab", "perf") and "perf" not in skip:
        steps.append(("perf", ["--scene", a.scenes, "--mode", "perf", "--res", a.res, "--runs", a.runs, "--session", f"{a.tag}_perf"]))
    if a.plan in ("baseline", "ab") and "profile" not in skip:
        steps.append(("profile", ["--scene", a.scenes, "--mode", "perf", "--res", a.res, "--runs", "1", "--max-runs", "1", "--profile", "--percam", "300", "--warmup", "5", "--measure", "10", "--heat-soak", "60", "--session", f"{a.tag}_profile"]))
    if a.plan in ("baseline", "ab", "visual") and "capture" not in skip:
        steps.append(("capture", ["--scene", a.scenes, "--mode", "capture", "--res", a.res, "--extra=--hdr=1", "--session", f"{a.tag}_capture"]))
        steps.append(("capture_pbrref", ["--scene", "pbr", "--mode", "capture", "--res", "1920x1080", "--extra=--hdr=1 --lighting=ref --export-ref=" + os.path.join(ROOT, "benchmarks", "results", "raw", f"{a.tag}_pbrref_export"), "--session", f"{a.tag}_capture_pbrref"]))
    if a.plan in ("baseline", "ab", "visual") and "temporal" not in skip:
        steps.append(("temporal", ["--scene", a.scenes, "--mode", "temporal", "--res", "1920x1080", "--session", f"{a.tag}_temporal"]))
    if a.plan == "baseline" and "reference" not in skip:
        steps.append(("reference_pbr", ["--scene", "pbr", "--mode", "reference", "--res", "1920x1080", "--preset", "reference_ssaa", "--timeout", "3600",
                                        "--extra=" + f"--seqs={PBR_SEQS} --jitter=16", "--session", f"{a.tag}_reference"]))
        steps.append(("reference_ext", ["--scene", "sponza,bistro_exterior,bistro_interior", "--mode", "reference", "--res", "1920x1080", "--preset", "reference_ssaa", "--timeout", "3600",
                                        "--extra=--seqs=sponza_pan,bistro_ext_path,bistro_int_path --jitter=8", "--session", f"{a.tag}_reference_ext"]))
    status = {}
    for name, args in steps:
        rc = runner(*common, *args) if not name.startswith("reference") else runner("--builds", builds.split(",")[0], *args)
        status[name] = rc
        if name.startswith("reference") and rc == 0:
            for b in [label(x) for x in builds.split(",")[:1]]:
                sess = args[args.index("--session") + 1]
                base = os.path.join(ROOT, "benchmarks", "results", "raw", sess, b)
                for sc in os.listdir(base) if os.path.isdir(base) else []:
                    tool("accumulate.py", os.path.join(base, sc), "--delete-inputs")
    if a.plan == "ab" and "analysis" not in skip and len(builds.split(",")) >= 2:
        b0, b1 = [label(x) for x in builds.split(",")[:2]]
        out = os.path.join(ROOT, "benchmarks", "results", "raw", f"{a.tag}_temporal_analysis")
        for item in a.seqs.split(","):
            sc, seq = item.split(":")
            ref = None
            for rs in [f"{a.baseline_tag}_reference", f"{a.baseline_tag}_reference_ext"]:
                cand = os.path.join(ROOT, "benchmarks", "results", "raw", rs, "stock", sc)
                if os.path.isdir(cand):
                    ref = cand
            t0d = os.path.join(ROOT, "benchmarks", "results", "raw", f"{a.tag}_temporal", b0, sc)
            t1d = os.path.join(ROOT, "benchmarks", "results", "raw", f"{a.tag}_temporal", b1, sc)
            if ref and os.path.isdir(t0d) and os.path.isdir(t1d):
                status[f"analysis_{seq}"] = tool("temporal.py", "--test", t0d, "--test2", t1d, "--labels", f"{b0},{b1}", "--ref", ref, "--seq", seq, "--out", out, "--anim")
    log(f"plan {a.plan} finished in {(time.time() - t0) / 60:.1f} min: {json.dumps(status)}")
    with open(os.path.join(ROOT, "benchmarks", "results", f"{a.tag}_suite.json"), "w", encoding="utf-8") as f:
        json.dump({"plan": a.plan, "tag": a.tag, "builds": builds, "fellow": a.fellow, "status": status, "minutes": (time.time() - t0) / 60,
                   "finished": datetime.datetime.now().isoformat()}, f, indent=1)
    return 0 if all(v == 0 for v in status.values()) else 1


if __name__ == "__main__":
    sys.exit(main())
