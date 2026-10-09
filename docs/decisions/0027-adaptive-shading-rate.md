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

v3 (`32cb3b9`, `fellow80`): gloss guard (`adaptive_vrs_gloss_guard`, on): a tile with at least 2 pixels where a glossy
surface's normal varies between neighbours (roughness < 0.5, 1 − dot > 0.5 roughness², from the pre-pass's
normal/roughness buffer) keeps full rate (PBR Lab's specular streaks).

## v2 measured (`fellow79`, `m6_f83_*`)

In one process (`m6_f83_all_ab`, threshold 0 vs 0.006, attachment in every block):

| scene | frame | opaque pass | estimate (in the Tonemap region) |
|---|---:|---:|---:|
| Lights Lab | **−22.8 %** | 1.263 → 0.539 ms | +0.121 ms |
| PBR Lab | **−6.0 %** | 0.920 → 0.676 ms | +0.138 ms |
| Bistro interior | **−3.3 %** | 1.547 → 1.292 ms | +0.181 ms |
| City Lab | +2.8 % | 0.620 → 0.533 ms | +0.158 ms |
| Sponza | +1.2 % | 2.239 → 2.218 ms | +0.139 ms |
| Bistro exterior | +1.2 % | 1.874 → 1.799 ms | +0.153 ms |

The estimate cost 0.12-0.18 ms: each tile's 64 threads left one thread to sum 64 × 8 partial results from shared
memory, ~900 dependent loads per tile (8160 tiles at 1080p).

Quality v3 (`m6_f84_capture`, `m6_f84_temporal`, against no attachment): captures 54-69 dB mean (Bistro interior
52.8 dB min, PBR Lab 63.5 dB min: the gloss guard fixed the streaks), temporal Lights Lab 59.8 dB, City Lab 61.5 dB,
Bistro exterior 55.6 dB (1.6 % of pixels above 2 levels), Sponza 48.6 dB (7.0 % above 2 levels): in motion the rates
come from where the content was a frame earlier; at 45°/s the image moves ~24 pixels a frame, more than the 3 × 3-tile
dilation covers.

v3 fast (`54c381a`, `fellow81`): the workgroup sums by a shared-memory tree (6 steps), pair counts computed, counters
per workgroup. Measured in `m6_f85_*`.

v4 (`fellow82`): two passes. The estimate (after the final image) only computes each tile's rates from the luma. Before
the next frame's opaque pass (after its depth pre-pass), an apply pass gives each tile the finest rate of the estimated
tiles around where its content was: five of its pixels (corners, centre) are reprojected with this frame's depth and
the camera motion into the estimated frame, and the tiles they fall in plus one on every side are read (the old 3 × 3
dilation when the view is still); content that was off screen, or spread over more than 7 tiles, gets full rate. The
gloss guard moves there and uses this frame's normals instead of the previous frame's. Counters spread over 64 slots.
Needs the depth pre-pass and no MSAA (otherwise full rate).

v4.1 (`fellow83`): disocclusion test. The estimate also keeps each tile's nearest and farthest view depth (from the
estimated frame's depth buffer and inverse projection; RG16F per tile); in the apply pass a reprojected point whose
depth in the estimated frame lies outside its tile's range by more than 5 % (it was hidden there, has come in front,
or belongs to a moving object) makes its tile full rate. Perspective only (orthographic views skip the test).

v4.1 fix (`fellow84`): the disocclusion test compared the reprojected point's homogeneous clip w (scaled by
1 / its current depth, ~1 for a still view) with the tiles' view depths, so every tile failed it and v4.1 shaded at
full rate everywhere (`m6_f87_capture`: 93-101 dB against off where v4 gave 54-74 dB). The point's depth in the
estimated frame is now clip.w divided by the unprojected point's homogeneous w. Also: the counters are cleared,
filled and read back one frame in 16 (their transfers no longer sit between compute and draw work every frame).

## v4 / v4.1 measured (`fellow82`, `fellow84`; `m6_f89_*`)

Captures against off (`m6_f89_capture`, v4.1): unchanged from v3 (still views): Bistro exterior 60.8 dB mean (53.8 min),
Bistro interior 54.3 (52.8), City Lab 74.1 (64.9), Lights Lab 60.3 (57.9), PBR Lab 65.0 (62.1), Sponza 66.6 (59.8).

Temporal sequences against off (60 frames, `m6_f89_temporal`):

| sequence | v3 (`m6_f84`) | v4 (reprojected rates) | v4.1 (+ disocclusion) |
|---|---|---|---|
| Bistro exterior path | 55.6 dB, 1.6 % > 2 | 56.9 dB, 1.2 % > 2 | 56.9 dB, 1.2 % > 2 |
| City Lab pan | 61.5 dB, max \|d\| 101 | 74.9 dB, max \|d\| 20 | 75.1 dB, max \|d\| 20 |
| Lights Lab walk | 59.8 dB | 60.1 dB | 60.1 dB |
| Sponza pan (45°/s) | 48.6 dB, 7.0 % > 2 | 49.8 dB, 5.3 % > 2 | 49.8 dB, 5.3 % > 2 |

Reprojection removed City Lab's worst pixels (a building edge shaded with a neighbour's coarse rate) and part of the
loss in the pans; the disocclusion test changes nothing in these camera paths (no object appears from behind
another). Sponza's fast pan still loses texture detail in 5 % of the pixels (by 3-21 levels): measured in `m6_f91`
(threshold, refresh, 2 × 2 limit, max step).

v4.1 in one process (`m6_f89_all_ab`, `fellow84`): the estimate costs 0.052-0.067 ms (Tonemap region) and the apply
pass 0.036-0.046 ms (its own region); frame Lights Lab −20.1 %, PBR Lab −5.6 %, Bistro interior −2.8 %, City Lab
+1.8 %, Sponza +0.9 %, Bistro exterior +1.0 %: more overhead than v3 fast (`m6_f85`, one pass of ~0.06 ms), and smaller
opaque savings in Bistro interior (−0.194 against −0.245 ms; the reprojected rates follow the motion of the path).

`fellow86`: the apply pass is recorded with the screen-space effects (it needs only the depth pre-pass), so with async
compute it runs on the second queue while the main queue renders the shadows; its 7 × 7 tile window is read by the
workgroup's threads in parallel (one thread read up to 49 texels before); the counters are read back after the
frame's main work. Motion guard (`adaptive_vrs_motion_px`, off): a tile whose centre moved more than N pixels since
the estimated frame shades at most 2 × 2, more than 2N at full rate.

## What drives the loss in motion (`m6_f91_vrs_motion`, `m6_f92b_*`)

Fast pans against off (`fellow84`/`fellow86`; Sponza pan 45°/s, Bistro exterior path):

| variant | Sponza pan | Bistro exterior path |
|---|---|---|
| defaults (threshold 0.006, max step 0.02, refresh 8, up to 4 × 4) | 49.8 dB, 5.3 % > 2 | 56.9 dB, 1.2 % > 2 |
| threshold 0.003 | 62.0 dB, 0.30 % > 2 | 76.5 dB, 0.006 % > 2 |
| max step 0.01 | 62.6 dB, 0.29 % > 2 | 82.1 dB, 0.000 % > 2 |
| no refresh | 49.1 dB, 6.1 % > 2 | 56.2 dB, 1.4 % > 2 |
| at most 2 × 2 | 49.9 dB, 5.1 % > 2 | 56.9 dB |
| motion guard 8 px | 62.2 dB, 0.53 % > 2 (City Lab 84.6, Lights Lab 62.0) | 56.9 dB (slow path: no tile over 8 px) |
| motion guard 16 px | 52.1 dB, 3.0 % > 2 | 56.9 dB |

In a still view TAA averages the coarse fragments over its 16 jittered frames (each coarse fragment is shaded at a
different sample position every frame), which hides most of the coarse shading; in motion it cannot. What loses
detail are tiles with moderate texture contrast that the criteria let through: the largest-step limit decides it
(0.01 instead of 0.02 keeps both pans above 62 dB), the 4 × 4 rate and the refresh period hardly matter. Speed of the
tighter settings: `m6_f93_step_ab`.

## Cost against benefit, per scene (`m6_f92b_all_ab`, `fellow86`)

With the apply pass on the async queue: Lights Lab −21.1 %, PBR Lab −5.2 %, Bistro interior −2.8 %, City Lab +1.4 %,
Sponza +0.9 %, Bistro exterior +1.0 %. The passes cost ~0.1 ms wherever they run (the estimate 0.05-0.07 ms on the main
queue; the apply pass lengthens the async queue, which is the critical path in PBR Lab, City Lab and Bistro interior),
and where little of the screen can be shaded coarsely (Sponza 5 % of the tiles, Bistro exterior 12 %) or the opaque
pass is cheap (City Lab: 43 % of the fragments saved, 0.05 ms) that is more than it saves. The motion guard at 8 px
costs much of the gain where the camera moves (Lights Lab −12.6 % instead of −21.1 %).

`fellow87`: adaptive shading is an autotune tunable (decision 0020; `shading/adaptive_vrs_autotune`, on): the engine
runs it on and off in alternating blocks of frames, compares the GPU time and keeps the faster way per scene,
re-checking now and then. Both ways look the same within the measured limits, so the switching is not visible; where
it does not pay it costs only its evaluations.
