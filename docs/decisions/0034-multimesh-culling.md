# 0034 — MultiMesh instance culling on the GPU

Status: **MEASURING** · Branch `fellow/geometry` · Toggle `rendering/fellow/geometry/multimesh_culling` (runtime, off),
`geometry/multimesh_cull_min_instances` (64) · Code: `shaders/effects/fellow_multimesh_cull.glsl`,
`render_forward_clustered.{h,cpp}` (`_fellow_multimesh_cull_*`), `storage_rd/mesh_storage.{h,cpp}` (the instance pool),
`fellow_settings.*`.

## Problem

Open-world vegetation is drawn with instancing: a MultiMesh per grass, bush, rock or tree type, one per terrain chunk or
one for the whole map. Godot culls a `MultiMeshInstance3D` as a whole (its AABB against the camera frustum and against
each shadow pass), so every instance of a MultiMesh any part of which is in a pass is drawn in that pass.

The new Foliage Lab (`benchmarks/project/scenes/foliage_lab.gd`: 150 k grass clumps of 24 triangles, 6 k trees, 6 k
bushes, 4 k rocks on a 512 m terrain, sun with four cascades), `m6_f98` (stock 4.7.2 against FellowEngine `fellow90`,
2 runs each, 1080p):

| layout | stock | FellowEngine | where the time goes (FellowEngine) |
|---|---:|---:|---|
| chunks (grass in 16 m chunks with a 90 m visibility range; the rest in 64 m blocks) | 3.61 ms | 3.40 ms | opaque 1.10, depth pre-pass 0.60 ms |
| one MultiMesh per type | 10.89 ms | 8.63 ms (−21 %: the shadow cache) | depth pre-pass 2.38, opaque 2.85 ms; 24 M triangles drawn |

With one MultiMesh per type the camera's passes draw all 150 k grass clumps and 6 k trees whatever the view; a careful
project splits them into chunks by hand.

## Change

Per frame, for the camera's passes (one view) and for every shadow pass with a view volume (directional cascades with
the pancaking near plane left out, spot lights, omni cube faces): every MultiMesh element whose materials neither move
vertices (`VERTEX`, `POSITION`, custom matrices, point size, trails) nor read `INSTANCE_ID`, with 3D transforms, at least
`multimesh_cull_min_instances` instances, no motion vectors between two buffers this frame and no shadow mesh of its own
becomes a job: its node transform, its mesh's AABB, its pass's planes.

* **Pool.** The current instance data of every MultiMesh used by a job is copied (on the GPU, when its data changed)
  into one buffer, so one dispatch reads all of them. Slots move when the pool grows; every job's offset is resolved
  again after collection until no growth happened in between.
* **Test** (a workgroup of 64 per block of 64 instances): the mesh's AABB under the instance's and the node's transform
  (center and half extents) against the planes, with a margin for the rounding of this transform against the vertex
  shader's (1e-4 relative, 1e-3 m absolute). An instance wholly outside one plane has no vertex in the volume.
* **Scan** (a thread per job): the blocks' output positions, in order, and the job's visible count.
* **Compact** (a workgroup per block): the visible instances' data copied in their order to the job's region of a
  compacted buffer; each draw's indirect arguments get the job's visible count.
* **Draw:** the element binds the compacted buffer as its transforms (the same layout; its region through the
  MultiMesh offset push constants, current = previous since its data does not change this frame) and draws indirectly.

Order is preserved, so blending and depth ties resolve as before; instances outside the volume could draw no pixel, so
the image is the same.

The camera's planes come from the projection the GPU uses: `get_cam_projection()` adds the TAA jitter after the y flip,
so before the flip its y has the other sign. The mesh clusters (0007) had used the jitter unflipped; fixed for both.

## Verification

lavapipe, Foliage Lab captures (960 × 540, 30 settling frames), off against on:

* chunks: 0 differing pixels; 8.5 % of the tested instances culled (the chunks already culled whole by stock).
* one MultiMesh per type, TAA off: 0 differing pixels; 48 % culled.
* one MultiMesh per type, TAA on, first version: 25 115 differing pixels (52.6 dB) — the same with every instance
  visible (planes removed), so not the test: when the pool grew in the middle of a collection, the jobs collected before
  read stale offsets for a frame, and TAA kept that frame in its history. Fixed (offsets resolved after collection):
  see below.
