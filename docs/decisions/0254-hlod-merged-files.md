# 0254 — FellowHLOD3D's merged proxies baked into files in the editor

Status: **KEEP** (a method, a property and the editor's tool: a node without baked proxies merges at run time as
before) · Branch `fellow/geometry` · Code: `modules/fellow_world/fellow_hlod_3d.{h,cpp}` (`merged_proxies`,
`bake_merged_proxies()`, a cell's signature in `build()`), `modules/fellow_editor/fellow_editor_plugin.{h,cpp}`
(Project > Tools > Bake HLOD: the merged proxies, then the impostors, one undoable action) · Checks:
`benchmarks/project/bench/hlod_merged_files_check.gd` (new), the editor check's HLOD step.

## Problem

`FellowHLOD3D` merges its far meshes per cell when it builds (decision 0122): every member's surfaces read, transformed
and appended, then detail levels generated (meshoptimizer) — on the main thread, at load (City Lab: 1.0 s; the
check's town of 1 600 houses: 2.4-2.6 s on the cloud's CPU). Decision 0253 baked the impostors into files; the merged
proxies were the rest of the readiness assessment's "built at run time".

## Change

* **A cell's signature**: the detail levels' switch, the cell size, and for each of its members (in the order the
  build finds them) its mesh's file, its material override's and surface override materials' files, and its transform
  in the node's space to a hundred-thousandth (basis) and a tenth of a millimetre (origin), hashed (SHA-256). A
  member whose mesh or material is not a file gives the cell no signature.
* **`bake_merged_proxies(directory)`** builds as `build()` does and saves each signed cell's merged mesh, with its
  detail levels, into `merged_<cell>_<n>.res`; `merged_proxies` (saved with the scene) keeps each under its signature
  with the indices of the members it merged; the build is then undone (the members' visibility parents as they were)
  and redone if the node was built.
* **`build()`** computes each cell's signature first: a cell found in `merged_proxies` takes its mesh and members
  without reading, merging or simplifying; the others merge as before. A moved, added or changed member changes its
  cell's signature, so a stale bake is never used — that cell merges at run time. `get_build_info()`'s
  `merged_loaded`.
* **The editor**: Project > Tools > Bake HLOD (was Bake HLOD Impostors) bakes the selected node's merged proxies, then
  its impostors, into `<scene>_hlod/merged` and `<scene>_hlod/impostors`, both properties set in one undoable action.

## Verification

`hlod_merged_files_check.gd` (lavapipe, 960 × 540; autotune off): a town of 1 600 houses (three meshes of 1 000-2 000
triangles in files, their materials in files) under a FellowHLOD3D merging them per 50 m cell with detail levels
(instancing off). (A) built at run time; (B) `bake_merged_proxies()`, the town saved as a scene; (C) that scene loaded
again, nothing from the cache, built: merged at run time, from the files, and merged at run time again. The editor
check: Bake HLOD on a node with six meshes in one cell and a MultiMesh — one merged proxy, one impostor, both files,
both properties saved with the scene. The image regression suite.

## Results

| the town's build | time | merged proxies | from files |
|---|---:|---:|---:|
| at run time (A; and the scene loaded again, twice) | 2 589 ms (2 425, 2 591) | 100 of 1 600 houses | 0 |
| **from the files** | **9.2 ms** | 100 of 1 600 houses | 100 |

The bake: 3.3 s, 100 files, 15.5 MB (their detail levels with them). The pictures: from the files against a run-time
merge, 12.2 % of the pixels differ; two run-time merges of the same scene differ as much (12.2 %), so that is lavapipe's
floor here, not the meshes — a proxy's surfaces and detail levels compared equal, built either way.

## Decision

Keep: a level's HLOD builds in milliseconds at load, from files baked once in the editor, and any change to a member
falls back to merging its cell.

## Rejected / not done

* **Saving the built proxies as nodes in the scene** (and the members' visibility parents): a member edited after the
  bake would keep a stale parent; the signatures make the bake a cache that can only miss.
* **Not yet**: the instanced proxies (MultiMeshes of repeated meshes) are still made at run time (cheap: no mesh is
  built); the files are not shared between nodes baking the same cell.
