# 0075 — TAA's 8 × 8 workgroups: the tile loaded by all 64 invocations

Status: **MEASURING** (off) · Branch `fellow/geometry` · Setting `rendering/fellow/postfx/taa_strided_load` · Code:
`shaders/effects/taa_resolve.glsl` (`FELLOW_TAA_STRIDED_LOAD`), `effects/taa.{h,cpp}` (variants 8-11),
`fellow_settings.*`.

## Problem

The TAA resolve costs 0.20-0.24 ms at 1080p (`m14`, with v2, shared velocities and direct output). Each 8 × 8 workgroup
first loads a 10 × 10 tile (colour, its tonemapped YCoCg, depth and velocity) into shared memory: the first 25
invocations load four entries each while the other 39 wait at the barrier. Decision 0023 had every invocation load, but
in 16 × 16 workgroups, and was slower (+0.006 to +0.021 ms); it left open whether the group size or the loads decided
that.

## Change

`postfx/taa_strided_load`: stock's 8 × 8 workgroups, the tile loaded by all 64 invocations in turn (invocation *i*
loads entries *i* and *i* + 64: two for 36 of them, one for 28) with 0023's loop. Four more shader variants (stock's
resolve and v2, with and without the fused velocity write). `taa_tile16` takes precedence when both are set.

## Why it is exact

Every entry is the same texel loaded by the same function with the same coordinates; only which invocation loads it
changes, and every invocation reads the tile after the same barrier.

## Verification

lavapipe, captures, demote diagnostics, autotune off, the setting off against on: PBR Lab (7 cameras), City Lab (3),
Lights Lab (4) at 480 × 270: **0 differing pixels**.

RTX: (pending, `m15_f220`).

## Decision

(pending)
