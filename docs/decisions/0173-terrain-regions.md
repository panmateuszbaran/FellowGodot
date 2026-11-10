# 0173 — Terrain regions streamed with the world's cells

Status: **KEEP** (a new property and methods: a terrain without a `region_group` draws as before) · Branch
`fellow/geometry` · Code: `modules/fellow_world/fellow_terrain_3d.{h,cpp}` (`region_group`, the group's registry and
neighbours, `_refresh_border_normals()`, the brushes carried across and `_sync_region_edges()`, `split_into_regions()`,
`prepare_data()`, the shader's `edge_exact` and `world_offset`, the foliage on the group's grid, `enter_usec`),
`modules/fellow_world/fellow_world_streamer_3d.{h,cpp}` (regions built on the instancing worker thread;
`split_into_cells()`'s `split_terrains`), `modules/fellow_editor/fellow_terrain_editor.{h,cpp}` (a stroke's undo over
every region it reached), `modules/fellow_editor/fellow_editor_plugin.cpp` (Split into Streaming Cells splits terrains)
· Checks: `benchmarks/project/bench/terrain_regions_check.gd`; the editor check's terrain brush (a stroke over two
regions).

## Problem

`FellowTerrain3D` holds its whole height field in memory — the CPU's copies and the GPU's textures with their mips:
213 MB for a 4 km map at 1 m (heights, normals, one splat map). World streaming (decision 0147's
`FellowWorldStreamer3D`) loads and frees a level's cells around the camera, but a terrain wider than two cells stayed
loaded whole. The readiness assessment's open-world row: "without streamed regions".

## Change

**Regions.** A terrain with a `region_group` is a tile of a larger height field; the terrains of a group with the same
spacing, edge to edge, find each other (a registry per group, on entering and leaving the world) and keep their shared
edges seamless:

* *Levels of detail*: regions of the same size, `patch_size`, `lod_distance` and `morph_ratio` are subtrees of the
  whole's quadtree, so the same levels meet along an edge; their border vertices are drawn from the exact heights and
  normals (mip 0: a region's mips see one side of its edge only, and its neighbour's the other).
* *Normals*: a region's border normals are made from its neighbours' heights while they are in the tree (and again
  when one comes or goes, or an edit reaches an edge), else from its own as before.
* *Textures*: the layers' textures and the macro variation are sampled in the group's space (`world_offset`), so they
  run on across an edge; the foliage is scattered on the group's grid.
* *Edits*: `sculpt()`, `paint()` and `paint_holes()` reach the regions under the brush; after a sculpt, each shared
  edge texel under it takes the mean of its regions' heights (a smoothing brush sees each region's own side). The
  editor's stroke records every region it changed: one undo restores them all.

**Splitting.** `split_into_regions(region_quads, directory, streamer)` cuts the terrain into regions of a power-of-two
multiple of `patch_size` quads (one texel more: the shared edge), padded with holes past the map's end so that all
have the same size, with the terrain's settings, layers, foliage, splat and hole maps; with a streamer each is saved as a
scene and becomes a `FellowStreamingCell3D` at its centre. `split_into_cells()`'s option `split_terrains` does it for
the terrains it meets (the editor's Split into Streaming Cells passes it).

**Loading without a hitch.** A cell's content is instanced on a worker thread (decision 0147); a region's data — the
heights, their bounds, mips, normals, splat maps and textures — is now built there too (`prepare_data()`), so that
entering the world makes only its collision shapes and instances on the main thread.

## Verification

`terrain_regions_check.gd` (lavapipe, 640 × 360): the same map whole and split, four views over a red sky, heights
along every shared edge from both sides, a raise and a smoothing brush over an edge, a diagonal flight across the map
with the streamer's 450 m (below). The editor check: a Raise stroke over two regions' edge changes both, their edge
equal; undone together, redone together. The image regression suite: every picture exact (no terrain of a group).

## Results

**A 4 km map at 1 m (4097 × 4097) in 256 regions of 256 m**:

| | whole | regions |
|---|---|---|
| the terrain's textures (heights RF, normals RG8, a splat map; their mips) | 213 MB | **17.6 MB** at most (21 regions loaded) |
| the GPU's texture memory, all | 226 MB | 30.9 MB at most |
| the main thread's time to enter the world | 487 ms | 2.4 ms a region (6.2 at most) |
| heights along the 474 shared edges (40,764 samples, both sides) | — | at most 7.6 µm apart (float rounding) |
| the same after a raise and a smoothing brush over an edge | — | the same |
| the sky through the ground where the whole has ground (cracks), four views | — | 0-26 pixels of 230,400, on far silhouettes (the regions' coarsest level is 256 m, the whole's 4 km) |
| pictures against the whole's (share of pixels over 8 levels) | — | 0.7-2.3 % (far silhouettes, the splat maps' blend edges) |

**The flight** (5.7 km diagonally at 30 m/s, 11,474 frames, the streamer's 450 m): 96 regions entered and 96 left; a
region entering the world took 2.4 ms of the main thread on average, 6.2 ms at most (its collision shapes, instances
and border normals), its data built on the worker thread that instanced it — 46-60 ms on the main thread before (one
run: 48 of them waiting to create a texture), and 487 ms for the whole map's. Lavapipe's frames: 74 ms at the median,
166 ms at the 99th percentile, the longest 317 ms (the flight's 33rd frame, as the first regions came in).

## Decision

Keep: an open world's terrain loads with its cells, its memory that of the regions around the camera, with no seam.

## Rejected / not done

* **One terrain with a streamed virtual height texture** (an indirection map, region pages in a texture array): one node,
  but every terrain sample through an indirection, and the mips at the pages' edges the same problem; regions reuse the
  streamer, the editor and the terrain as they are.
* **Regions overlapping by an apron** for the normals: every texel stored twice along the edges; the neighbours' heights
  are at hand when they matter (both regions in the tree).
* **Not yet**: the far terrain beyond the streamer's distance (milestone 66: proxies baked for unloaded cells); the
  normals' and splat maps' coarse mips still one-sided along an edge (a shading difference at a distance, within the
  pictures' 8-level differences above); collision shapes made on the main thread (a quarter of an entry).
