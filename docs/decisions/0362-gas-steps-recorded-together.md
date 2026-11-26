# 0362 — The gas volumes' steps of a physics frame recorded together

Status: **KEEP** (exact; no frame time measurable on the RTX laptop) · Branch `fellow/geometry` · Code:
`servers/rendering/renderer_rd/effects/fellow_gas.{h,cpp}` (`SimOp`, `_flush_sim_batch()`, `_sim_buffer_update()`,
`_sim_texture_clear()`, `_sim_read_buffer()`, `_record_sim_op()`), `servers/rendering/fellow_settings.cpp`
(`rendering/fellow/fluid/gas_batch_steps`), `doc/classes/ProjectSettings.xml`.

## Problem

Milestone 217's open point: the rooftops' five gas volumes (the brazier, three chimneys, the osteria's fireplace)
recorded some 250-400 small compute dispatches a frame, each in its own compute list — each list a command of the render
graph, its resources tracked, its dependencies found, sorted and given barriers at the frame's end. Measured on
lavapipe (the cloud's 2 vCPUs; a scene of five 32-56-cell volumes, 385 dispatches a frame, the editor build timed
around `FellowGas::process_pending()` and `RenderingDeviceGraph::end()`): the steps' recording 1.0-1.15 ms a frame,
of it 0.37-0.44 ms building each pass's uniform list and finding its set, 0.44-0.56 ms in the RenderingDevice's calls;
the render graph's end 0.80-0.93 ms (0.15-0.20 ms without the volumes).

## Change

* **`rendering/fellow/fluid/gas_batch_steps`** (on): the volumes' steps of one physics frame are queued — each
  volume's passes and the buffer updates, clears and read-backs between them, in its order — then recorded together:
  the k-th operation of every queued volume at once, the passes in one compute list (the volumes share no resource a
  pass writes; `dummy_3d` and the samplers are only read), the others before it. Each volume's operations keep their
  order (its k-th in a list after its (k−1)-th's), so each volume computes the same thing: the render graph gets about
  80 compute lists for five volumes instead of 385. The burning objects' steps of the frame still come after the
  volumes' (they read the gas's temperature). Not while `gas_profile_passes` times the passes one by one.
* `_dispatch_sim()` builds its uniform list in a list it reuses (no allocation per pass).

## Measurements

**Exact**: on lavapipe five volumes — sparse and dense, SOR and multigrid, half and full precision, MacCormack on and
off, a log burning in one, one box moving — rendered after 150 frames with the setting on and off: the same PNG, byte for
byte. The regression suite: 41 of 41 scenes as their goldens.

The cloud's CPU (lavapipe, the five volumes, 720 frames twice, alternating; ms a frame):

| | recording the steps | the render graph's end |
|---|---|---|
| each pass its own list (off) | 0.88 | 0.88 |
| recorded together (on) | 0.82 | 0.74 |

(The uniform list reused: the recording 1.0-1.15 → 0.85-0.9 ms in both.)

The RTX 5070 Ti laptop, 1920x1080, `climb_look.gd --perf=1` (480 frames after 3 s), on and off, twice, alternating
(job f619):

| view | GPU ms, on | off | render thread's CPU ms, on | off |
|---|---|---|---|---|
| the square | 9.26, 9.61 | 9.36, 9.57 | 2.10, 2.07 | 2.18, 2.15 |
| the brazier | 8.85, 9.09 | 9.07, 9.12 | 1.86, 1.90 | 1.83, 1.79 |
| in the osteria | 9.06, 9.20 | 9.09, 9.18 | 2.70, 2.96 | 2.77, 2.98 |

On the laptop's CPU the difference is within the runs' spread: recording a pass costs it little, and the rooftops'
render thread spends its time elsewhere (milestone 219: the brazier's omni shadow was 0.2-0.5 ms of it).

## Decision

KEEP, on: exact, fewer commands for the render graph (0.2 ms a frame on a slow CPU with five volumes), the same on the
RTX laptop.

## Rejected / not done

* A set of uniform sets made once per volume (each pass's set kept for both buffers' parities): the uniform list
  reused already took most of that cost.
* The red-black sweeps' two colours in one dispatch: not the same Gauss-Seidel order across workgroups (not exact).
