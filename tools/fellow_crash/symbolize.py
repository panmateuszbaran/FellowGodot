#!/usr/bin/env python3
"""FellowEngine milestone 97 (decision 0205): a crash report made readable.

    python tools/fellow_crash/symbolize.py <report.json> [--binary <the build's executable with symbols>]... [--json]

Prints the report: what happened, where (the frames, symbolized with the build's symbols when --binary is given:
`addr2line` for ELF executables, `llvm-symbolizer` for Windows executables with their PDB beside them), the game's
breadcrumbs and values, the scripts' backtraces and the last lines of the log. A Windows report's minidump
(crash_*.dmp beside it) opens in Visual Studio or WinDbg with the build's PDB. --json prints the frames as JSON
instead. A release template keeps no symbols: symbolize with the same commit's build made with debug_symbols=yes
(separate_debug_symbols=yes keeps them in a file of their own, <executable>.debugsymbols).
"""

import argparse
import json
import os
import shutil
import struct
import subprocess
import sys


def binary_kind(path):
    with open(path, "rb") as f:
        head = f.read(64)
    if head[:4] == b"\x7fELF":
        e_type = struct.unpack_from("<H", head, 16)[0]
        return ("elf", e_type == 2)  # ET_EXEC: absolute addresses; ET_DYN (PIE): offsets
    if head[:2] == b"MZ":
        with open(path, "rb") as f:
            data = f.read(4096)
        pe = struct.unpack_from("<I", data, 0x3C)[0]
        magic = struct.unpack_from("<H", data, pe + 24)[0]
        image_base = struct.unpack_from("<Q", data, pe + 24 + 24)[0] if magic == 0x20B else struct.unpack_from("<I", data, pe + 24 + 28)[0]
        return ("pe", image_base)
    return ("unknown", None)


def symbolize(frames, binary):
    """Adds "function" and "location" to the frames whose module is the binary's."""
    if not binary:
        return frames
    kind, extra = binary_kind(binary)
    module = os.path.basename(binary)
    stem = module.split(".debugsymbols")[0]
    picked = [f for f in frames if f.get("module") in (module, stem)]
    if not picked:
        return frames
    if kind == "elf":
        tool = shutil.which("addr2line") or shutil.which("llvm-addr2line")
        addrs = []
        for f in picked:
            off = int(f["offset"], 16)
            addrs.append(hex(int(f["address"], 16) if extra else off))
        cmd = [tool, "-e", binary, "-f", "-C"] + addrs
    elif kind == "pe":
        tool = shutil.which("llvm-symbolizer")
        if not tool:
            return frames
        addrs = [hex(extra + int(f["offset"], 16)) for f in picked]
        cmd = [tool, "--obj=" + binary, "--output-style=GNU"] + addrs
    else:
        return frames
    if not tool:
        return frames
    out = subprocess.run(cmd, capture_output=True, text=True).stdout.splitlines()
    if kind == "pe":
        out = [line for line in out if line.strip()]
    for i, f in enumerate(picked):
        if 2 * i + 1 < len(out):
            f["function"] = out[2 * i]
            f["location"] = out[2 * i + 1]
    return frames


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("report")
    ap.add_argument("--binary", action="append", default=[], help="the build's executable with symbols (or its .debugsymbols file), or a library a frame is in; again for more")
    ap.add_argument("--json", action="store_true", help="print the symbolized frames as JSON")
    ap.add_argument("--log", type=int, default=20, help="the last log lines to print")
    a = ap.parse_args()
    report = json.load(open(a.report, encoding="utf-8", errors="replace"))
    frames = report.get("frames", [])
    for binary in a.binary:
        frames = symbolize(frames, binary)
    if a.json:
        print(json.dumps(frames, indent=1))
        return 0
    c = report.get("context", {})
    print(f"{report.get('kind')}: {report.get('reason')}" + (f" at {report['fault_address']}" if "fault_address" in report else "") + (f" ({report['access']})" if "access" in report else ""))
    print(f"  {report.get('time_utc')}, {report.get('uptime_ms', 0) / 1000:.1f} s after start, frame {report.get('frame')}, scene {report.get('scene') or '-'}")
    t = report.get("thread", {})
    print(f"  thread {t.get('id')} ({'main' if t.get('main') else 'not the main thread'})")
    print(f"  {c.get('engine')} {c.get('commit', '')[:10]} ({c.get('build')}), {c.get('project')} {c.get('project_version')}")
    print(f"  {c.get('os')} {c.get('os_version')} ({c.get('distribution')}), {c.get('cpu')} x{c.get('cpu_count')}, {c.get('ram_mb')} MB")
    print(f"  {c.get('gpu') or '-'} ({c.get('gpu_vendor') or '-'}, {c.get('gpu_driver') or '-'}), {c.get('rendering_driver')} {c.get('rendering_method')}")
    m = report.get("memory", {})
    print(f"  memory: {m.get('resident_mb')} MB resident, {m.get('video_mb')} MB video")
    if report.get("minidump"):
        print(f"  minidump: {report['minidump']} ({report.get('minidump_bytes', '?')} bytes)")
    print("frames:")
    for i, f in enumerate(frames):
        where = f.get("function") or f.get("symbol") or "?"
        loc = f" {f['location']}" if f.get("location") and not f["location"].startswith("??") else ""
        print(f"  [{i}] {f.get('module')}+{f.get('offset')} {where}{loc}")
    for bt in report.get("script_backtraces", []):
        print(bt.rstrip())
    if report.get("breadcrumbs"):
        print("breadcrumbs:")
        for b in report["breadcrumbs"]:
            print(f"  {b['ms'] / 1000:9.3f} s [{b['category']}] {b['text']}")
    if report.get("custom"):
        print("values:")
        for k, v in report["custom"].items():
            print(f"  {k} = {v}")
    if report.get("log"):
        print(f"log (last {min(a.log, len(report['log']))} of {len(report['log'])} lines):")
        for line in report["log"][-a.log :]:
            print("  " + line)
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except BrokenPipeError:
        sys.exit(0)
