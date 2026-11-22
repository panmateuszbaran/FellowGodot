#!/usr/bin/env python3
"""FellowEngine's character course (milestone 169, decision 0311): course.gd on both physics engines, for each variant.

    python benchmarks/tools/character_course/run_course.py --godot bin/godot.linuxbsd.editor.x86_64 \\
        --variants stock=-1,steps=0.35 [--engines godot,jolt] [--seconds 10] [--out DIR] [--extra "--radius=0.3"]

A variant is label=step (the bodies' floor_step_height; -1 leaves the property alone: stock's behaviour). Writes
DIR/<engine>_<label>.json and prints the table of each lane and speed side by side.
"""

import argparse
import json
import os
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ENGINES = {"godot": "GodotPhysics3D", "jolt": "Jolt Physics"}


def run(godot, engine, step, seconds, out, extra):
    tmp = tempfile.mkdtemp(prefix="course_")
    try:
        for f in ("project.godot", "course.gd"):
            shutil.copy(os.path.join(HERE, f), tmp)
        with open(os.path.join(tmp, "override.cfg"), "w") as f:
            f.write('[physics]\n\n3d/physics_engine="%s"\n' % ENGINES[engine])
        cmd = [godot, "--headless", "--path", tmp, "-s", "course.gd", "--", "--out=" + out, "--step=%g" % step,
               "--seconds=%g" % seconds] + extra
        r = subprocess.run(cmd, capture_output=True, text=True, timeout=1800)
        if r.returncode != 0 or not os.path.exists(out):
            sys.stderr.write(r.stdout[-3000:] + r.stderr[-3000:])
            raise SystemExit("course failed: " + " ".join(cmd))
        errors = [l for l in (r.stdout + r.stderr).splitlines() if "ERROR" in l or "SCRIPT ERROR" in l]
        return json.load(open(out)), errors
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--godot", required=True)
    ap.add_argument("--variants", default="stock=-1,steps=0.35")
    ap.add_argument("--engines", default="godot,jolt")
    ap.add_argument("--seconds", type=float, default=10.0)
    ap.add_argument("--out", default=tempfile.mkdtemp(prefix="course_out_"))
    ap.add_argument("--extra", default="")
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    variants = [(v.split("=")[0], float(v.split("=")[1])) for v in a.variants.split(",")]
    for engine in a.engines.split(","):
        results = {}
        for label, step in variants:
            data, errors = run(a.godot, engine, step, a.seconds, os.path.join(a.out, "%s_%s.json" % (engine, label)),
                               a.extra.split() if a.extra else [])
            results[label] = {(r["lane"], r["speed"]): r for r in data["rows"]}
            if errors:
                print("%s %s: %d engine errors, first: %s" % (engine, label, len(errors), errors[0]))
        print("\n%s" % ENGINES[engine])
        print("%-17s %5s | " % ("lane", "m/s") + " | ".join("%-52s" % l for l, _ in variants))
        keys = list(results[variants[0][0]].keys())
        for k in keys:
            cells = []
            for label, _ in variants:
                r = results[label].get(k)
                if r is None:
                    cells.append("%-52s" % "-")
                    continue
                if r["lane"].startswith("stop"):
                    cells.append("%-52s" % ("stop x%5.2f top %.2f air %3d chg %3d drift %.4f %.0fus" % (
                        r["end_x"], r["max_y"], r["airborne_ticks"], r["floor_changes"], r["drift"], r["us_per_move"])))
                    continue
                cells.append("%-52s" % ("%s top %.2f air %3d chg %3d stuck %3d up %.2f %.0fus" % (
                    ("%5.2fs" % r["seconds"]) if r["reached"] else "x%5.2f" % r["end_x"], r["max_y"],
                    r["airborne_ticks"], r["floor_changes"], r["stuck_ticks"], r["max_rise"], r["us_per_move"])))
            print("%-17s %5.1f | " % k + " | ".join(cells))


if __name__ == "__main__":
    main()
