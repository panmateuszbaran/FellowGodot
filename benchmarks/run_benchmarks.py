#!/usr/bin/env python3
"""FellowEngine benchmark runner (stock Godot vs FellowEngine).

Examples
    python benchmarks/run_benchmarks.py --builds stock --scene all --mode perf
    python benchmarks/run_benchmarks.py --builds stock,fellow --scene pbr --mode perf --runs 5
    python benchmarks/run_benchmarks.py --builds stock,fellow --scene sponza --mode capture --res 1920x1080
    python benchmarks/run_benchmarks.py --builds fellow --fellow "postfx/taa_v2=true" --scene pbr --mode temporal

Methodology (see benchmarks/README.md):
  * each (scene, resolution, build) is run >= --runs times as a fresh process; runs of different builds are
    interleaved ABBA to cancel thermal / clock drift on the laptop;
  * inside a run: shader/pipeline warmup (all fixed cameras + >= 1 full flythrough pass, >= --warmup s),
    then whole flythrough passes for >= --measure s; the sequence is frame-locked (--fixed-fps 60) so every
    build renders exactly the same frames;
  * if the coefficient of variation of the per-run mean frame time exceeds --max-cv, extra runs are added
    (up to --max-runs) and the result is flagged NOISY if it stays above the threshold;
  * VSync off, no FPS cap, native resolution (render scale 1.0) unless a preset explicitly changes it;
  * hardware, driver, engine commit, build flags and every applied setting are written next to the results.
"""

from __future__ import annotations

import argparse
import csv
import datetime
import hashlib
import json
import math
import os
import platform
import shutil
import statistics
import subprocess
import sys
import threading
import time

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
PROJECT = os.path.join(ROOT, "benchmarks", "project")
RESULTS = os.path.join(ROOT, "benchmarks", "results")
BIN = os.path.join(ROOT, ".tools", "bin")
SCENES = ["pbr", "sponza", "bistro_exterior", "bistro_interior", "fluid"]
BUILD_FLAGS = "platform=windows target=template_release production=yes disable_path_overrides=no winrt=no accesskit=no d3d12=no (MSVC, see BUILDING_FELLOWENGINE.md)"

try:
    import psutil  # type: ignore
except ImportError:  # pragma: no cover
    psutil = None


# Windows: never let a child process open its own console window. When the runner itself has no console (started
# detached, e.g. from a scheduled job), every console child (nvidia-smi once per second, the Godot console wrapper)
# would otherwise get a new window that takes focus from the benchmark window: measured as ~480 ms stalls every
# ~1.03 s in Sponza (session godot-4.7.2_perf re-run of 2026-09-30, discarded).
NO_WINDOW = {"creationflags": 0x08000000} if os.name == "nt" else {}  # CREATE_NO_WINDOW


def log(msg: str) -> None:
    print(f"[bench {datetime.datetime.now().strftime('%H:%M:%S')}] {msg}", flush=True)


# --------------------------------------------------------------------------- hardware record

def _run(cmd, timeout=30) -> str:
    try:
        p = subprocess.run(cmd, shell=isinstance(cmd, str), capture_output=True, text=True, errors="replace", timeout=timeout, **NO_WINDOW)
        return (p.stdout or "").strip()
    except Exception as e:  # noqa: BLE001
        return f"ERROR: {e}"


def _ps(expr: str) -> str:
    return _run(["powershell", "-NoProfile", "-Command", expr])


def nvidia_query(fields: str) -> list[dict]:
    out = _run(["nvidia-smi", f"--query-gpu={fields}", "--format=csv,noheader,nounits"])
    rows = []
    keys = fields.split(",")
    for line in out.splitlines():
        vals = [v.strip() for v in line.split(",")]
        if len(vals) == len(keys):
            rows.append(dict(zip(keys, vals)))
    return rows


def git(*a) -> str:
    return _run(["git", "-C", ROOT, *a])


def sha256(path: str) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 22), b""):
            h.update(chunk)
    return h.hexdigest()


def hardware_record(exes: dict) -> dict:
    rec = {
        "timestamp_local": datetime.datetime.now().isoformat(),
        "host": platform.node(),
        "python": sys.version.split()[0],
        "os": {"platform": platform.platform(), "release": platform.release(), "version": platform.version()},
    }
    if os.name == "nt":
        rec["cpu"] = _ps("Get-CimInstance Win32_Processor | Select-Object Name,NumberOfCores,NumberOfLogicalProcessors,MaxClockSpeed | ConvertTo-Json -Compress")
        rec["ram"] = _ps("$c=Get-CimInstance Win32_ComputerSystem; $m=Get-CimInstance Win32_PhysicalMemory; [pscustomobject]@{TotalBytes=$c.TotalPhysicalMemory; Modules=@($m | Select-Object Capacity,Speed,ConfiguredClockSpeed,Manufacturer,PartNumber)} | ConvertTo-Json -Compress -Depth 3")
        rec["os_caption"] = _ps("(Get-CimInstance Win32_OperatingSystem | Select-Object Caption,Version,BuildNumber | ConvertTo-Json -Compress)")
        rec["system"] = _ps("Get-CimInstance Win32_ComputerSystem | Select-Object Manufacturer,Model | ConvertTo-Json -Compress")
        rec["power"] = _ps("$b=Get-CimInstance -Namespace root/wmi -ClassName BatteryStatus -ErrorAction SilentlyContinue | Select-Object -First 1 PowerOnline,Charging; [pscustomobject]@{Battery=$b; Scheme=(powercfg /getactivescheme)} | ConvertTo-Json -Compress")
        rec["display"] = _ps("Get-CimInstance Win32_VideoController | Select-Object Name,DriverVersion,CurrentHorizontalResolution,CurrentVerticalResolution,CurrentRefreshRate | ConvertTo-Json -Compress")
    rec["gpu"] = nvidia_query("name,driver_version,memory.total,pcie.link.gen.max,pcie.link.width.max,clocks.max.graphics,clocks.max.memory,power.limit,vbios_version")
    rec["engine"] = {
        "commit": git("rev-parse", "HEAD"),
        "describe": git("describe", "--tags", "--always", "--dirty"),
        "branch": git("rev-parse", "--abbrev-ref", "HEAD"),
        "status_short": git("status", "--short", "--untracked-files=no")[:4000],
    }
    rec["build_flags"] = BUILD_FLAGS
    vs = _run([os.path.expandvars(r"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"), "-latest", "-products", "*", "-property", "catalog_productDisplayVersion"]) if os.name == "nt" else ""
    msvc_dirs = []
    try:
        base = r"C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\VC\Tools\MSVC"
        msvc_dirs = os.listdir(base) if os.path.isdir(base) else []
    except OSError:
        pass
    rec["compiler"] = {"visual_studio": vs, "msvc_toolsets": msvc_dirs}
    rec["binaries"] = {}
    for name, exe in exes.items():
        real = exe.replace(".console.exe", ".exe")
        info = {"path": os.path.relpath(real, ROOT)}
        if os.path.exists(real):
            info.update({"size": os.path.getsize(real), "sha256": sha256(real), "mtime": datetime.datetime.fromtimestamp(os.path.getmtime(real)).isoformat()})
        meta = os.path.join(os.path.dirname(real), "BUILD_INFO.json")
        if os.path.exists(meta):
            with open(meta, encoding="utf-8") as f:
                info["build_info"] = json.load(f)
        rec["binaries"][name] = info
    return rec


# --------------------------------------------------------------------------- GPU telemetry

class GpuSampler(threading.Thread):
    """Samples clocks / temperature / power once per second (detects thermal or power throttling)."""

    FIELDS = "clocks.gr,clocks.mem,temperature.gpu,power.draw,utilization.gpu,memory.used,pstate,clocks_throttle_reasons.active"

    def __init__(self):
        super().__init__(daemon=True)
        self.samples: list[dict] = []
        self._stop = threading.Event()

    def run(self):
        while not self._stop.is_set():
            rows = nvidia_query(self.FIELDS)
            if rows:
                rows[0]["t"] = time.time()
                self.samples.append(rows[0])
            self._stop.wait(1.0)

    def stop(self):
        self._stop.set()

    def summary(self) -> dict:
        def num(k):
            vals = []
            for s in self.samples:
                try:
                    vals.append(float(s[k]))
                except (KeyError, ValueError):
                    pass
            return vals
        out = {"n": len(self.samples)}
        for k in ["clocks.gr", "clocks.mem", "temperature.gpu", "power.draw", "utilization.gpu", "memory.used"]:
            v = num(k)
            if v:
                out[k] = {"min": min(v), "mean": sum(v) / len(v), "max": max(v)}
        reasons = [s.get("clocks_throttle_reasons.active", "") for s in self.samples]
        out["throttle_reasons_seen"] = sorted(set(r for r in reasons if r and r not in ("0x0000000000000000", "[N/A]")))
        return out


# --------------------------------------------------------------------------- process control

def kill_tree(pid: int) -> None:
    if os.name == "nt":
        subprocess.run(["taskkill", "/PID", str(pid), "/T", "/F"], capture_output=True, **NO_WINDOW)
    elif psutil:
        try:
            p = psutil.Process(pid)
            for c in p.children(recursive=True):
                c.kill()
            p.kill()
        except psutil.Error:
            pass


def run_godot(exe: str, godot_args: list[str], user_args: list[str], timeout: float, log_path: str) -> dict:
    cmd = [exe, "--path", PROJECT, *godot_args, "--", *user_args]
    t0 = time.time()
    with open(log_path, "w", encoding="utf-8", errors="replace") as lf:
        lf.write(" ".join(f'"{c}"' if " " in c else c for c in cmd) + "\n\n")
        lf.flush()
        p = subprocess.Popen(cmd, stdout=lf, stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL, cwd=ROOT, **NO_WINDOW)
        peak_wset = 0
        peak_private = 0
        timed_out = False
        while True:
            rc = p.poll()
            if psutil:
                try:
                    procs = [psutil.Process(p.pid)] + psutil.Process(p.pid).children(recursive=True)
                    for pr in procs:
                        mi = pr.memory_info()
                        peak_wset = max(peak_wset, getattr(mi, "peak_wset", mi.rss))
                        peak_private = max(peak_private, getattr(mi, "private", 0))
                except psutil.Error:
                    pass
            if rc is not None:
                break
            if time.time() - t0 > timeout:
                timed_out = True
                kill_tree(p.pid)
                p.wait(10)
                break
            time.sleep(0.25)
    return {"returncode": p.returncode, "timed_out": timed_out, "wall_s": time.time() - t0, "peak_working_set": peak_wset, "peak_private_bytes": peak_private}


# --------------------------------------------------------------------------- statistics

def pct(sorted_vals, p):
    if not sorted_vals:
        return float("nan")
    k = (len(sorted_vals) - 1) * p / 100.0
    lo = math.floor(k)
    hi = min(lo + 1, len(sorted_vals) - 1)
    return sorted_vals[lo] + (sorted_vals[hi] - sorted_vals[lo]) * (k - lo)


def mean_std_cv(vals):
    vals = [v for v in vals if v is not None and not (isinstance(v, float) and math.isnan(v))]
    if not vals:
        return {"n": 0}
    m = statistics.fmean(vals)
    s = statistics.stdev(vals) if len(vals) > 1 else 0.0
    return {"n": len(vals), "mean": m, "std": s, "cv": (s / m) if m else 0.0, "min": min(vals), "max": max(vals)}


def aggregate(runs: list[dict]) -> dict:
    """Aggregate valid perf runs of one configuration."""
    valid = [r for r in runs if r.get("valid") and "summary" in r]
    ok = [r for r in valid if not r.get("hitch_frames")]
    out = {"runs_total": len(runs), "runs_valid": len(ok), "runs_excluded_hitches": len(valid) - len(ok)}
    if not ok and valid:
        # No hitch-free run at all: aggregate what exists, but say so.
        ok = valid
        out["runs_valid"] = len(ok)
        out["hitches_not_excluded"] = True
    if not ok:
        return out
    for field in ["frame_ms", "gpu_ms", "render_cpu_ms", "setup_cpu_ms", "process_ms", "draw_calls", "primitives", "objects", "shadow_draw_calls"]:
        out[field + "_run_means"] = mean_std_cv([r["summary"][field]["mean"] for r in ok if r["summary"][field].get("n")])
        pooled = sorted(v for r in ok for v in r["frames"][field])
        out[field + "_pooled"] = {p: pct(pooled, float(p[1:].replace("_", "."))) for p in ["p50", "p90", "p95", "p99", "p99_9"]}
        out[field + "_pooled"]["mean"] = statistics.fmean(pooled) if pooled else float("nan")
        out[field + "_pooled"]["max"] = pooled[-1] if pooled else float("nan")
    out["avg_fps_run"] = mean_std_cv([r["summary"]["avg_fps"] for r in ok])
    pooled_ft = sorted(v for r in ok for v in r["frames"]["frame_ms"])
    k = max(1, math.ceil(len(pooled_ft) * 0.01))
    out["fps_1pct_low_pooled"] = 1000.0 * k / sum(pooled_ft[-k:]) if pooled_ft else float("nan")
    out["stutter_frames_total"] = sum(r["summary"].get("stutter_frames", 0) for r in ok)
    out["frames_total"] = len(pooled_ft)
    out["pipeline_compilations_during_measurement"] = {
        kk: sum(r["measurement"]["pipeline_compilations"].get(kk, 0) for r in ok) for kk in ok[0]["measurement"]["pipeline_compilations"].keys()
    }
    out["video_mem_peak_bytes"] = max(r["memory_peak"]["video_mem_used"] for r in ok)
    out["texture_mem_peak_bytes"] = max(r["memory_peak"]["texture_mem_used"] for r in ok)
    out["buffer_mem_peak_bytes"] = max(r["memory_peak"]["buffer_mem_used"] for r in ok)
    out["process_peak_working_set_bytes"] = max(r.get("_proc", {}).get("peak_working_set", 0) for r in ok)
    out["load_time_ms"] = mean_std_cv([r["load_time_ms"] for r in ok])
    if any("gpu_passes_ms" in r for r in ok):
        names = sorted({n for r in ok for n in r.get("gpu_passes_ms", {})})
        out["gpu_passes_ms"] = {n: statistics.fmean([r["gpu_passes_ms"].get(n, 0.0) for r in ok if "gpu_passes_ms" in r]) for n in names}
    if any("per_camera" in r for r in ok):
        cams = sorted({c for r in ok for c in r.get("per_camera", {})})
        pc = {}
        for c in cams:
            rr = [r["per_camera"][c] for r in ok if c in r.get("per_camera", {})]
            pc[c] = {
                "gpu_ms": mean_std_cv([x["summary"]["gpu_ms"]["mean"] for x in rr]),
                "frame_ms": mean_std_cv([x["summary"]["frame_ms"]["mean"] for x in rr]),
                "render_cpu_ms": mean_std_cv([x["summary"]["render_cpu_ms"]["mean"] for x in rr]),
                "draw_calls": mean_std_cv([x["summary"]["draw_calls"]["mean"] for x in rr]),
                "primitives": mean_std_cv([x["summary"]["primitives"]["mean"] for x in rr]),
            }
            if any("gpu_passes_ms" in x for x in rr):
                names = sorted({n for x in rr for n in x.get("gpu_passes_ms", {})})
                pc[c]["gpu_passes_ms"] = {n: statistics.fmean([x["gpu_passes_ms"].get(n, 0.0) for x in rr if "gpu_passes_ms" in x]) for n in names}
        out["per_camera"] = pc
    return out


# --------------------------------------------------------------------------- main

def parse_build(spec: str) -> tuple[str, str, str, list[str]]:
    """'label[=dir][@k=v;k=v][#--arg=v --arg2=v]' -> (label, dir under .tools/bin, fellow toggles 'k=v,k=v', user args).

    Variants of one binary can be compared in the same interleaved session, e.g.
    --builds "stock,fellow_off=fellow@enabled=false,fellow_v2=fellow@postfx/taa_resolve_v2=true"
    --builds "f256=fellow#--fluid_res=256,f256_hidden=fellow#--fluid_res=256 --water_visible=0"."""
    extra = []
    if "#" in spec:
        spec, e = spec.split("#", 1)
        extra = e.split()
    toggles = ""
    if "@" in spec:
        spec, toggles = spec.split("@", 1)
    label, dirname = spec.split("=", 1) if "=" in spec else (spec, spec)
    return label.strip(), dirname.strip(), toggles.replace(";", ",").strip(), extra


def exe_for(build: str, godot_path: str | None) -> str:
    if godot_path:
        return godot_path
    return os.path.join(BIN, build, "godot.windows.template_release.x86_64.console.exe")


def keep_awake() -> None:
    """Ask Windows to keep the system and the display on while this process runs.

    A sleep in the middle of a session invalidates it, and with the display off (and the session locked after it)
    presentation stalls ~490 ms every ~1.03 s (measured in the milestone-1 and milestone-2 night runs).
    SetThreadExecutionState(ES_CONTINUOUS | ES_SYSTEM_REQUIRED | ES_DISPLAY_REQUIRED) is a per-process request,
    released automatically when the process exits; no power setting is changed. No-op on other platforms."""
    if os.name != "nt":
        return
    try:
        import ctypes
        ctypes.windll.kernel32.SetThreadExecutionState(0x80000000 | 0x00000001 | 0x00000002)
        log("keep-awake requested for this session (released when the runner exits)")
    except Exception as e:  # noqa: BLE001
        log(f"keep-awake not available: {e}")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--builds", default="stock", help="comma list of builds under .tools/bin/<build>/ (e.g. stock,fellow); "
                    "variants: label=dir@k=v;k=v (FellowEngine toggles for that variant)")
    ap.add_argument("--godot", default=None, help="explicit Godot executable (single build)")
    ap.add_argument("--scene", default="pbr", help="scene name, comma list, or 'all'")
    ap.add_argument("--mode", default="perf", choices=["perf", "capture", "temporal", "reference"])
    ap.add_argument("--res", default="1920x1080,2560x1440")
    ap.add_argument("--runs", type=int, default=3)
    ap.add_argument("--max-runs", type=int, default=6)
    ap.add_argument("--max-cv", type=float, default=0.03)
    ap.add_argument("--warmup", type=float, default=10.0)
    ap.add_argument("--measure", type=float, default=30.0)
    ap.add_argument("--percam", type=int, default=0, help="perf: also measure each fixed camera statically for N frames")
    ap.add_argument("--profile", action="store_true", help="perf: per-pass GPU timestamps (--gpu-profile; separate runs)")
    ap.add_argument("--preset", default="high")
    ap.add_argument("--set", default="", help="preset overrides k=v,k=v")
    ap.add_argument("--fellow", default="", help="FellowEngine toggles k=v,k=v (rendering/fellow/<k>)")
    ap.add_argument("--fellow-off-for", default="stock", help="builds that never receive --fellow toggles")
    ap.add_argument("--extra", default="", help="extra user args passed verbatim, space separated (e.g. --lighting=ref)")
    ap.add_argument("--cooldown", type=float, default=5.0)
    ap.add_argument("--heat-soak", type=float, default=240.0, help="perf: seconds of discarded GPU load before the first measured run (thermal steady state)")
    ap.add_argument("--timeout", type=float, default=900.0)
    ap.add_argument("--session", default=None, help="session name (default: timestamp)")
    ap.add_argument("--label", default="", help="free text label for the session")
    ap.add_argument("--no-telemetry", action="store_true", help="do not sample nvidia-smi during the session (diagnostics)")
    ap.add_argument("--merge", action="store_true", help="merge into an existing results/<session>.json: re-measured "
                    "(scene, res, build) entries replace the old ones, which are kept under 'superseded' with --supersede-note")
    ap.add_argument("--supersede-note", default="", help="reason recorded for entries replaced by --merge")
    args = ap.parse_args()

    specs = {}
    for spec in [x.strip() for x in args.builds.split(",") if x.strip()]:
        label, dirname, toggles, vextra = parse_build(spec)
        specs[label] = (dirname, toggles, vextra)
    builds = list(specs.keys())
    exes = {b: exe_for(specs[b][0], args.godot) for b in builds}
    off_for = args.fellow_off_for.split(",")

    def toggles_for(b: str) -> str:
        parts = []
        if args.fellow and b not in off_for:
            parts.append(args.fellow)
        if specs[b][1]:
            parts.append(specs[b][1])
        return ",".join(parts)
    for b, e in exes.items():
        if not os.path.exists(e):
            log(f"missing executable for build '{b}': {e}")
            return 2
    scenes = SCENES if args.scene == "all" else [s.strip() for s in args.scene.split(",")]
    resolutions = [r.strip() for r in args.res.split(",") if r.strip()]
    session = args.session or datetime.datetime.now().strftime("%Y%m%d_%H%M%S") + f"_{args.mode}"
    raw_dir = os.path.join(RESULTS, "raw", session)
    os.makedirs(raw_dir, exist_ok=True)

    hw = hardware_record(exes)
    with open(os.path.join(raw_dir, "hardware.json"), "w", encoding="utf-8") as f:
        json.dump(hw, f, indent=2)
    log(f"session {session}: builds={builds} scenes={scenes} res={resolutions} mode={args.mode}")
    keep_awake()

    sampler = GpuSampler()
    if not args.no_telemetry:
        sampler.start()
    all_results: dict = {}
    soak = {}
    if args.mode == "perf" and args.heat_soak > 0:
        # Laptop GPUs throttle thermally: bring the machine to a steady thermal state before measuring anything.
        b0 = builds[0]
        sd = os.path.join(raw_dir, "_heatsoak")
        os.makedirs(sd, exist_ok=True)
        log(f"heat soak {args.heat_soak:.0f} s ({scenes[0]} {resolutions[-1]} {b0}, discarded)")
        s0 = len(sampler.samples)
        run_godot(exes[b0], ["--fixed-fps", "60", "--disable-vsync", "--resolution", resolutions[-1], "--position", "0,0"],
                  [f"--scene={scenes[0]}", "--mode=perf", f"--res={resolutions[-1]}", "--tag=heatsoak", f"--out={sd}",
                   f"--warmup={args.heat_soak}", "--measure=1", f"--preset={args.preset}"], args.heat_soak + 600, os.path.join(sd, "heatsoak.log"))
        tele = GpuSampler()
        tele.samples = sampler.samples[s0:]
        soak = tele.summary()
        last = sampler.samples[-1] if sampler.samples else {}
        log(f"heat soak done: gpu clock {last.get('clocks.gr')} MHz, {last.get('temperature.gpu')} C, {last.get('power.draw')} W, reasons {last.get('clocks_throttle_reasons.active')}")
    try:
        for scene in scenes:
            for res in resolutions:
                runs_by_build = {b: [] for b in builds}
                n_target = args.runs if args.mode == "perf" else 1
                i = 0
                while True:
                    order = builds if i % 2 == 0 else list(reversed(builds))  # ABBA interleave
                    for b in order:
                        tag = b if not args.fellow or b in off_for else f"{b}+fx"
                        out_dir = os.path.join(raw_dir, b, scene)
                        os.makedirs(out_dir, exist_ok=True)
                        user = [f"--scene={scene}", f"--mode={args.mode}", f"--res={res}", f"--tag={tag}", f"--run={i}",
                                f"--out={out_dir}", f"--warmup={args.warmup}", f"--measure={args.measure}", f"--preset={args.preset}"]
                        if args.set:
                            user.append(f"--set={args.set}")
                        if toggles_for(b):
                            user.append(f"--fellow={toggles_for(b)}")
                        if args.percam:
                            user.append(f"--percam={args.percam}")
                        if args.profile:
                            user.append("--profile=1")
                        if args.extra:
                            user.extend(args.extra.split())
                        user.extend(specs[b][2])
                        gargs = ["--fixed-fps", "60", "--disable-vsync", "--resolution", res, "--position", "0,0"]
                        if args.profile:
                            gargs.append("--gpu-profile")
                        lp = os.path.join(out_dir, f"{scene}_{args.mode}_{res}_run{i}.log")
                        s0 = len(sampler.samples)
                        ef = os.path.join(out_dir, f"error_{scene}.json")
                        if os.path.exists(ef):
                            os.remove(ef)  # stale failure record of an earlier run of this build (written by main.gd)
                        log(f"{scene} {res} {b} run {i} ...")
                        proc = run_godot(exes[b], gargs, user, args.timeout, lp)
                        w, h = res.split("x")
                        jf = os.path.join(out_dir, f"{scene}_{args.mode}_{tag}_{w}x{h}_run{i}.json")
                        r = {"valid": False, "error": "no result file"}
                        if os.path.exists(jf):
                            with open(jf, encoding="utf-8") as f:
                                r = json.load(f)
                        elif os.path.exists(ef):
                            with open(ef, encoding="utf-8") as f:
                                r = {k: v for k, v in json.load(f).items() if k in ("valid", "error", "engine", "adapter", "fellow")}
                        r["_proc"] = proc
                        r["_json"] = os.path.relpath(jf, ROOT)
                        tele = GpuSampler()
                        tele.samples = sampler.samples[s0:]
                        r["_gpu_telemetry"] = tele.summary()
                        # Hitches: frames far above the run's median (> max(100 ms, 5 x median)). These are external stalls,
                        # not rendering cost (seen once as ~480 ms stalls every ~1 s in a thermally limited state).
                        fms = (r.get("frames") or {}).get("frame_ms") or []
                        if fms:
                            med = statistics.median(fms)
                            r["hitch_frames"] = sum(1 for x in fms if x > max(100.0, 5.0 * med))
                        runs_by_build[b].append(r)
                        msg = f"   -> rc={proc['returncode']} valid={r.get('valid')} wall={proc['wall_s']:.1f}s"
                        if r.get("summary"):
                            s = r["summary"]
                            msg += f" frame={s['frame_ms']['mean']:.3f}ms gpu={s['gpu_ms']['mean']:.3f}ms p99={s['frame_ms']['p99']:.3f}ms"
                        if not r.get("valid"):
                            msg += f" error={str(r.get('error', ''))[:120]}"
                        if r.get("hitch_frames"):
                            msg += f" HITCHES={r['hitch_frames']}"
                        gt = r["_gpu_telemetry"]
                        if "clocks.gr" in gt:
                            msg += f" | gpu {gt['clocks.gr']['mean']:.0f} MHz {gt['temperature.gpu']['mean']:.0f} C {gt.get('power.draw', {}).get('mean', 0):.0f} W"
                        log(msg)
                        if args.cooldown > 0:
                            time.sleep(args.cooldown)
                    i += 1
                    if i < n_target:
                        continue
                    if args.mode != "perf" or i >= args.max_runs:
                        break
                    noisy = False
                    for b in builds:
                        # A build that cannot run the scene at all (e.g. stock Godot on the Fluid Lab) is not retried.
                        if all("unsupported" in str(r.get("error", "")) for r in runs_by_build[b]):
                            continue
                        agg = aggregate(runs_by_build[b])
                        cv = agg.get("frame_ms_run_means", {}).get("cv", 0.0)
                        clean = sum(1 for r in runs_by_build[b] if r.get("valid") and not r.get("hitch_frames"))
                        if agg.get("runs_valid", 0) < args.runs or cv > args.max_cv or clean < args.runs:
                            noisy = True
                    if not noisy:
                        break
                    log("   variance above threshold -> adding a run")
                for b in builds:
                    key = f"{scene}|{res}|{b}"
                    entry = {"scene": scene, "resolution": res, "build": b, "preset": args.preset, "set": args.set,
                             "fellow": toggles_for(b), "binary": specs[b][0], "variant_args": specs[b][2], "mode": args.mode}
                    if args.mode == "perf":
                        agg = aggregate(runs_by_build[b])
                        cv = agg.get("frame_ms_run_means", {}).get("cv", 0.0)
                        agg["noisy"] = cv > args.max_cv
                        entry["aggregate"] = agg
                        entry["effective_settings"] = next((r.get("effective_settings") for r in runs_by_build[b] if r.get("valid")), None)
                        entry["fellow_state"] = next((r.get("fellow") for r in runs_by_build[b] if r.get("valid")), None)
                        entry["adapter"] = next((r.get("adapter") for r in runs_by_build[b] if r.get("valid")), None)
                        entry["engine"] = next((r.get("engine") for r in runs_by_build[b] if r.get("valid")), None)
                        entry["scene_desc"] = next((r.get("scene_desc") for r in runs_by_build[b] if r.get("valid")), None)
                    entry["runs"] = [{k: v for k, v in r.items() if k not in ("frames", "scene_desc", "effective_settings", "preset")} for r in runs_by_build[b]]
                    all_results[key] = entry
    finally:
        sampler.stop()

    summary = {"session": session, "label": args.label, "args": vars(args), "hardware": hw, "gpu_telemetry": sampler.summary(), "heat_soak_telemetry": soak, "results": all_results}
    os.makedirs(RESULTS, exist_ok=True)
    jpath = os.path.join(RESULTS, f"{session}.json")
    if args.merge and os.path.exists(jpath):
        with open(jpath, encoding="utf-8") as f:
            old = json.load(f)
        stamp = datetime.datetime.now().isoformat(timespec="seconds")
        superseded = old.get("superseded", {})
        for key in all_results:
            if key in old.get("results", {}):
                superseded[f"{key}|{stamp}"] = {"note": args.supersede_note, "entry": old["results"][key]}
        merged = dict(old.get("results", {}))
        merged.update(all_results)
        history = old.get("merge_history", [{"args": old.get("args"), "hardware": old.get("hardware"), "gpu_telemetry": old.get("gpu_telemetry"),
                                             "heat_soak_telemetry": old.get("heat_soak_telemetry"), "keys": list(old.get("results", {}).keys())}])
        history.append({"args": vars(args), "hardware": hw, "gpu_telemetry": summary["gpu_telemetry"], "heat_soak_telemetry": soak,
                        "keys": list(all_results.keys()), "note": args.supersede_note, "time": stamp})
        summary = dict(old)
        summary.update({"results": merged, "superseded": superseded, "merge_history": history})
        all_results = merged
        log(f"merged {len(history[-1]['keys'])} entries into {jpath} ({len(superseded)} superseded kept)")
    with open(jpath, "w", encoding="utf-8") as f:
        json.dump(summary, f, indent=1, default=str)
    if args.mode == "perf":
        cpath = os.path.join(RESULTS, f"{session}.csv")
        with open(cpath, "w", newline="", encoding="utf-8") as f:
            w = csv.writer(f)
            w.writerow(["scene", "resolution", "build", "preset", "fellow", "runs_valid", "noisy", "avg_fps", "frame_ms_mean", "frame_ms_std", "frame_ms_cv",
                        "gpu_ms_mean", "gpu_ms_cv", "render_cpu_ms_mean", "setup_cpu_ms_mean", "p50_ms", "p90_ms", "p95_ms", "p99_ms", "p99_9_ms",
                        "fps_1pct_low", "gpu_p95_ms", "gpu_p99_ms", "draw_calls", "primitives", "objects", "stutter_frames", "frames",
                        "pipeline_compiles_measure", "vram_peak_MB", "tex_mem_peak_MB", "proc_peak_wset_MB", "load_ms", "gpu_clock_MHz_mean", "gpu_temp_C_mean"])
            for e in all_results.values():
                a = e.get("aggregate", {})
                if not a.get("runs_valid"):
                    w.writerow([e["scene"], e["resolution"], e["build"], e["preset"], e["fellow"], 0])
                    continue
                w.writerow([
                    e["scene"], e["resolution"], e["build"], e["preset"], e["fellow"], a["runs_valid"], a["noisy"],
                    round(a["avg_fps_run"]["mean"], 2), round(a["frame_ms_run_means"]["mean"], 4), round(a["frame_ms_run_means"]["std"], 4), round(a["frame_ms_run_means"]["cv"], 4),
                    round(a["gpu_ms_run_means"]["mean"], 4), round(a["gpu_ms_run_means"]["cv"], 4), round(a["render_cpu_ms_run_means"]["mean"], 4), round(a["setup_cpu_ms_run_means"]["mean"], 4),
                    *[round(a["frame_ms_pooled"][p], 4) for p in ["p50", "p90", "p95", "p99", "p99_9"]],
                    round(a["fps_1pct_low_pooled"], 2), round(a["gpu_ms_pooled"]["p95"], 4), round(a["gpu_ms_pooled"]["p99"], 4),
                    round(a["draw_calls_run_means"]["mean"], 1), round(a["primitives_run_means"]["mean"], 0), round(a["objects_run_means"]["mean"], 1),
                    a["stutter_frames_total"], a["frames_total"], sum(a["pipeline_compilations_during_measurement"].values()),
                    round(a["video_mem_peak_bytes"] / 2**20, 1), round(a["texture_mem_peak_bytes"] / 2**20, 1), round(a["process_peak_working_set_bytes"] / 2**20, 1),
                    round(a["load_time_ms"]["mean"], 1),
                    round(statistics.fmean([r["_gpu_telemetry"]["clocks.gr"]["mean"] for r in e["runs"] if "clocks.gr" in r.get("_gpu_telemetry", {})] or [float("nan")]), 0),
                    round(statistics.fmean([r["_gpu_telemetry"]["temperature.gpu"]["mean"] for r in e["runs"] if "temperature.gpu" in r.get("_gpu_telemetry", {})] or [float("nan")]), 1),
                ])
        log(f"wrote {cpath}")
    log(f"wrote {jpath}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
