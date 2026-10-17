# 0053 — Volumetric fog: the density pass's light loops, walked by the subgroup

Status: **measuring** (`fellow133`) · Branch `fellow/geometry` · Setting `rendering/fellow/postfx/fog_light_loop`
(runtime: 0 stock, 1 subgroup, 2 subgroup in 2 × 2 × 16 workgroups, 3 stock loops in 2 × 2 × 16 workgroups) · Code:
`shaders/environment/volumetric_fog_process.glsl` (`FELLOW_FOG_MERGE`, `FELLOW_FOG_SHAPE`),
`environment/fog.{h,cpp}` (`VOLUMETRIC_FOG_PROCESS_SHADER_DENSITY_FELLOW_*`), `fellow_settings.*`.

## Problem

Volumetric fog's density pass lights every froxel (64 × 64 × 64 in the benchmark preset) with the omni, spot and area
lights of its cluster cell. In Lights Lab (240 positional lights, fog on) the pass takes 0.26 ms of a 2.35 ms frame,
against 0.09 ms in Sponza and Bistro exterior, whose fog has a few lights at most.

Each invocation walks its own cluster cell's light bits (`while (mask)`), and the 32 invocations of a warp (4 × 4 × 2
froxels of a 4 × 4 × 4 workgroup) belong to four or five cluster tiles and several depth slices: their masks differ, the
warp runs as many iterations as the longest of them, and in each iteration its invocations read different lights
(scattered loads, divergent branches, shadow lookups of different lights). Stock notes it: "positions in screen are too
spread apart, no hopes for optimizing with subgroups". The scene shader instead merges its pixels' masks over the
subgroup.

## Change

* **1, subgroup** (`FELLOW_FOG_MERGE`): the subgroup walks the words from the smallest first word to the largest last
  word of its invocations and, in each, the union of their bits (`subgroupOr`), highest bit first: the same light for
  every invocation at a time. Each invocation processes a bit only if its own cell's mask has it; its lights are the
  same, in the same order (words ascending, bits from the highest), with the same arithmetic.
* **2 and 3, 2 × 2 × 16 workgroups** (`FELLOW_FOG_SHAPE`): a warp's 2 × 2 × 8 froxels cover about one cluster tile, so
  that their masks are alike. Each froxel's computation does not depend on the workgroup. The half rate option
  (`postfx/fog_half_rate`, 0031) chooses its froxels by 4 × 4 × 4 block (stock's workgroup), now computed from the
  froxel's position: the same pattern.

## Verification

lavapipe, Lights Lab (fog density 0.01 and 0.05), against stock's loop: captures **0 differing pixels** for 1, 2 and 3;
60 frames with `postfx/fog_half_rate` for 2: 0. Negative control (1 with the lights of bit 0 dropped): 1 781 927
differing pixels, up to 14 levels: the fog's light contributions are visible in these images.

RTX (`m11_f155_fog_temporal`, 60-frame sequences, 1920 × 1080, each option against stock's loop in the same build):
Sponza 75.5 dB for 1, 2 and 3 (its run-to-run floor, `m10_f149`); Bistro exterior bit for bit in the frames recorded
(part of its runs were interrupted). Lights Lab: 3 at its floor (94.2 dB; two runs of one configuration give 94.2-100.5
dB), 1 and 2 a little below it, 90.9 and 91.0 dB, at most 4 levels off (the floor: 2) and a share of pixels off by more
than 2 that rounds to 0.000 %. lavapipe computes the same values for 1 and 2 as for stock's loop, bit for bit; the RTX's
compiler arithmetic for the two shaders may differ (contracted multiply-adds), which would explain differences of this
size in the fog, accumulated over frames. Not bit-exact on the RTX, then: a candidate for an exact option only if a
separate check confirms that cause.

## Measured

Pending: the in-process A/B of `m11_f155` was interrupted (the laptop was needed); queued again.
