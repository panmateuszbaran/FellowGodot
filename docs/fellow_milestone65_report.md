# FellowEngine — milestone 65 report (terrain regions streamed with the world's cells)

Status: **FINAL** for the cloud's part (2026-11-10): checked on lavapipe; the RTX's flight with its next job. Branch
`fellow/geometry`. Decision record: `docs/decisions/0173-terrain-regions.md`.

## 0. Result

The rest of the open-world ground agreed after the showcase (milestones 64-65): a terrain larger than memory. A
`FellowTerrain3D` is split into regions — terrains of one `region_group`, each a streaming cell of a
`FellowWorldStreamer3D` — that join without a seam and load without a hitch:

* **Memory**: a 4 km map at 1 m keeps 17.6 MB of terrain textures around the camera instead of 213 MB (21 of its 256
  regions loaded at most during a flight across it).
* **No seam**: along the 474 shared edges the two sides' heights are equal to float rounding (7.6 µm), also after
  brushes across an edge; the same levels of detail meet, the border vertices are drawn from the exact heights, the
  border normals from the neighbours' heights, the textures run on in the group's space. Over a red sky, no crack.
* **No hitch**: a region's data is built on the worker thread that instances its cell; entering the world costs the
  main thread 2.4 ms on average, 6.2 ms at most (46-60 ms before; the whole 4 km map 487 ms).
* **Edited as one**: the brushes reach every region under them and make their shared edges equal; the editor's undo
  restores all the regions a stroke changed. The editor's Split into Streaming Cells splits terrains into regions.

## 1. Development

* `modules/fellow_world/fellow_terrain_3d.{h,cpp}`: `region_group` and the group's registry (`_region_register()`,
  `_region_neighbor()`, `get_region_neighbors()`), `_refresh_border_normals()` from the neighbours' heights (on
  entering and leaving, after a rebuild, after an edit near an edge), `sculpt()`/`paint()`/`paint_holes()` carried to
  the regions under the brush (`_region_each_touched()`) and `_sync_region_edges()`, `split_into_regions()`,
  `prepare_data()`, the shader's `edge_exact` (border vertices from mip 0) and `world_offset` (the layers and the macro
  variation in the group's space), the foliage's cells on the group's grid, `get_stats()`'s `enter_usec`.
* `modules/fellow_world/fellow_world_streamer_3d.{h,cpp}`: a cell's terrains build their data on the instancing worker
  thread; `split_into_cells()`'s `split_terrains`.
* `modules/fellow_editor/fellow_terrain_editor.{h,cpp}`: a stroke's snapshots of every region it may reach, one undo
  for all; `fellow_editor_plugin.cpp`: Split into Streaming Cells splits terrains.
* `benchmarks/project/bench/terrain_regions_check.gd` (new); the editor check's terrain brush: a stroke over two
  regions.
* The class reference (`FellowTerrain3D.xml`, `FellowWorldStreamer3D.xml`), `docs/FELLOWENGINE_OPTIONS.md`.

## 2. Verification

* `terrain_regions_check.gd` on lavapipe (640 × 360) at 1 km (16 regions) and 4 km (256 regions): below.
* The editor check (1920 × 1080): every check passes; a Raise stroke over two regions' edge raises both (1.1 m at the
  edge, 0 difference between them), one undo restores both heightmaps byte for byte, one redo makes them again.
* The image regression suite (editor build and release template): every picture exact (no terrain of a group: the
  shader's new uniforms are 0).
* `xmllint` and `make_rst.py --dry-run`: no errors, no warnings.

## 3. Measurements

**4 km at 1 m, 256 regions of 256 m** (the whole map entering the world: 487 ms of the main thread; the split 186 ms,
129 MB of region scenes):

| | whole | regions |
|---|---|---|
| terrain textures | 213 MB | 17.6 MB at most (21 regions loaded) |
| all texture memory | 226 MB | 30.9 MB at most |
| heights along 474 shared edges (40,764 samples) | — | ≤ 7.6 µm apart |
| cracks (sky where the whole has ground), four views | — | 0-26 px of 230,400, on far silhouettes |
| pixels over 8 levels from the whole's picture | — | 0.7-2.3 % |

**The flight** (5.7 km at 30 m/s, the streamer's 450 m): 96 regions entered and 96 left; a region's entry 2.4 ms of
the main thread on average, 6.2 ms at most; lavapipe's frames 74 ms at the median, 166 ms at the 99th percentile, the
longest 317 ms as the first regions came in.

The pictures differ from the whole's in two places, neither a seam: far silhouettes (a region's coarsest level of
detail is its own size, 256 m; the whole's goes to 4 km) and the splat maps' blend edges (each region's texture
coordinates computed from its own size: rounding at the height blend's sharp threshold).

## 4. Limits

* Beyond the streamer's distance there is no terrain: the far ground needs milestone 66's proxies for unloaded cells.
* A region's coarse mips of its normals and splat maps see one side of its edges: a slight shading difference at a
  distance.
* The collision shapes are still made on the main thread when a region enters (part of its 2.4 ms).
* Regions must be axis-aligned, of one spacing and size (`split_into_regions()` makes them so).
* Measured on lavapipe.

## 5. Next

Milestone 66: proxies baked offline for unloaded cells (the far terrain among them), mesh LOD streaming.
