#!/usr/bin/env python3
"""FellowEngine's agent toolkit (milestone 51): what an AI agent (or a script) needs to check its own work in a
FellowEngine project without a person looking — every command prints JSON and exits 0 (fine), 1 (it ran and found
errors) or 2 (it could not run).

  capture   render a scene: from its camera, or from views framed on its contents (iso, front, back, left, right,
            top); a PNG per view, a contact sheet, image and rendering statistics, the errors with file and line
  describe  a scene's node tree as data: classes, transforms, bounds, the properties that differ from the defaults
  check     every scene and script of a project (or the ones given) loaded, instantiated and run a few frames
  run       a scene run for some seconds: frame time percentiles, GPU time, errors, the last frame
  compare   two PNGs: mean and largest difference, pixels over a threshold, PSNR, a difference image
  import    the project's assets imported by an editor build (needed once before the others, and after new assets)
  api       FellowEngine's / Godot's class reference for a class, from the repository's XML (or --search)
  init      write an AGENTS.md for a game project, telling agents how to use this toolkit

The engine binary: --godot, or $FELLOW_GODOT, or the newest bin/godot.* of this repository (an editor build is needed
for `import`; the others run with an export template too). On Linux without a display, capture and run use xvfb-run.

  python tools/fellow_agent/fellow_agent.py capture --project my_game --scene res://levels/forest.tscn --out /tmp/shot
"""

import argparse
import glob
import json
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import time
import zlib
import xml.etree.ElementTree as ET

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
GD_SCRIPT = os.path.join(HERE, "fellow_agent.gd")
LOG_LINE = re.compile(r"^(ERROR|SCRIPT ERROR|WARNING|SHADER ERROR): (.*)")


# ------------------------------------------------------------------ the engine

def find_godot(explicit):
    if explicit:
        return explicit
    if os.environ.get("FELLOW_GODOT"):
        return os.environ["FELLOW_GODOT"]
    cands = [p for p in glob.glob(os.path.join(REPO, "bin", "godot.*")) if os.access(p, os.X_OK) and not p.endswith((".pdb", ".exp", ".lib", ".map"))]
    if not cands:
        return None
    # Editor builds first, then the newest.
    cands.sort(key=lambda p: (".editor." in os.path.basename(p), os.path.getmtime(p)), reverse=True)
    return cands[0]


def needs_xvfb():
    return sys.platform.startswith("linux") and not os.environ.get("DISPLAY") and not os.environ.get("WAYLAND_DISPLAY")


def run_engine(godot, engine_args, timeout, rendering, size="1280x720"):
    cmd = [godot, "--audio-driver", "Dummy"] + engine_args
    if rendering and needs_xvfb():
        if not shutil.which("xvfb-run"):
            return None, "no display and no xvfb-run: capture and run need a renderer (install xvfb)", ""
        w, h = size.split("x")
        cmd = ["xvfb-run", "-a", "-s", f"-screen 0 {int(w) + 64}x{int(h) + 64}x24"] + cmd
    t0 = time.time()
    try:
        p = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=timeout)
        out = p.stdout.decode("utf-8", errors="replace")
        rc = p.returncode
    except subprocess.TimeoutExpired as e:
        out = (e.stdout or b"").decode("utf-8", errors="replace")
        rc = "timeout"
    return {"returncode": rc, "seconds": round(time.time() - t0, 2), "command": cmd}, None, out


def parse_output(out):
    result = None
    log = {}
    for line in out.splitlines():
        if line.startswith("FELLOW_AGENT {"):
            try:
                result = json.loads(line[len("FELLOW_AGENT "):])
            except ValueError:
                pass
            continue
        m = LOG_LINE.match(line)
        if m:
            key = f"{m.group(1)}: {m.group(2)}"
            log[key] = log.get(key, 0) + 1
    log_lines = [{"line": k, "count": v} for k, v in log.items()]
    return result, log_lines


def engine_command(a, script_args, rendering):
    godot = find_godot(a.godot)
    if not godot:
        return fail("no engine binary: pass --godot or set FELLOW_GODOT")
    project = os.path.abspath(a.project)
    if not os.path.exists(os.path.join(project, "project.godot")):
        return fail(f"no project.godot in {project}")
    out_dir = os.path.abspath(a.out) if getattr(a, "out", None) else tempfile.mkdtemp(prefix="fellow_agent_")
    os.makedirs(out_dir, exist_ok=True)
    engine_args = ["--path", project]
    if not rendering:
        engine_args.append("--headless")
    else:
        engine_args += ["--resolution", a.size, "--fixed-fps", str(a.fps)] if a.command == "capture" else ["--resolution", a.size]
        if a.command == "run" and not a.vsync:
            engine_args.append("--disable-vsync")
    if getattr(a, "renderer", None):
        engine_args += ["--rendering-method", a.renderer]
    if getattr(a, "driver", None):
        engine_args += ["--rendering-driver", a.driver]
    engine_args += ["-s", GD_SCRIPT, "--", f"--out={out_dir}", f"--seed={getattr(a, 'seed', 1)}"] + script_args
    info, err, out = run_engine(godot, engine_args, a.timeout, rendering, getattr(a, "size", "1280x720"))
    if err:
        return fail(err)
    with open(os.path.join(out_dir, "engine.log"), "w", encoding="utf-8") as f:
        f.write(out)
    result, log_lines = parse_output(out)
    if result is not None:
        report = result
    else:
        rc = info["returncode"]
        why = "timed out" if rc == "timeout" else (f"crashed (signal {-rc if rc < 0 else rc - 128})" if (isinstance(rc, int) and (rc < 0 or rc > 128)) else f"exited with {rc}")
        report = {"ok": False, "error": f"the engine {why} before printing a result: see engine.log"}
        if not os.path.basename(godot).count(".editor.") and ".template_release" in os.path.basename(godot):
            report["hint_build"] = "a release template does not check scripts' errors (a call on null crashes it): use an editor build or a debug template"
    report["engine_run"] = {"binary": godot, "returncode": info["returncode"], "seconds": info["seconds"]}
    report["engine_log"] = os.path.join(out_dir, "engine.log")
    if result is not None and result.get("debug_build") is False:
        report["warning_build"] = "a release build: scripts' errors are not checked (a call on null can crash it); use an editor build or a debug template for check and run"
    if not os.path.isdir(os.path.join(project, ".godot")):
        report["hint"] = "the project has not been imported (no .godot/): textures and other imported assets will not load; run `fellow_agent.py import` with an editor build first"
    # Lines the Logger could not catch (before the script started, after it finished, other threads' prints).
    caught = {e.get("message", "") for e in report.get("errors", [])}
    extra = [l for l in log_lines if not any(c and c in l["line"] for c in caught)]
    if extra:
        report["log_lines"] = extra[:40]
    return finish(report, out_dir, a)


def fail(message):
    print(json.dumps({"ok": False, "error": message}, indent=1))
    return 2


def finish(report, out_dir, a):
    with open(os.path.join(out_dir, "report.json"), "w", encoding="utf-8") as f:
        json.dump(report, f, indent=1)
    report_path = os.path.join(out_dir, "report.json")
    errors = [e for e in report.get("errors", []) if e.get("type") != "warning"]
    if a.quiet:
        brief = {k: report[k] for k in ("command", "ok", "scene", "summary", "error") if k in report}
        brief["errors"] = len(errors)
        brief["report"] = report_path
        if "views" in report:
            brief["images"] = [v.get("image") for v in report["views"]]
        print(json.dumps(brief, indent=1))
    else:
        report["report"] = report_path
        print(json.dumps(report, indent=1))
    if not report.get("ok", False) and report.get("engine_run", {}).get("returncode") not in (0, 1):
        return 2
    return 0 if report.get("ok") and not errors else 1


# ------------------------------------------------------------------ PNG (for compare; 8-bit, non-interlaced)

def read_png(path):
    with open(path, "rb") as f:
        data = f.read()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError(f"{path}: not a PNG")
    pos = 8
    idat = b""
    w = h = depth = ctype = interlace = None
    while pos < len(data):
        n, kind = struct.unpack(">I4s", data[pos:pos + 8])
        chunk = data[pos + 8:pos + 8 + n]
        pos += 12 + n
        if kind == b"IHDR":
            w, h, depth, ctype, _, _, interlace = struct.unpack(">IIBBBBB", chunk)
        elif kind == b"IDAT":
            idat += chunk
        elif kind == b"IEND":
            break
    if depth != 8 or interlace != 0 or ctype not in (0, 2, 4, 6):
        raise ValueError(f"{path}: only 8-bit, non-interlaced grey/RGB/RGBA PNGs")
    ch = {0: 1, 2: 3, 4: 2, 6: 4}[ctype]
    raw = zlib.decompress(idat)
    stride = w * ch
    out = bytearray(h * stride)
    prev = bytearray(stride)
    i = 0
    for y in range(h):
        ft = raw[i]
        line = bytearray(raw[i + 1:i + 1 + stride])
        i += 1 + stride
        for x in range(stride):
            a = line[x - ch] if x >= ch else 0
            b = prev[x]
            c = prev[x - ch] if x >= ch else 0
            if ft == 1:
                line[x] = (line[x] + a) & 255
            elif ft == 2:
                line[x] = (line[x] + b) & 255
            elif ft == 3:
                line[x] = (line[x] + ((a + b) >> 1)) & 255
            elif ft == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[x] = (line[x] + (a if pa <= pb and pa <= pc else (b if pb <= pc else c))) & 255
        out[y * stride:(y + 1) * stride] = line
        prev = line
    # To RGB.
    rgb = bytearray(w * h * 3)
    for p in range(w * h):
        s = out[p * ch:p * ch + ch]
        if ch <= 2:
            rgb[p * 3:p * 3 + 3] = bytes((s[0], s[0], s[0]))
        else:
            rgb[p * 3:p * 3 + 3] = s[:3]
    return w, h, rgb


def write_png(path, w, h, rgb):
    raw = b"".join(b"\x00" + bytes(rgb[y * w * 3:(y + 1) * w * 3]) for y in range(h))

    def chunk(kind, body):
        return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body) & 0xFFFFFFFF)

    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
                + chunk(b"IDAT", zlib.compress(raw, 6)) + chunk(b"IEND", b""))


def compare_fast(a):
    # With Pillow and NumPy (when installed) the same numbers, faster ($FELLOW_AGENT_PURE=1: the standard library's).
    if os.environ.get("FELLOW_AGENT_PURE"):
        return None
    try:
        import numpy as np
        from PIL import Image
    except ImportError:
        return None
    ia = np.asarray(Image.open(a.a).convert("RGB")).astype(np.int32)
    ib = np.asarray(Image.open(a.b).convert("RGB")).astype(np.int32)
    if ia.shape != ib.shape:
        return {"ok": False, "error": f"sizes differ: {ia.shape[1]}x{ia.shape[0]} and {ib.shape[1]}x{ib.shape[0]}"}
    d = np.abs(ia - ib)
    m = d.max(axis=2)
    mse = float((d * d).mean())
    r = {"command": "compare", "a": a.a, "b": a.b, "size": [ia.shape[1], ia.shape[0]],
         "mean_abs_diff": round(float(d.mean()), 4), "max_diff": int(m.max()),
         "pixels_over_threshold": round(float((m > a.threshold).mean()), 6), "threshold": a.threshold,
         "psnr_db": None if mse == 0 else round(10 * float(np.log10(255 * 255 / mse)), 2), "identical": int(m.max()) == 0}
    if a.out:
        Image.fromarray(np.minimum(m * 8, 255).astype(np.uint8)).convert("RGB").save(a.out)
        r["diff_image"] = os.path.abspath(a.out)
    r["ok"] = r["pixels_over_threshold"] <= a.max_fraction
    return r


def cmd_compare(a):
    try:
        fast = compare_fast(a)
    except (OSError, ValueError) as e:
        return fail(str(e))
    if fast is not None:
        print(json.dumps(fast, indent=1))
        return 0 if fast.get("ok") else 1
    try:
        wa, ha, pa = read_png(a.a)
        wb, hb, pb = read_png(a.b)
    except (OSError, ValueError) as e:
        return fail(str(e))
    if (wa, ha) != (wb, hb):
        print(json.dumps({"ok": False, "error": f"sizes differ: {wa}x{ha} and {wb}x{hb}"}, indent=1))
        return 1
    n = wa * ha
    total = 0
    sq = 0
    worst = 0
    over = 0
    diff = bytearray(n * 3)
    for p in range(n):
        d0 = abs(pa[p * 3] - pb[p * 3])
        d1 = abs(pa[p * 3 + 1] - pb[p * 3 + 1])
        d2 = abs(pa[p * 3 + 2] - pb[p * 3 + 2])
        m = max(d0, d1, d2)
        total += d0 + d1 + d2
        sq += d0 * d0 + d1 * d1 + d2 * d2
        worst = max(worst, m)
        if m > a.threshold:
            over += 1
        v = min(255, m * 8)
        diff[p * 3:p * 3 + 3] = bytes((v, v, v))
    mse = sq / (n * 3)
    psnr = float("inf") if mse == 0 else 10 * __import__("math").log10(255 * 255 / mse)
    r = {"command": "compare", "a": a.a, "b": a.b, "size": [wa, ha], "mean_abs_diff": round(total / (n * 3), 4),
         "max_diff": worst, "pixels_over_threshold": round(over / n, 6), "threshold": a.threshold,
         "psnr_db": None if psnr == float("inf") else round(psnr, 2), "identical": worst == 0}
    if a.out:
        write_png(a.out, wa, ha, diff)
        r["diff_image"] = os.path.abspath(a.out)
    r["ok"] = over / n <= a.max_fraction
    print(json.dumps(r, indent=1))
    return 0 if r["ok"] else 1


# ------------------------------------------------------------------ import

def cmd_import(a):
    godot = find_godot(a.godot)
    if not godot:
        return fail("no engine binary: pass --godot or set FELLOW_GODOT")
    project = os.path.abspath(a.project)
    info, err, out = run_engine(godot, ["--headless", "--editor", "--import", "--path", project], a.timeout, False)
    if err:
        return fail(err)
    _, log_lines = parse_output(out)
    errors = [l for l in log_lines if not l["line"].startswith("WARNING")]
    imported = len(glob.glob(os.path.join(project, ".godot", "imported", "*")))
    r = {"command": "import", "project": project, "ok": info["returncode"] == 0, "engine_run": {"binary": godot,
         "returncode": info["returncode"], "seconds": info["seconds"]}, "imported_files": imported,
         "errors": errors[:40], "warnings": [l for l in log_lines if l["line"].startswith("WARNING")][:20]}
    print(json.dumps(r, indent=1))
    if info["returncode"] not in (0,):
        return 2
    return 1 if errors else 0


# ------------------------------------------------------------------ api

def doc_files():
    files = glob.glob(os.path.join(REPO, "doc", "classes", "*.xml"))
    files += glob.glob(os.path.join(REPO, "modules", "*", "doc_classes", "*.xml"))
    files += glob.glob(os.path.join(REPO, "platform", "*", "doc_classes", "*.xml"))
    return {os.path.splitext(os.path.basename(f))[0]: f for f in files}


def first_sentence(text, full):
    t = " ".join((text or "").split())
    if full or len(t) < 200:
        return t
    m = re.match(r"(.{40,}?[.!?])\s", t)
    return m.group(1) if m else t[:200] + "..."


def cmd_api(a):
    docs = doc_files()
    if not docs:
        return fail("no class reference found (run from a FellowEngine source tree)")
    if a.search:
        q = a.search.lower()
        hits = []
        for cls, path in sorted(docs.items()):
            if q in cls.lower():
                hits.append({"class": cls})
            try:
                root = ET.parse(path).getroot()
            except ET.ParseError:
                continue
            for tag in ("member", "method", "signal", "constant"):
                for e in root.iter(tag):
                    if q in (e.get("name") or "").lower():
                        hits.append({"class": cls, tag: e.get("name")})
        print(json.dumps({"command": "api", "search": a.search, "hits": hits[:200], "count": len(hits)}, indent=1))
        return 0
    if a.cls not in docs:
        import difflib
        close = sorted({c for c in docs if a.cls.lower() in c.lower()} | set(difflib.get_close_matches(a.cls, list(docs), 8, 0.7)))[:20]
        print(json.dumps({"ok": False, "error": f"no class {a.cls}", "did_you_mean": close}, indent=1))
        return 1
    root = ET.parse(docs[a.cls]).getroot()
    lines = [f"{a.cls} (inherits {root.get('inherits', '-')})  [{os.path.relpath(docs[a.cls], REPO)}]",
             first_sentence(root.findtext("brief_description"), True)]
    desc = first_sentence(root.findtext("description"), a.full)
    if desc:
        lines += ["", desc]
    members = root.find("members")
    if members is not None and len(members):
        lines += ["", "Members:"]
        for m in members:
            lines.append(f"  {m.get('name')}: {m.get('type')} = {m.get('default', '')}  — {first_sentence(m.text, a.full)}")
    methods = root.find("methods")
    if methods is not None and len(methods):
        lines += ["", "Methods:"]
        for m in methods:
            ret = m.find("return")
            params = ", ".join(f"{p.get('name')}: {p.get('type')}" + (f" = {p.get('default')}" if p.get("default") else "") for p in m.findall("param"))
            lines.append(f"  {m.get('name')}({params}) -> {ret.get('type') if ret is not None else 'void'}  — {first_sentence(m.findtext('description'), a.full)}")
    signals = root.find("signals")
    if signals is not None and len(signals):
        lines += ["", "Signals:"]
        for s in signals:
            params = ", ".join(f"{p.get('name')}: {p.get('type')}" for p in s.findall("param"))
            lines.append(f"  {s.get('name')}({params})  — {first_sentence(s.findtext('description'), a.full)}")
    constants = root.find("constants")
    if constants is not None and len(constants):
        lines += ["", "Constants:"]
        for c in constants:
            enum = f" ({c.get('enum')})" if c.get("enum") else ""
            lines.append(f"  {c.get('name')} = {c.get('value')}{enum}  — {first_sentence(c.text, a.full)}")
    print("\n".join(lines))
    return 0


# ------------------------------------------------------------------ init

def cmd_init(a):
    project = os.path.abspath(a.project)
    if not os.path.exists(os.path.join(project, "project.godot")):
        return fail(f"no project.godot in {project}")
    target = os.path.join(project, "AGENTS.md")
    if os.path.exists(target) and not a.force:
        return fail(f"{target} exists (--force to replace it)")
    with open(os.path.join(HERE, "AGENTS.project.md"), encoding="utf-8") as f:
        text = f.read()
    tool = os.path.relpath(os.path.join(HERE, "fellow_agent.py"), project) if not a.absolute else os.path.join(HERE, "fellow_agent.py")
    text = text.replace("{{FELLOW_AGENT}}", tool.replace("\\", "/"))
    with open(target, "w", encoding="utf-8") as f:
        f.write(text)
    print(json.dumps({"ok": True, "written": target}, indent=1))
    return 0


# ------------------------------------------------------------------ main

def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="command", required=True)

    def engine_parser(name, help_text, rendering):
        p = sub.add_parser(name, help=help_text)
        p.add_argument("--project", required=True, help="the project's directory (with project.godot)")
        p.add_argument("--godot", help="the engine binary (default: $FELLOW_GODOT, then this repository's bin/)")
        p.add_argument("--out", help="a directory for the report and pictures (default: a new temporary one)")
        p.add_argument("--timeout", type=float, default=600.0)
        p.add_argument("--quiet", action="store_true", help="print a short summary; the full report is in report.json")
        p.add_argument("--seed", type=int, default=1, help="the global random sequence's seed (randf(), particles without a fixed seed)")
        if rendering:
            p.add_argument("--size", default="1280x720", help="the window's size")
            p.add_argument("--renderer", choices=["forward_plus", "mobile", "gl_compatibility"])
            p.add_argument("--driver", help="--rendering-driver (vulkan, opengl3, d3d12, metal)")
        return p

    p = engine_parser("capture", "render a scene to PNGs", True)
    p.add_argument("--scene", required=True, help="res:// path of the scene (a .tscn, .scn, an imported .glb/.gltf, or a node script that builds one)")
    p.add_argument("--views", default="auto", help="auto (the scene's camera, else iso), or a list of iso,front,back,left,right,top,camera")
    p.add_argument("--camera", help="a Camera3D's path in the scene to use for the 'camera' view")
    p.add_argument("--frames", type=int, default=30, help="frames rendered before each picture (TAA, streaming, effects settle)")
    p.add_argument("--fps", type=int, default=60, help="the fixed frame rate (--fixed-fps: the same pictures every run)")
    p.add_argument("--preview-lighting", choices=["auto", "on", "off"], default="auto", help="a sun and sky where the scene has none (auto)")

    p = engine_parser("describe", "a scene's tree as data", False)
    p.add_argument("--scene", required=True)
    p.add_argument("--max-nodes", type=int, default=400)
    p.add_argument("--depth", type=int, default=12)

    p = engine_parser("check", "load, instantiate and run every scene and script", False)
    p.add_argument("--scenes", help="comma-separated res:// scenes (default: every .tscn/.scn and .gd outside addons/)")
    p.add_argument("--frames", type=int, default=5)
    p.add_argument("--addons", action="store_true", help="include addons/")
    p.add_argument("--render", action="store_true", help="with a renderer instead of --headless (RenderingDevice nodes run)")

    p = engine_parser("run", "run a scene and measure it", True)
    p.add_argument("--scene", required=True)
    p.add_argument("--seconds", type=float, default=5.0)
    p.add_argument("--warmup", type=int, default=30, help="frames before measuring")
    p.add_argument("--vsync", action="store_true", help="keep V-Sync (off by default: the frame times are the engine's)")
    p.add_argument("--no-picture", action="store_true")

    p = sub.add_parser("compare", help="compare two PNGs")
    p.add_argument("a")
    p.add_argument("b")
    p.add_argument("--out", help="write a difference image (8x amplified) here")
    p.add_argument("--threshold", type=int, default=8, help="a pixel counts as different above this (0-255)")
    p.add_argument("--max-fraction", type=float, default=0.001, help="ok while at most this share of pixels differ")

    p = sub.add_parser("import", help="import the project's assets (an editor build)")
    p.add_argument("--project", required=True)
    p.add_argument("--godot")
    p.add_argument("--timeout", type=float, default=1800.0)

    p = sub.add_parser("api", help="the class reference of a class")
    p.add_argument("cls", nargs="?", default="")
    p.add_argument("--search", help="classes and members whose name contains this")
    p.add_argument("--full", action="store_true", help="whole descriptions, not first sentences")

    p = sub.add_parser("init", help="write AGENTS.md into a game project")
    p.add_argument("--project", required=True)
    p.add_argument("--force", action="store_true")
    p.add_argument("--absolute", action="store_true", help="the toolkit's absolute path in it (default: relative)")

    a = ap.parse_args()
    if a.command == "capture":
        extra = [f"--cmd=capture", f"--scene={a.scene}", f"--views={a.views}", f"--frames={a.frames}",
                 f"--preview_lighting={a.preview_lighting}"] + ([f"--camera={a.camera}"] if a.camera else [])
        return engine_command(a, extra, True)
    if a.command == "describe":
        return engine_command(a, ["--cmd=describe", f"--scene={a.scene}", f"--max_nodes={a.max_nodes}", f"--depth={a.depth}"], False)
    if a.command == "check":
        extra = ["--cmd=check", f"--frames={a.frames}"] + ([f"--scenes={a.scenes}"] if a.scenes else []) + (["--addons=true"] if a.addons else [])
        if a.render:
            a.size, a.renderer, a.driver, a.fps, a.vsync = "640x360", None, None, 60, True
        return engine_command(a, extra, a.render)
    if a.command == "run":
        extra = ["--cmd=run", f"--scene={a.scene}", f"--seconds={a.seconds}", f"--warmup={a.warmup}",
                 f"--picture={'false' if a.no_picture else 'true'}"]
        a.fps = 0
        return engine_command(a, extra, True)
    if a.command == "compare":
        return cmd_compare(a)
    if a.command == "import":
        return cmd_import(a)
    if a.command == "api":
        if not a.cls and not a.search:
            return fail("give a class name or --search")
        return cmd_api(a)
    if a.command == "init":
        return cmd_init(a)
    return 2


if __name__ == "__main__":
    sys.exit(main())
