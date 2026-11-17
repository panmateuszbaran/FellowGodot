# 0255 — Terrain proxies simplified to an error on screen, not to a share of their triangles

Status: **KEEP** (`bake_proxies()`'s `terrain_error`, 2 pixels by default; 0: to `ratio` as before) · Branch
`fellow/geometry` · Code: `modules/fellow_world/fellow_cell_proxies.{h,cpp}` (`bake_proxy()`'s terrain error: the
terrain's triangles simplified apart), `modules/fellow_world/fellow_world_streamer_3d.cpp` (the error from the distance
at which a proxy is first drawn; the groups' count from their cells' other triangles),
`modules/fellow_editor/fellow_editor_plugin.{h,cpp}` (the dialog's terrain error) · Checks:
`benchmarks/project/bench/world_proxies_check.gd` (`--relief`, `--terrain_error`: how far the terrain's proxies are
from the terrain), `benchmarks/project/scenes/frontier_lab.gd` (version 8: baked at the distances it streams at).

## Problem

Frontier Lab on the RTX (milestone 131, job f468) with groups of far cells (decision 0252) drew fans of long thin
triangles across the far terrain, and a town 2 km away was gone — under the ground. A terrain region's proxy keeps its
outer edges (its neighbours' proxies meet them) and was simplified, like the rest, to `ratio` (5 %) of its triangles:
a Frontier region sampled at 32 × 32 quads has 128 vertices on its edges and 2 048 triangles, and 5 % is 102 — fewer
than its locked edges need (a polygon of 128 corners takes 126 triangles), so the simplifier removed every vertex
inside it and spanned the region with a fan from its edges. A group of 4 × 4 regions has 512 vertices on its edges and
was given its regions' triangles over the square of its distance's factor: the same fan over a kilometre, valleys and
the towns' platforms in them covered by the plane between the hills.

World Lab's terrain (decision 0174) hid it: ±12 m of gentle noise is near enough to a plane over 64 m.

## Change

* **`bake_proxy()`'s `p_terrain_error`** (metres): a cell's terrain triangles are simplified apart from the rest, with
  their outer edges locked, as far as that error allows (meshoptimizer's absolute error, no count) — a flat region
  keeps a few triangles, a mountain what it needs; the rest to `ratio` (or a group's count) as before.
* **`bake_proxies()`'s `terrain_error`** (2): pixels of a view of 1 080 lines with Godot's default field of
  view (75°), at the distance at which the proxy is first drawn — a cell's: its load distance and `unload_margin`; a
  group's: the distance at which it switches (decision 0252). So a group's terrain may stray as many times farther as
  it is farther. The groups' count is taken from their cells' proxies' other triangles. The result has
  `terrain_triangles` and `group_terrain_triangles`; the editor's dialog has the error.
* **Frontier Lab** bakes its proxies at the distances it streams at (the ground's 1 000 m and 100 m margin, the cells'
  450 m and 50 m — the bake had used the streamer's defaults, 200 m and 25 m), version 8.

## Verification

`world_proxies_check.gd` with `--relief=60` (World Lab's terrain ±60 m instead of ±12 m; lavapipe, 640 × 360, groups
of 4 × 4): the terrain's proxies — its 144 regions' and their 9 groups' — sampled every 2 m under each against the
terrain's height there; the draws, the triangles, the picture against every cell loaded (A). By ratio (0, as before)
and by an error of 1, 2 and 4 pixels. Then Frontier Lab generated again on the laptop with groups and without (job
f469): its far views captured, and streamed.

## Results

World Lab with ±60 m of terrain, the regions' proxies (first drawn at 185 m: 1 px is 0.26 m) and the groups' (from
about 370 m):

| terrain proxies | regions' triangles | groups' triangles | regions: farthest / mean | groups: farthest / mean / > 2 m | C: pixels > 16 levels off A |
|---|---:|---:|---:|---:|---:|
| by ratio (5 %, as before) | 9 042 | 2 330 | 31.1 / 3.69 m | 50.4 / 9.43 m / 81 % | 1.46 % |
| 1 px | 69 882 | 54 322 | 1.85 / 0.40 m | 2.88 / 0.41 m / 0 % | 0.63 % |
| **2 px** | **55 652** | **27 266** | **2.08 / 0.41 m** | **3.85 / 0.51 m / 0.4 %** | **0.70 %** |
| 4 px | 30 558 | 11 516 | 4.16 / 0.50 m | 6.50 / 0.84 m / 6.8 % | 0.81 % |

(The regions are sampled at 16 quads a side, a vertex every 4 m: what remains at 1 px — 0.4 m on average, 1.9 m at
most — is that sampling's distance from the 1 m terrain, which the simplifier does not see.) The bake: 0.56 s → 0.67
s for the 144 regions. The view draws 114 draws either way; 487 k triangles by ratio, 512 k at 2 px, 537 k at 1 px. Halving the error
from 2 px to 1 px doubles the groups' triangles for a tenth of a per cent of the picture; 4 px halves them again and
puts 7 % of a group's ground more than 2 m off.

World Lab as decision 0252 measured it (±12 m, the defaults): the regions' proxies 17 702 triangles (9 042 by ratio),
the groups' 6 368 (2 330); the ground under them 0.15 m and 0.27 m off on average (1.3 m and 2.7 m at most); C 0.42 %
of the picture more than 16 levels off A (0.65 % in decision 0252), D 0.48 % (0.36 %); 107 draws, 455 k triangles
(451 k). The editor check as before.

Frontier Lab's 4 km world is generated again and measured on the laptop in job f469.

## Decision

Keep, 2 pixels by default: a terrain's far proxies follow its relief — valleys, and the towns in them, stay where
they are — for the triangles the relief needs (a flat region keeps a few); a level of gentle terrain pays a few
thousand triangles more. `terrain_error` 0 keeps the ratio's count.

## Rejected / not done

* **A minimum count from the locked edges** (say three triangles a locked vertex): it ends the fans but not the error —
  a share of the triangles is still blind to where the relief is.
* **Unlocking the edges**: neighbouring proxies simplified apart would open cracks between them, and a group's edges
  would not meet its neighbours' cells' proxies.
* **The error for the buildings too**: `ratio` stays the knob for them (a proxy's buildings are boxes from afar);
  decision 0251's measurements stand.
* **Not yet**: the terrain is sampled at `terrain_quads` before it is simplified (the floor above); the error's view
  (1 080 lines, 75°) is fixed, not the project's camera.
