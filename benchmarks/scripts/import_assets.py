#!/usr/bin/env python3
"""Import the external benchmark assets (Sponza glTF, Bistro FBX) into the benchmark project.

Runs the (stock) editor binary headless with --import, which imports every resource once and quits.
Before that, per-texture import presets are written for Bistro so the ORCA conventions are honoured:
  *.tga      -> VRAM compressed with mipmaps; *_Normal.tga additionally normal_map=1 + normal_map_invert_y (DirectX)
  *.dds      -> not imported: Godot loads DDS natively (BC formats + authored mips). DirectX BC5 normal maps are
                converted losslessly by fetch_bistro.py into external/bistro/TexturesGL/.
Sponza (glTF 2.0, external PNG textures): the editor normally switches textures used by 3D materials to VRAM
compression + mipmaps ("detect 3D" reimport), but that only happens in an interactive editor session, NOT in a
headless --import: the first baseline ran with all 155 Sponza textures uncompressed and without mipmaps
(6.8 GB VRAM). fix_sponza_imports() applies exactly what the editor's detection would set:
  compress/mode=VRAM compressed, mipmaps/generate=true, *_Normal*: compress/normal_map=1 (BC5 / RGTC);
glTF normal maps already use the OpenGL convention (no Y flip). The .import files are edited in place (UIDs are
kept), which makes the next --import re-import them.

The same imported data is used by stock and FellowEngine runs (the importer is not modified by FellowEngine).

Usage: python benchmarks/scripts/import_assets.py [--editor path\\to\\godot.windows.editor.x86_64.console.exe] [--timeout 7200]
"""

import argparse
import glob
import os
import subprocess
import sys
import time

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
PROJECT = os.path.join(ROOT, "benchmarks", "project")
DEFAULT_EDITOR = os.path.join(ROOT, ".tools", "bin", "stock", "godot.windows.editor.x86_64.console.exe")

TEXTURE_IMPORT = """[remap]

importer="texture"
type="CompressedTexture2D"

[params]

compress/mode=2
compress/high_quality={hq}
compress/normal_map={nm}
mipmaps/generate=true
process/normal_map_invert_y={flip}
detect_3d/compress_to=0
"""


def write_bistro_presets() -> int:
    n = 0
    # DDS files are loaded natively (not imported); remove stale presets from older versions of this script.
    stale = set(glob.glob(os.path.join(PROJECT, "external", "bistro", "Textures*", "*.dds.import")))
    stale |= set(glob.glob(os.path.join(PROJECT, "external", "bistro", "Textures*", "*.DDS.import")))
    for f in stale:
        if os.path.exists(f):
            os.remove(f)
    for tex in glob.glob(os.path.join(PROJECT, "external", "bistro", "Textures", "*")):
        low = tex.lower()
        if not low.endswith(".tga") or os.path.exists(tex + ".import"):
            continue
        is_normal = "_normal." in low
        with open(tex + ".import", "w", encoding="utf-8") as f:
            f.write(TEXTURE_IMPORT.format(hq="false", nm="1" if is_normal else "0", flip="true" if is_normal else "false"))
        n += 1
    return n


def fix_sponza_imports() -> int:
    """Edit Sponza texture .import files in place (keeps uid); returns the number of files changed."""
    n = 0
    base = os.path.join(PROJECT, "external", "sponza")
    for imp in glob.glob(os.path.join(base, "**", "*.import"), recursive=True):
        low = imp.lower()
        if not (low.endswith(".png.import") or low.endswith(".jpg.import") or low.endswith(".jpeg.import")):
            continue
        with open(imp, encoding="utf-8") as f:
            lines = f.read().split("\n")
        is_normal = "normal" in os.path.basename(low)
        want = {"compress/mode": "2", "mipmaps/generate": "true", "detect_3d/compress_to": "0",
                "compress/normal_map": "1" if is_normal else "0"}
        out, changed = [], False
        for line in lines:
            k = line.split("=", 1)[0]
            if k in want and line != f"{k}={want[k]}":
                line = f"{k}={want[k]}"
                changed = True
            out.append(line)
        if changed:
            with open(imp, "w", encoding="utf-8", newline="\n") as f:
                f.write("\n".join(out))
            n += 1
    return n


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--editor", default=DEFAULT_EDITOR)
    ap.add_argument("--timeout", type=float, default=7200)
    a = ap.parse_args()
    print(f"[import] wrote {write_bistro_presets()} Bistro texture presets", flush=True)
    print(f"[import] updated {fix_sponza_imports()} Sponza texture import settings (VRAM compressed + mipmaps)", flush=True)
    t0 = time.time()
    cmd = [a.editor, "--headless", "--path", PROJECT, "--import"]
    print("[import] " + " ".join(cmd), flush=True)
    p = subprocess.Popen(cmd, cwd=ROOT, **({"creationflags": 0x08000000} if os.name == "nt" else {}))
    try:
        rc = p.wait(timeout=a.timeout)
    except subprocess.TimeoutExpired:
        subprocess.run(["taskkill", "/PID", str(p.pid), "/T", "/F"], capture_output=True)
        print("[import] TIMEOUT", flush=True)
        return 3
    print(f"[import] finished rc={rc} in {time.time() - t0:.0f} s", flush=True)
    return rc


if __name__ == "__main__":
    sys.exit(main())
