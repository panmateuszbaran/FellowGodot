#!/usr/bin/env python3
"""FellowEngine's image regression suite (milestone 54).

Renders the scenes of tools/fellow_regression/project — FellowEngine's features, one group of settings at a time
(suite.json) — from their cameras with tools/fellow_agent (fixed frame rate, fixed random seed), and checks:

* each picture against its golden image (goldens/<group>/<scene>.png);
* no engine error while it rendered;
* with --template: the release template's picture against the editor build's (debug builds check what release builds
  do not: milestone 51 found positional lights missing in every debug build this way, too late);
* groups with "same_as": their pictures against another group's (FellowEngine off against on: the exact options must
  leave the frame stock's).

On lavapipe (Mesa's CPU Vulkan driver) the pictures are the same every run of one build, so a difference is a change.
Another Mesa or LLVM version renders slightly differently: compare with --tolerance there, or bless goldens of its own.

    python tools/fellow_regression/regression.py run --godot bin/godot.linuxbsd.editor.x86_64 \\
        [--template bin/godot.linuxbsd.template_release.x86_64] [--only group/scene,...] [--out DIR]
    python tools/fellow_regression/regression.py bless --godot bin/godot.linuxbsd.editor.x86_64 [--only ...]
    python tools/fellow_regression/regression.py bless|run --godot ... --goldens DIR     # another GPU: its own goldens

Prints one JSON report (also report.json in --out) and exits 0 (every check passed), 1 (a check failed) or 2 (it
could not run). On Linux without a display, run it under xvfb-run.
"""

import argparse
import json
import os
import shutil
import subprocess
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
AGENT = os.path.join(HERE, "..", "fellow_agent", "fellow_agent.py")
GOLDENS = os.path.join(HERE, "goldens")


def load_png(path):
    """(width, height, bytes of RGB) — through Pillow when there is one, else the standard library's zlib."""
    try:
        from PIL import Image  # noqa: PLC0415

        im = Image.open(path).convert("RGB")
        return im.width, im.height, im.tobytes()
    except ImportError:
        pass
    import struct  # noqa: PLC0415
    import zlib  # noqa: PLC0415

    with open(path, "rb") as f:
        data = f.read()
    pos, chunks, w, h, ct = 8, b"", 0, 0, 0
    while pos < len(data):
        n = struct.unpack(">I", data[pos:pos + 4])[0]
        kind = data[pos + 4:pos + 8]
        body = data[pos + 8:pos + 8 + n]
        if kind == b"IHDR":
            w, h, _, ct = struct.unpack(">IIBB", body[:10])
        elif kind == b"IDAT":
            chunks += body
        pos += 12 + n
    bpp = {2: 3, 6: 4}[ct]
    raw = zlib.decompress(chunks)
    out = bytearray()
    prev = bytearray(w * bpp)
    i = 0
    for _ in range(h):
        ft = raw[i]
        line = bytearray(raw[i + 1:i + 1 + w * bpp])
        i += 1 + w * bpp
        for x in range(len(line)):
            a = line[x - bpp] if x >= bpp else 0
            b = prev[x]
            c = prev[x - bpp] if x >= bpp else 0
            if ft == 1:
                line[x] = (line[x] + a) & 255
            elif ft == 2:
                line[x] = (line[x] + b) & 255
            elif ft == 3:
                line[x] = (line[x] + (a + b) // 2) & 255
            elif ft == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[x] = (line[x] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        prev = line
        for x in range(w):
            out += line[x * bpp:x * bpp + 3]
    return w, h, bytes(out)


def compare(a_path, b_path, threshold=8):
    """Largest difference of a channel, the share of pixels differing at all and by more than threshold levels."""
    wa, ha, a = load_png(a_path)
    wb, hb, b = load_png(b_path)
    if (wa, ha) != (wb, hb):
        return {"size_mismatch": [wa, ha, wb, hb], "identical": False, "max_diff": 255, "share_over": 1.0}
    try:
        import numpy as np  # noqa: PLC0415

        d = np.abs(np.frombuffer(a, np.uint8).astype(np.int16) - np.frombuffer(b, np.uint8).astype(np.int16)).reshape(-1, 3).max(axis=1)
        max_diff = int(d.max())
        share_any = float((d > 0).mean())
        share_over = float((d > threshold).mean())
    except ImportError:
        n = wa * ha
        max_diff = 0
        any_n = over_n = 0
        for i in range(n):
            m = max(abs(a[3 * i] - b[3 * i]), abs(a[3 * i + 1] - b[3 * i + 1]), abs(a[3 * i + 2] - b[3 * i + 2]))
            if m:
                any_n += 1
                max_diff = max(max_diff, m)
                if m > threshold:
                    over_n += 1
        share_any, share_over = any_n / n, over_n / n
    return {"identical": max_diff == 0, "max_diff": max_diff, "share_differing": round(share_any, 6),
            "share_over": round(share_over, 6)}


def within(cmp, tol):
    # share_over 0 means not used: the default tolerance (0, 0) passes only identical pictures.
    return bool(cmp.get("identical") or cmp.get("max_diff", 255) <= tol["max_diff"]
                or (tol["share_over"] > 0 and cmp.get("share_over", 1.0) <= tol["share_over"]))


def capture(godot, project, scene, suite, out_dir, timeout):
    """One scene's picture through fellow_agent; (png path or None, the agent's report)."""
    cmd = [sys.executable, AGENT, "capture", "--godot", godot, "--project", project, "--scene", scene,
           "--views", "camera", "--size", suite["size"], "--frames", str(suite["frames"]), "--fps", str(suite["fps"]),
           "--preview-lighting", "off", "--out", out_dir, "--quiet", "--timeout", str(timeout)]
    try:
        subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=timeout + 60)
    except subprocess.TimeoutExpired:
        return None, {"error": "timeout"}
    report_path = os.path.join(out_dir, "report.json")
    if not os.path.exists(report_path):
        return None, {"error": "no report"}
    with open(report_path, encoding="utf-8") as f:
        report = json.load(f)
    png = os.path.join(out_dir, "view_camera.png")
    return (png if os.path.exists(png) else None), report


def errors_of(report):
    errs = []
    for e in report.get("errors", []) or []:
        if isinstance(e, dict) and e.get("type") == "warning":
            continue
        errs.append(e if isinstance(e, str) else f"{e.get('type', '')} {e.get('message', '')} ({e.get('function', '')})")
    for e in report.get("log_lines", []) or []:
        line = e.get("line", "") if isinstance(e, dict) else str(e)
        if line.startswith("ERROR") or line.startswith("SCRIPT ERROR"):
            errs.append(line)
    # Environmental: the audio driver under xvfb.
    return [e for e in errs if "ERR_CANT_OPEN" not in e and "ALSA" not in e]


def project_copy(group, settings, root, editor=None):
    dst = os.path.join(root, "project_" + group)
    if not os.path.exists(dst):
        shutil.copytree(os.path.join(HERE, "project"), dst, ignore=shutil.ignore_patterns(".godot"))
        if settings:
            lines = {}
            for key, value in settings.items():
                section, name = key.split("/", 1)
                lines.setdefault(section, []).append(f"{name}={json.dumps(value) if isinstance(value, str) else str(value).lower() if isinstance(value, bool) else value}")
            with open(os.path.join(dst, "override.cfg"), "w", encoding="utf-8") as f:
                for section, entries in lines.items():
                    f.write(f"[{section}]\n\n" + "\n".join(entries) + "\n\n")
        if editor:
            # Imported as a game's project is before it runs on a template (the global script class cache: without it
            # a release template prints "Could not load global script cache").
            subprocess.run([editor, "--headless", "--path", dst, "--import"], stdout=subprocess.DEVNULL,
                           stderr=subprocess.DEVNULL, timeout=600)
    return dst


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("command", choices=["run", "bless"])
    ap.add_argument("--godot", required=True, help="an editor build (debug checks on): the goldens' build")
    ap.add_argument("--template", default="", help="a release template (disable_path_overrides=no) to check against the editor build")
    ap.add_argument("--only", default="", help="group/scene or group entries, comma-separated")
    ap.add_argument("--out", default="", help="a directory for the pictures and report.json (default: a new temporary one)")
    ap.add_argument("--timeout", type=float, default=240.0, help="seconds per capture")
    ap.add_argument("--tolerance", default="", help="max_diff,share_over: a picture passes within either (default: suite.json's)")
    ap.add_argument("--goldens", default="", help="a directory of goldens instead of the repository's (another GPU or driver: bless into it, then run against it)")
    args = ap.parse_args()

    with open(os.path.join(HERE, "suite.json"), encoding="utf-8") as f:
        suite = json.load(f)
    tol = dict(suite["tolerance"])
    if args.tolerance:
        a, b = args.tolerance.split(",")
        tol = {"max_diff": int(a), "share_over": float(b)}
    if not os.path.exists(args.godot):
        print(json.dumps({"ok": False, "error": f"no binary {args.godot}"}))
        return 2
    goldens = os.path.abspath(args.goldens) if args.goldens else GOLDENS
    out = os.path.abspath(args.out) if args.out else tempfile.mkdtemp(prefix="fellow_regression_")
    os.makedirs(out, exist_ok=True)
    only = [o for o in args.only.split(",") if o]

    def tol_for(scene):
        # A scene's own tolerance (suite.json "scene_tolerance"): the larger of it and the run's, field by field.
        own = suite.get("scene_tolerance", {}).get(scene, {})
        return {k: max(tol[k], own.get(k, tol[k])) for k in tol}

    def wanted(group, scene):
        return not only or group in only or f"{group}/{scene}" in only

    builds = [("debug", os.path.abspath(args.godot))]
    if args.template and args.command == "run":
        builds.append(("release", os.path.abspath(args.template)))
    results, failures = [], []
    t0 = time.time()
    pictures = {}
    for group, spec in suite["groups"].items():
        scenes = [s for s in spec["scenes"] if wanted(group, s)]
        if not scenes:
            continue
        project = project_copy(group, spec.get("settings", {}), out, builds[0][1] if len(builds) > 1 else None)
        for scene in scenes:
            r = {"group": group, "scene": scene}
            for build, binary in builds:
                d = os.path.join(out, group, scene, build)
                os.makedirs(d, exist_ok=True)
                t = time.time()
                png, report = capture(binary, project, f"res://scenes/{scene}.tscn", suite, d, args.timeout)
                r[build] = {"seconds": round(time.time() - t, 1), "picture": png, "errors": errors_of(report)}
                if report.get("error"):
                    r[build]["error"] = report["error"]
                if png is None:
                    failures.append(f"{group}/{scene} ({build}): no picture ({report.get('error', 'see ' + d)})")
                    continue
                pictures[(group, scene, build)] = png
                if r[build]["errors"]:
                    failures.append(f"{group}/{scene} ({build}): {len(r[build]['errors'])} errors, the first: {r[build]['errors'][0]}")
            png = pictures.get((group, scene, "debug"))
            golden = os.path.join(goldens, group, f"{scene}.png")
            if args.command == "bless":
                if png:
                    os.makedirs(os.path.dirname(golden), exist_ok=True)
                    shutil.copyfile(png, golden)
                    r["blessed"] = golden
            elif png:
                if os.path.exists(golden):
                    r["golden"] = compare(png, golden)
                    if not within(r["golden"], tol_for(scene)):
                        failures.append(f"{group}/{scene}: differs from its golden (max {r['golden']['max_diff']}, {r['golden']['share_over'] * 100:.3f} % of pixels over 8 levels)")
                else:
                    r["golden"] = {"missing": True}
                    failures.append(f"{group}/{scene}: no golden (bless it)")
                rel = pictures.get((group, scene, "release"))
                if rel:
                    r["release_vs_debug"] = compare(rel, png)
                    if not within(r["release_vs_debug"], tol_for(scene)):
                        failures.append(f"{group}/{scene}: the release template's picture differs from the editor build's (max {r['release_vs_debug']['max_diff']})")
            results.append(r)
    # Groups that must render like another (FellowEngine off against on).
    for group, spec in suite["groups"].items():
        other = spec.get("same_as")
        if not other:
            continue
        for scene in spec["scenes"]:
            a = pictures.get((group, scene, "debug"))
            b = pictures.get((other, scene, "debug"))
            if a and b:
                c = compare(a, b)
                for r in results:
                    if r["group"] == group and r["scene"] == scene:
                        r[f"vs_{other}"] = c
                if not c["identical"] and spec.get("same_exactly", True):
                    failures.append(f"{group}/{scene}: differs from {other}/{scene} (max {c['max_diff']}, {c['share_differing'] * 100:.3f} % of pixels)")
    summary = {"ok": not failures, "command": args.command, "out": out, "goldens": goldens, "seconds": round(time.time() - t0, 1),
               "scenes": len(results), "failures": failures, "results": results, "tolerance": tol}
    with open(os.path.join(out, "report.json"), "w", encoding="utf-8") as f:
        json.dump(summary, f, indent=1)
    print(json.dumps(summary, indent=1))
    return 0 if not failures else 1


if __name__ == "__main__":
    sys.exit(main())
