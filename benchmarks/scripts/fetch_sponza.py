#!/usr/bin/env python3
"""Fetch the modern Intel Sponza (Intel Graphics Research Samples) for the FellowEngine benchmarks.

Source page : https://www.intel.com/content/www/us/en/developer/topic-technology/graphics-research/samples.html
License     : Creative Commons Attribution (CC BY 4.0) -- see credits_license.txt shipped in each package.
Attribution : "Intel Sponza" by Intel Corporation (Intel Graphics Research Samples).

Only the glTF variant (+ its textures and the license file) is extracted into
benchmarks/project/external/sponza/<package>/ ; the 3ds Max / FBX / USD sources stay in the cached zip.
Intel's own offline renders (Render_Main_*.png) are extracted to benchmarks/assets/sponza_intel_renders/
for visual orientation only (different renderer/lighting: NOT used as a metric reference).

Usage:
    python benchmarks/scripts/fetch_sponza.py                # main + curtains + ivy + trees
    python benchmarks/scripts/fetch_sponza.py --packages main curtains
    python benchmarks/scripts/fetch_sponza.py --with-candles # adds the 3 GB emissive candles package
"""

import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(__file__))
import _fetch_common as fc  # noqa: E402

# Exact package versions as published (content-length recorded 2026-09-29).
PACKAGES = {
    "main": {
        "url": "https://cdrdv2.intel.com/v1/dl/getContent/830833",
        "file": "main_sponza.zip",
        "size": 3987608266,
        "prefix": "main_sponza/",
        "gltf": "NewSponza_Main_glTF_003.gltf",
    },
    "curtains": {
        "url": "https://cdrdv2.intel.com/v1/dl/getContent/726650",
        "file": "pkg_a_curtains.zip",
        "size": 786898766,
        "prefix": "pkg_a_curtains/",
        "gltf": "NewSponza_Curtains_glTF.gltf",
    },
    "ivy": {
        "url": "https://cdrdv2.intel.com/v1/dl/getContent/726656",
        "file": "pkg_b_ivy.zip",
        "size": 1383921436,
        "prefix": "pkg_b_ivy/",
        "gltf": "NewSponza_IvyGrowth_glTF.gltf",
    },
    "trees": {
        "url": "https://cdrdv2.intel.com/v1/dl/getContent/726662",
        "file": "pkg_c_trees.zip",
        "size": 951147802,
        "prefix": "pkg_c_trees/",
        "gltf": "NewSponza_CypressTree_glTF.gltf",
    },
    "candles": {
        "url": "https://cdrdv2.intel.com/v1/dl/getContent/726676",
        "file": "pkg_d_candles.zip",
        "size": None,
        "prefix": "pkg_d_candles/",
        "gltf": None,
    },
}


def wanted(name: str, prefix: str) -> bool:
    rel = name[len(prefix):] if name.startswith(prefix) else name
    low = rel.lower()
    if "/" not in rel:
        return low.endswith((".gltf", ".bin")) or low == "credits_license.txt"
    return low.startswith("textures/")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--packages", nargs="+", default=["main", "curtains", "ivy", "trees"], choices=list(PACKAGES))
    ap.add_argument("--with-candles", action="store_true")
    ap.add_argument("--no-hash", action="store_true", help="skip sha256 of the archives")
    args = ap.parse_args()
    pkgs = list(args.packages) + (["candles"] if args.with_candles and "candles" not in args.packages else [])

    for key in pkgs:
        p = PACKAGES[key]
        fc.log(f"package {key}")
        zpath = fc.download(p["url"], p["file"], p["size"])
        out_dir = os.path.join(fc.EXTERNAL_DIR, "sponza", key)
        files = fc.extract(zpath, out_dir, lambda n, pre=p["prefix"]: wanted(n, pre), strip_prefix=p["prefix"])
        fc.log(f"  extracted {len(files)} files to {out_dir}")
        if key == "main":
            ref_dir = os.path.join(fc.REPO_ROOT, "benchmarks", "assets", "sponza_intel_renders")
            fc.extract(zpath, ref_dir, lambda n: n.startswith("main_sponza/Render_Main_"), strip_prefix="main_sponza/")
        fc.write_manifest(out_dir, {
            "asset": f"Intel Sponza ({key})",
            "publisher": "Intel Corporation - Graphics Research Samples",
            "source_page": "https://www.intel.com/content/www/us/en/developer/topic-technology/graphics-research/samples.html",
            "download_url": p["url"],
            "archive": p["file"],
            "archive_size": os.path.getsize(zpath),
            "archive_sha256": None if args.no_hash else fc.sha256(zpath),
            "license": "CC BY 4.0 (see credits_license.txt)",
            "gltf": p["gltf"],
            "extracted_files": len(files),
        })
    fc.log("done")
    return 0


if __name__ == "__main__":
    sys.exit(main())
