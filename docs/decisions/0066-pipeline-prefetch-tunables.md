# 0066 — Pipelines of autotune's other choices compiled ahead

Status: **REJECT** (removed: `a8da05a`, reverted in `4662427`) · Branch `fellow/geometry` · Setting
`rendering/fellow/frame/pipeline_prefetch_tunables` (was: runtime, off).

## Problem

Autotune (0020) switches exact optimizations on and off in blocks of frames to measure them. Three of them are
specialization constants of the colour passes' pipelines: the directional and positional shadow pyramids' early-outs
(0005) and light tile culling (0049). The first frame that uses a combination of these bits with a material, a
vertex format and the frame's other bits (0057's absent features) compiles that pipeline; until it is ready the draw
uses the ubershader.

* Stress Lab compiles 40-88 specialization pipelines during every final's 20 s measurement (`m12_f178_final`,
  `m11_f168`), after a warm-up that compiled 70; stock compiles none after its warm-up. Its runs draw 129-3964 draws
  with the ubershader during the measurement.
* Warm sequences (`m13_f180_floor`, each sequence run once before the captured run): FellowEngine compiles 2-50
  specialization pipelines during the captured 60 frames (Sponza 50, PBR Lab 9, Bistro interior 7, Lights Lab 2) with
  autotune on, none with autotune off (`m13_f187_floor_noat`).

The evaluations land in scene states the warm-up did not combine them with.

## Change

When a colour pass (opaque, motion, transparent) asks for a specialized pipeline that is not compiled yet, it also asks,
at low priority, for the pipelines with each bit autotune may switch in that pass flipped (the pyramids where their
settings and autotune are on; light tile culling in the opaque pass where it can run this frame). An evaluation that
switches one bit then finds its pipelines compiled. Nothing changes in what is drawn with which pipeline once it is
compiled: exact.

The prefetched pipelines count as specialization compilations in the bench's counters.

## Verification

lavapipe, Stress Lab captures (960 × 540, 4 cameras, the setting off against on):

* The run with prefetching **did not exit**: after its captures the main thread waited forever in
  `Main::cleanup()` → `WorkerThreadPool::exit_languages_threads()`, which waits until every worker is idle with both
  task queues empty, while low-priority pipeline tasks stayed in the pool's low-priority queue and the workers slept
  (backtrace from a relinked binary with symbols). Low-priority tasks are only promoted when another low-priority task
  ends or a pool thread waits; this pool's accounting left them unclaimed. Low priority was the point (not to compete
  with the pipelines the frame needs).
* Images: 1 pixel off by 1 level in one capture (the run drew 2114 draws with the ubershader against 574: on two
  threads the prefetched pipelines delayed the ones the frame needed); the captures run no autotune evaluation, so
  there was nothing for the prefetch to win.

## Decision

**REJECT, removed.** At low priority it can hang the engine's exit; at normal priority it competes with the
compilations the frame needs, and it compiles up to four pipelines for one wherever autotune never switches the bits.
What it was for (no ubershader frames when an evaluation switches a pipeline bit in a new scene state) stays open: the
pipelines would have to be requested when an evaluation starts, for the materials then in view, or the evaluation
could wait until its first frames compiled nothing (0064 already leaves such frames out of the measurement).
