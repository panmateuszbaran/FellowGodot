#!/usr/bin/env python3
"""Import the external benchmark assets (Sponza glTF, Bistro FBX) into the benchmark project.

Runs the (stock) editor binary headless with --import, which imports every resource once and quits.
Before that, per-texture import presets are written for Bistro so the ORCA conventions are honoured:
  *.tga      -> VRAM compressed with mipmaps; *_Normal.tga additionally normal_map=1 + normal_map_invert_y (DirectX)
  *.dds      -> not imported: Godot loads DDS natively (BC formats + authored mips). DirectX BC5 normal maps are
                converted losslessly by fetch_bistro.py into external/bistro/TexturesGL/.
Sponza (glTF 2.0) needs no presets: glTF normal maps already use the OpenGL convention.

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


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--editor", default=DEFAULT_EDITOR)
    ap.add_argument("--timeout", type=float, default=7200)
    a = ap.parse_args()
    print(f"[import] wrote {write_bistro_presets()} Bistro texture presets", flush=True)
    t0 = time.time()
    cmd = [a.editor, "--headless", "--path", PROJECT, "--import"]
    print("[import] " + " ".join(cmd), flush=True)
    p = subprocess.Popen(cmd, cwd=ROOT)
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
