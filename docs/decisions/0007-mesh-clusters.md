# 0007 — Mesh clusters (frustum culling inside large surfaces)

Status: **MEASURING** · Branch `fellow/geometry` · Toggle `rendering/fellow/geometry/mesh_clusters` (default **off**)
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
   and each LOD; LOD triangles are assigned to leaves by descending the split planes with their centroid) gets a
   **copy** ordered by leaf, so every node of the tree is one contiguous index range in every copy, with its own
   bounds per LOD (inflated by 0.1 % + 0.1 mm).
2. **The originals stay untouched** and are still used for whole-surface draws and for every pass without a cull
   volume, in particular the transparent pass: the result of alpha blending depends on the triangle order inside a
   draw, so reordering it would change the image.
3. **Draw**: the depth pre-pass and the opaque pass (camera frustum with the TAA jitter the GPU uses) and shadow passes
   (shadow camera planes; without the near plane when casters are pancaked; not for dual-paraboloid) pass world-space
   planes to the render list. For an element with clusters, drawn once (no instancing, no multimesh, no particles,
   no skinning), the planes are moved into the instance's local space (`n' = Bᵀn`, `d' = d − n·o`) and the tree is
   walked: nodes entirely outside one plane are skipped, nodes entirely inside all planes are emitted whole, adjacent
   ranges are merged, and each range is drawn with `draw_list_draw_indexed_range` (new RenderingDevice call:
   `vkCmdDrawIndexed` with a first index) from the leaf-ordered copy of the element's LOD.
4. A triangle whose cluster is entirely outside one frustum plane is entirely outside it too and covers no pixel,
   so the image cannot change. Depth pre-pass and opaque pass may draw different subsets (the pre-pass draws the
   shadow mesh with its own clusters): every triangle that covers a pixel is drawn by both.

Cost: the leaf-ordered copies double the index memory of clustered surfaces (reported as
`geometry_cluster_index_bytes`); the tree walk runs on the render thread per clustered element and pass.

## Expected benefit

Fewer vertices shaded and primitives assembled in the depth pre-pass, the opaque pass and shadow passes of scenes
with large surfaces; nothing in scenes made of small objects (instance culling already handles them).

## Verification so far

* lavapipe (Linux, Mesa llvmpipe), PBR Lab with every surface ≥ 256 triangles clustered at 64 triangles per cluster
  (stress setting), 4 fixed cameras at 960×540: **0 differing pixels** against the toggle off, with clusters in the
  camera passes only, with shadow passes too, and with the directional shadow cache on (cache + clusters vs cache).
  24 surfaces clustered; 8.5 k cluster draws, 1.67 M of 20.1 M triangles skipped in the camera passes.

## Actual performance

*pending (`m3_cl_profile`).*

## Actual visual quality

*pending (`m3_cl_temporal`, RTX).*

## Regression risk

Index memory of clustered surfaces doubles. Meshes created while the toggle is off are never clustered (the toggle
is read at creation). Render-thread CPU per clustered element (tree walk, one draw call per visible range).
Vertices moved by the shader (`VERTEX`/`POSITION` writes, custom model-view/projection, grow, billboards, wind) could
leave the cluster bounds, which come from the mesh data (stock covers such displacement with the instance's
`extra_cull_margin`, which clusters do not see): elements whose shader can do that are drawn whole.

## Decision

*pending.*
