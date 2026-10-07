# 0023 — TAA resolve in 16 × 16 workgroups

Status: **MEASURING** · Branch `fellow/postfx` · Toggle `rendering/fellow/postfx/taa_tile16` (runtime, default off until
measured) · Code: `shaders/effects/taa_resolve.glsl` (`GROUP_SIZE`, `FELLOW_TAA_STRIDED_LOAD`), `effects/taa.{h,cpp}`
(variants 4-7), `fellow_settings.*`.

## Problem

The TAA resolve (stock's and v2, decision 0001) costs 0.23-0.31 ms at 1080p in every benchmark scene (`m4_f66_profile`):
after SSAO the most expensive full-screen pass. Each 8 × 8 workgroup first loads a 10 × 10 tile (colour, its tonemapped
YCoCg for v2, depth) into shared memory: 100 entries for 64 pixels, loaded by the first 25 threads, four each, while the
other 39 wait at the barrier.

## Change

With the toggle, the resolve runs in 16 × 16 workgroups (an 18 × 18 tile: 324 entries for 256 pixels, 1.27 instead of
1.56 loads per pixel), and every thread of the group loads tile entries in turn. Stock's resolve and v2 alike (four more
shader variants).

## Why it is exact

Every pixel reads the same tile entries (the same texels, loaded the same way, clamped at the screen's edges as before)
and computes the same expressions; only which thread loads an entry and the group's size change.

## Verification

* lavapipe, off vs on: v2, Lights Lab walk and City Lab walk (60 frames each); stock resolve, Lights Lab walk:
  **0 differing pixels**.

## Result

(pending)

## Decision

(pending)
