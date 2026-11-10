# 0174 — Proxies baked for unloaded streaming cells, and streamed meshes

Status: **KEEP** (a method run offline and two properties: a streamer without proxies and with `mesh_distance` 0 works
as before) · Branch `fellow/geometry` · Code: `modules/fellow_world/fellow_cell_proxies.{h,cpp}` (new: a cell's
content merged, simplified and coloured; a mesh's stand-in), `modules/fellow_world/fellow_world_streamer_3d.{h,cpp}`
(`bake_proxies()`, `mesh_distance`, `max_mesh_loads`, the streamed meshes' loads and releases, a baked proxy hidden
under the editor's preview), `modules/fellow_world/fellow_terrain_3d.{h,cpp}` (`make_proxy_arrays()`),
`modules/fellow_editor/fellow_editor_plugin.{h,cpp}` (Project > Tools > Bake Streaming Proxies) · Checks:
`benchmarks/project/bench/world_proxies_check.gd`; the editor check's proxies step and `stream_back.gd`.

## Problem

World streaming (decision 0147) frees the cells beyond the streamer's distance, and with them everything in them: past
200 m there was nothing — no buildings on the horizon, and since terrain regions (decision 0173) no ground either. A
cell's own children could be its proxy (shown while the cell is unloaded), but nothing made them: an artist would
model a low-detail version of every cell by hand. And a loaded cell held every mesh at full detail however far it was
within the distance. The readiness assessment's streaming row: "no baked proxies for unloaded cells, no mesh LOD
streaming".

## Change

**Proxies, baked offline.** `FellowWorldStreamer3D.bake_proxies(directory, options)` opens each cell's scene and
makes one mesh of it:

* *Gathered*: its visible `MeshInstance3D`s (not skinned), `MultiMeshInstance3D`s (up to 256 instances a node, a
  sample of larger ones: far away their density is what shows) and `FellowTerrain3D` regions
  (`make_proxy_arrays(quads)`: a grid of `terrain_quads`, 32, along the region's side, holes left out), in the cell's
  space.
* *Coloured*: each surface's vertices take its material's mean albedo — the albedo colour times the albedo texture's
  mean (a small mip, decompressed, averaged in linear space; cached per texture); a terrain's vertices the layers'
  colours times their textures' means, weighted by the splat maps. One shared material (vertex colour as albedo,
  roughness 0.9), no texture.
* *Simplified*: Godot's LOD generation (meshoptimizer, vertices welded within 60° of normal) over the merged mesh; the
  first level with at most `ratio` (5 %) of the triangles is kept, its vertices compacted.

The proxy is saved as `<cell>_proxy.res` and added to the cell as a `MeshInstance3D` child named `FellowProxy`
(replacing an earlier one): a cell's own children are already shown while it is unloaded and hidden while it is loaded
(decision 0147). It casts no shadow (`proxy_shadows`) and can end at `proxy_distance`. In the editor, where a cell
shows its scene (decision 0151's preview), the baked proxy's instance is hidden without changing its saved visibility.

**Streamed meshes.** With `stream_meshes`, the cells' meshes of at least `mesh_min_triangles` (2000) triangles are
replaced in their scenes by stand-ins — the same mesh simplified to `mesh_ratio` (15 %) of its triangles, its
materials, UVs, tangents and colours kept — and the full meshes saved apart (`meshes/<cell>_<n>.res`, unless a mesh is
a file already). Once a cell is loaded the streamer measures each stand-in's distance from the camera and the targets:
within `mesh_distance` its full mesh is requested from the ResourceLoader's threads (`max_mesh_loads`, 2, at once) and
swapped in when it arrives; beyond `mesh_distance + unload_margin` the stand-in is put back and the full mesh freed. 0
(the default): the full meshes whenever the cell is loaded.

**In the editor**: Project > Tools > Bake Streaming Proxies for the selected streamer (a directory, and whether to
stream the large meshes), its proxies owned by the scene.

## Verification

`world_proxies_check.gd` (lavapipe, 640 × 360): a world of 12 × 12 cells of 64 m (World Lab's cells: ten buildings as
one mesh of 1.5 m quads, 40 crates, Streaming Lab's textures) on a 768 m terrain split into 144 regions, both under
streamers, seen from a corner across the world: (A) every cell loaded; (B) streamed to 160 m; (C) B with proxies
baked for the cells and the regions; (D) C with the buildings streamed by 70 m; (E) a flight across the world at
30 m/s with D's setup. The editor check: the menu item and its dialog on the split level's cells, a proxy for each,
owned by the level and saved with it; `stream_back.gd`: the proxies shown in a game while their cells are unloaded,
hidden once loaded. The image regression suite: every picture exact (no proxies in its scenes).

## Results

**World of 12 × 12 cells** (lavapipe, 640 × 360; the streamer's 160 m and its 25 m margin; the cells loaded from afar,
so that none is kept by the margin):

| | A: all loaded | B: streamed | C: B with proxies | D: C with streamed meshes (70 m) |
|---|---|---|---|---|
| cells and regions loaded | 144 + 144 | 9 + 9 | 9 + 9 | 9 + 9 |
| triangles drawn | 2.45 M | 431 k | 494 k | **349 k** |
| draw calls | 517 | 77 | 347 | 347 |
| buffer memory (meshes) | 148.0 MB | 20.0 MB | 22.0 MB | **17.1 MB** |
| pixels over 16 levels from A's picture | — | 7.8 % (the horizon empty) | **2.0 %** | 2.0 % |
| mean difference from A's picture, a channel | — | 0.051 | **0.0053** | 0.0052 |

**The bakes**: the cells' 1,695,586 triangles → 58,378 in their proxies (3.4 %), 4.7 s for 144 cells; the regions'
73,728 → 9,264 (16 quads a side), 0.33 s; with `stream_meshes` the 144 buildings' 1,626,466 triangles → 203,198 in their
stand-ins (12.5 %), 9.4 s.

**Flights** (2,059 frames across the world at 30 m/s, 12 m over the ground; 88 cells entered):

| | full meshes whenever loaded | streamed by 70 m |
|---|---|---|
| buffer memory, mean / most | 42.2 / 51.4 MB | **24.6 / 28.2 MB** |
| lavapipe's frames, median / 99th percentile | 394 / 825 ms | 279 / 557 ms |
| full meshes loaded on the way | 91 | 54 |
| the main thread's swap (set the mesh; free the full one) | 80 µs on average | 60 µs on average, 4.7 ms at most (a full mesh freed) |

The proxies cost 2 MB and 63 k triangles here, and a draw each (270 for the unloaded cells and regions in view); the
pictures show the buildings' masses and the ground to the end of the world, in their materials' mean colours, without
shadows.

**On the RTX** (job f373, 1920 × 1080, the release template): the picture 7.6 → 1.9 % off with proxies; mesh memory
39.8 → 41.8 MB with them, 36.9 MB with streamed meshes; the flights' mesh memory 62.0 → 44.4 MB on average (71.2 → 48.0
at most), frames 1.8 → 1.6 ms at the 99th percentile, the longest 2.3 and 2.2 ms; a swap 18 µs on average, 36 µs at
most.

## Decision

Keep: a streamed world keeps its horizon — buildings and ground beyond the streamer's distance — from proxies baked in
one call, and a loaded cell's large meshes stay simplified until the camera comes near.

## Rejected / not done

* **Proxies at run time** (made as cells unload): the same merging and simplification on the main thread at the
  worst moment, and nothing to show for a cell never loaded.
* **Textured proxies** (an atlas baked per cell, as HLOD's impostors): a texture per cell to stream again; from beyond
  the streamer's distance a vertex colour per a few metres is what the pixels show.
* **One proxy per group of cells** (a quadtree of proxies, as HLOD's cascades): fewer draws far away, but each cell's
  proxy is its own unit that loads and unloads with it; the proxies here cost 270 draws in view.
* **Streaming a mesh's levels of detail one by one** (Nanite-like pages): two states — the stand-in and the full mesh
  — and Godot's own LODs within each.
* **Not yet**: proxies of a region and its loaded neighbour meet at different resolutions (small cracks along an
  unloaded region's edge, at the streamer's distance); skinned meshes, blend shapes and custom arrays are not streamed;
  the stand-ins are in the cells' scenes, so a cell's scene must be baked again after its meshes change.
