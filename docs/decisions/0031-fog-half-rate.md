# 0031 — Volumetric fog: half the froxels lit per frame (visually lossless)

Status: **MEASURING** · Branch `fellow/geometry` · Toggle `rendering/fellow/postfx/fog_half_rate` (runtime, off) · Code:
`shaders/environment/volumetric_fog_process.glsl` (`MODE_DENSITY`), `environment/fog.{h,cpp}`, `fellow_settings.*`.

"Visually identical, measured" track (as 0027, 0028, 0030).

## Problem

The volumetric fog's density pass lights every froxel every frame (64 × 64 × 64 by default: fog volumes, every
directional and clustered light with its shadow, ambient and GI), then blends the result with the previous frame's
reprojected value (stock's temporal reprojection, history weight 0.9 by default). In the scenes with fog the pass costs
0.10 ms (Sponza, Bistro exterior) to 0.22 ms (Lights Lab: many shadowed lights in the fog) (`m6_f83_all_ab`).

## Change

With temporal reprojection on (otherwise nothing changes): the froxels of one checkerboard parity ((x + y + z + frame)
odd) are not lit this frame and keep their reprojected value (NaN/Inf guarded, clamped, the fog-volume maps cleared as
stock does); the others are lit and blended with a history weight of b² instead of b, which is what stock's blend
gives over two frames. Each froxel's Halton jitter steps through the whole table across its own updates (index
(frame / 2) mod 16; stock's 16-frame cycle would otherwise give each parity only 8 of the 16 positions). Froxels whose
reprojection falls outside the previous volume are lit every frame as stock does.

A still view converges to the same mean as stock (the same exponential average per two frames, all Halton positions);
the response to a light change is the same per unit of time (b² per two frames either way). Fog volumes are still
rasterised every frame with stock's jitter; a froxel not lit in a frame drops that frame's volume density, which its
next update brings in.

## Verification plan

* lavapipe Lights Lab: still captures and the walk sequence, off vs on (PSNR).
* RTX: captures, temporal sequences, in-process A/B (Lights Lab, Sponza, Bistro exterior).

## lavapipe (`fellow83` candidate)

Lights Lab (volumetric fog, many shadowed lights), 960 × 540, off vs on: still captures after 30 frames 58.3-65.4 dB
(max |d| 11; the far, low and street views 63-65 dB, looking up into the fog 58 dB); the 60-frame walk 60.7 dB mean,
57.0 dB min, max |d| 8 (16 % of the pixels differ, by 1-3 levels: the fog's own temporal noise follows a different
jitter sequence).

## Measured on RTX (`fellow83`/`fellow84`)

Captures (`m6_f87_capture`): Bistro exterior 63.6 dB mean (61.1 min), Lights Lab 61.4 (58.0), Sponza 66.8 (62.6),
scenes without fog at their floor. Temporal sequences (`m6_f89_temporal`): Bistro exterior 60.1 dB (58.9 min), Lights
Lab 60.1 (56.6 min, max |d| 29, 0.001 % > 2), Sponza 72.3 (71.3).

In one process (`m6_f89_all_ab`, `fellow84`) the fog pass did **not** get cheaper (Lights Lab 0.237 → 0.242 ms,
Sponza and Bistro exterior +0.012 ms): the checkerboard was per froxel, so every 32-thread warp of a 4 × 4 × 4
workgroup kept half its threads busy for the full lighting time. `fellow86`: the parity is per workgroup (a 3D
checkerboard of 4 × 4 × 4 blocks), so half the workgroups return at once. lavapipe Lights Lab captures unchanged
(58.4-65.4 dB).
