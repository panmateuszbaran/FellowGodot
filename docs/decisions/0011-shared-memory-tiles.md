# 0011 — Screen-space passes: neighbourhoods loaded once per workgroup

Status: **REJECT** (SSAO: default off) / **no measurable effect** (TAA) · Branch `fellow/geometry` · Toggle
`rendering/fellow/postfx/ssao_shared_tile` (default **off** from `fellow25`) · Code: `shaders/effects/ssao.glsl`
(`FELLOW_SHARED_TILE`), `effects/ss_effects.*` (gather pipelines 3-5), `shaders/effects/taa_resolve.glsl`
(`FELLOW_TAA_V2`).

## Problem

After milestone 2, SSAO costs 0.44-0.61 ms per frame in every scene (sizing, milestone-3 report §2), its gather pass
0.28-0.38 ms (`m3_f19_profile`). At the benchmark preset (high quality, full size) each gather invocation reads its
own and its four neighbours' depths with two `textureGather` calls and five normals: neighbouring invocations read the
same texels. The TAA v2 resolve already loads a 10×10 tile of colour and depth per 8×8 workgroup, but every thread
converted each of its nine neighbours to tonemapped YCoCg again.

## Change

* **SSAO** (`ssao_shared_tile`): each 8×8 workgroup loads the depths and normals of its tile plus a one-texel border
  (100 loads for 64 pixels) into shared memory, with the gathers' mirrored addressing and the same full-resolution
  normal coordinates; neighbour normals come from the tile only at full size, where they are the layer's neighbouring
  pixels. Separate gather pipelines; stock's are kept for the toggle off.
* **TAA v2**: the tonemapped YCoCg value of each tile texel is computed once, when the tile is loaded.

## Verification

lavapipe: PBR Lab captures at full and half SSAO size and the Lights Lab 60-frame walk **bit-identical** to the
previous build (SSAO), the Lights Lab walk bit-identical (TAA). RTX (`m3_f23_temporal`, `fellow22` vs `fellow23` and
tile off vs on): every scene at its run-to-run floor (Lights Lab 88.0 dB, max |d| 4; Sponza 75.0 dB, max 3; Bistro
interior 67.3 dB and PBR Lab 73.3 dB as their null tests).

RTX, Bistro exterior (`m3_f29_temporal`, `fellow27` with the tile off vs on): **not bit-identical** — 81.4 dB mean
(min 75.6), max |d| 5/255, no pixel off by more than 2/255 in any frame (the scene is bit-identical run to run, 99 dB):
the GPU's gathers and its shared-memory loads of the same texels do not end in exactly the same AO everywhere (lavapipe
cannot show it). Within the image-identity tolerance of the other scenes' floors, but not exact; moot, as rejected
below.

## Result

Profile (`m3_f23_profile`, one run each, GPU ms): SSAO pass with / without the tile 0.323 / 0.325 (Lights Lab),
0.354 / 0.357 (Bistro interior), 0.363 / 0.387 (Bistro exterior), 0.415 / 0.435 (Sponza), 0.362 / 0.341 (PBR Lab) —
within the profile's ±0.05 ms. TAA: −0.018 to +0.011 ms. Interleaved A/B (`m3_f23_perf`, 3 runs each, `fellow22` =
reference, `f23` = both, `f23_nt` = TAA change only):

| scene | fellow22 ms | both | Δ | p | TAA change only | Δ | p |
|---|---:|---:|---:|---:|---:|---:|---:|
| Lights Lab | 3.012 | 2.980 | −1.1 % | 0.24 | 2.995 | −0.6 % | 0.44 |
| Bistro interior | 4.092 | 4.073 | −0.4 % | 0.16 | 4.108 | +0.4 % | 0.64 |
| Bistro exterior | 5.017 | 4.964 | −1.1 % | 0.14 | 4.993 | −0.5 % | 0.44 |
| Sponza | 7.162 | 7.167 | +0.1 % | 0.94 | 7.113 | −0.7 % | 0.46 |
| PBR Lab | 2.883 | 2.883 | 0.0 % | 0.99 | 2.848 | −1.2 % | 0.13 |

No difference beyond noise: these passes are not bound by the redundant loads they remove (the texture cache already
serves neighbouring invocations; the SSAO gather's cost is its 12 × 2 sample taps per pixel, not the centre
neighbourhood).

## Decision

* SSAO shared tile: **REJECT** — default off, code kept behind the toggle (exact, no gain).
* TAA tile conversion: no measurable effect; kept (exact, no toggle, simpler inner loop).
