#!/usr/bin/env python3
"""FellowEngine showcase: downloads the rooftops scene's photographic textures from Poly Haven (polyhaven.com, CC0 — no
attribution needed, free for any use) into showcase/rooftops/textures/ (not part of the repository: .gitignore). Each
set: its colour, its normal map (OpenGL), its AO/roughness/metal map and its height (displacement), at 2K. Without
them the scene's materials fall back to their procedural versions (tools/build_rooftops.gd).

    python showcase/tools/fetch_textures.py [--force]

Then import them: open the project in the editor once, or `godot --headless --editor --quit --path showcase` (each with
an .import file written here: compressed for the GPU, the normal maps as normal maps). The scene's photographic
materials (rooftops/materials_photo, made by tools/build_rooftops.gd) take the place of the procedural ones when it
finds them (scripts/rooftops.gd)."""

import argparse
import json
import os
import sys
import urllib.request

# The sets, by what they are in the scene.
SETS = {
    "plaster_smooth": "plastered_wall",
    "brick": "brick_wall_006",
    "roof": "clay_roof_tiles_02",
    "stone": "sandstone_blocks_08",
    "square_slabs": "stone_floor",
    "street_fan": "patterned_cobblestone",
    "cobbles": "cobblestone_floor_08",
    "pavement": "precast_stone_paving",
    "wood": "weathered_brown_planks",
    "wood_dark": "old_planks_02",
    "shutter_green": "green_rough_planks",
    "shutter_painted": "distressed_painted_planks",
    # Milestone 156: the owner's reference — a city of warm, rough-faced sandstone ashlar.
    "ashlar": "sandstone_blocks_05",
}
# Milestone 158: and its height (Poly Haven's displacement map): the parallax's depth (the stone's joints, the tiles').
MAPS = {"Diffuse": "diff", "nor_gl": "nor_gl", "arm": "arm", "Displacement": "disp"}
HEADERS = {"User-Agent": "FellowEngine-showcase/1.0 (fetch_textures.py)"}


def _get(url):
    return urllib.request.urlopen(urllib.request.Request(url, headers=HEADERS), timeout=120)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--force", action="store_true")
    args = ap.parse_args()
    here = os.path.dirname(os.path.abspath(__file__))
    out = os.path.join(here, "..", "rooftops", "textures")
    os.makedirs(out, exist_ok=True)
    info = {}
    for role, asset in SETS.items():
        with _get("https://api.polyhaven.com/files/" + asset) as r:
            files = json.load(r)
        with _get("https://api.polyhaven.com/info/" + asset) as r:
            meta = json.load(r)
        info[role] = {"asset": asset, "dimensions_mm": meta.get("dimensions"), "maps": {}}
        for key, short in MAPS.items():
            url = files[key]["2k"]["jpg"]["url"]
            name = "%s_%s_2k.jpg" % (asset, short)
            path = os.path.join(out, name)
            info[role]["maps"][short] = name
            if not os.path.exists(path + ".import"):
                # How Godot imports it: compressed for the GPU, mipmapped; a normal map as one.
                with open(path + ".import", "w") as f:
                    f.write('[remap]\n\nimporter="texture"\ntype="CompressedTexture2D"\n\n[params]\n\ncompress/mode=2\n'
                            "compress/normal_map=%d\nmipmaps/generate=true\n" % (1 if short == "nor_gl" else 0))
            if os.path.exists(path) and not args.force:
                continue
            print("fetching", name, flush=True)
            with _get(url) as r, open(path + ".part", "wb") as f:
                while True:
                    chunk = r.read(1 << 20)
                    if not chunk:
                        break
                    f.write(chunk)
            os.replace(path + ".part", path)
    with open(os.path.join(out, "textures.json"), "w") as f:
        json.dump(info, f, indent=1)
    with open(os.path.join(out, "LICENSE.txt"), "w") as f:
        f.write("These textures are from Poly Haven (https://polyhaven.com), CC0 1.0 (public domain).\n")
    print("done:", len(info), "sets in", os.path.normpath(out))


if __name__ == "__main__":
    sys.exit(main())
