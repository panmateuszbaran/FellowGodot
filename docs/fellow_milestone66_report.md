# FellowEngine — milestone 66 report (proxies for unloaded cells, streamed meshes)

Status: **FINAL** for the cloud's part (2026-11-10): checked on lavapipe; the RTX's run with its next job. Branch
`fellow/geometry`. Decision record: `docs/decisions/0174-cell-proxies.md`.

## 0. Result

The last of the items agreed after the showcase was set aside (63: recorded pipelines; 64-65: the terrain's holes,
fades, impostors and regions; 66: streaming's authoring side). A streamed world keeps its horizon and holds its large
meshes at full detail only near the camera:

* **Proxies, baked in one call**: `FellowWorldStreamer3D.bake_proxies()` makes each cell a proxy — its meshes,
  MultiMesh instances and terrain regions merged, simplified to 5 % (meshoptimizer) and coloured per vertex by their
  materials' mean albedo — shown while the cell is unloaded. A 768 m world streamed to 160 m: 7.8 % of the picture
  wrong (the horizon empty) → 2.0 %, for 2 MB and 63 k triangles.
* **Streamed meshes**: with `stream_meshes` a cell's large meshes are stand-ins (15 % of their triangles) until the
  camera is within the streamer's `mesh_distance`, their full meshes loaded on the ResourceLoader's threads and freed
  again beyond it: a flight across the world 42.2 → 24.6 MB of mesh memory on average (51.4 → 28.2 at most).
* **In the editor**: Project > Tools > Bake Streaming Proxies for the selected streamer; a baked proxy hidden while
  the editor shows the cell's scene.

## 1. Development

* `modules/fellow_world/fellow_cell_proxies.{h,cpp}` (new): a cell's content gathered, coloured (a texture's mean in
  linear space, cached), merged and simplified (`ImporterMesh::generate_lods()`, the level with at most the ratio of
  triangles, compacted); a mesh's stand-in with its attributes and materials; one shared proxy material.
* `modules/fellow_world/fellow_world_streamer_3d.{h,cpp}`: `bake_proxies()`; `mesh_distance`, `max_mesh_loads`, the
  streamed meshes registered as cells enter and leave, loaded and released by distance, their stats in `get_stats()`;
  a baked proxy (meta `_fellow_cell_proxy`) hidden under the editor's preview.
* `modules/fellow_world/fellow_terrain_3d.{h,cpp}`: `make_proxy_arrays(quads)`.
* `modules/fellow_editor/fellow_editor_plugin.{h,cpp}`: the Bake Streaming Proxies menu item and dialog.
* `benchmarks/project/bench/world_proxies_check.gd` (new); the editor check's proxies step and `stream_back.gd`.
* The class reference (`FellowWorldStreamer3D.xml`, `FellowStreamingCell3D.xml`, `FellowTerrain3D.xml`),
  `docs/FELLOWENGINE_OPTIONS.md`.
* Milestones 64-65 on the RTX (job f372) added to their reports; `terrain_holes_check.gd`'s fade measure made
  independent of the picture's height (it read the sky at 1080 rows).

## 2. Verification

* `world_proxies_check.gd` on lavapipe (640 × 360), 12 × 12 cells: below.
* The editor check (1920 × 1080): every check passes; Bake Streaming Proxies through its menu item and dialog on the
  split level's 15 cells: 15 proxies owned by the level and saved with it, 16 files; `stream_back.gd`: in a game the 15
  proxies shown before the cells load, none after.
* The image regression suite (editor build and release template): every picture exact (no proxies in its scenes).
* `xmllint` and `make_rst.py --dry-run`: no errors, no warnings.

## 3. Measurements

**A world of 12 × 12 cells of 64 m** (each ten buildings as one mesh of 1.5 m quads and 40 crates, Streaming Lab's
textures) on a terrain split into 144 regions, seen from a corner across it; the streamer's 160 m:

| | all loaded | streamed | + proxies | + streamed meshes (70 m) |
|---|---|---|---|---|
| triangles drawn | 2.45 M | 431 k | 494 k | 349 k |
| draw calls | 517 | 77 | 347 | 347 |
| mesh memory | 148.0 MB | 20.0 MB | 22.0 MB | 17.1 MB |
| pixels over 16 levels from the whole's | — | 7.8 % | 2.0 % | 2.0 % |

The bakes: 1.70 M triangles → 58 k for the cells' proxies (4.7 s), 74 k → 9.3 k for the regions' (0.33 s), the
buildings 1.63 M → 203 k as stand-ins (9.4 s).

**Flights** (2,059 frames at 30 m/s, 88 cells entered): mesh memory 42.2 MB on average and 51.4 MB at most with the
full meshes whenever loaded, 24.6 and 28.2 MB streamed by 70 m (54 full meshes loaded on the way); the main thread's
swap 60 µs on average, 4.7 ms at most (freeing a full mesh, on lavapipe); lavapipe's frames 394 → 279 ms at the median.

## 4. Limits

* A proxy is a draw: 270 in view here. One proxy per group of far cells (HLOD's cascades over the proxies) would cut
  them.
* Vertex colours, no texture: right for the distance beyond the streamer's, not nearer; no shadows by default.
* An unloaded region's proxy and its loaded neighbour meet at different resolutions: small cracks at the streamer's
  distance.
* Skinned meshes, blend shapes and custom arrays are not streamed; a cell's scene is baked again after its meshes
  change.
* Measured on lavapipe.

## 5. Next

The plan agreed on 2026-11-10 is done. Next for the engine: the RTX's run of this milestone; then what the readiness
assessment lists first — the far terrain's and proxies' draws grouped, virtual texturing, other GPUs.
