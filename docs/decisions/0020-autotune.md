# 0020 — Autotune: exact optimizations chosen by measurement

Status: **MEASURING** · Branch `fellow/geometry` · Toggles `rendering/fellow/autotune/enabled` (runtime, default off
until measured), `autotune/block_frames` (16), `autotune/interval_frames` (600), `autotune/min_gain_percent` (0.5) ·
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

Tunables: the directional and the positional shadow pyramids (`shadows/directional_max_mip`,
`shadows/positional_max_mip`, each when it is on), one evaluated at a time. The tuner only switches an
optimization off for a frame; with the setting off it is never turned on.

## Why it is exact

Each tunable is exact on its own (decision 0005: the pyramid only skips work whose result it proves; verified per
frame on lavapipe and RTX), and switching it between frames keeps that: a directional pyramid turned off is invalidated
and is rebuilt whole the next frame it runs. The timestamps are graph commands between draw lists. So every frame is
pixel-identical whatever the tuner chooses.

## Verification

* lavapipe, autotune off vs on with evaluations every 20-80 frames in blocks of 8 (so the pyramids are switched on and
  off across the compared frames; 45-223 frames per run with a pyramid off): Lights Lab walk (60 frames, both
  pyramids), PBR Lab (7 cameras), Stress Lab temporal (120 frames), City Lab (3 cameras): **0 differing pixels**.

## Result

(to be measured: `fellow61`)

## Decision

(pending)
