# 0075 — TAA's 8 × 8 workgroups: the tile loaded by all 64 invocations

Status: **REJECT** (left in, off) · Branch `fellow/geometry` · Setting `rendering/fellow/postfx/taa_strided_load` · Code:
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

RTX, `fellow166` (`m15_f220`): the warm sequences off against on **bit for bit** (Bistro interior, PBR Lab, Sponza,
Lights Lab: 99 dB, max |d| 0).

## Measurement

In one process (`m15_f220_taa_strided_ab`, blocks of 30 frames, autotune off), off against on:

| scene | TAA pass | GPU time |
|---|---:|---:|
| Sponza | 0.267 → 0.281 ms (+0.014) | +0.0 % (p 0.90) |
| Bistro exterior | 0.248 → 0.238 (−0.010) | −0.2 % (p 0.07) |
| Bistro interior | 0.230 → 0.228 (−0.003) | −0.0 % (p 0.77) |
| City Lab | 0.195 → 0.193 (−0.001) | −0.1 % (p 0.88) |
| Lights Lab | 0.221 → 0.217 (−0.004) | +0.1 % (p 0.64) |

## Decision

**REJECT** (left in, off). Exact, but no scene's GPU time moves and the TAA pass goes either way by 1-5 %. With 0023
this answers its open question: neither the workgroup's size nor how its invocations share the tile's loads limits
the pass.
