# 0381 — The hierarchy's cuts as indirect draws; instanced runs drawn with their own cuts

Status: **KEEP** (`geometry/cluster_dag_indirect` on; with `geometry/cluster_dag`, itself off) · Branch
`fellow/geometry` · Code: `render_forward_clustered.{h,cpp}` (`_fellow_dag_cut`, `_fellow_dag_prepare`, the clusters'
draw), `drivers/vulkan/rendering_device_driver_vulkan.cpp` (indexed indirect draws without `multiDrawIndirect`),
`fellow_cluster_dag_settings.h`

## Problem

Two things found after decision 0379:

* **Draw calls.** A surface instance's cut is drawn as one draw call per range of adjacent clusters: 5 000-7 000 a
  frame over the passes in 0379's test, against about 100 for the same scene at full detail (the k-d clusters merge
  into a few ranges). Each draw is CPU time on the render thread.
* **Instanced runs.** Godot's Forward+ draws consecutive elements with the same mesh, material and pipeline as one
  instanced draw (a run). The mesh clusters (0007, 0063) and the hierarchy only handled single instances, so a field of
  the same rock — the case Nanite is made for — was drawn whole: in 0379's test most of the twelve rocks were, at full
  detail, and outside its counts (see the correction there).

## Change

* **Indirect draws** (`geometry/cluster_dag_indirect`, on): before a pass's draw list begins, `_fellow_dag_prepare`
  walks the cuts of the pass's elements that have a hierarchy (the camera passes of one render still share one walk)
  and writes their ranges as indexed indirect draw commands into this frame's buffer (one per frame in flight, each
  pass its own part, grown by powers of two); the draw loop then draws each such element with one
  `draw_list_draw_indirect` of its commands. An element the loop finds it cannot draw so falls back to its ranges. The
  Vulkan driver takes the draws one by one on a device without `multiDrawIndirect`.
* **Runs** (with `geometry/cluster_dag`): a run whose mesh has a hierarchy is drawn instance by instance — each
  element's base index pushed, its own cut, its own indirect draw (or ranges). A run without one stays instanced.

## Checks (lavapipe)

0379's test, 8 frames averaged at a fixed 60 fps, every pass:

| view | stock's primitives | hierarchy: triangles | clusters | ranges | indirect draws | render thread CPU, ranges / indirect |
|---|---:|---:|---:|---:|---:|---|
| ground | 31.6 M | 5.88 M | 50 259 | 6 818 | 57 | 3.34 / **2.37 ms** |
| close | 29.0 M | 6.18 M | 52 202 | 6 852 | 47 | 3.24 / **2.40 ms** |
| high | 9.78 M | 1.36 M | 11 771 | 1 488 | 30 | 0.96 / **0.84 ms** |

* Indirect against ranges: the same images bit for bit (the same triangles in the same order), the same walks; the
  render thread's recording 27-29 % shorter where the cuts are large (lavapipe's own draw calls are cheap: on a
  desktop driver the saving should be larger).
* With the runs, every rock is cut: against full detail 44.2 / 44.2 / 36.5 dB (the rocks' far sides and silhouettes
  now simplified too); cracks: none (sky through only within 3 pixels of the horizon).
* The full-detail scene records in 0.45-0.53 ms (a hundred draws): the hierarchy's walks (2.0-2.2 ms a frame, 42 of
  them) are its CPU cost, against 81-86 % fewer triangles for the GPU. Which wins is the RTX's to say.
* Regression suite (the hierarchy off): `geometry`, `instances`, `crowd`, `terrain`, `shadows`, `stock/geometry`
  unchanged.

## Decision

KEEP: indirect on (the same image, less CPU), runs drawn with their cuts (without it the hierarchy misses the scenes
it is for). Next: the walks — one cut without planes shared by all the shadow passes of a frame, or the walk on the GPU.
