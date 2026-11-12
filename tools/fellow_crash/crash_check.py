#!/usr/bin/env python3
"""FellowEngine milestone 97 check (decision 0205): FellowCrashReporter's reports from real crashes.

    python tools/fellow_crash/crash_check.py --godot <editor or template> --out <dir> [--kinds segfault,abort,...]
        [--symbols <the same build with symbols>]

Makes a project of its own in <dir>/project (the reports on, written to <dir>/reports, GDScript's call stacks tracked)
and runs tools/fellow_crash/crash_check.gd in it: for each kind of crash (a null pointer written, abort(), a trap, a
stack overflow, a null pointer on a worker thread, an integer divided by zero) a process that crashes on purpose two
script functions deep, and one 30 frames into a game with a scene current — its report is read (the reason, the frames, the breadcrumbs and values the game set, the
marker it printed, the script's backtrace, the machine) and the next run finds it pending, then acknowledged; a
report written by the game; the reports off (none written); the cost of printing with the reporter on and off;
milestone 98: the main thread stalled for 3.5 s and going on, and frozen in a script's loop (killed), each a hang
report. One
JSON line: CRASH_CHECK {...} (also <dir>/crash_check.json); exit code 1 when something was otherwise than expected.
"""

import argparse
import glob
import json
import os
import platform
import re
import shutil
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import symbolize  # noqa: E402

WINDOWS = os.name == "nt"
ARM = platform.machine().lower() in ("aarch64", "arm64")

EXPECTED = {
    "segfault": ("EXCEPTION_ACCESS_VIOLATION", "SIGSEGV"),
    "abort": ("SIGABRT", "SIGABRT"),
    "trap": ("EXCEPTION_BREAKPOINT", "SIGTRAP" if ARM else "SIGILL"),
    "stack_overflow": ("EXCEPTION_STACK_OVERFLOW", "SIGSEGV"),
    "thread": ("EXCEPTION_ACCESS_VIOLATION", "SIGSEGV"),
    "divide": ("EXCEPTION_INT_DIVIDE_BY_ZERO", "SIGFPE"),
}


def read_minidump(path):
    """A minidump's header and the streams a debugger starts from: the exception (its code and thread), the threads,
    the modules."""
    import struct

    data = open(path, "rb").read()
    if data[:4] != b"MDMP":
        return {"valid": False}
    count, rva = struct.unpack_from("<II", data, 8)
    out = {"valid": True, "streams": count}
    for i in range(count):
        kind, size, at = struct.unpack_from("<III", data, rva + 12 * i)
        if kind == 6:  # ExceptionStream: ThreadId, alignment, MINIDUMP_EXCEPTION (code first)
            thread, _, code = struct.unpack_from("<III", data, at)
            out["exception_thread"] = thread
            out["exception_code"] = "0x%08x" % code
        elif kind == 3:  # ThreadListStream
            out["threads"] = struct.unpack_from("<I", data, at)[0]
        elif kind == 4:  # ModuleListStream
            out["modules"] = struct.unpack_from("<I", data, at)[0]
    return out


def write_project(path, reports, enabled=True, log_lines=50, hang_seconds=None):
    os.makedirs(path, exist_ok=True)
    shutil.copy(os.path.join(HERE, "crash_check.gd"), os.path.join(path, "crash_check.gd"))
    with open(os.path.join(path, "project.godot"), "w", newline="\n") as f:
        f.write(
            "config_version=5\n\n[application]\n\nconfig/name=\"Crash Check\"\nconfig/version=\"1.2.3\"\n\n[debug]\n\n"
            f"fellow/crash_reports/enabled={'true' if enabled else 'false'}\n"
            f"fellow/crash_reports/directory=\"{reports.replace(os.sep, '/')}\"\n"
            f"fellow/crash_reports/log_lines={log_lines}\n"
            "settings/gdscript/always_track_call_stacks=true\n"
            + (f"fellow/crash_reports/hang_seconds={hang_seconds}\n" if hang_seconds is not None else "")
        )


def run(godot, project, mode, extra=(), timeout=180, windowed=False):
    head = [godot] + ([] if windowed else ["--headless"])
    cmd = head + ["--path", project, "-s", "res://crash_check.gd", "--", "--mode=" + mode] + list(extra)
    t0 = time.time()
    try:
        p = subprocess.run(cmd, capture_output=True, text=True, errors="replace", timeout=timeout)
        rc, out = p.returncode, p.stdout + p.stderr
    except subprocess.TimeoutExpired as e:
        rc, out = "timeout", (e.stdout or b"").decode(errors="replace") if isinstance(e.stdout, bytes) else (e.stdout or "")
    return rc, out, time.time() - t0


def line(out, tag):
    m = re.search(r"^" + tag + r" (\{.*\})$", out, re.M)
    return json.loads(m.group(1)) if m else None


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--godot", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--kinds", default="segfault,abort,trap,stack_overflow,thread" + ("" if ARM else ",divide"))
    ap.add_argument("--symbols", help="the same build with symbols (or a library, e.g. libc), to symbolize the frames; comma-separated")
    ap.add_argument("--cost-lines", type=int, default=20000)
    ap.add_argument("--windowed", action="store_true", help="the late crash in a window (the GPU in its report)")
    a = ap.parse_args()
    if WINDOWS:
        import ctypes

        # No "stopped working" dialog for the crashes on purpose (the children inherit the error mode).
        ctypes.windll.kernel32.SetErrorMode(0x0001 | 0x0002 | 0x8000)
    out_dir = os.path.abspath(a.out)
    project = os.path.join(out_dir, "project")
    reports = os.path.join(out_dir, "reports")
    shutil.rmtree(out_dir, ignore_errors=True)
    os.makedirs(reports)
    write_project(project, reports)
    godot = os.path.abspath(a.godot)
    failures = []
    result = {"platform": sys.platform, "machine": platform.machine(), "godot": os.path.basename(godot), "kinds": {}}

    for kind in a.kinds.split(",") + ["later"]:
        for f in glob.glob(os.path.join(reports, "*")):
            os.remove(f)
        if kind == "later":
            # A null pointer 30 frames into a game (the frame the reporter last saw a frame or two before) with a scene current (in a window with --windowed).
            rc, out, secs = run(godot, project, "crash", ["--kind=segfault", "--delay=30"], windowed=a.windowed)
        else:
            rc, out, secs = run(godot, project, "crash", ["--kind=" + kind])
        r = {"rc": rc, "seconds": round(secs, 2), "armed": ("CRASH_CHECK_ARMED " + kind) in out}
        found = sorted(glob.glob(os.path.join(reports, "crash_*.json")))
        r["reports"] = len(found)
        if rc == 0 or rc == "timeout" or len(found) != 1:
            failures.append(f"{kind}: rc {rc}, {len(found)} reports")
            open(os.path.join(out_dir, f"{kind}_output.txt"), "w", encoding="utf-8").write(out)
            result["kinds"][kind] = r
            continue
        shutil.copy(found[0], os.path.join(out_dir, f"{kind}_report.json"))
        try:
            rep = json.load(open(found[0], encoding="utf-8", errors="replace"))
            r["valid_json"] = True
        except ValueError as e:
            rep = {}
            r["valid_json"] = False
            failures.append(f"{kind}: the report is not JSON ({e})")
        want = EXPECTED["segfault" if kind == "later" else kind][0 if WINDOWS else 1]
        r["reason"] = rep.get("reason")
        r["frames"] = len(rep.get("frames", []))
        r["main_thread"] = rep.get("thread", {}).get("main")
        r["breadcrumbs"] = [b["text"] for b in rep.get("breadcrumbs", [])]
        r["custom"] = rep.get("custom", {})
        r["marker_in_log"] = any("CRASH_CHECK_MARKER 4242" in l for l in rep.get("log", []))
        r["warning_in_log"] = any("CRASH_CHECK_WARNING" in l for l in rep.get("log", []))
        r["log_lines"] = len(rep.get("log", []))
        bts = rep.get("script_backtraces", [])
        r["script_backtrace"] = bool(bts) and "_level_two" in bts[0] and "_level_one" in bts[0]
        r["context"] = {k: rep.get("context", {}).get(k) for k in ("project", "project_version", "build", "os", "cpu_count", "gpu", "gpu_driver", "executable")}
        r["frame"] = rep.get("frame")
        r["scene"] = rep.get("scene")
        r["memory"] = rep.get("memory")
        r["minidump"] = rep.get("minidump")
        if rep.get("minidump"):
            dump = os.path.join(reports, rep["minidump"])
            r["minidump_bytes"] = os.path.getsize(dump) if os.path.exists(dump) else 0
            md = read_minidump(dump) if os.path.exists(dump) else {"valid": False}
            r["minidump_streams"] = md
            if not md["valid"] or md.get("threads", 0) < 2 or md.get("modules", 0) < 5:
                failures.append(f"{kind}: minidump {md}")
            # A crash's dump holds its exception, the report's code (abort() and pure virtual calls have none).
            if "code" in rep and md.get("exception_code") != rep["code"]:
                failures.append(f"{kind}: the minidump's exception {md.get('exception_code')}, the report's {rep['code']}")
        if a.symbols:
            sym = rep.get("frames", [])
            for b in a.symbols.split(","):
                sym = symbolize.symbolize(sym, os.path.abspath(b))
            names = [f.get("function", "") for f in sym]
            r["symbolized"] = names[:8]
            # With the engine's symbols, debug_crash() (or what it called) is among the frames; with libc's (the
            # engine's stripped), abort()'s frames are.
            r["named_frames"] = [n for n in names if n and not n.startswith("??")][:6]
        checks = {
            "reason": r["reason"] == want,
            "frames": r["frames"] >= 3,
            "thread": r["main_thread"] == (kind != "thread"),
            "breadcrumbs": "level two" in r["breadcrumbs"] and "level loaded" in r["breadcrumbs"],
            "custom": r["custom"].get("level") == "crash_check" and r["custom"].get("player_position", "").startswith("(1.5, 2"),
            "marker": r["marker_in_log"] and r["warning_in_log"],
            "context": r["context"]["project"] == "Crash Check" and r["context"]["project_version"] == "1.2.3",
        }
        if kind == "later":
            checks["later"] = r["frame"] >= 25 and r["scene"] == "Harbour" and "Harbour" in r["breadcrumbs"]
            if a.windowed:
                checks["gpu"] = bool(r["context"]["gpu"])
        # The script's backtrace needs the heap and a stack: not after a stack overflow, nor from another thread.
        if kind not in ("stack_overflow", "thread"):
            checks["script_backtrace"] = r["script_backtrace"]
        r["checks"] = checks
        for k, ok in checks.items():
            if not ok:
                failures.append(f"{kind}: {k}")
        # The next run finds it pending, acknowledges it, and then finds none.
        rc2, out2, _ = run(godot, project, "pending")
        p = line(out2, "CRASH_CHECK_PENDING") or {}
        r["pending"] = p
        if not p.get("found") or os.path.basename(p.get("path", "")) != os.path.basename(found[0]) or p.get("after_acknowledge"):
            failures.append(f"{kind}: pending {p}")
        result["kinds"][kind] = r

    # Milestone 98: hangs. The main thread stalled 3.5 s (the watchdog at 2 s), then going on: one report, its stall
    # and recovery, the main thread's frames (and on Linux its script's: the watchdog asks the main thread for them).
    hang_project = os.path.join(out_dir, "project_hang")
    write_project(hang_project, reports, hang_seconds=2.0)
    hangs = {}
    for mode, extra, timeout in (("hang", ["--stall_ms=3500"], 120), ("freeze", [], 9)):
        for f in glob.glob(os.path.join(reports, "*")):
            os.remove(f)
        rc, out, secs = run(godot, hang_project, mode, extra, timeout=timeout)
        found = sorted(glob.glob(os.path.join(reports, "hang_*.json")))
        h = {"rc": rc, "seconds": round(secs, 2), "reports": len(found)}
        if len(found) == 1:
            rep = json.load(open(found[0], encoding="utf-8", errors="replace"))
            shutil.copy(found[0], os.path.join(out_dir, f"{mode}_report.json"))
            h.update({k: rep.get(k) for k in ("kind", "stalled_ms", "recovered_after_ms", "frame", "scene")})
            h["frames"] = len(rep.get("frames", []))
            h["script_frames"] = [f"{f['function']}:{f['line']}" for f in rep.get("script_frames", [])]
            h["thread_main"] = rep.get("thread", {}).get("main")
            h["minidump_bytes"] = rep.get("minidump_bytes")
            if rep.get("minidump"):
                h["minidump_streams"] = read_minidump(os.path.join(reports, rep["minidump"]))
            if mode == "hang":
                h["went_on"] = "CRASH_CHECK_AFTER_STALL" in out
                ok = rc == 0 and h["went_on"] and 2000 <= (h["stalled_ms"] or 0) < 3500 and 3000 <= (h["recovered_after_ms"] or 0) <= 5000
            else:
                ok = rc == "timeout" and "recovered_after_ms" not in rep and (h["stalled_ms"] or 0) >= 2000
            ok = ok and h["kind"] == "hang" and h["frames"] >= 3 and h["thread_main"] is True
            want = ["_stall_two", "_stall_one"] if mode == "hang" else ["_spin_two", "_spin_one"]
            if not WINDOWS:
                ok = ok and [f.split(":")[0] for f in h["script_frames"][:2]] == want
            if not ok:
                failures.append(f"{mode}: {h}")
        else:
            failures.append(f"{mode}: rc {rc}, {len(found)} hang reports")
            open(os.path.join(out_dir, f"{mode}_output.txt"), "w", encoding="utf-8").write(out)
        hangs[mode] = h
    result["hangs"] = hangs

    # A report written by the game.
    rc, out, _ = run(godot, project, "report")
    rep = line(out, "CRASH_CHECK_REPORT") or {}
    result["report"] = {k: rep.get(k) for k in ("kind", "reason", "frames", "breadcrumbs", "custom", "script_backtraces")}
    if rc != 0 or rep.get("kind") != "report" or rep.get("reason") != "a soft failure" or "before the report" not in rep.get("breadcrumbs", []) or rep.get("custom", {}).get("checkpoint") != "7" or not rep.get("script_backtraces") or "_write" not in rep["script_backtraces"][0]:
        failures.append(f"report: {rep}")
    result["report"]["stats"] = rep.get("stats")

    # Off: nothing written, the process still crashes.
    off_project = os.path.join(out_dir, "project_off")
    off_reports = os.path.join(out_dir, "reports_off")
    os.makedirs(off_reports)
    write_project(off_project, off_reports, enabled=False)
    rc, out, _ = run(godot, off_project, "crash", ["--kind=segfault"])
    result["off"] = {"rc": rc, "reports": len(os.listdir(off_reports))}
    if rc == 0 or result["off"]["reports"] != 0:
        failures.append(f"off: {result['off']}")

    # The cost: printing with the reporter on and off (the output thrown away), breadcrumbs and values.
    cost = {}
    for name, proj in (("on", project), ("off", off_project)):
        best = None
        for _ in range(3):
            rc, out, _ = run(godot, proj, "cost", ["--lines=%d" % a.cost_lines])
            c = line(out, "CRASH_CHECK_COST")
            if c and (best is None or c["print_ns"] < best["print_ns"]):
                best = c
        cost[name] = {k: round(v, 1) for k, v in (best or {}).items() if isinstance(v, float)}
    result["cost"] = cost

    result["failures"] = failures
    with open(os.path.join(out_dir, "crash_check.json"), "w") as f:
        json.dump(result, f, indent=1)
    print("CRASH_CHECK " + json.dumps(result, sort_keys=True))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
