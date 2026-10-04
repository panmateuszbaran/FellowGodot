# 0014 — Cluster pack pass: one workgroup per tile

Status: **KEEP** (`fellow35`; on by default from `fellow38`) · Branch `fellow/geometry` · Toggle `rendering/fellow/lights/cluster_pack_parallel`
(default **on**) · Code: `shaders/cluster_store.glsl` (`FELLOW_PARALLEL`), `cluster_builder_rd.{h,cpp}`,
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
* RTX (`m3_f35_temporal`, five scenes, toggle off vs on): Bistro exterior 99 dB (bit-identical); Lights Lab, Bistro
  interior, PBR Lab and Sponza at their run-to-run floor (the same values as `fellow33` vs `fellow35` with the toggle
  off in the same session).

## Result

Profile (`m3_f35_profile`, one run each): pack pass **Lights Lab 0.119 → 0.069 ms** (stock 0.074), Bistro interior
0.013 → 0.010 ms; Bistro exterior and Sponza have no cluster elements (the pass does not run there).

Frame (`m3_f35_perf`, 3 interleaved runs, reference toggle off): Lights Lab −1.3 % (p 0.068), Bistro interior −0.8 %
(p 0.28), PBR Lab +0.6 % (p 0.60); Bistro exterior +1.7 % (p 0.005) although the pass never runs in that scene. The A/A
check `m3_f36_aa` (the same configuration under three labels, 3 runs each, the toggle variant between them) gives
−1.4 % (p 0.049), −0.7 % and −0.6 % (toggle) against the first label: with 3 runs, Bistro exterior differences up to
about 1.5 % can come out "significant" with nothing changed; the +1.7 % in a scene where the change cannot act is read
as the same effect.

Longer A/B (`m3_f37_pack`, 2026-10-04, 5-6 ABBA-interleaved runs; the laptop's power draw moved between 65 and 81 W
across runs, CV up to 4 %, so the per-run pairs are the useful comparison — `ab_stats.py` reports them since this
session):

| scene | toggle off ms | on ms | Δ (paired) | p (paired) | pairs faster |
|---|---:|---:|---:|---:|---:|
| Lights Lab | 3.246 | 3.172 | −2.3 % | 0.155 | 5/5 |
| Bistro interior | 4.251 | 4.210 | −1.0 % | 0.001 | 6/6 |

## Decision

**KEEP, on by default** (from `fellow38`). Exact (bit-identical, lavapipe and RTX); the pack pass is 0.05 ms faster in
Lights Lab and below stock's; the frame is faster in all 11 run pairs of the two scenes with cluster elements.
