#!/usr/bin/env python3
"""The agent toolkit's self-test (milestone 51): every command of fellow_agent.py on test_project/, with what each
must report. Prints one line per check and a JSON summary; exits 1 if any failed.

  python tools/fellow_agent/selftest.py --godot <an editor build> --out <new directory> [--size 640x360]
"""

import argparse
import json
import os
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
TOOL = os.path.join(HERE, "fellow_agent.py")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--godot", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--size", default="640x360")
    ap.add_argument("--no-import", action="store_true", help="the project is already imported (or the build cannot)")
    a = ap.parse_args()
    out = os.path.abspath(a.out)
    project = os.path.join(out, "test_project")
    if os.path.exists(project):
        print(f"{project} exists: give a new --out", file=sys.stderr)
        return 2
    shutil.copytree(os.path.join(HERE, "test_project"), project)
    results = []

    def tool(name, *args):
        cmd = [sys.executable, TOOL, name] + list(args)
        if name not in ("compare", "api", "init"):
            cmd += ["--godot", a.godot]
        p = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        text = p.stdout.decode("utf-8", errors="replace")
        try:
            data = json.loads(text)
        except ValueError:
            data = {"text": text}
        return p.returncode, data

    def expect(what, ok, detail=""):
        results.append({"check": what, "ok": bool(ok), "detail": detail})
        print(("PASS " if ok else "FAIL ") + what + (f"  ({detail})" if detail else ""), flush=True)

    def report(d):
        path = d.get("report")
        if path and os.path.exists(path):
            with open(path, encoding="utf-8") as f:
                return json.load(f)
        return d

    if not a.no_import:
        rc, d = tool("import", "--project", project)
        expect("import", rc in (0, 1) and d.get("imported_files", 0) >= 0, f"rc {rc}, {d.get('imported_files')} files")

    # check: the good scenes and scripts pass, each planted fault is reported where it is.
    rc, d = tool("check", "--project", project, "--out", os.path.join(out, "check"))
    r = report(d)
    by_path = {x["path"]: x for x in r.get("scenes", []) + r.get("scripts", [])}
    expect("check: exits 1 (faults planted)", rc == 1, f"rc {rc}")
    for good in ("res://scenes/fellow_3d.tscn", "res://scenes/with_camera.tscn", "res://scenes/ui.tscn", "res://scenes/spinning.tscn", "res://scripts/spinner.gd"):
        expect(f"check: {good} ok", by_path.get(good, {}).get("ok"), json.dumps(by_path.get(good, {}).get("errors", []))[:200])
    parse = by_path.get("res://scripts/does_not_parse.gd", {})
    expect("check: the parse error found", parse and not parse.get("ok") and any("does_not_parse.gd" in (e.get("file", "") + e.get("message", "")) for e in parse.get("errors", [])),
           json.dumps(parse.get("errors", []))[:300])
    rt = by_path.get("res://scenes/runtime_error.tscn", {})
    rt_loc = [e.get("script_location", "") for e in rt.get("errors", [])]
    expect("check: the runtime error found, with its line", rt and not rt.get("ok") and any(l.endswith("fails_at_ready.gd:7") for l in rt_loc), str(rt_loc))
    md = by_path.get("res://scenes/missing_dependency.tscn", {})
    expect("check: the missing dependency found", md and not md.get("ok") and "res://scenes/gone.tscn" in md.get("missing_dependencies", []), str(md.get("missing_dependencies")))

    # capture: framed views of a scene without a camera or light.
    rc, d = tool("capture", "--project", project, "--scene", "res://scenes/fellow_3d.tscn", "--views", "iso,top,front", "--size", a.size, "--out", os.path.join(out, "views"))
    r = report(d)
    views = r.get("views", [])
    expect("capture views: exit 0", rc == 0, f"rc {rc} {r.get('error', '')}")
    expect("capture views: three pictures and a sheet", len(views) == 3 and all(v.get("image") and os.path.exists(v["image"]) for v in views) and os.path.exists(r.get("sheet", "")))
    expect("capture views: none blank", views and all(not v.get("stats", {}).get("blank", True) for v in views), str([v.get("stats", {}).get("luma_spread") for v in views]))
    expect("capture views: preview lighting added", r.get("preview_lighting_added") == ["sun", "environment"], str(r.get("preview_lighting_added")))
    # capture: the scene's camera, twice — the same picture.
    shots = []
    for i in range(2):
        rc, d = tool("capture", "--project", project, "--scene", "res://scenes/with_camera.tscn", "--size", a.size, "--out", os.path.join(out, f"camera{i}"))
        r = report(d)
        v = (r.get("views") or [{}])[0]
        shots.append(v.get("image", ""))
        if i == 0:
            expect("capture camera: the scene's camera used", v.get("view") == "camera" and v.get("camera", {}).get("path") == "Camera" and not r.get("preview_lighting_added"), str(v.get("camera")))
    rc, d = tool("compare", shots[0], shots[1])
    expect("compare: two captures identical (deterministic)", rc == 0 and d.get("identical"), f"max diff {d.get('max_diff')}")
    rc, d = tool("compare", views[0]["image"] if views else "", views[2]["image"] if len(views) > 2 else "", "--out", os.path.join(out, "diff.png"))
    expect("compare: different views differ", rc == 1 and not d.get("ok") and os.path.exists(os.path.join(out, "diff.png")), f"over {d.get('pixels_over_threshold')}")
    # capture: a 2D scene.
    rc, d = tool("capture", "--project", project, "--scene", "res://scenes/ui.tscn", "--size", a.size, "--out", os.path.join(out, "ui"))
    r = report(d)
    v = (r.get("views") or [{}])[0]
    expect("capture 2D: the screen, not blank", rc == 0 and v.get("view") == "screen" and not v.get("stats", {}).get("blank", True), str(v.get("stats")))

    # describe
    rc, d = tool("describe", "--project", project, "--scene", "res://scenes/fellow_3d.tscn", "--out", os.path.join(out, "describe"))
    r = report(d)
    kids = {k["name"]: k for k in r.get("tree", {}).get("children", [])}
    expect("describe: the tree with FellowEngine's nodes and their properties", rc == 0 and kids.get("Pond", {}).get("class") == "FellowWater3D"
           and kids.get("Pond", {}).get("properties", {}).get("extent") == "Vector2(4, 2)" and kids.get("Crate", {}).get("position") == "Vector3(-1.5, 0.5, 0)",
           json.dumps(kids.get("Pond"))[:200])

    # run
    rc, d = tool("run", "--project", project, "--scene", "res://scenes/spinning.tscn", "--seconds", "2", "--size", a.size, "--out", os.path.join(out, "run"))
    r = report(d)
    expect("run: frames measured, a last frame", rc == 0 and r.get("frames", 0) > 10 and r.get("frame_ms", {}).get("p50", 0) > 0 and os.path.exists(r.get("image", "")),
           f"{r.get('frames')} frames, {r.get('frame_ms')}")

    # api and init
    p = subprocess.run([sys.executable, TOOL, "api", "FellowWater3D"], stdout=subprocess.PIPE)
    text = p.stdout.decode("utf-8", errors="replace")
    expect("api: FellowWater3D's reference", p.returncode == 0 and "extent" in text and "Members:" in text)
    rc, d = tool("init", "--project", project)
    agents = os.path.join(project, "AGENTS.md")
    expect("init: AGENTS.md written", rc == 0 and os.path.exists(agents) and "fellow_agent.py" in open(agents, encoding="utf-8").read())

    failed = [x for x in results if not x["ok"]]
    summary = {"checks": len(results), "failed": len(failed), "failures": failed, "out": out}
    with open(os.path.join(out, "selftest.json"), "w", encoding="utf-8") as f:
        json.dump({"summary": summary, "results": results}, f, indent=1)
    print(json.dumps(summary, indent=1))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
