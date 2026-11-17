# 0253 — FellowHLOD3D's impostors baked into files in the editor

Status: **KEEP** (a method, a property and an editor tool: a node without baked impostors bakes at run time as before) ·
Branch `fellow/geometry` · Code: `modules/fellow_world/fellow_hlod_3d.{h,cpp}` (`impostors`, `bake_impostors()`,
`impostors_baked`, `build()` taking them), `modules/fellow_world/fellow_impostor_baker.cpp` (frames asked for while a
bake runs), `modules/fellow_editor/fellow_editor_plugin.{h,cpp}` (Project > Tools > Bake HLOD Impostors) · Checks:
`benchmarks/project/bench/hlod_impostor_files_check.gd` (new), the editor check's HLOD step.

## Problem

`FellowHLOD3D` bakes its impostors (decision 0122) when it builds, at run time: one view a frame through an internal
baker, 8 × 8 views and a few frames of warm-up for each mesh. Until a mesh's bake ends its far instances are drawn as
full meshes, and the bake's viewports render meanwhile. The readiness assessment's tools row: "HLOD's impostors baked
at run time". The terrain's foliage already bakes its impostors into files (decision 0172).

## Change

* **`bake_impostors(directory)`** finds the meshes `build()` would give impostors — its repeated meshes (the same mesh,
  override and instance settings, `instance_threshold` times) and its `MultiMeshInstance3D` nodes, with at least
  `impostor_min_triangles` — without building, and bakes each through the baker's portable path (decision 0172:
  the atlases lossless, with their mips) into `impostor_<n>.res`, saved as it finishes; `impostors_baked(count)` when
  the last is. Returns the bakes queued.
* **`impostors`** (saved with the scene): the baked impostors by key — the mesh's file, the material override's file,
  `impostor_frames` and `impostor_resolution`. A mesh made at run time has no file and no key (baked at run time as
  before); other views or another resolution do not use a bake made for different ones. `build()` looks each impostor
  up first and, once built, hands its instances over at once; the build's info gains `impostors_loaded`.
* **The editor**: Project > Tools > Bake HLOD Impostors (Bake HLOD since decision 0254) bakes the selected node's impostors beside the scene
  (`<scene>_impostors/`, the scene saved first: the keys are files), the property set in one undoable action, and
  reports the count. The baker now asks for the next frame while it runs (`Fellow::request_redraw()`): in the editor
  frames are drawn only on a change, and a bake waited for one between its views. The property is replaced, not
  changed in place, as a bake finishes (the undo step holds the old dictionary).

## Verification

`hlod_impostor_files_check.gd` (lavapipe, 960 × 540): a forest of 1 000 instances of one tree (a trunk and a crown,
2 480 triangles, saved as a file) in a `MultiMeshInstance3D` under a `FellowHLOD3D` with impostors from 60 m. (A)
`build()` at run time; (B) `bake_impostors()`, the forest saved as a scene; (C) that scene loaded again with nothing
from the resource cache and built: once with its impostors cleared (baked at run time again), twice with them. Each
picture 60 frames after its impostors are there; autotune off. The editor check: a FellowHLOD3D over a MultiMesh of a
mesh in a file, Bake HLOD Impostors from the Tools menu: the report, one impostor in the node and one file, the
property saved with the scene. The image regression suite.

## Results

| | frames until the impostors | until then (lavapipe) | its longest frame | build |
|---|---:|---:|---:|---:|
| baked at run time (A) | 68 | 35 s | 6.8 s | 13 ms |
| the scene loaded again, baked at run time | 68 | 29 s | 1.3 s | 745 ms |
| **the scene loaded again, impostors from the files** | **0** | — | — | 1.6 ms |

The file: 548 KB for the tree's impostor (two 1 024² atlases with their mips, lossless). The bake into files: 69
frames, 10 s. The pictures: the files' against the run-time bake's, 6.9 % of the pixels differ (0.15 levels on
average); two loads of the same files differ as much (9.2 %, 0.21 levels) — on lavapipe the forest's far instances
differ from frame to frame (the GPU's culling of the MultiMesh, decision 0034), so that is the floor. The second load's
build took 889 ms (the first load's forest released meanwhile), the first 1.6 ms. In the editor the bake took 124
frames (the editor check).

Found on the way: in the editor the baker stalled between views until something else redrew the editor (frames are
drawn there only on a change; the same baker serves the terrain's Bake Impostors, decision 0172). It now asks for the
next frame.

**On the laptop** (milestone 131, job f464: RTX 5070 Ti Laptop, 960 × 540, the release template, 2 500 trees): the
run-time bake still takes 68 frames, 0.41-0.81 s there (its longest frame 132-460 ms); from the files the impostors are
there at once (the build 0.8-2.9 ms). The pictures: the files' against the run-time bake's 8.3 % of the pixels (0.16
levels on average), two loads of the files 11.0 % (0.23).

## Decision

Keep: a scene's far trees are impostors from its first frame, with no bake at run time, for half a megabyte a mesh.

## Rejected / not done

* **Baking the whole HLOD build into the scene** (the merged proxies and their detail levels as files, decision 0122's
  1 s for City Lab at load): the members' visibility parents would be saved into the level, and a change to a member
  would need a bake again; this milestone bakes the part that costs frames. (The merged proxies: decision 0254, as a
  cache keyed by the members.)
* **VRAM-compressed atlases**: as decision 0172 — lossless keeps the impostors as the run-time bake draws them.
* **Not yet**: impostors of meshes without files (made by a script) are baked at run time; the files are not shared
  between nodes baking the same mesh.
