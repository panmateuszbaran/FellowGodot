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
report; milestone 102: the reports uploaded to a collector on this machine (multipart/form-data, as Sentry's,
BugSplat's and Backtrace's minidump endpoints take them) — refused (500), unreachable, accepted, over TLS (a
self-signed certificate for 127.0.0.1), and on their own at the start without minidumps. One
JSON line: CRASH_CHECK {...} (also <dir>/crash_check.json); exit code 1 when something was otherwise than expected.
"""

import argparse
import glob
import json
import os
import platform
import re
import shutil
import socket
import ssl
import subprocess
import sys
import threading
import time
from email.parser import BytesParser
from email.policy import default as email_default
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

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


class Collector:
    """A crash report collector on this machine: answers POST /ok with 200 and /fail with 500, and keeps each
    request's multipart parts (name, file name, content type, bytes)."""

    def __init__(self, cert=None, key=None):
        received = self.received = []

        class Handler(BaseHTTPRequestHandler):
            def do_POST(self):
                body = self.rfile.read(int(self.headers.get("Content-Length", 0)))
                ctype = self.headers.get("Content-Type", "")
                msg = BytesParser(policy=email_default).parsebytes(b"Content-Type: " + ctype.encode() + b"\r\nMIME-Version: 1.0\r\n\r\n" + body)
                parts = []
                if msg.is_multipart():
                    for part in msg.iter_parts():
                        parts.append(
                            {
                                "name": part.get_param("name", header="content-disposition"),
                                "filename": part.get_filename(),
                                "type": part.get_content_type() if part.get("Content-Type") else None,
                                "data": part.get_payload(decode=True) or b"",
                            }
                        )
                received.append({"path": self.path, "content_type": ctype.split(";")[0], "bytes": len(body), "user_agent": self.headers.get("User-Agent"), "parts": parts})
                code = 200 if self.path.startswith("/ok") else 500
                reply = json.dumps({"id": "%032x" % len(received)} if code == 200 else {"error": "refused on purpose"}).encode()
                self.send_response(code)
                self.send_header("Content-Type", "application/json")
                self.send_header("Content-Length", str(len(reply)))
                self.end_headers()
                self.wfile.write(reply)

            def log_message(self, *args):
                pass

        self.server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        if cert:
            ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
            ctx.load_cert_chain(cert, key)
            self.server.socket = ctx.wrap_socket(self.server.socket, server_side=True)
        self.port = self.server.server_address[1]
        self.thread = threading.Thread(target=self.server.serve_forever, daemon=True)
        self.thread.start()

    def close(self):
        self.server.shutdown()
        self.server.server_close()


def free_port():
    s = socket.socket()
    s.bind(("127.0.0.1", 0))
    port = s.getsockname()[1]
    s.close()
    return port


def write_project(path, reports, enabled=True, log_lines=50, hang_seconds=None, extra=""):
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
            + extra
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


def _json_files(d):
    return sorted(os.path.basename(f) for f in glob.glob(os.path.join(d, "*.json")))


def _upload(godot, project, url=None):
    rc, out, secs = run(godot, project, "upload", ["--url=" + url] if url else [], timeout=120)
    u = line(out, "CRASH_CHECK_UPLOAD") or {}
    u["rc"] = rc
    if not u.get("finished") or rc != 0:
        u["output"] = out[-2000:]
    return u


def _parts_summary(req):
    out = {}
    for p in req["parts"]:
        out[p["name"]] = {"filename": p["filename"], "type": p["type"], "bytes": len(p["data"])}
        if p["filename"] is None:
            out[p["name"]]["value"] = p["data"].decode(errors="replace")
    return out


def upload_lanes(godot, out_dir, failures):
    """Milestone 102: two reports (a crash, with its minidump on Windows, and the game's) refused, unreachable, then
    accepted; one over TLS (and refused where the certificate is not trusted); one on its own at the start."""
    res = {}
    project = os.path.join(out_dir, "project_upload")
    reports = os.path.join(out_dir, "reports_upload")
    os.makedirs(reports)
    write_project(project, reports)
    run(godot, project, "crash", ["--kind=abort"])
    time.sleep(1.2)  # the next report a second later: oldest first is the crash
    run(godot, project, "report")
    files = _json_files(reports)
    dumps = sorted(os.path.basename(f) for f in glob.glob(os.path.join(reports, "*.dmp")))
    res["reports"] = files
    res["minidumps"] = dumps
    crash = [f for f in files if f.startswith("crash_")]
    game = [f for f in files if f.startswith("report_")]
    if len(crash) != 1 or len(game) != 1 or (WINDOWS and len(dumps) != 1):
        failures.append(f"upload: the reports to send {files} {dumps}")
        return res
    originals = {f: open(os.path.join(reports, f), "rb").read() for f in files + dumps}
    order = [crash[0], game[0]]
    col = Collector()
    try:
        # Refused: both tried, oldest first, both kept.
        u = _upload(godot, project, f"http://127.0.0.1:{col.port}/fail")
        res["refused"] = u
        if u.get("rc") != 0 or [x["file"] for x in u.get("uploaded", [])] != order or any(x["ok"] or x["code"] != 500 for x in u.get("uploaded", [])) or u.get("finished") != [[0, 2]] or u.get("reports_after") is None or sorted(u["reports_after"]) != files or len(col.received) != 2:
            failures.append(f"upload refused: {u}")
        # Unreachable: no response, both kept.
        u = _upload(godot, project, f"http://127.0.0.1:{free_port()}/ok")
        res["unreachable"] = {k: u.get(k) for k in ("uploaded", "finished", "reports_after", "msec", "rc")}
        # -3: no connection (UPLOAD_CANT_CONNECT); refused at once (decision 0214: on Windows not at the 30 s timeout), the
        # second report not tried.
        if u.get("rc") != 0 or u.get("finished") != [[0, 2]] or any(x["ok"] or x["code"] != -3 for x in u.get("uploaded", [])) or sorted(u.get("reports_after", [])) != files or u.get("msec", 99999) > 10000:
            failures.append(f"upload unreachable: {u}")
        # Accepted: both sent, oldest first, moved to sent/ unchanged; the fields, the report, the minidump.
        col.received.clear()
        u = _upload(godot, project, f"http://127.0.0.1:{col.port}/ok")
        sent_dir = os.path.join(reports, "sent")
        res["accepted"] = {k: u.get(k) for k in ("uploaded", "finished", "reports_after", "msec", "rc")}
        res["accepted"]["sent_dir"] = sorted(os.listdir(sent_dir)) if os.path.isdir(sent_dir) else []
        res["accepted"]["requests"] = [{"path": r["path"], "content_type": r["content_type"], "bytes": r["bytes"], "user_agent": r["user_agent"], "parts": _parts_summary(r)} for r in col.received]
        ok = u.get("rc") == 0 and [x["file"] for x in u.get("uploaded", [])] == order and all(x["ok"] and x["code"] == 200 for x in u.get("uploaded", []))
        ok = ok and u.get("finished") == [[2, 0]] and u.get("reports_after") == [] and sorted(res["accepted"]["sent_dir"]) == sorted(files + dumps)
        ok = ok and all(open(os.path.join(sent_dir, f), "rb").read() == originals[f] for f in files + dumps if os.path.exists(os.path.join(sent_dir, f)))
        ok = ok and len(col.received) == 2
        if ok:
            for req, name in zip(col.received, order):
                parts = {p["name"]: p for p in req["parts"]}
                kind = "crash" if name.startswith("crash_") else "report"
                ok = ok and req["content_type"] == "multipart/form-data" and parts.get("report", {}).get("data") == originals[name] and parts["report"]["filename"] == name
                ok = ok and parts.get("kind", {}).get("data") == kind.encode() and parts.get("project", {}).get("data") == b"Crash Check" and parts.get("version", {}).get("data") == b"1.2.3"
                ok = ok and len(parts.get("commit", {}).get("data", b"")) >= 7 and parts.get("reason", {}).get("data")
                dump = name[:-5] + ".dmp"
                if dump in originals:
                    ok = ok and parts.get("upload_file_minidump", {}).get("data") == originals[dump] and parts["upload_file_minidump"]["filename"] == dump
                else:
                    ok = ok and "upload_file_minidump" not in parts
        if not ok:
            failures.append(f"upload accepted: {res['accepted']}")
    finally:
        col.close()

    # TLS: a certificate for 127.0.0.1 the project trusts (network/tls/certificate_bundle_override), and the same
    # server refused by a project that does not trust it.
    cert = os.path.join(out_dir, "collector.crt")
    key = os.path.join(out_dir, "collector.key")
    rc, out, _ = run(godot, project, "makecert", ["--cert=" + cert.replace(os.sep, "/"), "--key=" + key.replace(os.sep, "/")])
    tls = {}
    if not (line(out, "CRASH_CHECK_MAKECERT") or {}).get("ok"):
        failures.append("upload tls: no certificate made")
    else:
        col = Collector(cert=cert, key=key)
        try:
            for name, trusted in (("untrusted", False), ("trusted", True)):
                proj = os.path.join(out_dir, "project_tls_" + name)
                reps = os.path.join(out_dir, "reports_tls_" + name)
                os.makedirs(reps)
                write_project(proj, reps, extra=f"\n[network]\n\ntls/certificate_bundle_override=\"{cert.replace(os.sep, '/')}\"\n" if trusted else "")
                run(godot, proj, "report")
                before = len(col.received)
                u = _upload(godot, proj, f"https://127.0.0.1:{col.port}/ok")
                tls[name] = {k: u.get(k) for k in ("uploaded", "finished", "reports_after", "msec", "rc")}
                tls[name]["received"] = len(col.received) - before
                if trusted:
                    good = u.get("finished") == [[1, 0]] and tls[name]["received"] == 1 and u.get("reports_after") == []
                else:
                    # -4: the handshake refused (UPLOAD_TLS_HANDSHAKE): the collector's certificate not trusted.
                    good = u.get("finished") == [[0, 1]] and tls[name]["received"] == 0 and len(u.get("reports_after", [])) == 1 and u["uploaded"][0]["code"] == -4
                good = good and u.get("rc") == 0
                if not good:
                    failures.append(f"upload tls {name}: {tls[name]} {u.get('output', '')[-600:]}")
        finally:
            col.close()
    res["tls"] = tls

    # Milestone 106 (decision 0214): to "localhost" — its IPv6 address first (the collector listens on IPv4 only: refused),
    # then its IPv4 one: sent, without waiting for a timeout.
    col = Collector()
    try:
        proj = os.path.join(out_dir, "project_localhost")
        reps = os.path.join(out_dir, "reports_localhost")
        os.makedirs(reps)
        write_project(proj, reps)
        run(godot, proj, "report")
        u = _upload(godot, proj, f"http://localhost:{col.port}/ok")
        lh = {k: u.get(k) for k in ("uploaded", "finished", "msec", "rc")}
        lh["received"] = len(col.received)
        if u.get("rc") != 0 or u.get("finished") != [[1, 0]] or len(col.received) != 1 or u.get("msec", 99999) > 10000:
            failures.append(f"upload localhost: {lh} {u.get('output', '')[-600:]}")
        res["localhost"] = lh
    finally:
        col.close()

    # On their own: upload_automatically, minidumps not sent (upload_minidumps off).
    col = Collector()
    try:
        proj = os.path.join(out_dir, "project_auto")
        reps = os.path.join(out_dir, "reports_auto")
        os.makedirs(reps)
        write_project(proj, reps)
        run(godot, proj, "crash", ["--kind=abort"])
        with open(os.path.join(proj, "project.godot"), "a", newline="\n") as f:
            f.write(f"fellow/crash_reports/upload_url=\"http://127.0.0.1:{col.port}/ok\"\nfellow/crash_reports/upload_automatically=true\nfellow/crash_reports/upload_minidumps=false\n")
        u = _upload(godot, proj)
        auto = {k: u.get(k) for k in ("before", "uploaded", "finished", "reports_after", "msec", "rc")}
        auto["parts"] = [sorted(p["name"] for p in r["parts"]) for r in col.received]
        auto["sent_dir"] = sorted(os.listdir(os.path.join(reps, "sent"))) if os.path.isdir(os.path.join(reps, "sent")) else []
        if u.get("rc") != 0 or u.get("before") != 1 or u.get("finished") != [[1, 0]] or len(col.received) != 1 or "upload_file_minidump" in auto["parts"][0] or "report" not in auto["parts"][0]:
            failures.append(f"upload auto: {auto} {u.get('output', '')[-600:]}")
        res["auto"] = auto
    finally:
        col.close()
    return res


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--godot", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--kinds", default="segfault,abort,trap,stack_overflow,thread" + ("" if ARM else ",divide"))
    ap.add_argument("--symbols", help="the same build with symbols (or a library, e.g. libc), to symbolize the frames; comma-separated")
    ap.add_argument("--cost-lines", type=int, default=20000)
    ap.add_argument("--windowed", action="store_true", help="the late crash in a window (the GPU in its report)")
    ap.add_argument("--lanes", default="crashes,hangs,report,upload,off,cost", help="which parts to run, comma-separated")
    a = ap.parse_args()
    lanes = a.lanes.split(",")
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

    for kind in (a.kinds.split(",") + ["later"]) if "crashes" in lanes else []:
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
    for mode, extra, timeout in (("hang", ["--stall_ms=3500"], 120), ("freeze", [], 9)) if "hangs" in lanes else ():
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
    if "report" in lanes:
        rc, out, _ = run(godot, project, "report")
        rep = line(out, "CRASH_CHECK_REPORT") or {}
        result["report"] = {k: rep.get(k) for k in ("kind", "reason", "frames", "breadcrumbs", "custom", "script_backtraces")}
        if rc != 0 or rep.get("kind") != "report" or rep.get("reason") != "a soft failure" or "before the report" not in rep.get("breadcrumbs", []) or rep.get("custom", {}).get("checkpoint") != "7" or not rep.get("script_backtraces") or "_write" not in rep["script_backtraces"][0]:
            failures.append(f"report: {rep}")
        result["report"]["stats"] = rep.get("stats")

    # Milestone 102: the reports uploaded.
    if "upload" in lanes:
        result["upload"] = upload_lanes(godot, out_dir, failures)

    # Off: nothing written, the process still crashes.
    off_project = os.path.join(out_dir, "project_off")
    off_reports = os.path.join(out_dir, "reports_off")
    os.makedirs(off_reports)
    write_project(off_project, off_reports, enabled=False)
    if "off" in lanes:
        rc, out, _ = run(godot, off_project, "crash", ["--kind=segfault"])
        result["off"] = {"rc": rc, "reports": len(os.listdir(off_reports))}
        if rc == 0 or result["off"]["reports"] != 0:
            failures.append(f"off: {result['off']}")

    # The cost: printing with the reporter on and off (the output thrown away), breadcrumbs and values.
    if "cost" in lanes:
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
