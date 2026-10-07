# 0024 — SSAO and SSIL gathers with one sample rotation per workgroup

Status: **REJECT** (option kept, off) · Branch `fellow/postfx` · Toggle `rendering/fellow/postfx/ss_coherent_gather` (runtime, default
off until measured) · Code: `shaders/effects/ssao.glsl`, `shaders/effects/ssil.glsl` (`FELLOW_COHERENT_GATHER`),
`effects/ss_effects.{h,cpp}` (gather variants, `_fellow_ssao_gather_pipeline()`, `_fellow_ssil_gather_pipeline()`, the
gathers' dispatch), `fellow_settings.*`.

## Problem

SSAO is the most expensive screen-space effect in every benchmark scene: removing it saves 0.50-0.72 ms of GPU time
per frame at 1080p (`m5_f69_sizing`: Bistro exterior 0.50, Sponza 0.54, Bistro interior 0.62, City Lab 0.62, Lights
Lab 0.68, PBR Lab 0.72 ms; 25-35 % of the GPU frame in the labs and City Lab), most of it the gather ("Process SSAO",
0.32-0.46 ms). SSIL, where it is on, costs 0.32-0.49 ms (Bistro, Sponza).

The gather (Intel's ASSAO, as stock ports it) works on four deinterleaved layers; each pixel takes 12 pairs of depth
samples (quality high) on a disk scaled by its depth and turned by one of five rotations, chosen per pixel by
`(2y + x) % 5`. Neighbouring threads of a workgroup therefore use different rotations: their taps go in five different
directions from neighbouring pixels, so the texture accesses of a warp are spread over five disks instead of one.
Shared-memory tiles for the centre and neighbour depths and normals (decision 0011) changed nothing measurable: those
loads are not what limits it.

## Change

With the toggle, the gathers' threads are mapped to pixels differently: the layer is cut into 40 × 8 tiles, and each
tile is computed by five workgroups of 64 threads, one per rotation: a workgroup takes the eight pixels of its rotation
in each of the tile's rows (every fifth pixel; the class shifts by two per row), and each half of the workgroup (a warp
of 32 threads) four of them in each of the eight rows, a 20 × 8 footprint of pixels with the same rotation: on smooth
surfaces their taps are nearly translated copies of each other.
The dispatch has five workgroups per tile (`(width + 39) / 40 · 5` × `(height + 7) / 8`). SSAO and SSIL, all their
gather variants (base pass and adaptive pass of the ultra quality included).

## Why it is exact

Every pixel is computed by the same code from the same inputs (its coordinates, the layer's textures and constants);
no thread reads another thread's result. Only which thread computes which pixel, and the workgroup grid, change. Each
pixel of the layer belongs to exactly one tile, one rotation class and one position in its row, so every pixel is
written exactly once, as before. (The shared tile of decision 0011 maps threads as stock does and takes precedence.)

## Verification

* lavapipe, off vs on (SSIL switched on in every scene): City Lab captures (SSAO high, full size; SSIL medium, half
  size), Lights Lab walk (60 frames; SSAO and SSIL ultra: base pass, importance maps, adaptive pass), PBR Lab captures
  (SSAO low, half size; SSIL high, full size): **0 differing pixels**; 728, 1488 and 1688 coherent gather dispatches
  (`postfx_coherent_gathers`).

RTX (`fellow69`, `m5_f71_temporal`, off vs on): Bistro exterior bit-identical (99 dB), City Lab, Lights Lab and Sponza
at their run-to-run floor (max |d| 1, 2, 3; no pixel above 2); 500-1000 coherent dispatches per sequence.

## Result

In one process (`m5_f71_cg_ab`, blocks of 30 frames; GPU time of the passes, off → on):

| scene | Process SSAO | Process SSIL | GPU time | frame |
|---|---:|---:|---:|---:|
| City Lab | 0.372 → 0.388 ms (+0.016) | | +1.8 % | +1.2 % |
| Lights Lab | 0.344 → 0.370 ms (+0.026) | | +1.4 % | +1.3 % |
| Bistro exterior | 0.347 → 0.344 ms (−0.003) | 0.202 → 0.196 ms (−0.006) | −0.4 % | −0.4 % |
| Sponza | 0.384 → 0.450 ms (+0.066) | 0.284 → 0.277 ms (−0.006) | +1.0 % | +0.9 % |

The SSAO gather gets slower, up to 17 % (Sponza), SSIL's a little faster: the taps' spread over five rotations is not
what limits the gather. Probably (not measured) the wider footprint costs more than the taps gain: each warp's pixels
now span 20 × 8 pixels of the layer instead of 8 × 4, so its own depths and normal loads (five full-resolution normal
texels per pixel) touch more cache lines.

## Decision

**REJECT (kept as an option, off).** Exact, but slower for SSAO in three of four scenes.
