# 0159 — Foliage scattered by the terrain: FellowTerrainFoliage

Status: **KEEP** (a new resource of FellowTerrain3D's: nothing changes without it) · Branch `fellow/geometry` · Code:
`modules/fellow_world/fellow_terrain_3d.{h,cpp}` (`FellowTerrainFoliage`, `FellowTerrain3D.foliage`,
`foliage_cells_per_frame`), icon, class reference · Checks: `benchmarks/project/bench/terrain_check.gd --foliage=1`;
Slice Lab `--slice_terrain=node --slice_foliage=node`.

## Problem

The second half of point 3 of the plan towards AAA: a terrain is covered — grass, flowers, bushes, rocks, trees —
and an open world has far more of it than anyone places by hand. FellowEngine's labs placed theirs in GDScript at
load (Slice Lab: 9 000 trees in 16 MultiMeshes, 40 000 clumps of grass in 32 m chunks over one meadow), every
instance for the whole map in memory and the grass only where the script put it. Godot has no scattering of its own;
FellowEngine has the pieces it needs (MultiMesh culling per instance, decision 0034; per-instance distance ranges,
decision 0122; the wind's bounded vertex motion, decision 0125).

## Change

**`FellowTerrainFoliage`** (a resource) describes one kind: a `mesh` (with its own materials, FellowEngine's wind
included), the terrain `layer` it grows on (0-8), its `density` per square metre where that layer's weight is 1,
`min_weight` (below it none; above it, fewer where the weight is lower), the `range` from the camera it is made and
drawn to, its `cell_size` (0: a quarter of the range, 8-256 m), the slopes and heights it grows on (`slope_min`,
`slope_max`, `height_min`, `height_max`), `scale_min`/`scale_max`, `random_yaw`, `align_to_normal` (0 upright, 1 on
the slope), `sink`, `cast_shadows`, `seed`.

**`FellowTerrain3D.foliage`** scatters each kind in square cells around the camera:

* Every frame (cheap: the cells' indices within the range), a cell the camera has come within range of starts on a
  worker thread (at most `foliage_cells_per_frame`, 8, started a frame): points jittered on a grid of the kind's
  density (a hash of the kind, the seed and the point's grid index: the same terrain gives the same instances), each
  kept by the layer's weight (bilinear from a CPU copy of the splat maps), the slope and the height, turned, tilted,
  scaled and placed on the ground (the finest level's triangles).
* A finished cell becomes a MultiMesh of its own, written once: its motion vectors are its own (a MultiMesh whose
  buffer is rewritten takes another instance's transform for the previous frame), and FellowEngine's MultiMesh culling
  culls its instances one by one in each pass, and by distance beyond the kind's range for the camera
  (`fellow_instance_set_instance_range`).
* A cell is freed 15 % beyond the range (hysteresis: a camera on a cell's edge does not make and free it in turn). One
  dropped before its task has run is freed once it has: the main thread never waits for a cell (a wait for a queued
  task may run other queued work on the main thread, a pipeline's compilation among them).
* Each kind's mesh has a hidden instance from the start, so its pipelines compile when the terrain is set up.
* Changing a kind, the terrain's data or its layers scatters again; the cells follow the terrain's transform,
  visibility and render layers.

## Verification

* `terrain_check.gd --foliage=1` (lavapipe, 513² heights): grass boxes over the base layer to 60 m, trees to 500 m,
  rocks over the rock layer aligned to its slope: 120-170 cells, 16 000-21 000 instances; looked at from four heights.
* Slice Lab `--slice_terrain=node --slice_foliage=node`: the script's tree and grass rules turned into two
  grass-coloured layers (forest, meadow) the terrain scatters over.
* RTX 5070 Ti Laptop: Slice Lab's frame time with the script's trees and grass against the terrain's (frame-locked,
  interleaved, three runs each; jobs f354-f356); 10 minutes of its soak on the terrain with scattered foliage (f355,
  then with the fixes f357).
* Cloud CPU, headless: the terrain's own time a frame with a camera flying over Slice Lab's valley at 12 m/s.

## Results

Slice Lab on the RTX at 1920 × 1080 (FellowTerrain3D at 1 m; trees to the far plane, grass to 90 m):

| | the script's trees and grass | scattered by the terrain |
|---|---|---|
| GPU a frame, f354 / f355 / f356 | 3.653 / 3.617 / 3.587 ms | 3.637 / 3.624 / 3.583 ms |
| the render thread, f355 / f356 | 1.158 / 1.155 ms | 1.026 / 1.036 ms |
| the terrain's main-thread time a frame, the laptop in the flythrough (f356) | 17 µs | 23 µs (90 cells, 10 600 instances) |
| the same, cloud CPU flying at 12 m/s | 4.0 µs | 6.9 µs (88 cells, 9 284 instances; at most 69 µs) |

The 10-minute soak on the terrain with scattered foliage (f355): 236 fps, nothing growing (video memory 400 → 401 MB,
nodes and orphans constant), but 10 hitches against the chunks' 2 in 5 minutes: 6 surface pipelines (21.5 ms) when
the first grass cell appeared (the grass mesh's first instance), and four of 139-274 ms that compiled nothing on the
main thread, two with 0.5-1.2 s of pipelines compiling on the worker threads in that frame: the main thread
waiting for a dropped cell's queued task, and running other queued work meanwhile. With both fixes (above), the same
10 minutes (f357): **1 hitch** (17 ms, the GPU's fence), the largest frame 17 ms, 236 fps, no pipeline compiled in a
hitch; video memory 400 → 401 MB, objects +2, nodes and orphans constant. The process's resident memory rose 2 262 →
2 600 MB from the first to the tenth minute (the chunks' slice: 1 457 → 1 620 MB in 19 minutes); headless on the cloud
CPU (no GPU driver) it stays at 133 MB through 36 000 frames of the same flight with 90 cells made and freed around
the camera: __F358__

## Decision

**KEEP.** The terrain scatters what Slice Lab's script placed, at the same GPU cost and a shorter render thread, a few
microseconds a frame on the main thread, deterministic, and only around the camera; with its two hitch fixes it plays
ten minutes with one hitch.

Not done: a fade at the range's end (instances appear and disappear there; the kinds' materials can fade
themselves), impostors for far kinds (decision 0122's are FellowHLOD3D's), foliage that collides, painting density
masks in the editor (the layers' weights are the masks).
