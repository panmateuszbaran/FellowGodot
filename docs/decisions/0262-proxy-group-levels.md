# 0262 — Levels of proxy groups: a coarser proxy over the groups' for farther away

Status: **IN PROGRESS** (KEEP expected) (`bake_proxies()`'s `group_levels`, 1 by default: the groups alone, as before) · Branch
`fellow/geometry` · Code: `modules/fellow_world/fellow_world_streamer_3d.cpp` (the levels' bake from the level below's
proxies, their visibility parents, `_update_group()` up the levels), `modules/fellow_world/fellow_cell_proxies.{h,cpp}`
(a proxy marked as a terrain's; an error floor for the objects' simplification; a coarser level's texels from the
surfaces facing them, a proxy's texture read inside its charts), `modules/fellow_editor/fellow_editor_plugin.{h,cpp}`
(the dialog's levels) · Checks: `benchmarks/project/bench/world_proxies_check.gd` (`--group_levels`, `--far=1`: the
world from outside it), the editor check (two levels baked from the dialog; shown in a game, hidden once the cells
load), `benchmarks/project/scenes/frontier_lab.gd` (`--frontier_group_levels`).

## Problem

Decision 0252 gave a group of `group_cells` × `group_cells` far cells one proxy, the cells' proxies its dependants: World
Lab's corner view 347 → 107 draws, Frontier Lab's 755 → 468. One level only — the readiness review's streaming row:
"one level of proxy groups (no hierarchy)". Beyond the groups, every group is still a draw with the detail and texels
of the distance it is first drawn from, however much farther it is: Frontier Lab's 4 km world has 64 groups of cells
and 16 of regions, a 16 km world of 128 m cells would have 1 024 and 256 — the far world a thousand draws again, and its
triangles and textures in memory at a detail no view needs past a few kilometres.

## Change

* **`group_levels`** (1-4; 1: as before): after the groups, `bake_proxies()` groups their proxies again, `group_cells`
  by `group_cells` on the groups' grid, level by level; a set of two or more gets one proxy, `FellowGroupProxy_L<n>_<x>_<z>`
  (its file `group_l<n>_<x>_<z>_proxy.res`) under the streamer.
* **Baked from the level below's proxies**, not from the cells' content again: their meshes and textures merged (the
  textures sampled at their finest level), simplified and textured anew — a level costs a fraction of the groups'
  bake, and a proxy mostly of a terrain's triangles (a group of regions) is marked as a terrain's
  (`FellowCellProxies::TERRAIN_META`): simplified to its error with its outer edges locked, so that neighbours meet
  without cracks, as decision 0255's regions and groups.
* **Its detail for its distance**: it is drawn from where every proxy below it would be drawn (the farthest's begin
  plus how far its centre is from the new proxy's: visibility ranges measure to the instance's centre); by the factor
  `s` that is past the proxies below it (at most `group_cells`), its texels are `s` times coarser, its objects keep
  their triangles over `s²` — or, if more, as many as an error of `terrain_error` pixels (2) needs at that distance
  (the count alone left a few triangles a building, no shape: World Lab's houses as slabs), its terrain that error.
  Its texture may be twice as wide a level (it holds many groups' charts; at most 4 096).
* **Its switch**: it is the visibility parent of the proxies below it (Godot's HLOD dependency: they are hidden beyond
  its begin, it is drawn there; `group_fade` fades them into it, as decision 0256's groups); `_update_group()` walks up
  the levels when a cell loads or unloads — a level's proxy is shown only while all the proxies below it are (none of
  its cells loaded), hidden it leaves them to their own ranges.
* **Its texture**: a coarser level's texels look far along their normals (the reach is a proxy's simplification
  error, metres here) and would pick up whatever lies there — a roof across a wall's edge, the next building's wall, a
  crate in front: each texel takes the first surface facing its own way, the first hit if none does; a texel no ray
  finds (an edge's, its ray along the source's edge) is tried again a quarter of the way to its triangle's centre, and
  if it misses again it is filled from its neighbours (decision 0251's dilation) instead of the sources' mean colour;
  a proxy's texture is read a texel and a half inside its triangle (its charts' borders are compressed blocks shared
  with the padding around them: grey fringes on every face otherwise).
* **Every proxy's faces facing the right way** (a fix for the cells' and groups' proxies too, decisions 0174, 0251,
  0252): a simplified triangle's normal took its side from its corners' source normals — on the welded mesh one of each
  corner's faces', at a box's corner the roof's or the other wall's for a wall's triangle (at right angles: either
  side). Proxies baked from content made of many quads have most corners inside a wall and were mostly right; a level
  baked from proxies, all corners and edges, had many of its walls lit from behind (below). The simplifier keeps each triangle's winding: a face
  now takes the source's winding (the side most of its triangles' own corners' normals give), its corners' normals
  turning it only where they clearly face the other way (a source wound against the rest).
* **Rebaking** removes every level (their dependants' parents cleared first). The editor's dialog has the levels and
  reports each one's groups and triangles; the result's `"group_levels"` lists them.

## Verification

`world_proxies_check.gd` (lavapipe, 640 × 360; World Lab's cells of 64 m — ten buildings of 1.5 m quads, 40 crates,
Streaming Lab's textures — on a terrain of regions of the same size) with `--far=1`: a view from outside the world's
corner, high, looking at its centre (F), every cell unloaded and C's proxies baked: with the coarser levels and with them
hidden (the groups' proxies alone), each against the world all loaded from there (A's).

Measurements being finished.

## Decision

Pending the measurements.

## Rejected / not done

* **Each level baked from the cells' content**: every cell's scene loaded again per level (Frontier Lab's cells' bake
  is 200 s for one level); the level below's proxies hold what a coarser one keeps, the terrain's edges where it has
  them.
* **A level's objects to a count only** (as the groups'): with `s²` fewer triangles than the groups' (already a twentieth
  of the content's), a building kept five — slabs on the horizon.
* **A proxy below kept apart into its terrain and its objects**: a group's proxy is one mesh; one mostly of a terrain's
  triangles counts as a terrain's (its outer edges locked, its error), else as objects. The streamers of World Lab and
  Frontier Lab keep the terrain's regions and the cells apart, so their proxies are one or the other; a level whose
  cells hold both is simplified by the majority's rule (its objects to the same error either way).
* **Streaming the levels' proxies** (loaded with the level, as the groups' and the cells'): the coarser levels are a few
  draws and a few MB; their textures streamed with the cells' would be the next step for worlds far beyond 4 km.
