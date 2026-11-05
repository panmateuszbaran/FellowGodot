#!/usr/bin/env python3
"""FellowEngine's editor check (milestone 50).

Copies benchmarks/tools/editor_check/project to a new directory, opens it in a FellowEngine editor build, and lets the
project's editor plugin check the fellow_editor module: the class icons, the .bvh importer, the nodes' gizmos (and a
picture of them where the editor renders), the "Split into Streaming Cells" tool through its menu item and dialog, and
the Performance monitors; then loads the level the editor split as a game would (stream_back.gd). Writes editor.log,
stream_back.log, editor_check.json and gizmos.png to --out.

    python run_editor_check.py --godot bin/godot.linuxbsd.editor.x86_64 --out /tmp/editor_check [--headless]
"""

import argparse
import json
import os
import shutil
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--godot", required=True, help="a FellowEngine editor binary")
    ap.add_argument("--out", required=True, help="a new directory for the project's copy and the results")
    ap.add_argument("--headless", action="store_true", help="no window (no picture of the gizmos)")
    ap.add_argument("--driver", default="", help="--rendering-driver for the editor (vulkan, opengl3)")
    ap.add_argument("--timeout", type=float, default=900.0, help="seconds before the editor is stopped")
    ap.add_argument("--editor-args", default="", help="more arguments for the editor (e.g. --verbose)")
    ap.add_argument("--perf", default="", metavar="SCENE",
                    help="no check: the 3D view of SCENE (res://) redrawn every frame and measured (EDITOR_PERF)")
    ap.add_argument("--perf-seconds", type=float, default=10.0)
    ap.add_argument("--perf-idle", action="store_true", help="with --perf: the editor's default (redraw only on a change): how often it redraws")
    ap.add_argument("--perf-still", action="store_true", help="with --perf: FellowEngine's nodes still (the 3D toolbar's Animate toggle off)")
    ap.add_argument("--setting", action="append", default=[], metavar="SECTION/KEY=VALUE",
                    help="a project setting written into the copy's project.godot, e.g. rendering/fellow/enabled=false")
    ap.add_argument("--plain", type=int, default=0, metavar="FRAMES",
                    help="no check: the project without its plugin, the editor quit after FRAMES frames (its log only)")
    args = ap.parse_args()

    out = os.path.abspath(args.out)
    project = os.path.join(out, "project")
    if os.path.exists(project):
        print(f"{project} exists: give a new --out", file=sys.stderr)
        return 2
    os.makedirs(out, exist_ok=True)
    shutil.copytree(os.path.join(HERE, "project"), project)
    if args.plain:
        cfg = os.path.join(project, "project.godot")
        with open(cfg, encoding="utf-8") as f:
            text = f.read()
        with open(cfg, "w", encoding="utf-8") as f:
            f.write(text.split("[editor_plugins]")[0])

    if args.setting:
        cfg = os.path.join(project, "project.godot")
        with open(cfg, encoding="utf-8") as f:
            text = f.read()
        extra = {}
        for kv in args.setting:
            key, value = kv.split("=", 1)
            section, name = key.split("/", 1)
            extra.setdefault(section, []).append(f"{name}={value}")
        for section, lines in extra.items():
            text += f"\n[{section}]\n\n" + "\n".join(lines) + "\n"
        with open(cfg, "w", encoding="utf-8") as f:
            f.write(text)

    cmd = [args.godot, "--editor", "--path", project] + args.editor_args.split()
    if args.plain:
        cmd += ["--quit-after", str(args.plain)]
    if args.headless:
        cmd.append("--headless")
    if args.driver:
        cmd += ["--rendering-driver", args.driver]
    cmd += ["--", f"--out={out}"]
    if args.perf:
        cmd += [f"--perf={args.perf}", f"--perf_seconds={args.perf_seconds}", f"--perf_continuous={'false' if args.perf_idle else 'true'}",
                f"--perf_animate={'false' if args.perf_still else 'true'}"]
    log_path = os.path.join(out, "editor.log")
    t0 = time.time()
    with open(log_path, "w", encoding="utf-8", errors="replace") as log:
        try:
            rc = subprocess.run(cmd, stdout=log, stderr=subprocess.STDOUT, timeout=args.timeout).returncode
        except subprocess.TimeoutExpired:
            rc = "timeout"
    seconds = time.time() - t0

    with open(log_path, encoding="utf-8", errors="replace") as f:
        lines = f.read().splitlines()
    result = None
    for line in lines:
        if line.startswith("EDITOR_CHECK {"):
            result = json.loads(line[len("EDITOR_CHECK "):])
    errors = [line for line in lines if line.startswith("ERROR:") or line.startswith("SCRIPT ERROR:")]
    if args.perf:
        perf = None
        for line in lines:
            if line.startswith("EDITOR_PERF {"):
                perf = json.loads(line[len("EDITOR_PERF "):])
        summary = {"returncode": rc, "seconds": round(seconds, 1), "errors": len(errors), "first_errors": errors[:10], "perf": perf}
        with open(os.path.join(out, "editor_perf.json"), "w", encoding="utf-8") as f:
            json.dump(summary, f, indent=1)
        print(json.dumps(summary, indent=1))
        return 0 if perf else 1
    if args.plain:
        summary = {"returncode": rc, "seconds": round(seconds, 1), "errors": len(errors), "first_errors": errors[:10],
                   "warnings": [line for line in lines if line.startswith("WARNING:")][:10]}
        with open(os.path.join(out, "editor_check.json"), "w", encoding="utf-8") as f:
            json.dump(summary, f, indent=1)
        print(json.dumps(summary, indent=1))
        return 0

    # The level the editor split and saved, loaded as a game would and streamed back.
    back_log = os.path.join(out, "stream_back.log")
    with open(back_log, "w", encoding="utf-8", errors="replace") as log:
        try:
            back_rc = subprocess.run([args.godot, "--headless", "--path", project, "-s", "res://stream_back.gd"],
                                     stdout=log, stderr=subprocess.STDOUT, timeout=300).returncode
        except subprocess.TimeoutExpired:
            back_rc = "timeout"
    back = None
    with open(back_log, encoding="utf-8", errors="replace") as f:
        for line in f.read().splitlines():
            if line.startswith("STREAM_BACK {"):
                back = json.loads(line[len("STREAM_BACK "):])
    summary = {"returncode": rc, "seconds": round(seconds, 1), "errors": len(errors), "first_errors": errors[:10],
               "check": result, "stream_back_returncode": back_rc, "stream_back": back}
    with open(os.path.join(out, "editor_check.json"), "w", encoding="utf-8") as f:
        json.dump(summary, f, indent=1)
    print(json.dumps(summary, indent=1))
    ok = rc == 0 and result is not None and not result.get("failures") and back_rc == 0
    print("EDITOR_CHECK_OK" if ok else "EDITOR_CHECK_FAILED")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
