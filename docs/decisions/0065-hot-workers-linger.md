# 0065 — Hot workers that do not spin through a CPU-bound frame

Status: **KEEP** (`fellow156`: `cpu/hot_workers_usec` 30 by default, was 300) · experiments left in, off:
`cpu/hot_workers_yield`, `cpu/hot_workers_idle_priority` · Branch `fellow/geometry` · Code: `fellow_parallel.{h,cpp}`
(`_hot_helper`), `fellow_settings.*` (stats `cpu_hot_yields`, `cpu_hot_lowered`).

## Problem

City Lab's runs with FellowEngine have had frames of 16-45 ms since milestone 10: 22-183 frames over 6 ms per run,
1 % low 54-203 fps, against none for stock (1 % low 212-239 fps). The GPU time and the render thread's time of those
frames are normal. Milestone 11 put them down to the machine's load.

## Evidence

* The excess time of a run matches the render thread's waits for the present thread and the time spent in
  `vkQueuePresentKHR` (`m12_f178_final`, every City Lab run). Per frame (`m13_f186_perframe`, `--fellow_frames=1`):
  each spike is one `vkQueuePresentKHR` call of 6-45 ms; the mean call takes 0.42-0.46 ms (0.1 ms with FellowEngine
  disabled).
* Not the present or submit threads (`m13_f185`: both on 32-57 spikes per run, both off 2-129, present thread only
  0-143), not async compute, four swap chain images or a frame queue of three (`m13_f186`), not the frame rate (paced
  to 330 fps: 0-104; to 420 fps: 42-113; `m13_f188`, a diagnostic pacing of the main thread).
* Only in runs whose frames are limited by the CPU (the render thread waits on fences for 0.03-0.17 s of a run; runs
  limited by the GPU, 1.3-2.1 s of fence waits, have none) — the condition in which the hot workers run
  (`cpu/hot_workers_gate`).
* None with the hot workers not spinning (`cpu/hot_workers_usec` 0: twelve runs over `m13_f188`, `m13_f189`,
  `m13_f191`, 0 frames over 6 ms, 1 % low 280-313 fps) and almost none with the render thread's parallel work off
  (`cpu/enabled` off: 1-3 per run).
* With 12 processes spinning beside it (`m13_f181_load12`): 1 frame per run.

With a linger of 300 µs the hot workers (ten by default) stay after each loop of a CPU-bound frame of 2 ms, and the
loops follow one another, so they spin through the whole frame. The machine has eight performance cores. A thread the
presentation needs then waits for a core until a scheduler quantum ends.

## Variants (City Lab, four runs each unless noted)

| hot workers | frames over 6 ms per run | 1 % low (fps) | mean frame |
|---|---|---|---|
| 10, linger 300 µs (before; `m13_f189`, `m13_f190`, `m13_f192`, twelve runs) | 5-109 | 56-254 | 1.97-2.18 ms |
| yield the core every 64 pauses (`m13_f189_city`) | 24-95 | 60-171 | 2.04-2.08 |
| idle at the lowest thread priority, with the yield (`m13_f191_city`) | 0-44 | 127-350 | 1.97-2.06 |
| the same without the yield | 0-49 | 107-370 | 1.83-2.06 |
| 8 workers (`m13_f190_hot`, three runs) | 29-78 | 79-137 | 1.96-2.02 |
| 6 workers (three runs) | 0-8 | 259-339 | 2.01-2.04 |
| 4 workers (three runs) | 0-67 | 72-327 | 2.00-2.09 |
| linger 100 µs (three runs) | 97-136 | 50-63 | 2.04-2.26 |
| linger 60 µs (`m13_f192_linger`) | 0-112 | 55-339 | 2.01-2.05 |
| **linger 30 µs** (`m13_f190_hot`, `m13_f192_linger`, eight runs) | **0-1** | **325-358** | 1.94-2.11 |
| no hot workers (twelve runs) | 0 | 280-313 | 2.18-2.40 |
| stock | 0-1 | 212-239 | 2.98-3.23 |

In one process, autotune off, in blocks of 30 frames (the spikes counted in the means):

* `m13_f189_ab`: the yield +7.9 % on City Lab's frame, no hot workers +17.2 %; Bistro exterior and interior (limited
  by the GPU: the hot workers gated off) unchanged.
* `m13_f191_ab`: the idle priority +3.6 %, no hot workers +15.8 %; Bistro exterior unchanged.
* `m13_f192_ab`: linger 60 µs +8.2 %, 30 µs +7.0 %, none +13.4 % in City Lab (render thread +4.8 % at 30 µs);
  Stress Lab −4.8 %, −5.3 %, −6.9 % (its frame is limited by the main thread's script, which the spinning workers
  slowed).

## Change

`cpu/hot_workers_usec` is 30 by default (`fellow156`). A hot worker leaves 30 µs after its last chunk (or the last
loop's start): the loops of a burst (the cull, the lists) still find their workers spinning; between bursts the cores
are free, and the next burst posts its helpers again. The yield and the idle priority are left in as experiments, off.
Which thread runs a chunk changes, never what a chunk does: exact.

## Decision

**KEEP: linger 30 µs by default.** City Lab's 1 % low goes from 56-254 to 325-358 fps (stock 212-239) with its mean
frame between processes unchanged (in one process +7 %); Stress Lab −5 %; nothing changes where the GPU is the limit.
Confirmed in the milestone 13 final (`m13_f193`).
