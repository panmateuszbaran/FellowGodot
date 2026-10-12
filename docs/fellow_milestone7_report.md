# FellowEngine — milestone 7 report (vegetation: MultiMesh instances culled one by one)

Status: **DRAFT** (`fellow91`-`fellow95`, measured 2026-10-11/12). Branch `fellow/geometry`. Benchmark machine as in
milestone 6 (Acer Predator PHN16S-71, Core Ultra 9 275HX, RTX 5070 Ti Laptop GPU, NVIDIA GPU only, Windows 11,
1920×1080, "high" preset). Decision records `docs/decisions/0034`-`0036`; user-facing summary
`docs/FELLOWENGINE_OPTIONS.md`.

## 0. Result

<!-- final table: m7_f105 -->

## 1. The problem: Godot culls a MultiMesh as a whole

Open-world vegetation is drawn with instancing: a `MultiMeshInstance3D` per grass, bush, rock or tree type, either one
for the whole map or one per terrain chunk. Godot culls such a node as a whole (its box against the camera's frustum and
against every shadow pass), so every instance of a node any part of which is in a pass is drawn in that pass. With one
MultiMesh per type the camera's passes draw all 150 k grass clumps and 6 k trees whatever the view; a careful project
splits its vegetation into chunks by hand, and still draws every instance of every chunk that touches a pass.

Foliage Lab (new in milestone 6, `benchmarks/project/scenes/foliage_lab.gd`) has both layouts: 150 k grass clumps of 24
triangles, 6 k trees of 420, 6 k bushes, 4 k rocks on a 512 m terrain, a sun with four cascades. Stock 4.7.2: 10.9 ms
with one MultiMesh per type, 3.6-3.8 ms in chunks. Milestone 7 adds three variants: LOD meshes as the importer makes
them (`--foliage_lods=1`), wind (`--foliage_wind=1`: grass, bushes and tree crowns sway in a vertex shader driven by
`TIME`, each node with an extra cull margin that bounds the sway) and margins or sway scaled for sensitivity tests.

## 2. MultiMesh culling (0034, exact, on by default from `fellow93`)

Per frame, for the camera's passes and for every shadow pass with a view volume, every eligible MultiMesh element
becomes a job: its node's transform, its mesh's box (grown by the node's extra cull margin, as stock grows the node's
box), its pass's planes. On the GPU, in three dispatches per pass group:

* **Test**: each instance's box (the mesh's box under the instance's and the node's transform) against the planes, with
  a margin for the rounding of this transform against the vertex shader's. An instance wholly outside one plane has no
  vertex in the volume.
* **Scan**: the visible instances' output positions, in order (a workgroup per job: from `fellow92`; one thread per job
  had made the camera's cull 0.295 ms in Foliage Lab, now 0.092 ms).
* **Compact**: the visible instances' data copied in order into a compacted buffer; each draw's indirect arguments get
  its job's visible count.

The draws bind the compacted buffer as their transforms and draw indirectly. Order is preserved, so blending and depth
ties resolve as before; an instance outside the volume could draw no pixel, so the image is the same.

Eligible: materials that neither move vertices nor read `INSTANCE_ID`, 3D transforms, at least 64 instances. Not
culled, drawn the stock way: a MultiMesh whose whole box is inside the pass's volume (nothing to cull, `fellow93`), and
a pass group whose MultiMesh draws, with every instance, would draw fewer than 300 000 indices
(`geometry/multimesh_cull_min_indices`, `fellow95`): there the dispatches cost about what they can save (PBR Lab's
600 grass cards: +0.6 % in one process before this rule). Autotune (0020) measures culling against drawing whole while
there is something to cull and keeps the faster way (`TUNE_MULTIMESH_CULLING`, `fellow92`).

MultiMeshes whose data changes every frame (crowds, debris; with motion vectors their draws read the current and the
previous frame's data) are culled from `fellow94`: their pool slot holds both halves and the compaction copies each
visible instance's previous data to the same position of a second region.

**Image.** lavapipe renders bit for bit the same images with culling as without: Foliage Lab in both layouts (captures,
TAA sequences), with LOD meshes, PBR Lab, Stress Lab (120 frames, the moving boxes culled), every version from
`fellow91` to `fellow95`. On RTX two runs of one build differ (shadow and TAA work with unordered GPU writes); the
null test in `m7_f101` (the same build twice: 78.27 dB mean, 74.93 min, max |d| 11) matches culling off against on
(78.21 / 74.65 dB, max |d| 11-12), and `fellow93`'s defaults against `fellow92`'s are at the floor in Foliage Lab
(78.23 dB), PBR Lab (73.6 / 72.3 dB, its floor since milestone 3) and Stress Lab (99 dB, identical).

**Time** (`m7_f102`/`m7_f103`, RTX, 1080p):

| scene | stock | culling off | culling on |
|---|---:|---:|---:|
| Foliage Lab, one MultiMesh per type | 10.94 ms | 8.98 ms | **5.34 ms (−51.2 %)** |
| Foliage Lab, chunks | 3.84 ms | 3.66 ms | 3.50 ms (−8.9 %) |

In one process (autotune off, 240 block pairs): one MultiMesh per type −39.7 % (depth pre-pass −1.26, opaque −1.29,
shadows −0.94 ms; the camera's cull 0.092 ms), chunks −4.1 %.

## 3. Per-instance LOD (0035, option, off: changes the image)

Stock picks a MultiMesh's LOD level once for the whole node; a MultiMesh spread over the map contains the camera and
draws every instance at full detail. With culling, each instance can get the level stock's rule (distance to its box,
its scale, the project's `mesh_lod_threshold`) would give it as a node of its own: the instances of each level form a
bucket, drawn with that level's indices. Off by default: the image changes where many far instances coarsen together
(Foliage Lab from above: 33 dB on lavapipe, 39.9 dB on RTX — far grass clumps lose whole blades within meshoptimizer's
error budget); at ground level 48-65 dB. With the bench's light meshes it saves 2.2 % (`m7_f102_lod_ab`).

## 4. Swaying vegetation (0036, option, off)

Vegetation in games nearly always sways in a vertex shader, which 0034 leaves out (the instance's box no longer bounds
its vertices). Stock gives projects `GeometryInstance3D.extra_cull_margin` for exactly this, for whole nodes. Under
`geometry/multimesh_cull_vertex_motion` a material that writes `VERTEX` is culled when its node has a margin, which is
taken as the bound of the displacement (in the node's space) and added to every instance's box.

The image is identical as long as the material keeps within the margin: lavapipe 0 differing pixels in captures,
chunks and a 60-frame TAA sequence with the sway driven by `TIME` at a fixed 60 fps; RTX at the floor (78.14 dB). The
test sees what too small a margin loses: with an eight times stronger sway and margins at 1 %, 1 591 pixels differ
(swaying grass at the view's edges, shadows of instances culled from cascades); with the margins scaled with the sway,
none. It is an option because it assumes more of the project than stock does (stock only needs the whole node's
vertices within the node's grown box).

Foliage Lab with wind, one MultiMesh per type (`m7_f103_wind_single`): stock 10.68 ms, FellowEngine defaults 10.34 ms
(−3.2 %: the swaying MultiMeshes are drawn whole and their shadows are not cached), with the option **6.31 ms
(−40.9 %)**; in one process −39.9 % (shadows −1.99, pre-pass −1.16, opaque −1.11 ms).

## 5. Autotune

* `TUNE_MULTIMESH_CULLING` (0020): exact, starts on, applies while there is something to cull.
* An image-changing tunable that an undecided evaluation leaves off is evaluated again at the base interval instead of
  at a doubling one (`fellow92`). PBR Lab's adaptive shading (0027) had been on for 5 320, 5 320 and 487 of ~8 850
  frames in three runs (`m7_f100`); with the rule 5 320 in all three, and the visually lossless options together give
  PBR Lab −5.5 % (p 0.002), Lights Lab −24.9 %, City Lab −6.0 %, Sponza −1.6 % (within noise) against the defaults
  (`m7_f102_vrs_rule`).

## 6. Tooling

* `variant_diff.py` stripped every occurrence of a short label from the file names it pairs (`a`/`b` occur inside
  scene and sequence names), so `m7_f100`'s null test reported nothing; it now strips only the label component.
* Jobs are now dropped into a queue folder that a runner the user started executes one at a time (`.tools/queue`).
* `debug/multimesh_stats` reads the culling's instance counters back (off by default: no per-frame readback).

## 7. Incidents

* **Pool growth in the middle of a collection** (`fellow91` before release): jobs collected before the pool grew read
  stale offsets for a frame; TAA kept that frame in its history (52.6 dB along silhouettes). Found by removing the
  planes (every instance visible: the difference stayed), fixed by resolving every job's offset after collection.
* **A pause request** stopped job f101 mid-session; its remaining sessions ran as f102.

## 8. Next

<!-- to be written with the final results -->
