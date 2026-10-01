# 0007 — Mesh clusters (frustum culling inside large surfaces)

Status: **KEEP** · Branch `fellow/geometry` · Toggle `rendering/fellow/geometry/mesh_clusters` (default **off**)
· Code: `servers/rendering/renderer_rd/storage_rd/fellow_mesh_clusters.*`, `mesh_storage.cpp` (`mesh_add_surface`),
`render_forward_clustered.cpp` (`_render_list_template`, `_render_scene`, `_render_shadow_append`),
`rendering_device.cpp` (`draw_list_draw_indexed_range`)

## Problem

Stock Godot culls and selects LODs per *instance surface*: an instance whose AABB intersects the frustum is drawn
whole, at the LOD chosen from the distance between the camera and that AABB. Modern assets put millions of triangles
in one surface that spans the level (Sponza: the ivy is **one 4.9 M-triangle surface** covering a whole wall; the
cypress leaves 0.8 M, the curtains 0.1-0.2 M each). The camera is usually inside or near such an AABB, so the surface
is always drawn complete and always at LOD 0, including everything behind the camera.

## Evidence (before the change)

* After milestone 2 the largest Sponza passes are geometry passes: opaque 3.08 ms, depth pre-pass 1.57 ms (shadows
  0.37 ms with the cache) of 8.83 ms GPU (`m2_mm_profile`, night state, relative).
* Stock submits on average **27.0 M primitives per frame** in the Sponza flythrough (visible + shadow passes,
  `RENDERING_INFO_TOTAL_PRIMITIVES_IN_FRAME`, `godot-4.7.2_perf`), for a scene of 12.8 M distinct triangles.
* Offline analysis of the same flythrough (`benchmarks/tools/cluster_potential.py`, glTF geometry, the benchmark's
  camera path and projection, LOD 0 assumed; `benchmarks/results/m3_cluster_potential.json`), triangles per frame of
  the camera pass, mean over 84 path samples:

| | triangles | vs stock |
|---|---:|---:|
| stock (instance AABB vs frustum) | 8.74 M | — |
| exact (triangles not entirely outside one frustum plane) | 4.31 M | −51 % |
| clusters ≤ 4096 triangles (k-d, AABB vs frustum) | 4.65 M | −47 % (240 draw ranges) |
| clusters ≤ 16384 | 4.95 M | −43 % (215 ranges) |
| clusters ≤ 65536 | 5.52 M | −37 % (207 ranges) |

  Half of what stock submits for the camera can never reach a pixel; 4096-triangle clusters recover 93 % of that
  with about 240 draw calls (adjacent visible clusters merge into one range).
* Whether the depth pre-pass and the opaque pass are bound by that geometry is measured separately with stock alone
  (`m3_geo_*`: coarser mesh LOD thresholds and Sponza without some of its parts).

## Stock implementation

`RendererSceneCull` tests instance AABBs against the frustum; `RenderForwardClustered::_fill_render_list` picks one
LOD per surface; `_render_list_template` binds the surface's (LOD's) index array and draws it whole. Off-screen
triangles are discarded by the GPU after vertex shading and primitive assembly.

## Proposed implementation

1. **Build** (once, when a mesh surface is created with the toggle on): surfaces with ≥ `cluster_min_surface_triangles`
   (32768) triangles, triangle primitive, no bones, no blend shapes. Positions are decoded from the vertex data
   (compressed or not); a k-d tree splits the triangles at the median centroid of the longest axis until a leaf holds
   ≤ `cluster_triangles` (4096). Nodes are stored in depth-first pre-order; every index buffer of the surface (base
   and each LOD; LOD triangles are assigned to leaves by descending the split planes with their centroid) is
   reordered by leaf, so every node of the tree is one contiguous index range in every buffer, with its own bounds
   per LOD (inflated by 0.1 % + 0.1 mm). The reordered index data is kept on the CPU for now ("pending").
2. **Decision at set-up** (`RenderForwardClustered::_fellow_decide_clusters`, the first time an instance of the
   surface is set up for drawing): if none of its materials (material chain and overlay) blends and none moves
   vertices, the surface's own index buffers are **replaced** by the reordered ones (same format and size: no extra
   GPU memory); otherwise the clusters are dropped and the surface keeps stock's order — alpha blending inside a draw
   depends on the triangle order, and vertex-moving shaders could leave the cluster bounds. A blending surface then
   uses an **order-preserving variant** built at the same time: the same kind of tree over chunks of consecutive
   triangles of each index buffer in its existing order (as many chunks as k-d leaves), so the alpha pass can cull it
   without changing the order of anything it draws. Shadow meshes (only ever
   drawn in depth and shadow passes) are always reordered. A surface reordered earlier and later set up with a
   blending material is counted (`geometry_cluster_blended_after_activation`) and warned about once.
3. **Draw**: the depth pre-pass, the opaque, motion-vector and transparent passes (camera frustum with the TAA jitter
   the GPU uses) and shadow passes
   (shadow camera planes; without the near plane when casters are pancaked; not for dual-paraboloid) pass world-space
   planes to the render list. For an element with active clusters, drawn once (no instancing, no multimesh, no
   particles, no skinning, a shader that cannot move vertices), the planes are moved into the instance's local space
   (`n' = Bᵀn`, `d' = d − n·o`) and the tree is walked: nodes entirely outside one plane are skipped, nodes entirely
   inside all planes are emitted whole, adjacent ranges are merged, and each range is drawn with
   `draw_list_draw_indexed_range` (new RenderingDevice call: `vkCmdDrawIndexed` with a first index) from the element's
   own (reordered) index buffer of its LOD. Passes without planes draw the whole buffer, as stock.
4. A triangle whose cluster is entirely outside one frustum plane is entirely outside it too and covers no pixel,
   so the image cannot change. Depth pre-pass and opaque pass may draw different subsets (the pre-pass draws the
   shadow mesh with its own clusters): every triangle that covers a pixel is drawn by both.

Cost: no extra GPU memory; CPU memory for the reordered index data from the creation of the surface to its first
set-up (`geometry_cluster_pending_bytes`); one re-upload of the index buffers at the first set-up; the tree walk on the
render thread per clustered element and pass.

A first version kept the original buffers and added reordered copies: Sponza **+446 MB of VRAM** (stock peak 2.53 GB).
It is replaced by the version above (same culling, same draws).

## Expected benefit

Fewer vertices shaded and primitives assembled in the depth pre-pass, the opaque pass and shadow passes of scenes
with large surfaces; nothing in scenes made of small objects (instance culling already handles them).

## Verification so far

* lavapipe (Linux, Mesa llvmpipe), PBR Lab with every surface ≥ 256 triangles clustered at 64 triangles per cluster
  (stress setting), 4 fixed cameras at 960×540: **0 differing pixels** against the toggle off, with clusters in the
  camera passes only, with shadow passes too, and with the directional shadow cache on (cache + clusters vs cache).
  24 surfaces clustered; 8.5 k cluster draws, 1.67 M of 20.1 M triangles skipped in the camera passes.
* In-place version, same test: **0 differing pixels**; 23 surfaces reordered, 1 dropped (the alpha-blended glass
  sphere), same draws and culled triangles as the copy version.
* With the order-preserving variant in the transparent pass (the glass sphere): **0 differing pixels**.
* Offline (Sponza flythrough): the order-preserving chunks of the alpha-blended cypress leaves and ground cover cut
  the blended triangles submitted per frame from 1.04 M to 0.62 M (chunks of the leaves span 1.5 m of a 15 m tree).

## Actual performance

Per-pass GPU timestamps, 1920×1080, one profiled run per variant over the flythrough (`m3_cl_profile`,
`benchmarks/results/m3_cl_profile_table.md`), **night state** (relative only: the Bistro interior row, where no surface
is clustered and the variant is identical to stock, moved by +3.3 %). `cl` = clusters on, `m2` = the milestone-2
shadow configuration (cache + sampling skips).

| Sponza pass (ms) | stock | + clusters | m2 | m2 + clusters |
|---|---:|---:|---:|---:|
| Depth pre-pass | 1.521 | **0.869 (−43 %)** | 1.544 | **0.866** |
| Opaque pass | 3.181 | **2.494 (−22 %)** | 2.943 | **2.314** |
| Directional shadows | 3.378 | 3.361 | 0.266 | 0.256 |
| Transparent pass | 1.881 | 1.807 | 1.645 | 1.661 |
| **sum of timestamped passes** | **11.84** | **10.38 (−12.3 %)** | **8.40 (−29.0 %)** | **7.20 (−39.2 %)** |
| render-thread CPU | 0.56 | 0.62 | 0.35 | 0.39 |

Per frame in Sponza (fellow counters over the 2521 measured frames): with the milestone-2 configuration the clustered
surfaces submit **4.15 M** triangles instead of 10.9 M (62 % skipped) in 138 draw calls (depth pre-pass + opaque);
without the shadow cache 22.7 M are drawn and 7.2 M skipped (shadow cascades see most of the scene).

| Scene | effect |
|---|---|
| Sponza | −1.2 to −1.5 ms GPU (−12 to −14 % of the frame); +0.04-0.06 ms render-thread CPU |
| Bistro exterior | neutral (−0.3 / +1.2 %, within noise): its large surfaces are few; 20-27 % of their triangles skipped |
| Bistro interior, PBR Lab | no surface reaches 32768 triangles: nothing clustered (null tests) |

The shadow passes gain nothing measurable on the stock path: a cascade covers nearly the whole atrium from the sun.

Frame-level day-state numbers: milestone-2 report (`fellow-m2_perf`).

## Actual visual quality

`m3_cl_temporal` (RTX 5070 Ti, 60-frame sequences after a 200-frame pre-roll, 1920×1080; copy version `fellow14`,
identical culling and draws to the in-place version), `variant_diff.py`:

| sequence | FellowEngine defaults vs + clusters: PSNR mean (min) | max abs. diff | pixels > 2/255 |
|---|---|---:|---:|
| sponza_pan (142 surfaces clustered) | 80.4 dB (74.8) | 1 | 0.000 % |
| bistro_ext_path | 101.2 dB (99.0) | 1 | 0.000 % |
| bistro_int_path | 69.3 dB (67.2) | 5 | 0.000 % |
| PBR Lab sequences (no surface reaches 32768 triangles: null test of the GPU's run-to-run variation) | 67.8-82.1 dB (66.2) | 18 | 0.000 % |

Image-identical within the run-to-run variation of the GPU (which the PBR null test shows to be larger than any
difference in the clustered scenes). The in-place version is re-checked in the same way (`m3_cl16_temporal`).

## Regression risk

Meshes created while the toggle is off are never clustered (the toggle is read at creation). Reading a clustered
surface back (`ArrayMesh.surface_get_arrays`, trimesh collision from a mesh at runtime) returns the same triangles in
cluster order. A blending material assigned after a surface was reordered blends its triangles in cluster order (an
order that is arbitrary in stock as well — the importer's vertex-cache optimisation chose it — but different;
counted and warned about). The first set-up re-uploads the index buffers (a one-time cost per surface, at load in
practice). Render-thread CPU per clustered element (tree walk, one draw call per visible range).
Vertices moved by the shader (`VERTEX`/`POSITION` writes, custom model-view/projection, grow, billboards, wind) could
leave the cluster bounds, which come from the mesh data (stock covers such displacement with the instance's
`extra_cull_margin`, which clusters do not see): elements whose shader can do that are drawn whole.

## Decision

**KEEP** — part of the FellowEngine configuration. Exact (image-identical within the GPU's run-to-run variation),
no extra GPU memory in the in-place version, −1.2 ms GPU in the geometry-heavy scene and neutral elsewhere. Next
steps measured offline above: a GPU two-phase occlusion test of the same clusters (−24 % of the remaining camera-pass
triangles on average in Sponza, −65 % in its galleries).

## Follow-up potential (offline, same flythrough, opaque camera pass)

* **Occlusion** (`benchmarks/tools/occlusion_potential.py` + `occlusion_raster.c`: the opaque geometry rasterised
  per camera at 480×270, a cluster counted hidden when every depth pixel under its projected AABB is nearer than its
  nearest point): of the triangles in frustum-visible clusters (LOD 0), **−24 % on average** are hidden behind nearer
  geometry (4.14 M → 3.13 M per frame), up to **−65 %** in the upper gallery (artist camera 4). An exact version needs
  a two-phase depth-pyramid test on the GPU (indirect draws).
* **Per-cluster LOD** (`benchmarks/tools/cluster_lod_potential.py`: each cluster simplified on its own with its border
  locked — any mix of levels stays watertight — and the coarsest level whose error projects to ≤ 1 px, stock's LOD
  rule, chosen per cluster): 4.14 M → **0.94 M** triangles per frame at LOD 0 of the clusters. For reference, stock's
  per-surface LOD with the same rule (whole-surface simplification, no locked borders) is estimated at 2.73 M, and
  stock's LOD combined with these frustum clusters at about 0.8 M: most of what per-cluster LOD would save, frustum
  culling at stock's own LOD already saves. Position-only error metric (no normal/UV term): optimistic.
