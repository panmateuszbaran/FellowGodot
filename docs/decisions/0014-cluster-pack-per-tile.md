# 0014 — Cluster pack pass: one workgroup per tile

Status: **in test** (`fellow35`) · Branch `fellow/geometry` · Toggle `rendering/fellow/lights/cluster_pack_parallel`
(default **off** until measured) · Code: `shaders/cluster_store.glsl` (`FELLOW_PARALLEL`), `cluster_builder_rd.{h,cpp}`,
`fellow_settings.*`.

## Problem

The Forward+ cluster build rasterises every light, decal and probe proxy into a per-tile render buffer, then a
compute pass ("Pack 3D Cluster Elements") turns it into what the scene shader reads: per tile and element type, one
bit per element and, for each of the 32 depth slices, the range of element indices present. Stock runs **one
invocation per 32×32-pixel tile** (2040 at 1080p): it walks the tile's elements and, for each, read-modify-writes
every slice the element covers. With few invocations and long dependent memory chains the pass is latency-bound:
Lights Lab (242 lights) 0.075 ms in stock, **0.115-0.122 ms** with FellowEngine's depth slices (0008), where an element
near the camera covers several thin logarithmic slices instead of one 125 m linear slice (`m3_f33_profile`,
`m3_f33a_ablation`).

## Change

A second version of the pack shader: one 32-invocation workgroup per tile, one invocation per depth slice. Every
invocation walks the tile's element list (the same loads for all 32, served by the cache) and keeps, per element type,
the index range of its own slice in a register, in stock's encoding; it writes its slice word once at the end, only if
an element reached it (the buffer is cleared every frame, as before). The element bits are set with `atomicOr` by the
invocation `index mod 32`. Minimum, maximum and OR do not depend on the order in which elements are visited, so the
stored words are exactly stock's (with or without depth slices).

## Verification

* lavapipe, toggle off vs on: Lights Lab 60-frame walk, PBR Lab 7 cameras (with and without the depth slices of 0008),
  Stress Lab 4 cameras: **0 differing pixels**. Negative control (the parallel version leaving slices 0-8 unwritten):
  3613 pixels differ in PBR Lab's dark room.
* RTX (`m3_f35_temporal`, five scenes): pending.

## Result

Pending (`m3_f35_profile`, `m3_f35_perf`).

## Decision

Pending.
