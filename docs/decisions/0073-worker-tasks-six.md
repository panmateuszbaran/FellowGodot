# 0073 — Six worker tasks for the render thread's parallel loops

Status: **KEEP** (from `fellow163`) · Branch `fellow/geometry` · Setting `rendering/fellow/cpu/worker_tasks` (-1:
automatic; 10 before) · Code: `fellow_settings.*`.

## Problem

City Lab, limited by the CPU, has frames of 6-50 ms in some sessions with FellowEngine and almost never with stock.
Milestone 13 (0065) traced them to the hot workers spinning through the frame and shortened their linger from 300 µs
to 30 µs: none in milestone 13's final. They came back on 2026-10-22 for every FellowEngine build (`m14_f213_final`:
`fellow162` 0, 33, 30 per run, 1 % low 124-296 fps; `fellow158` 12-30; stock 0-8). Each is one `vkQueuePresentKHR`
of 5-45 ms that the render thread then waits for.

`m15_f214_city_pacing` (five runs each, interleaved): the defaults 0-53 frames over 6 ms per run; without the present
thread 11-66; without the submit thread 4-41; **without hot workers 0-22 (four runs of five 0-2)**; without the render
thread's parallel work 1-9; stock 0-1. The machine has 8 performance cores (Core Ultra 9 275HX: 8 P + 16 E, no SMT);
ten worker tasks and the render thread busy at once leave the system's and the driver's threads that presentation
needs without a free performance core, the more so when something else runs on the machine.

`m15_f215_city_workers` (five runs each, interleaved):

| worker tasks | frames over 6 ms per run | 1 % low (fps) | mean frame against 10 |
|---|---|---|---|
| 10 (default) | 38, 22, 38, 40, 13 | 98-184 | |
| 8 | 1, 0, 32, 0, 23 | 125-296 | +0.3 % (n.s.) |
| **6** | **0, 1, 0, 0, 1** | **243-275** | +2.5 % (n.s.) |
| 4 | 1, 0, 0, 0, 1 | 248-279 | +4.5 % (n.s.) |
| 10, idle at the lowest priority | 11, 18, 6, 5, 47 | 102-223 | +1.8 % (n.s.) |
| stock | 0, 1, 3, 0, 0 | 192-233 | +36 % |

## Change

`cpu/worker_tasks` -1 by default: half the logical processors less two, at least 1 and at most 6 — 6 on the benchmark
machine (24 logical processors). A project can still set a number (0: the render thread alone).

## Measurement

`m15_f216` (`fellow162`, 6 worker tasks against its default 10, between processes):

| scene | runs | mean frame, 6 against 10 | frames over 6 ms per run, 10 / 6 | 1 % low (fps), 10 / 6 |
|---|---|---|---|---|
| City Lab | 5 | +1.3 % (n.s.) | 16, 32, 9, 23, 0 / **0, 0, 0, 2, 0** | 117-281 / **246-289** |
| Stress Lab | 5 | +1.6 % (n.s.) | (its script's frames: 75-153 / 67-250) | 125-158 / 126-156 |
| Bistro exterior | 3 | +0.5 % (n.s.) | | 148-163 / 154-168 |
| Bistro interior | 3 | −0.8 % (n.s.) | | 216-254 / 247-251 |
| Lights Lab | 3 | 0.0 % (n.s.) | | 270-313 / 273-320 |
| PBR Lab | 3 | −1.2 % (n.s.) | | 242-328 / 280-336 |
| Foliage Lab | 3 | 0.0 % (n.s.) | | 221-230 / 226-237 |
| Sponza | 3 | +2.4 % (n.s., noisy) | | 107-125 / 105-120 |

## Verification

Which thread runs a chunk changes, never what a chunk does (the loops' results are joined in order): exact. lavapipe,
City Lab captures with 10 against the automatic count: 0 differing pixels.

## Decision

**KEEP: automatic, 6 here.** City Lab's 1 % low from 117-281 to 246-289 fps (stock 192-233) with its mean frame
within the runs' spread; no scene's mean frame moves. On machines with fewer cores the count falls with them.
