# FellowEngine — milestone 130 report (HLOD merged proxies baked into files)

Status: **FINAL** (2026-11-16). Branch `fellow/geometry`. Without the GPU (the cloud's CPU and lavapipe).

## 0. Result

* `FellowHLOD3D.bake_merged_proxies(directory)` bakes the merged proxies and their detail levels into files, kept in
  the new `merged_proxies` under a signature of each cell's members (their meshes' and materials' files, their
  transforms); `build()` takes a cell's mesh when its members are as baked and merges the others (decision 0254).
* A town of 1 600 houses (100 cells): its build 2.4-2.6 s → **9.2 ms**; 15.5 MB of files; the pictures as a run-time
  merge's within lavapipe's floor (two run-time merges of the same scene differ as much).
* Project > Tools > Bake HLOD (was Bake HLOD Impostors, milestone 129) bakes the merged proxies, then the impostors,
  into `<scene>_hlod/`, one undoable action. With milestone 129 the readiness assessment's "HLOD built at run time" is
  down to its instanced proxies (MultiMeshes of repeated meshes, no mesh built).

## 1. Checks

`bench/hlod_merged_files_check.gd` (new); `bench/hlod_impostor_files_check.gd` (as in milestone 129); the editor
check's HLOD step (one merged proxy and one impostor baked, both properties saved with the scene); the image
regression suite; the class reference.

## 2. Next

The proxies, groups and HLOD bakes on the RTX in Frontier Lab's 4 km world (the load time, the draws' CPU cost).
