# FellowEngine — milestone 129 report (HLOD impostors baked into files)

Status: **FINAL** (2026-11-16). Branch `fellow/geometry`. Without the GPU (the cloud's CPU and lavapipe).

## 0. Result

* `FellowHLOD3D.bake_impostors(directory)` bakes the impostors the node would bake at run time — its repeated meshes'
  and its MultiMeshes' — into files, kept in its new `impostors` property (by the mesh's and material override's files,
  the views and the resolution) and saved with the scene; `build()` takes them at once (decision 0253). In the editor:
  Project > Tools > Bake HLOD Impostors on the selected node, undoable. The readiness assessment's "HLOD's impostors
  baked at run time" is closed (its merged proxies are still built at run time).
* A forest of 1 000 trees of 2 480 triangles on lavapipe: impostors after 68 frames of baking (29-35 s) → from the first
  frame; the picture as the run-time bake's within lavapipe's own frame-to-frame floor (6.9 % of the pixels against
  9.2 % between two loads of the same files, 0.15 levels on average). 548 KB a tree.
* The impostor baker asks for frames while it runs: in the editor it waited for something else to redraw between its
  views (it serves the terrain's Bake Impostors too).

## 1. Checks

`bench/hlod_impostor_files_check.gd` (new); the editor check's new HLOD step (the report, the node's `impostors`, the
file, the property saved with the scene); the image regression suite; the class reference (doctool's output matches).

## 2. Next

The HLOD's merged proxies built in the editor too (City Lab's 1 s at load).
