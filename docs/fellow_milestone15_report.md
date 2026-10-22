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

## 3. SSR's resolve (0074, REJECT)

SSR's resolve reads each half-resolution sample (four textures) and prepares it (normal decoded and normalized,
roughness remapped, tone mapping inverted) once for each of the up to four pixels that use it. Sharing the prepared
samples per workgroup (`fellow164`): GPU −0.4 to −0.5 % in Sponza and the Bistros in one process, but the warm
sequences are not bit for bit on the RTX (Bistro interior max |d| 3; lavapipe was): stored to shared memory, the
prepared values are rounded where the compiler had kept them in registers or fused them. Sharing the reads only
(`fellow165`): bit for bit, and no measurable gain (−0.1 to −0.3 %, n.s.) — the pass is limited by its arithmetic, not
its reads. Rejected; with bit-for-bit runs on the RTX (milestone 14) such a difference is now visible in one job.

## 4. TAA's tile loaded by every invocation (0075, REJECT)

TAA's 8 × 8 workgroups load their 10 × 10 tile with 25 invocations, four entries each, while the other 39 wait at the
barrier. With all 64 loading in turn (at most two each; the same entries, the same values): the warm sequences bit
for bit on the RTX, the TAA pass −4 % in Bistro exterior and +5 % in Sponza, no scene's GPU time moved
(`m15_f220`). With decision 0023 (16 × 16 workgroups, slower) neither the group's size nor the sharing of its loads
is what limits the pass. Rejected.

## 5. A new scene: Crowd Lab (skeletal animation)

No benchmark scene had animated characters. Crowd Lab (`benchmarks/project/scenes/crowd_lab.gd`, `--scene=crowd`)
builds 256 procedural humanoids (52 bones each: spine, head, arms with five three-bone fingers, legs; 4992 vertices
and 8736 triangles; one shared mesh and Skin), each with its own Skeleton3D and AnimationPlayer playing one of four
looping clips with a rotation track per bone, walking along lanes under a sun with four cascades. The players are
in manual mode and seeked by `set_time()` (the bench's rule for anything animated); `--crowd=N` changes the count.

`m15_f222`, `m15_f224` (between processes, 3 runs each): stock 5.19-5.99 ms per frame, `fellow166` 5.40-6.11 ms, the
GPU 1.6-2.1 ms and the render thread 0.5-1.0 ms: the frame is the **main thread** animating the characters (about
4.5 ms), in stock's code. FellowEngine off from start-up, without async compute, without autotune: the same within
the runs' spread (5.5-5.9 ms); the separate render thread does not help either (5.9-6.0 ms: the main thread is the
frame). In one process (`m15_f223`) FellowEngine's render-thread work and persistent instance data each cost Crowd
Lab 1.5 % of its frame (render thread +0.08 to +0.10 ms: all 256 instances change every frame).

## 6. Skeletal animation without its known results (0076, KEEP)

A profile of Crowd Lab's main thread (headless with the dummy renderer, `perf` on a build with its symbol table)
found work whose result is known: every Skeleton3D saves and restores all its bones around its modifiers because it
always has one — the compatibility PhysicalBoneSimulator3D, active, with no physical bone to move (12 % of the
frame); slerp at weight 1 (one animation playing) still computes `acos()` and three sines to get the coefficients 0
and 1; each track searches for its last key and passes its value through a script hook nobody overrides. The three
shortcuts give the same bone poses bit for bit (every bone of every character hashed every 25 frames; lavapipe
captures 0 differing pixels). RTX, in one process: Crowd Lab's frame −4.9, −5.0 and −6.0 % each, **−15.8 %**
together (5.49 → 4.62 ms); between processes 6.11 → 5.10 ms (stock 5.99), 1 % low 116 → 131 fps. On from
`fellow168`, which also keeps the simulator's count of physical bones and looks each skeleton up once per run of its
tracks (`m15_f226`): −19.6 % in one process (5.46 → 4.39 ms), the warm sequence bit for bit on the RTX, and against
stock between processes 5.25 → **4.60 ms** (−12.3 %; 1 % low 117 → 132 fps).

## 7. Next

(pending)
