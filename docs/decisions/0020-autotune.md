# 0020 — Autotune: exact optimizations chosen by measurement

Status: **MEASURING** · Branch `fellow/geometry` · Toggles `rendering/fellow/autotune/enabled` (runtime, default off
until measured), `autotune/block_frames` (16), `autotune/interval_frames` (600), `autotune/min_gain_percent` (0.5),
`autotune/max_interval_sec` (30), `autotune/max_duty_percent` (20) ·
Code: `servers/rendering/fellow_autotune.{h,cpp}`, `rendering_server_default.cpp` (`_draw`: the frame hooks and the
timestamps read back), `fellow_settings.h` (`shadow_directional_max_mip()` asks the tuner).

## Problem

Several FellowEngine optimizations are exact (they change no pixel) but their cost and benefit depend on the scene.
The directional shadow pyramid of decision 0005 is the clearest case: it saves Sponza 14 % of its frame (blocker
searches and filter taps skipped where the pyramid proves a window fully lit or fully shadowed) but costs City Lab
2.7 % and its draw-heavy variant 2.0 % (`m4_f62_*_power`: its build, 0.05 ms of GPU time per frame, saves nothing in
City Lab's opaque pass). No static default is right for both, and a heuristic (light angle, filter quality, scene size)
would have to predict the savings of the shading pass, which depend on what the camera sees.

## Change

The render thread measures instead. Every `autotune/interval_frames` frames (the first time after 40 % of it; doubled
while the choice holds, up to 16 times; back to the base interval when it changes) a tunable is evaluated:

* 6 pairs of blocks of `autotune/block_frames` frames, the optimization on in one block of each pair and off in the
  other, in the order on, off, off, on, on, off… (a linear drift of the GPU's clock or of the view cancels out over two
  pairs). The first 4 frames of each block are not counted (the choice just changed: a pyramid turned back on is
  rebuilt whole, pipelines of the other specialization may still be compiling).
* The GPU time of each counted frame: two timestamps around the frame's viewports (`<FellowTune:n`, `>FellowTune:n`,
  captured only during evaluations; names starting with `<`/`>` are skipped by the engine's and the benchmark's
  profilers), read back when the frame slot is reused, matched to the frame by the number in their name.
* Decision: per pair, GPU time off minus on; the faster choice wins if the mean difference exceeds
  `autotune/min_gain_percent` of the frame's GPU time and 2.5 standard errors of the pairs' differences; otherwise the
  choice stays.

Tunables, each only when its setting is on, one evaluated at a time (the most overdue):

* by GPU time: the directional and the positional shadow pyramids (`shadows/directional_max_mip`,
  `shadows/positional_max_mip`);
* by the frame's wall time (the render thread's time from one frame's viewports to the next frame's: they move work
  between the render thread and the GPU): async compute (`frame/async_compute`, decision 0015: it gains 1-3 % where
  the GPU is the limit and costs 3 % in CPU-bound frames; the device must have created its second queue at start-up),
  the frame plan (`frame/replay_frame_plan`, decision 0019) and persistent instance data (`cpu/persistent_instances`,
  decision 0022, since `fellow68`: the render thread writes 16 bytes per list element instead of 176, and the shaders
  read the rest through the element's slot).

Not a tunable: the directional shadow cache (decision 0004). Its renders choose mesh LODs by the cache's texel size, not
by the camera distance as stock does, so its shadow maps can differ from stock's by a LOD (lavapipe, City Lab walk: cache
on vs off differ by up to 5/255 in a few dozen pixels for ten frames); switching it between frames would show that.

An evaluation also ends after 3 pairs when the difference is clear (3 % of the time and 4 standard errors): fewer
frames run the slower way where the optimization matters most.

A choice is evaluated again before its interval is over when the scene or the view changed: when the frames' wall time
(a moving average over ~32 frames, in which one frame counts at most twice the average, so a compilation stall does not
move it) differs by 20 % from what it was at the decision, and at least every `autotune/max_interval_sec` seconds (the
interval's growth to 16 times 600 frames is otherwise up to minutes at low frame rates). Not sooner than half the base
interval after the decision (a moving view changes the frame time all the time); the interval then grows again from the
base. The tuner only switches an
optimization off for a frame; with the setting off it is never turned on.

Since `fellow68`: a change of the frame time brings a tunable's evaluation forward only after its recheck spacing (from
half the base interval, doubled each time such an evaluation keeps the choice, up to 16 base intervals; back to half the
base when it changes the choice), and the frames of an evaluation are left out of the moving average. In Sponza's camera
path the frame time kept changing by more than 20 %: 27-28 evaluations brought forward per run, every tunable each
time, none of which changed a choice, and 2529-2589 times per run of 7685 frames a tunable ran off in a frame (the
directional pyramid saves 14 % there). The candidate defaults (persistent instance data, async compute, autotune) were
1.2 % slower than `fellow67`'s defaults there (`m5_f69_gpu_cand`, 0/3 runs faster, GPU time +1.2 %), where async compute
alone gains 0.9 % and persistent instance data has 290 list elements per frame: most likely the evaluations' cost.

Since `fellow70`: each evaluation's pairs are pooled with those of the tunable's earlier evaluations in the same
conditions (frames within 20 % of the time they had when the pool started, and no other tunable's choice changed
since; at most 36 pairs, older ones weighed down beyond that), and the decision is taken on the pooled mean and
standard error. In the draw-heavy City Lab async compute costs about 3 % of the frame, close to what six pairs of
16-frame blocks of a noisy CPU-bound frame can resolve: `m5_f70_dh_cand` ran with async compute in 2028, 6282 and
10 195 of 10 868 frames in its three runs (frame 2.480, 2.551, 2.569 ms; the defaults' runs 2.553 ms on average).

`fellow68` against the same settings without autotune (`m5_f70_gpu_cand`, Sponza, 3 ABBA runs): +2.9 % frame (p 0.02),
+2.5 % GPU time, with no choice changed in two of the runs: the cost of the evaluations themselves. 24-26 evaluations
per 57-second run (four tunables, each evaluated after its interval, after 30 seconds and after a change of the frame
time), 192 frames each (96 with a tunable off): evaluations took about 60 % of the frames, and in Sponza an evaluation
of the directional pyramid runs its "off" blocks 14 % slower. Since `fellow70` the evaluations of all tunables together
take at most `autotune/max_duty_percent` (20) of the frames: after an evaluation of L frames the next one waits
4 L frames.

## Why it is exact

Each tunable is exact on its own (decision 0005: the pyramid only skips work whose result it proves; verified per
frame on lavapipe and RTX), and switching it between frames keeps that: a directional pyramid turned off is invalidated
and is rebuilt whole the next frame it runs. The timestamps are graph commands between draw lists. So every frame is
pixel-identical whatever the tuner chooses.

## Verification

* lavapipe, autotune off vs on with evaluations every 20-80 frames in blocks of 8 (so the pyramids are switched on and
  off across the compared frames; 45-223 frames per run with a pyramid off): Lights Lab walk (60 frames, both
  pyramids), PBR Lab (7 cameras), Stress Lab temporal (120 frames), City Lab (3 cameras): **0 differing pixels**. With
  async compute created at start-up and the frame plan on, all four tunables evaluated: Lights Lab walk, draw-heavy City
  Lab captures, PBR Lab: **0 differing pixels**; with the pyramids off (so that async compute is the one evaluated:
  47 of the 90 frames' async submissions left out): Lights Lab walk, draw-heavy City Lab: **0 differing pixels**.
* lavapipe with the Khronos validation layer (`--gpu-validation`), frame plan, parallel recording and autotune with
  frequent evaluations: draw-heavy City Lab captures, and under async compute the Lights Lab walk: no validation
  message.
* RTX (`fellow61`, pyramids only): `cpu/verify` 0 mismatches with evaluations every 40-640 frames (City Lab, Lights,
  Stress temporal; City, PBR, Blend, Bistro exterior captures; 48-342 frames with a pyramid off per run). Identity
  autotune vs off (`m4_f64_temporal`, six scenes and the draw-heavy `city_pan`, 48 frames per sequence with the
  pyramid off): Bistro exterior bit-identical (99 dB), every other scene at its floor.

## Result

`fellow61` (the two pyramids only; no early end of evaluations yet), between processes (ABBA; `m4_f64_*_auto`,
2026-10-07 05:00-06:10; reference: the defaults, autotune off):

| scene | autotune | directional pyramid off | autotune's choice (evaluations, frames with a pyramid off / all) |
|---|---:|---:|---|
| City Lab (4 runs) | **−4.4 %** frame (4/4), −5.1 % GPU | −4.5 % | pyramid off after the second evaluation (11 evaluations, 18 318 / 19 873) |
| draw-heavy City Lab (4 runs) | **−2.9 %** (p 0.01, 4/4), −3.4 % GPU | −2.6 % | off (9 evaluations, 17 726 / 18 072) |
| Sponza (3 runs) | −0.1 % (n.s.) | (+14 %, milestone 3) | kept on (6 evaluations, 576 / 7 685) |
| Bistro exterior (3 runs) | +0.5 % (n.s., 1/3) | | kept on (8 evaluations, 768 / 12 084) |
| Bistro interior (3 runs) | +0.4 % (n.s., 1/3) | | kept on (768 / 14 478) |
| Lights Lab (3 runs) | +1.0 % (n.s., 0/3) | | kept on (768 / 14 490) |
| PBR Lab (3 runs) | −1.2 % (n.s., 2/3) | | off (10 394-14 700 / 14 550-16 351) |

Autotune reaches the better static choice in both City Lab variants and keeps the pyramid where it pays. What it costs
where the pyramid stays on is its evaluations: 5-6 % of a 30-second run's frames ran with a pyramid off (the first
evaluations come early and the interval only grows to 9600 frames), +0.4 to +1.0 % in Bistro and the Lights Lab, none
of it significant in three runs. The early end of an evaluation (`f3ea954`) shortens exactly those evaluations.

`fellow62` (`4577fc0`: async compute and the frame plan as tunables, the most overdue tunable evaluated next; no
re-evaluation on change yet), ABBA between processes (`m4_f65_*_auto`, 2026-10-07 06:40-07:59; reference: the defaults,
async compute off, autotune off; `async` = async compute created at start-up and always used; `auto` = async compute
created at start-up + autotune):

| scene | async compute | async compute + autotune | autotune's choices |
|---|---:|---:|---|
| City Lab (4 runs) | −0.9 % (n.s., 1/4) | **−3.7 %** (4/4, p 0.06) | directional pyramid off, async compute off |
| draw-heavy City Lab (4 runs) | **+1.2 %** (0/4, p 0.05) | **−1.4 %** (4/4, p 0.02) | async compute off most of the time (2-4 flips per run) |
| Bistro exterior (3 runs) | **−2.4 %** (3/3) | **−1.9 %** (3/3, p 0.11) | everything kept on (one flip in one run) |
| Bistro interior (3 runs) | +0.2 % (n.s.) | +0.6 % (n.s., 0/3, p 0.19) | everything kept on; 1113 of 14478 frames with one tunable off |
| Lights Lab (3 runs) | −1.1 % (3/3) | −1.2 % (3/3, p 0.08) | kept on |
| Sponza (3 runs) | −0.9 % (3/3) | −1.0 % (3/3) | kept on |

GPU time follows the frame time in the GPU-bound scenes (Bistro exterior −2.1 %, Lights −0.9 %, Sponza −1.0 %, Bistro
interior +0.5 %). Verification of `fellow62` with async compute and frequent evaluations (`m4_f65_verify_*`): 0
`cpu/verify` mismatches, 0 async conflicts; identity against async compute alone (`m4_f65_temporal`, six scenes): at the
floor everywhere, City Lab and Bistro exterior bit-identical.

Async compute in Sponza ran in only 118-135 of 7685 frames in five of the six runs with it (7335-7345 in the other
two): `fellow_async_compute_begin()` skips the frame while any transfer worker is recording, and an upload nothing draws
yet keeps a worker recording (see 0015).

## Decision

(pending: async compute + autotune as the defaults, measured in all scenes with the re-evaluation of `6ea366a`)
