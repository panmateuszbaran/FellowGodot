# 0034 — MultiMesh instance culling on the GPU

Status: **MEASURING** (on by default in `fellow93`, measured in `m7_f103`) · Branch `fellow/geometry` · Toggle
`rendering/fellow/geometry/multimesh_culling` (runtime; off until `fellow92`),
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
* **Scan** (a workgroup per job; from `fellow92`, before a thread per job): the blocks' output positions, in order
  (each thread sums a share of the job's blocks, then a prefix sum over the 64 shares), and the job's visible count.
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
  read stale offsets for a frame, and TAA kept that frame in its history. Fixed (offsets resolved after collection).
* `fellow91`, TAA on: one MultiMesh per type 0 differing pixels (48 % of 47.8 M tested instances culled over the
  captures), chunks 0 differing pixels.

RTX 5070 Ti Laptop (`fellow91`, `m7_f99_*`), 1080p, off against on:

| | frames | PSNR mean / min | max \|d\| | pixels > 2 |
|---|---:|---:|---:|---:|
| one MultiMesh per type, `foliage_walk` (TAA) | 60 | 78.25 / 74.93 dB | 12 | 0.000 % |
| chunks, `foliage_walk` | 60 | 78.20 / 74.75 dB | 11 | 0.000 % |
| one MultiMesh per type, three captures | 3 | 84.03 / 74.48 dB | 13 | 0.002 % |

lavapipe renders these bit for bit the same; on RTX two runs of one build also differ (shadow and TAA work with
unordered GPU writes), so these numbers are only meaningful against that floor. `m7_f100`'s null test (the same build
twice) produced no report (its labels `a`/`b` also occur inside the file names, which `variant_diff.py` stripped;
fixed in `fellow92`), so the floor is measured again in `m7_f101`.

`m7_f101` (`fellow92`, one MultiMesh per type, `foliage_walk`, 60 frames, TAA), one session of three processes:

| | PSNR mean / min | max \|d\| | pixels > 2 |
|---|---:|---:|---:|
| culling on against culling on (the floor) | 78.27 / 74.93 dB | 11 | 0.000 % |
| culling off against on (first run) | 78.21 / 74.65 dB | 11 | 0.000 % |
| culling off against on (second run) | 78.21 / 74.72 dB | 12 | 0.000 % |

The difference is the floor's: on RTX too the culling leaves the image as it is.

## Performance

In one process (`m7_f99_*_ab`, blocks of frames alternating off / on, per-pass GPU timings):

| layout | frame | where |
|---|---:|---|
| one MultiMesh per type | **−36.7 %** | depth pre-pass −1.48 ms, opaque −1.41 ms, shadows −0.96 ms; the camera's cull 0.295 ms |
| chunks | −3.9 % | the culls 0.032 ms; render thread +0.034 ms |

Between processes (`m7_f100`, 3 runs each, 1080p, frame ms):

| scene | stock | FellowEngine, culling off | culling on |
|---|---:|---:|---:|
| Foliage Lab, one MultiMesh per type | 11.62 | 9.39 (−19.2 %) | **5.87 (−49.5 %)** |
| Foliage Lab, chunks | 3.74 | 3.45 (−7.9 %) | 3.29 (−12.1 %) |
| PBR Lab (one MultiMesh: 600 grass cards in a patch) | 2.66 | 2.64 (−1.1 %) | 2.69 (+1.1 %, within noise) |

Where the MultiMeshes are already split into chunks the gain is small and a scene with only a few small ones can lose
the culls' cost, so from `fellow92`:

* **Parallel scan.** One thread per job walked all of a job's blocks in turn (about 2 300 for the ~150 k grass
  clumps), a serial chain likely to be most of the camera's 0.295 ms. A workgroup per job now does it; `m7_f101`
  measures the cull again.
* **Autotune tunable** (`TUNE_MULTIMESH_CULLING`, 0020): while culling is on and the lists have MultiMesh elements it
  can take (within the last 120 frames), autotune measures the frame with and without it and keeps the faster way. It
  is exact, so it is switched like the other exact tunables.

### `fellow92` measured (`m7_f102`, 1080p)

Between processes, 3 runs each, frame ms (culling on: autotuned):

| scene | stock | culling off | culling on | on against stock |
|---|---:|---:|---:|---:|
| Foliage Lab, one MultiMesh per type | 10.94 | 8.98 | **5.34** | **−51.2 %** (p < 0.001, 3/3) |
| Foliage Lab, chunks | 3.84 | 3.66 | 3.50 | −8.9 % (p 0.001, 3/3) |
| PBR Lab | 2.64 | 2.62 | 2.69 | +1.9 % (within noise, p 0.63) |
| Stress Lab (400 moving boxes: new data every frame, so not culled under TAA's motion vectors) | 5.16 | 5.12 | 5.11 | −0.9 % (within noise) |

In one process (autotune off, blocks alternating off / on, 240 block pairs):

| scene | frame | GPU | render thread | where |
|---|---:|---:|---:|---|
| Foliage Lab, one MultiMesh per type | −39.7 % (−3.35 ms) | −3.36 ms | +0.021 ms | camera's cull **0.092 ms** (was 0.295: the parallel scan); depth pre-pass −1.26, opaque −1.29, shadows −0.94 ms |
| Foliage Lab, chunks | −3.8 % | −0.131 ms | +0.037 ms | culls 0.029 ms; pre-pass −0.09, opaque −0.08, shadows +0.03 ms |
| PBR Lab | +0.8 % (+0.020 ms) | +0.020 ms | +0.036 ms | camera's cull 0.013 ms, shadow passes +0.062 ms, nothing culled worth it |

PBR Lab's patch is small and mostly in view: the culls cost more than they save, and 0.02 ms is below what autotune
can tell apart (undecided keeps the choice). From `fellow93`:

* **Nothing to cull, no job.** A MultiMesh whose whole box (stock's box, with the node's margin) lies inside the pass's
  volume is drawn whole, as stock draws it, without a job. A set without jobs dispatches nothing. Exact (drawing an
  instance can never lose a pixel).
* **Counters only for diagnostics.** The instance counters (tested, drawn, coarse) are read back from the GPU only with
  `debug/multimesh_stats` (the benchmark captures turn it on).

### MultiMeshes whose data changes (`fellow94`)

Until `fellow93` a MultiMesh whose data changed this frame or the last was left out: with motion vectors (TAA) its
draws read two halves of its buffer, the current data and the previous frame's. From `fellow94` such a MultiMesh's
pool slot holds both halves (the current data, then the previous), and the compaction copies each visible instance's
previous data to the same position of a second region, which the draw's previous-offset push constant names. An
instance is tested at its current transform (where it is drawn); its previous transform only feeds its motion vector,
as in stock. A MultiMesh updated every frame (crowds, debris, Stress Lab's 400 moving boxes) is culled like a static
one.

lavapipe, Stress Lab (the boxes move every frame; TAA), `stress_pan` and `stress_sun`, 120 frames: culling off against
on **0 differing pixels**, with 1.79 M of the boxes tested and 0.47 M drawn (73 % culled, most of them in the shadow
passes). Foliage Lab one MultiMesh per type, captures: 0 differing pixels.

RTX (`m7_f104_stress_temporal`, `fellow94`, 120 frames of Stress Lab with TAA): against `fellow93` (which drew the
boxes whole) and against culling off, **bit for bit the same** (99 dB, max |d| 0).

### Too little to save (`fellow95`)

With the "nothing to cull" rule PBR Lab still lost 0.6 % in one process (`m7_f103_mm_ab`: the camera's cull 0.011 ms,
shadow passes +0.029 ms; render thread +0.031 ms): its 600 grass cards are 7 200 indices a draw, less than the
dispatches and their barriers cost. From `fellow95` a pass group (the camera's passes, or the shadow passes) is culled
only when its MultiMesh draws would draw at least `geometry/multimesh_cull_min_indices` (300 000, about 100 k
triangles) with every instance; below that they are drawn the stock way. lavapipe: PBR Lab every group below
(0 culls, 7 captures identical), Foliage Lab chunks none below (identical).

