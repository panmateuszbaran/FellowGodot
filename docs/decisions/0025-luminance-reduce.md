# 0025 — Auto exposure: the luminance reduction with two barriers per workgroup

Status: **MEASURING** · Branch `fellow/postfx` · Toggle `rendering/fellow/postfx/fast_luminance_reduce` (runtime, default
off until measured) · Code: `shaders/effects/luminance_reduce.glsl` (`FELLOW_FAST_REDUCE`), `effects/luminance.{h,cpp}`
(three more variants), `fellow_settings.*`.

## Problem

With auto exposure on (Bistro exterior and interior, Sponza), the "Auto exposure" pass costs 0.09-0.10 ms of GPU time
per frame at 1080p (`m5_f69_sizing`), as much as the whole volumetric fog of those scenes. It reduces the frame's
maximum channel per pixel to one average in four compute passes (1920 × 1080 → 240 × 135 → 30 × 17 → 4 × 3 → 1), each
workgroup of 64 threads summing its 8 × 8 values in shared memory by a tree: six steps (index t += index t + size, size
32, 16, 8, 4, 2, 1), a barrier after each, most threads idle after the first. The first pass has 32 400 workgroups.

## Change

With the toggle, after the values are loaded (and the barrier that follows), eight threads each compute what the tree
holds at their index (0-7) after its steps 32, 16 and 8, from the 64 loaded values, and the first thread computes the
steps 4, 2 and 1 from those eight: two barriers instead of seven per workgroup. The rest of the pass (the average, the
adaptation of the final pass) is unchanged.

## Why it is exact

The same additions on the same operands in the same grouping: after the steps 32 and 16 index t (< 16) holds
(x[t] + x[t+32]) + (x[t+16] + x[t+48]); after the step 8 index t (< 8) holds that plus the same for t + 8, which is what
the eight threads compute; the steps 4, 2 and 1 then give ((p0 + p4) + (p2 + p6)) + ((p1 + p5) + (p3 + p7)). Float
addition is commutative, so only the grouping matters, and it is the tree's.

## Verification

* lavapipe, PBR Lab captures (7 cameras) with auto exposure switched on (`--set=cam.auto_exposure_enabled=true`) and a
  fixed frame step (`--fixed-fps 60`: the exposure adapts by the frame's time step, so without it two runs of the same
  build already differ by 1/255 in tens of thousands of pixels): off vs off 0 differing pixels, **off vs on 0 differing
  pixels**.

## Result

(pending)

## Decision

(pending)
