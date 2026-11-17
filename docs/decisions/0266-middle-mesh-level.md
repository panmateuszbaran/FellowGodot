# 0266 — Streamed meshes in three levels: a middle level between the stand-in and the full mesh

Status: **IN PROGRESS** (opt-in: `bake_proxies()`'s `mesh_middle_ratio` 0 and `FellowWorldStreamer3D.mesh_middle_distance` 0 by
default — two levels, as before) · Branch `fellow/geometry` · Code: `modules/fellow_world/fellow_world_streamer_3d.{h,cpp}`
(the middle level's bake, the streamed meshes' levels) · Checks: `benchmarks/project/bench/world_proxies_check.gd`
(`--mesh_middle_ratio`, `--mesh_middle_distance`: lane D and the streamed flight), the image regression suite.

## Problem

A streamed mesh (decision 0174) has two states: its stand-in (15 % of its triangles, in the cell's scene) and its full
mesh, loaded within `mesh_distance` (World Lab: 70 m) and dropped beyond. The full mesh's vertices and indices are
memory for the whole of that radius, though beyond a few tens of metres Godot's own levels of detail draw a fraction of
its triangles; a smaller radius shows the stand-in where its 15 % is seen. The readiness review: "meshes streamed in two
states only" (engines that ship AAA games stream a mesh's levels one by one).

## Change

* **`bake_proxies()`'s `mesh_middle_ratio`** (0: none): with `stream_meshes`, each streamed mesh also gets a middle
  level, that share of its triangles (`simplified_mesh()`: its materials, UVs, tangents and colours kept; its own levels
  of detail generated), saved apart once per full mesh's file (`meshes/<mesh>_middle_<hash>.res`, a meta on the stand-in
  naming it). The result has `middle_meshes`, `middle_triangles`.
* **`FellowWorldStreamer3D.mesh_middle_distance`** (0: none): a streamed mesh within `mesh_distance` shows its full mesh,
  within `mesh_middle_distance` its middle level, beyond it its stand-in; each level loaded on the ResourceLoader's
  threads when it is wanted (a few at a time, `max_mesh_loads`) and swapped in when it lands (crossing over with
  `crossfade`, decision 0259), the level it replaces dropped; a coarser level only past its distance plus
  `unload_margin`. `get_stats()`'s `streamed_meshes_middle`.

## Verification

`world_proxies_check.gd` (lavapipe, 640 × 360, without TAA so that pictures compare bit for bit; decision 0174's World
Lab, 8 × 8 cells of 64 m, each with ten buildings of 1.5 m quads as one mesh of 11 400 triangles, streamed to 160 m):
D (the corner's view with the buildings streamed) against the world all loaded (A), and two flights across the world
at 12 m (30 m/s), the full meshes whenever loaded and streamed: the buffers' video memory a frame, the loads and swaps.

## Results

PENDING

## Decision

PENDING

## Rejected / not done

* **Godot's own levels of detail as the middle level** (the full mesh's vertices with a coarser index buffer): the
  vertices are most of the memory; a middle level needs fewer of them.
* **More levels** (a chain, each loaded in turn): each level is a file, a load and a swap; three cover the stand-in's
  distance, the full mesh's and the band between them where Godot's own levels already draw a fraction of it.
