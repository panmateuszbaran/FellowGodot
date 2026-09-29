#!/usr/bin/env python3
"""Fetch Amazon Lumberyard Bistro v5.2 (NVIDIA ORCA) for the FellowEngine benchmarks.

Source page : https://developer.nvidia.com/orca/amazon-lumberyard-bistro
Download    : https://developer.nvidia.com/bistro  (redirects to Bistro_v5_2.zip, 894,377,473 bytes)
License     : Creative Commons Attribution 4.0 International (LICENSE.txt in the archive)
Attribution : Amazon Lumberyard Bistro, Open Research Content Archive (ORCA), Amazon Lumberyard, 2017.

Extracted into benchmarks/project/external/bistro/ (git-ignored):
    BistroExterior.fbx, BistroInterior.fbx, Textures/*.dds|tga, san_giuseppe_bridge_4k.hdr, LICENSE/README.

Conversion procedure (documented in benchmarks/README.md):
  * Godot's built-in FBX importer (ufbx) imports the .fbx files directly.
  * DDS textures are loaded natively by Godot (BC1/BC3/BC5 with their authored mipmaps, no re-compression).
  * The BC5 normal maps use the DirectX convention; they are converted losslessly (compressed-domain
    green inversion, see _bc5.py) into external/bistro/TexturesGL/ which the benchmark scene prefers.
  * The FBX material slots do not carry the ORCA metal/rough convention, so materials are rebuilt when the
    scene is loaded by benchmarks/project/scenes/bistro.gd from the texture naming convention in README.txt:
        *_BaseColor  RGB albedo, A opacity (alpha-scissor for foliage)
        *_Specular   R occlusion, G roughness, B metalness   -> ORMMaterial3D orm_texture
        *_Normal     DirectX convention -> TexturesGL/ (converted)
        *_Emissive   RGB emission
The InteriorWine variant is not used (its extra material data lives in a Falcor .pyscene).
"""

import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(__file__))
import _bc5  # noqa: E402
import _fetch_common as fc  # noqa: E402

URL = "https://developer.nvidia.com/bistro"
ARCHIVE = "Bistro_v5_2.zip"
SIZE = 894377473
PREFIX = "Bistro_v5_2/"


def wanted(name: str) -> bool:
    rel = name[len(PREFIX):] if name.startswith(PREFIX) else name
    low = rel.lower()
    if low.startswith("textures/"):
        return True
    return low in (
        "bistroexterior.fbx",
        "bistrointerior.fbx",
        "san_giuseppe_bridge_4k.hdr",
        "license.txt",
        "readme.txt",
        "changelog.txt",
        "bistroexterior.pyscene",
    )


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--no-hash", action="store_true")
    args = ap.parse_args()
    zpath = fc.download(URL, ARCHIVE, SIZE)
    out_dir = os.path.join(fc.EXTERNAL_DIR, "bistro")
    files = fc.extract(zpath, out_dir, wanted, strip_prefix=PREFIX)
    fc.log(f"extracted {len(files)} files to {out_dir}")
    gl_dir = os.path.join(out_dir, "TexturesGL")
    os.makedirs(gl_dir, exist_ok=True)
    flipped = 0
    for name in sorted(os.listdir(os.path.join(out_dir, "Textures"))):
        if name.lower().endswith("_normal.dds"):
            dst = os.path.join(gl_dir, name)
            if not os.path.exists(dst) and _bc5.flip_green_dds(os.path.join(out_dir, "Textures", name), dst):
                flipped += 1
    fc.log(f"converted {flipped} DirectX BC5 normal maps to OpenGL convention in {gl_dir}")
    fc.write_manifest(out_dir, {
        "asset": "Amazon Lumberyard Bistro v5.2",
        "publisher": "Amazon Lumberyard / NVIDIA Open Research Content Archive (ORCA)",
        "source_page": "https://developer.nvidia.com/orca/amazon-lumberyard-bistro",
        "download_url": URL,
        "archive": ARCHIVE,
        "archive_size": os.path.getsize(zpath),
        "archive_sha256": None if args.no_hash else fc.sha256(zpath),
        "license": "CC BY 4.0 (LICENSE.txt)",
        "extracted_files": len(files),
    })
    fc.log("done")
    return 0


if __name__ == "__main__":
    sys.exit(main())
