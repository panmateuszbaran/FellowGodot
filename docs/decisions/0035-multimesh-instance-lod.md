# 0035 — Per-instance mesh LOD for culled MultiMeshes

Status: **MEASURING** (an option that changes the image, off) · Branch `fellow/geometry` · Toggle
`rendering/fellow/geometry/multimesh_instance_lod` (runtime, off; works with `geometry/multimesh_culling`, 0034) ·
Code: `shaders/effects/fellow_multimesh_cull.glsl` (buckets per level), `render_forward_clustered.{h,cpp}`
(`_fellow_multimesh_cull_collect`: a job per surface; the draw loop: a draw per level), `storage_rd/mesh_storage.h`
(`fellow_mesh_surface_get_lod_edges`) · Bench: Foliage Lab `--foliage_lods=1`.

## Problem

Godot picks a mesh's detail level per node: the distance from the camera to the node's box, its largest scale, the
project's `mesh_lod_threshold` (1 pixel by default; `RendererRD::MeshStorage::mesh_surface_get_lod`). A
`MultiMeshInstance3D` is one node, so all its instances get the level of its whole box. A MultiMesh spread over the map
contains the camera: distance 0, level 0 for every instance however far it is. Projects split their vegetation into
chunks to get coarser levels in the distance (and culling); with one MultiMesh per type every tree is drawn at full
detail.

## Change

With MultiMesh culling (0034) the instances are tested one by one on the GPU anyway, so in the camera's passes each
visible instance can also get the level stock's rule would give it as a node of its own:

* **Jobs.** Under the option, in the camera's passes (one view, a perspective camera, `mesh_lod_threshold` > 0, without
  FellowEngine's LOD distance bias or orthographic extent), a MultiMesh surface whose mesh has LOD levels is a job of its
  own (its surfaces' levels differ). Up to 7 levels after level 0 are used (a mesh with more keeps its coarsest ones
  unused: finer than stock's rule, never coarser).
* **Test.** A visible instance's level: the distance from the camera to its box (the mesh's box under the instance's
  and the node's transform) times the LOD distance multiplier, its transform's largest scale times the node's LOD bias,
  then stock's loop over the levels' edge lengths. Each level is a bucket.
* **Scan and compact.** Per bucket, the instances in their order; the buckets one after the other in the job's region.
* **Draw.** Per level, that level's index array is bound and drawn indirectly with the bucket's count and first
  instance. The depth pre-pass and the colour pass draw the same job, so their levels match.
* Shadow passes keep stock's level for the node.

The image changes: far instances are drawn coarser, within the error the project's threshold allows per instance.
Instances are drawn level by level, so the order between levels changes too (for opaque materials this only matters
for exact depth ties; for a blended MultiMesh its instances blend in another order).

## Verification (lavapipe, 960 × 540, Foliage Lab with one MultiMesh per type and `--foliage_lods=1`)

The vegetation meshes get LOD levels the way the importer makes them (`ImporterMesh.generate_lods`, meshoptimizer).

* Culling with these meshes, off against on (the option off): 0 differing pixels in the three captures.
* Culling against culling with per-instance LOD:

| camera | PSNR | pixels differing | > 2 levels | > 8 levels | max \|d\| |
|---|---:|---:|---:|---:|---:|
| aerial | 33.2 dB | 181 370 | 22.2 % | 10.7 % | 113 |
| forest | 64.6 dB | 348 | 0.02 % | 0.01 % | 37 |
| meadow | 48.2 dB | 2 559 | 0.26 % | 0.14 % | 143 |

6.5 M of the 29.1 M instances drawn over the captures (camera and shadow passes) were drawn at a coarser level.

What changes is visible from above: the far grass clumps (8 separate blades each) lose whole blades — meshoptimizer
removes blades within its error budget for one clump, but over thousands of clumps the lost coverage shows as a sparser,
lighter meadow; far trees get slightly smoother outlines. Near the camera (forest, meadow) the change is small.

So it is **not** visually lossless (FellowEngine's bar is ≥ 50 dB on stills). It is the project's own LOD trade-off,
applied per instance as stock applies it to separate nodes, and is offered as such: off by default, controlled by the
project's `mesh_lod_threshold`.

## Pending

RTX: the frame time against culling alone and the image on the GPU (`m7_f101`).
