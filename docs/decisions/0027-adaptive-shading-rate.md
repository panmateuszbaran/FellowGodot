# 0027 — Adaptive shading rate (visually lossless)

Status: **MEASURING** (`fellow77`) · Branch `fellow/geometry` · Toggle `rendering/fellow/shading/adaptive_vrs` (creates
the attachment with the render buffers), `shading/adaptive_vrs_threshold` (runtime; 0 = full rate everywhere),
`shading/adaptive_vrs_max_rate` (2 × 2 or 4 × 4) · Code: `shaders/effects/fellow_adaptive_vrs.glsl`,
`render_forward_clustered.cpp` (`_fellow_adaptive_vrs_*`), `render_scene_buffers_rd.cpp`, pipeline flag
`PipelineRasterizationState::fellow_full_rate_shading` (Vulkan driver), `scene_shader_forward_clustered.cpp`.

First change of the "visually identical, measured" track (agreed 2026-10-08): not bit-exact, judged by image
comparisons against full-rate shading (PSNR, largest differences) on captures and temporal sequences.

## Problem

After milestone 5 the opaque colour pass is the largest GPU cost in the heavy scenes (Sponza 2.04 ms of 7.1, Bistro
exterior 1.84 of 4.6, Bistro interior 1.38 of 3.7; `m6_f77_occ_ab`), Sponza's transparent pass another 1.33 ms. Most of
it is fragment shading (lights, shadows, GI), done once per pixel even where the result varies less than the eye can
see (flat walls, dark areas, distant smooth surfaces).

## Change

Godot already supports a shading rate attachment (VK_KHR_fragment_shading_rate) for viewports with a VRS mode. With
the toggle, every 3D viewport without a VRS mode of its own gets the attachment, and after each frame's final image
(tonemapped) a compute pass sets the next frame's rate per attachment texel (16 × 16 pixels on NVIDIA):

* per tile, the luma gradient along each axis is estimated from squared differences at strides 1, 2 and 4 (the
  largest estimate is kept: a tile shaded at 2 × 2 is flat inside its fragments, but its stride-2 differences still
  show the content's gradient);
* a fragment 2 pixels wide misses about 0.5 gradients per pixel, one 4 pixels wide about 1.12 (RMS over the pixels it
  covers); per axis the coarsest width whose error stays under the threshold (luma 0-1; default 0.006, about 1.5/255)
  is chosen; no 4 × 1 or 1 × 4;
* the rates are dilated over 3 × 3 tiles (the finest per axis) for content that moves between frames;
* the first frame of new render buffers is shaded at full rate.

Materials that discard (alpha scissor, alpha hash, DISCARD), use alpha antialiasing, write DEPTH or emulate point size
keep one fragment per pixel (pipeline rate 1 × 1, attachment ignored): a coarse fragment that discards would remove
pixels the full-rate pre-pass kept.

## Verification plan

* Threshold 0 (attachment present, every tile 1 × 1) against no attachment: same images bit for bit.
* Captures and temporal sequences (motion) at thresholds 0.006 and 0.012 against full rate: PSNR, largest difference.
* In-process A/B (threshold 0 / 0.006 / 0.012): frame and pass GPU times; counters (fraction of fragments shaded).

## v1 measured (`fellow77`, `m6_f80_*`)

In one process (`m6_f80_vrs_ab`, blocks of 30 frames, attachment present in every block; threshold 0 = every tile at
full rate):

| scene | 0.006: frame | 0.012: frame | opaque pass (0 → 0.006 → 0.012) |
|---|---:|---:|---|
| Lights Lab | **−26.7 %** | **−29.7 %** | 1.23 → 0.40 → 0.34 ms |
| PBR Lab | **−9.0 %** | **−12.6 %** | |
| Bistro interior | **−7.1 %** | **−14.5 %** | 1.50 → 1.15 → 0.90 ms |
| Sponza | +0.5 % (n.s.) | −4.1 % | 2.37 → 2.33 → 2.11 ms |
| Bistro exterior | 0.0 % | −4.2 % | 1.86 → 1.75 → 1.55 ms |
| City Lab | +2.9 % | +0.9 % | 0.52 → 0.43 → 0.39 ms (the estimate's own cost is larger) |

Image differences against no attachment (`m6_f80_vrs_capture`, `m6_f80_vrs_temporal`):

* Threshold 0 (attachment, every tile 1 × 1): each scene's floor (Lights Lab bit-identical).
* 0.006: captures 50-62 dB mean; temporal Lights Lab 58.7 dB (max |d| 17, 0.008 % of pixels above 2 levels), City Lab
  57.1 dB, Bistro exterior 52.3 dB (2.7 % above 2), Sponza 45.8 dB (12.5 % above 2; its auto exposure also follows the
  image). PBR Lab's specular capture 44.4 dB: thin bright reflection streaks on a dark sphere turned into blocks (a dark
  tile with a few bright lines has a small RMS gradient).
* 0.012: 38-57 dB; not visually lossless in Sponza and Bistro.

v2 (`90c69e9`): a tile is shaded coarsely only if its largest neighbour step (times 0.5 for 2 pixels, 1.5 for 4) also
stays under `adaptive_vrs_max_step` (0.02); every tile at full rate one frame in 8 (`adaptive_vrs_refresh`), so that
texture detail lost to coarse shading is seen again; counters fixed (they were cleared inside the compute list).
Measured in `m6_f82_vrs_*`.
