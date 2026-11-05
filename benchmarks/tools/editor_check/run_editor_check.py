#!/usr/bin/env python3
"""FellowEngine's editor check (milestone 50).

Copies benchmarks/tools/editor_check/project to a new directory, opens it in a FellowEngine editor build, and lets the
project's editor plugin check the fellow_editor module: the class icons, the .bvh importer, the nodes' gizmos (and a
picture of them where the editor renders), the "Split into Streaming Cells" tool through its menu item and dialog, and
the Performance monitors. Writes editor.log, editor_check.json and gizmos.png to --out.

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
    args = ap.parse_args()

    out = os.path.abspath(args.out)
    project = os.path.join(out, "project")
    if os.path.exists(project):
        print(f"{project} exists: give a new --out", file=sys.stderr)
        return 2
    os.makedirs(out, exist_ok=True)
    shutil.copytree(os.path.join(HERE, "project"), project)

    cmd = [args.godot, "--editor", "--path", project]
    if args.headless:
        cmd.append("--headless")
    if args.driver:
        cmd += ["--rendering-driver", args.driver]
    cmd += ["--", f"--out={out}"]
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
    summary = {"returncode": rc, "seconds": round(seconds, 1), "errors": len(errors), "first_errors": errors[:10],
               "check": result}
    with open(os.path.join(out, "editor_check.json"), "w", encoding="utf-8") as f:
        json.dump(summary, f, indent=1)
    print(json.dumps(summary, indent=1))
    ok = rc == 0 and result is not None and not result.get("failures")
    print("EDITOR_CHECK_OK" if ok else "EDITOR_CHECK_FAILED")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
