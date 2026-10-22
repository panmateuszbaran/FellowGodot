# FellowEngine — milestone 15 report (frame pacing on a busy machine)

Status: **DRAFT** (`fellow163`-). Branch `fellow/geometry`. Benchmark machine as before (Acer Predator PHN16S-71, Core
Ultra 9 275HX: 8 performance and 16 efficient cores, RTX 5070 Ti Laptop GPU, NVIDIA GPU only, Windows 11, 1920×1080,
"high" preset). Decision records `docs/decisions/0073`-; user-facing summary `docs/FELLOWENGINE_OPTIONS.md`.

## 0. Result

(pending)

## 1. The question

Milestone 14's final (`m14_f213_final`) found City Lab's frames of 6-50 ms back for every FellowEngine build, where
milestone 13's final had none: 0, 33, 30 frames per run for `fellow162` (1 % low down to 124 fps), 12-30 for milestone
13's `fellow158`, 0-8 for stock. The code had not changed; the machine had. This milestone asks what FellowEngine
needs from the machine for its frames to come out evenly, and gives it less.

## 2. Six worker tasks (0073, KEEP)

`m15_f214_city_pacing` (five runs each, interleaved): the defaults 0-53 frames over 6 ms per run; without the present
thread 11-66; without the submit thread 4-41; without hot workers 0-22; without the render thread's parallel work 1-9;
stock 0-1. Each such frame is one `vkQueuePresentKHR` of 5-45 ms that the render thread waits for. Ten worker tasks and
the render thread busy at once leave the machine's 8 performance cores without one for the threads the presentation
needs (the system's, the driver's, the compositor's), the more so when anything else runs.

`m15_f215_city_workers`: with 6 worker tasks 0-1 frames over 6 ms per run and 1 % low 243-275 fps (10: 13-40 and
98-184; stock 0-3 and 192-233); 8 is not enough (0-32), 4 no better than 6; idle hot workers at the lowest priority do
not help (5-47). `m15_f216` (between processes, every scene): City Lab 1 % low 117-281 → 246-289 fps, its mean frame
+1.3 % (n.s.); no scene's mean frame moves (−1.2 to +2.4 %, n.s.).

`cpu/worker_tasks` is automatic from `fellow163`: half the logical processors less two, at least 1, at most 6 (6 on
this machine). `m15_f217_city` (`fellow163` built with it, five runs each, interleaved, right after the build): `fellow163` 36,
10, 0, 0, 0 frames over 6 ms (1 % low 120, 236, 285, 289, 309 fps), `fellow162` 43, 1, 20, 24, 54 (90-255), stock 0-1
(195-247); mean frame −4.4 % against `fellow162` (n.s., 4 of 5 pairs). The first two rounds followed the build at once
(the new executable was probably still being scanned); from the third on, none.

## 3. Next

(pending)
