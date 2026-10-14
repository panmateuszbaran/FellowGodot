# 0049 — Light tile culling: the lights that can reach an 8 × 8 tile's geometry

Status: **KEEP, on by default from `fellow127` under autotune** (exact; switched on per scene only where measured faster:
Lights Lab −5.6 %, elsewhere off) · Branch `fellow/geometry` · Toggle `rendering/fellow/lights/tile_cull` (runtime;
specialization bit 26 of the opaque colour pass only; autotune `TUNE_LIGHT_TILE_CULL`, starts off) · Code: `shaders/effects/fellow_light_tile_cull.glsl`,
`render_forward_clustered.*` (`_fellow_light_tile_cull`, binding 42 of the render pass set,
`ImplementationData::fellow_tile_cull_width`), `scene_forward_clustered.glsl` (`fellow_tile_mask`, the omni and spot
loops), `scene_forward_clustered_inc.glsl`, `scene_shader_forward_clustered.*` (`FELLOW_WRITES_DEPTH`),
`fellow_settings.*` (stat `lights_tile_cull_frames`).

## Problem

0045 sized the positional lights: in Lights Lab the loop and the range tests are most of the lights' cost (0.63 of
0.92 ms before 0045's v2), not their shading or shadows. A pixel's light loop visits the lights of its cluster cell:
the 32 × 32 pixel tile's lights whose depth range covers the pixel's depth slice, merged over the warp. Per pixel
(lavapipe, Lights Lab captures, diagnostics 262144 + 8 and + 32): the loop visits 9.8-16.1 lights, of which 3.1-7.7 are
within range and 2.6-7.6 shaded. The others pass through the loop, their data is read and their range test fails.

0043 dropped the out-of-range lights before the loop, per pixel: exact and slower, because each pixel still ran every
range test. 0038's smaller cluster tiles cost more in the cluster build than they saved. 0046 and 0047 made each visit
cheaper and lost more to registers (0048).

## Change

After the depth pre-pass (and the cluster build), a compute pass takes each 8 × 8 pixel tile of the depth buffer:

* the nearest and the farthest depth of its pixels (pixels at the cleared depth, without geometry, are left out);
* the view-space bounding box of the tile's frustum between those depths (the colour pass's projection with this
  frame's jitter, inverted), widened by 1/1000 of its largest coordinate plus 1/10000;
* for every omni and spot light of the 32 × 32 cluster tile that holds it (its bits at any depth), whether the light's
  sphere (its range widened by 1/1000 plus 1/10000) meets the box.

The result is a mask per tile and element type, laid out as the cluster's words. In the opaque colour pass the omni and
spot loops AND each cluster word with the tile's word before the warp merges the pixels' masks: the loop visits the
same lights in the same order, minus the ones the tile does not reach.

### Why it is exact

With the depth pre-pass the opaque colour pass tests depth for equality: every fragment it shades lies at a depth the
pre-pass stored, inside its tile's box. A light whose widened sphere misses the box is farther than its range from each
of those fragments: their range test (`light_length * inv_radius >= 1.0`) skips it, which the zero attenuation skip
(lights/zero_attenuation_skip, on by default) makes a skip of the whole light. Dropping it earlier changes nothing for
any fragment; the lights that remain are visited in the same order (highest bit first, word by word), so the sums are
the same.

Used only where that holds: the opaque colour pass (not the transparent pass), materials for which the zero attenuation
skip applies (`FELLOW_BACKFACING_LIGHT_SKIP_OK`: no custom light code, rim, backlight, transmittance, clearcoat, toon
or wrap diffuse, shadow to opacity) that shade where they are depth tested (no `LIGHT_VERTEX`, no depth writes), with
the skip on, a depth pre-pass, one view, no MSAA and no variable rate shading (a coarse fragment's position is not one
of its pixels').

The margins are far above the float error between a fragment's interpolated position and the one its depth gives.

## Verification (lavapipe)

* Images, on against off: Lights Lab captures **0 differing pixels**.
* Lights per pixel (Lights Lab captures, loop iterations / shaded):

| capture | off | on | within range (per pixel) |
|---|---:|---:|---:|
| far | 9.84 / 2.61 | **4.62** / 2.61 | 3.08 |
| low | 16.07 / 7.56 | **9.18** / 7.56 | 7.71 |
| street | 11.99 / 5.78 | **7.58** / 5.78 | 6.11 |
| up | 13.60 / 4.80 | **6.91** / 4.80 | 5.31 |

The loop visits half as many lights; the lights shaded are the same.

## Measured

### v1 (`fellow119`, `m9_f136`): a workgroup per tile; the fragment ANDs the tile's word with the cluster's

* Registers: 96 with 64 bytes of local memory, as off (`m9_f136_pstats_*`).
* Images (`m9_f136_tc_temporal`, on against off): Bistro exterior bit for bit, City Lab 101.18 dB (one level, a few
  pixels), Bistro interior 67.1, Lights Lab 96.9, PBR Lab 76.3 dB (their floors). lavapipe: Lights Lab, PBR Lab, City
  Lab, Stress Lab 0 differing pixels.
* In one process (`m9_f136_tc_ab`): the culling pass **0.15-0.23 ms**; Lights Lab's opaque pass −0.209 ms (1.160 →
  0.951), frame −1.0 %; Bistro interior opaque pass +0.067 ms, frame +8.9 %; PBR Lab +6.1 %, City Lab +6.9 % (the
  pass alone); Bistro exterior (no omni or spot light in view, no pass) 0.0 %.

### v2 (`fellow120`, `m9_f137`): a workgroup per 16 × 16 pixels; the tile's word replaces the cluster's

The pass reads the cluster tile's words once and builds the four boxes once: **0.034-0.049 ms**. In one process
(`m9_f137_tc_ab`):

| scene | frame | opaque pass | culling pass |
|---|---:|---:|---:|
| Lights Lab | **−3.3 %** (p 10^-39) | 1.108 → 0.980 ms (−0.128) | 0.049 |
| Bistro interior | **+3.8 %** (p 10^-71) | 1.305 → 1.423 (+0.117) | 0.046 |
| PBR Lab (2 positional lights) | +1.9 % | 0.813 → 0.817 | 0.045 |
| City Lab (48 lights, sparse) | +1.8 % (n.s.) | 0.473 → 0.458 | 0.034 |

Images at the floor again (`m9_f137_tc_temporal`). Bistro interior's opaque pass is slower although its light loops
visit a subset of the lights they visited before, and v2's fragment code (a choice between the two words) did worse
than v1's (an AND) in both Lights Lab (−0.128 against −0.209 ms) and Bistro interior (+0.117 against +0.067): the
driver's code for the colour pass changes with the shape of the change (0048).

### v3-v5: separating the pass, the code path and the culling (`m9_f138`-`m9_f140`)

v3 (`fellow121`): the AND of v1, gated by the specialization bit alone. Diagnostics: `debug/tile_cull_passthrough` keeps
every candidate (the code path without its effect), `debug/tile_cull_pass_only` (from v5) runs the pass without the
colour pass reading it. v4 (`fellow122`): the masks word-major (made the pass slower: 0.05-0.07 ms, no gain in the colour
pass; dropped in v5) and read only for non-empty cluster words (kept). v5 (`fellow123`, `m9_f140_tc_ab`), frame against
off:

| scene | on | passthrough | pass only | opaque pass: on / passthrough / pass only |
|---|---:|---:|---:|---:|
| Lights Lab | **−5.8 %** | +2.5 % | +1.6 % | −0.210 / +0.015 / −0.009 ms |
| Bistro interior | **+4.9 %** | +5.8 % | +2.6 % | +0.055 / +0.066 / +0.015 |
| City Lab | +0.6 % (n.s.) | +1.3 % (n.s.) | +2.2 % | −0.008 / +0.004 / +0.001 |

(v3, `m9_f138`: Lights Lab −6.1 %, Bistro interior +3.5 %, PBR Lab +3.9 %, City Lab +0.7 %; v4: PBR Lab's opaque pass
+0.005 ms instead of +0.047 with the conditional reads.) The pass costs 0.034-0.054 ms; in Lights Lab culling takes
0.2 ms off the opaque pass. In Bistro interior the culling itself saves nothing (on ≈ passthrough), and with the masks
in use every pass of the frame is 2-6 % slower (glow, SSAO, TAA, tonemap alike), as if the GPU ran at a lower clock: a
cost outside the shader's code, measured but not explained.

## Per scene: autotune

The cost and the benefit depend on the scene (lights per pixel and how much of them the tiles cut). Both choices are
exact, so the toggle is an autotune tunable (`TUNE_LIGHT_TILE_CULL`, decision 0020): while the pass can run (the
conditions above, omni or spot lights in view), autotune compares blocks of frames with and without it and keeps the
faster.

Between processes with autotune on (the defaults otherwise; 3 interleaved runs of 20 s per build and scene; stat
`lights_tile_cull_frames`: the frames of a run that used the pass, of about 7 000):

* `m9_f143` (`fellow125`, the tunable starting on): Lights Lab **−5.6 %** (p 0.009 paired, 3/3 pairs; the pass on in
  8 852-8 884 frames); but undecided evaluations kept it on in Bistro interior (781-7 006 frames), PBR Lab (192-8 400)
  and City Lab (837-10 535), where it costs: +0.3 %, +2.4 %, +1.2 % (all within noise).
* From `fellow126` the tunable starts off and is switched on only by an evaluation that measures it faster, as the
  visually lossless tunables (undecided evaluations repeated at the base interval, pooled). `m9_f144`: on in Lights Lab
  (5 192-6 993 frames), only its evaluations' frames elsewhere (Bistro interior 144-192, PBR Lab 192, City Lab 288;
  Bistro exterior: no omni/spot light in view, 0). Frame times in that session were noisy (Bistro interior's first
  reference run 4.37 ms against 3.45-3.52 for the others; Lights Lab's last pair +3 % with both runs slow): Lights Lab
  −1.8 % (two pairs −4.8 % and −4.0 %, the third +3.1 %), the others within noise (Bistro interior −4.1 % from the
  outlier, City Lab −0.9 %, PBR Lab +1.4 %, Bistro exterior +0.8 %).

## Decision

**KEEP, on by default from `fellow127` under autotune** (starts off; on per scene once measured faster). Exact either
way. Where many lights overlap (Lights Lab) the light loops visit half the lights and the frame is 5-6 % faster; in the
other scenes autotune leaves it off after its first evaluations.

Not explained: in Bistro interior the frame gets slower with the masks in use, in every pass, although the light loops
visit a subset of their lights (v3-v5 above). With autotune it does not matter for the defaults; a future change of
the pass or its placement should look at it again (`debug/tile_cull_passthrough`, `debug/tile_cull_pass_only`).

### Again, with and without async compute (`m10_f148`, `fellow129`, autotune off)

| scene | async compute on (default) | opaque pass | async compute off | opaque pass |
|---|---:|---:|---:|---:|
| Bistro interior | **−0.4 %** (p 0.0001) | 1.348 → 1.266 ms | +1.3 % (the pass, 0.045 ms) | 1.337 → 1.338 |
| Lights Lab | **−5.6 %** | 1.068 → 0.886 | **−6.0 %** | 1.096 → 0.898 |

The slowdown of every pass in Bistro interior (`m9_f138`-`m9_f140`) does not recur in the same configuration: there the
culling now takes 0.08 ms off the opaque pass when async compute is on (the async queue's SSAO and SSIL run beside it)
and nothing when it is off; why is not known. Read as session-dependent; autotune measures it per scene either way.

## v6: split around the async compute part (`fellow132`, measuring)

With async compute (on by default on NVIDIA, 0015) the screen-space effects run on the second queue beside the shadows,
the light clusters and the volumetric fog, and the main queue waits for them before the opaque pass. Light tile
culling ran after that wait, on the critical path, although it needs only the depth pre-pass and the clusters. Its
light tests can run beside the async part, but its depth reads cannot simply move there: the async part samples the
depth buffer too, and the render graph records the depth buffer's layout transition in whichever part uses it first,
which the other queue would not wait for.

So the pass is split (`debug/tile_cull_split`: 0 when async compute runs, the default; 1 always; 2 never): the tiles'
nearest and farthest depths (`MODE_DEPTH_BOUNDS`, 8 bytes per tile) are recorded before the async part, which leaves the
depth buffer in its sampled layout for both queues; the rest (`MODE_FROM_BOUNDS`) reads those bounds and runs after the
light clusters, beside the async part. The same values, the same masks.

* lavapipe (autotune off): Lights Lab captures, split against whole 0 differing pixels (121 frames split); PBR Lab
  captures, split against culling off 0 differing pixels (211 frames split).

