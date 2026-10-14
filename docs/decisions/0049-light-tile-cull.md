# 0049 — Light tile culling: the lights that can reach an 8 × 8 tile's geometry

Status: **under test** (`fellow119`, job `m9_f136`) · Branch `fellow/geometry` · Toggle `rendering/fellow/lights/tile_cull`
(runtime; specialization bit 26 of the opaque colour pass only) · Code: `shaders/effects/fellow_light_tile_cull.glsl`,
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

### v3 (`fellow121`, `m9_f138`)

The AND of v1, gated by the specialization bit alone; `debug/tile_cull_passthrough` keeps every candidate (the code path
without its effect). Pending.
