# 0266 — Streamed meshes in three levels: a middle level between the stand-in and the full mesh

Status: **KEEP** (opt-in: `bake_proxies()`'s `mesh_middle_ratio` 0 and `FellowWorldStreamer3D.mesh_middle_distance` 0 by
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

On World Lab's own buildings (flat walls of 1.5 m quads: their stand-ins lose nothing a view shows) every setting gave
the corner's view the same pixels: the full meshes within 35 m instead of 70 m saved the flight's buffers 24.0 → 20.0
MB with the stand-ins alone. So the check got curved props (`--rocks=6`: bumpy spheres of 4 096 triangles, 4-8 m across,
six a cell, each a streamed mesh) and a view over the roofs (`--ground=1`: 30 m up a quarter of the way in, looking down
across the world), at 960 × 540 (6 × 6 cells: 252 streamed meshes, 1.32 M triangles; the stand-ins 12.5 % of them, the
middle levels 25 %, asked 40 %).

## Results

The view over the roofs against the world all loaded (518 400 pixels; 227 of them more than 16 levels off with the full
meshes within 70 m — the proxies and stand-ins beyond), its buffers' video memory, and the flight's:

| streamed meshes | pixels > 16 levels off | > 4 levels | the view's buffers | the flight's buffers (mean / max) |
|---|---:|---:|---:|---:|
| full within 70 m (two levels, decision 0174's) | 227 | 997 | 33.3 MB | 33.0 / 38.5 MB |
| full within 35 m | 392 | 1 248 | 27.0 MB | 27.5 / 30.2 MB |
| full within 35 m, the middle level within 70 m | 317 | 1 180 | 27.9 MB | 28.3 / 31.1 MB |
| **full within 20 m, the middle level within 70 m** | **318** | 1 181 | **25.1 MB** | **26.9 / 29.9 MB** |

The middle level within 70 m halves what the stand-ins add between 35 and 70 m (+90 pixels instead of +165), and lets
the full meshes keep to 20 m with no more: a quarter less of the view's buffers than the full meshes within 70 m
(−8.2 MB; the flight's −18 %), for 91 pixels more of half a million more than 16 levels off. The corner's view (lane D)
the same pixels within one in every setting (1 853-1 856 off A). Loads on the flight: 129 with two levels, 219-254
with three (each mesh loads its middle level, then its full one), the main thread's swaps 36-60 µs on average.

## Decision

Keep, opt-in: a world whose streamed meshes' simplification shows at a distance (curved, detailed props) holds its full
meshes in a smaller radius with a middle level around it — less memory for no more error than the stand-ins over a
larger one. Without the options nothing changes (two levels, as before); a world of flat buildings gains nothing from
it.

## Rejected / not done

* **Godot's own levels of detail as the middle level** (the full mesh's vertices with a coarser index buffer): the
  vertices are most of the memory; a middle level needs fewer of them.
* **More levels** (a chain, each loaded in turn): each level is a file, a load and a swap; three cover the stand-in's
  distance, the full mesh's and the band between them where Godot's own levels already draw a fraction of it.
