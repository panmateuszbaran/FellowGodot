#!/usr/bin/env python3
"""Run a soak (milestone 55): the bench's --mode=soak with the process's memory sampled from outside.

A release template counts no static memory (OS.get_static_memory_usage() is 0 there), so a leak would only show in
object counts; this runner samples the engine process's resident memory (psutil, else /proc on Linux, tasklist on
Windows) and, where nvidia-smi exists, the GPU's memory in use, every --every seconds, into process_memory.csv next
to the result. soak_report.py adds both to the summary (start, end, growth per hour after the warm-up).

    python benchmarks/tools/soak/run_soak.py --godot <binary> --out <dir> [--minutes 60] [--res 1920x1080]
        [--warmup 60] [--engine-args="--render-thread separate"] [-- <more bench arguments, e.g. --slice_without=fire>]
"""

import argparse
import csv
import os
import shutil
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
PROJECT = os.path.normpath(os.path.join(HERE, "..", "..", "project"))


def rss_mb(pid):
    try:
        import psutil  # noqa: PLC0415

        # With its children: Windows' *.console.exe is a small wrapper that starts the engine's own executable.
        proc = psutil.Process(pid)
        total = proc.memory_info().rss
        for child in proc.children(recursive=True):
            try:
                total += child.memory_info().rss
            except psutil.Error:
                pass
        return total / 1048576.0
    except Exception:  # noqa: BLE001
        pass
    status = f"/proc/{pid}/status"
    if os.path.exists(status):
        with open(status, encoding="utf-8") as f:
            for line in f:
                if line.startswith("VmRSS:"):
                    return int(line.split()[1]) / 1024.0
    if os.name == "nt":
        out = subprocess.run(["tasklist", "/FI", f"PID eq {pid}", "/FO", "CSV", "/NH"], capture_output=True, text=True).stdout
        parts = [p.strip('"') for p in out.strip().split('","')]
        if len(parts) >= 5:
            digits = "".join(c for c in parts[4] if c.isdigit())
            if digits:
                return int(digits) / 1024.0
    return None


def gpu_mb():
    smi = shutil.which("nvidia-smi")
    if not smi:
        return None
    try:
        out = subprocess.run([smi, "--query-gpu=memory.used", "--format=csv,noheader,nounits"], capture_output=True, text=True, timeout=10).stdout
        return float(out.strip().splitlines()[0])
    except Exception:  # noqa: BLE001
        return None


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--godot", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--scene", default="slice")
    ap.add_argument("--minutes", type=float, default=60.0)
    ap.add_argument("--warmup", type=float, default=60.0)
    ap.add_argument("--res", default="1920x1080")
    ap.add_argument("--every", type=float, default=5.0, help="seconds between memory samples")
    ap.add_argument("--fellow", default="post/atmosphere/enabled=true,post/clouds/enabled=true", help="the bench's --fellow (empty: none)")
    ap.add_argument("--sky", default="atmosphere")
    ap.add_argument("--engine-args", default="", help="engine arguments before '--', e.g. \"--render-thread separate\"")
    ap.add_argument("rest", nargs="*", help="more bench arguments (after --)")
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    user = [f"--res={a.res}", f"--out={os.path.abspath(a.out)}", f"--scene={a.scene}", "--mode=soak",
            f"--soak_minutes={a.minutes}", f"--soak_warmup={a.warmup}"]
    if a.fellow:
        user.append(f"--fellow={a.fellow}")
    if a.sky:
        user.append(f"--sky={a.sky}")
    cmd = [a.godot, "--path", PROJECT, "--rendering-driver", "vulkan", "--resolution", a.res, "--position", "0,0"] + a.engine_args.split() + ["--"] + user + a.rest
    log = open(os.path.join(a.out, "engine.log"), "w", encoding="utf-8")
    t0 = time.time()
    proc = subprocess.Popen(cmd, stdout=log, stderr=subprocess.STDOUT)
    with open(os.path.join(a.out, "process_memory.csv"), "w", newline="", encoding="utf-8") as f:
        w = csv.writer(f)
        w.writerow(["t", "process_mb", "gpu_used_mb"])
        while proc.poll() is None:
            r = rss_mb(proc.pid)
            g = gpu_mb()
            w.writerow([round(time.time() - t0, 1), round(r, 1) if r is not None else "", round(g, 1) if g is not None else ""])
            f.flush()
            time.sleep(a.every)
    log.close()
    print(f"engine exit {proc.returncode} after {time.time() - t0:.0f} s; memory samples in {os.path.join(a.out, 'process_memory.csv')}")
    return proc.returncode


if __name__ == "__main__":
    sys.exit(main())
