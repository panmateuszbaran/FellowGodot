# 0252 — One proxy for a group of far cells

Status: **KEEP** (`bake_proxies()`'s `group_cells`, 4 by default; 0: none) · Branch `fellow/geometry` · Code:
`modules/fellow_world/fellow_world_streamer_3d.{h,cpp}` (the groups' bake, `_update_group()`, the editor's preview),
`modules/fellow_world/fellow_cell_proxies.{h,cpp}` (`bake_proxy()`'s triangle target; a terrain's outer edges kept on
the welded mesh), `modules/fellow_editor/fellow_editor_plugin.{h,cpp}` (the dialog's group size) · Checks:
`benchmarks/project/bench/world_proxies_check.gd` (`--groups`; the groups shown), the editor check (`stream_back.gd`:
the groups' proxies shown in a game, hidden once their cells load).

## Problem

Every unloaded cell draws its own proxy (decisions 0174, 0251): World Lab's corner view, 144 cells and 144 terrain
regions, costs 347 draws, 270 of them proxies; a 4 km world of 64 m cells would have thousands in view. The readiness
assessment's streaming row: "a proxy a draw (no grouping of far cells)".

## Change

`bake_proxies()` groups the cells `group_cells` by `group_cells` (4) on a grid of their boxes' centres in the
streamer's space (the pitch: `group_cells` times the cells' median size). After baking a group's cells' proxies it bakes
one more from their content together (each cell's content placed as in the streamer, around the group's centre):

* **Its detail**: it is seen from farther than its cells' proxies — from the distance at which none of its cells can be
  loaded: the load distance (the cells' own if larger), `unload_margin`, and the farthest point of the cells' boxes
  from the proxy's centre (visibility ranges measure to the instance's centre). By that factor `s` (at most
  `group_cells`): as many triangles as its cells' proxies over `s²`, texels `s` times theirs. A terrain keeps its
  cells' quads, and only the group's outer edges are locked (the lock now found on the welded mesh: edges of one
  terrain triangle), so it meets its neighbours' proxies without cracks.
* **Its switch**: a `MeshInstance3D` under the streamer (`FellowGroupProxy_<x>_<z>`, its file
  `group_<x>_<z>_proxy.res`) whose `visibility_range_begin` is that distance (or `group_distance`), and the cells'
  proxies' `visibility_parent`: Godot's HLOD dependency hides them beyond it and shows it, with one test for the group.
* **Its cells loading**: the streamer hides a group's proxy while any of its cells is loaded (a target or a prefetch
  point can load a cell the camera is far from); a hidden visibility parent leaves its dependants to their own
  ranges, so the group's other cells show their own proxies.

A bake again removes the earlier groups (their dependants' parents cleared first). In the editor the groups' proxies
are hidden under the cells' preview as the cells' own are. The dialog has the group size and reports the groups.

## Verification

`world_proxies_check.gd` (lavapipe, 640 × 360; decision 0174's World Lab: 12 × 12 cells of 64 m and 144 terrain
regions, streamed to 160 m and seen from a corner), with groups of 4 × 4 (9 groups of cells, 9 of regions) and without:
the draws, the triangles, the pictures' difference from every cell loaded (A), and the groups shown, none with a cell of
theirs loaded. The editor check: the split level's 15 cells give 3 groups, owned by the level and saved with it; in a
game they are shown while their cells are unloaded and hidden once `load_now()` loads them. The image regression suite.

## Results

World Lab (C: the cells' and regions' proxies; D: C with the buildings streamed by 70 m; textured proxies, 0.5 m):

| | draws | triangles drawn | mesh memory | proxies' textures | bake, cells | C: pixels > 16 levels off A / mean | D |
|---|---:|---:|---:|---:|---:|---:|---:|
| each cell its proxy | 347 | 518 590 | 25.2 MB | 7.9 MiB | 14.5 s | 0.43 % / 0.0019 | 0.16 % / 0.0011 |
| **groups of 4 × 4** | **107** | **451 333** | 26.2 MB | 9.3 MiB | 22.3 s | 0.65 % / 0.0022 | 0.36 % / 0.0015 |

16 of the 18 groups were drawn — the two holding the loaded cells near the camera hidden, their other cells' proxies
shown — and none with a cell of theirs loaded. The groups add 15 858 triangles for the cells and 2 304 for the regions
(kept in memory with the cells' proxies), 1.5 MB of textures and 8 s of bake. Their switch is at about 370 m here (160
+ 25 + 185 m); past it the far cells are a few draws.

A first version took the groups' triangles as a share of their content's: after `stream_meshes` had put stand-ins in
the content, that share of an already simplified mesh left a triangle or two a building — shards on the horizon (D: 4 %
of the picture off). The groups now take their cells' proxies' triangles as the measure.

## Decision

Keep, on by default: far cells cost a draw a group instead of a draw a cell (World Lab 347 → 107 draws), for a fifth of
a per cent more of the picture off at the switch's distance.

## Rejected / not done

* **A hierarchy** (groups of groups, as HLOD's cascades): one level here; a 4 km world of 64 m cells still has a few
  hundred groups, each a draw.
* **Merging the cells' proxies' meshes** instead of baking from content: their textures would need an atlas of
  atlases, and the group's simplification would start from a simplified mesh.
* **A fade** between a group and its cells' proxies (`visibility_range_begin_margin` with a dither): the switch pops,
  as cell proxies already do; the same distance is beyond the shadows' and most fog's.
* **Not yet**: a group's proxy is resident with the level like the cells' proxies; groups do not follow cells moved
  after the bake.
