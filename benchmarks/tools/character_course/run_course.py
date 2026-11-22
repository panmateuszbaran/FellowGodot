#!/usr/bin/env python3
"""FellowEngine's character courses on both physics engines, for each variant.

    python benchmarks/tools/character_course/run_course.py --godot bin/godot.linuxbsd.editor.x86_64 \\
        --variants stock=-1,steps=0.35 [--engines godot,jolt] [--seconds 10] [--out DIR] [--extra=--radius=0.3]
    python benchmarks/tools/character_course/run_course.py --godot ... --course push --variants stock=-1:-1,push=500:80

course.gd (milestone 169, decision 0311): a variant is label=step (the bodies' floor_step_height; -1 leaves it alone:
stock's behaviour). push_course.gd (milestone 170, decision 0312): label=force:mass (rigid_push_force,
rigid_standing_mass). Writes DIR/<engine>_<label>.json and prints the table of each lane side by side.
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


def run(godot, engine, value, seconds, out, extra, course="steps"):
    tmp = tempfile.mkdtemp(prefix="course_")
    script = "push_course.gd" if course == "push" else "course.gd"
    try:
        for f in ("project.godot", script):
            shutil.copy(os.path.join(HERE, f), tmp)
        with open(os.path.join(tmp, "override.cfg"), "w") as f:
            f.write('[physics]\n\n3d/physics_engine="%s"\n' % ENGINES[engine])
        if course == "push":
            force, mass = value.split(":")
            args = ["--push=" + force, "--mass=" + mass]
        else:
            args = ["--step=%g" % float(value)]
        cmd = [godot, "--headless", "--path", tmp, "-s", script, "--", "--out=" + out, "--seconds=%g" % seconds] + args + extra
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
    ap.add_argument("--course", default="steps", choices=["steps", "push"])
    a = ap.parse_args()
    if a.course == "push" and a.seconds == 10.0:
        a.seconds = 6.0
    os.makedirs(a.out, exist_ok=True)
    variants = [(v.split("=")[0], v.split("=")[1]) for v in a.variants.split(",")]
    for engine in a.engines.split(","):
        results = {}
        for label, step in variants:
            data, errors = run(a.godot, engine, step, a.seconds, os.path.join(a.out, "%s_%s.json" % (engine, label)),
                               a.extra.split() if a.extra else [], a.course)
            if a.course == "push":
                results[label] = {(r["lane"], 0): r for r in data["rows"]}
                if errors:
                    print("%s %s: %d engine errors, first: %s" % (engine, label, len(errors), errors[0]))
                continue
            results[label] = {(r["lane"], r["speed"]): r for r in data["rows"]}
            if errors:
                print("%s %s: %d engine errors, first: %s" % (engine, label, len(errors), errors[0]))
        print("\n%s" % ENGINES[engine])
        if a.course == "push":
            for k in results[variants[0][0]]:
                print("%-9s | " % k[0] + " | ".join(
                    "%s: body %5.2f obj %5.2f y%+.2f v %.2f up %.2f tilt %5.1f..%5.1f push %.2f %.0fus" % (
                        label, r["body_moved"], r["object_moved"], r["object_y"], r["max_speed"], r["max_up_speed"],
                        r["min_tilt"], r["max_tilt"], r["pushing_speed"], r["us_per_move"])
                    for label, r in ((l, results[l][k]) for l, _ in variants)))
            continue
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
