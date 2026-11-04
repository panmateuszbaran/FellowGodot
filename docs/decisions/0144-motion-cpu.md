# 0144 — Neural motion's CPU work off the main thread

Status: **KEEP** (the same motion: within 1e-5 m of the previous code over 12 s; the same images within 1 level) ·
Branch `fellow/geometry` · Module: `modules/fellow_motion` (`fellow_motion_service`, `fellow_motion_rig`,
`fellow_motion_character_3d`, `fellow_motion_model`) · Test: `tests/fmb_cpu_profile.gd`.

## Problem

Milestone 43 measured the GPU's side of neural motion; the CPU's was left on the main thread. Each character, every
frame, interpolated all 77 joints of its segment (51 of them the reference pose in every frame), shifted its 32-pose
history by a 69 KB `memmove`, transformed all 77 joints to world space for its heading and allocated scratch arrays;
and every result it received was decoded into poses and endpoint-corrected on the main thread (58 frames × 77 joints,
two quaternion slerps each). Motion Lab, 16 characters in flush mode, cloud CPU (`fmb_cpu_profile.gd`): the
characters' `advance()` took 24 µs a character in a frame without results and 54 µs in a frame with them — for 64
characters 1.5-3.5 ms of the main thread a frame.

## Change

* **The service encodes, decodes and corrects.** A request carries the four context poses and the four target poses
  (moved, not copied); the service encodes them, runs the batch, decodes each prediction into poses and corrects its
  endpoints — each step spread over the WorkerThreadPool, one task per request. Asynchronous, none of it runs on the
  main thread; in flush mode it runs in `flush_requests()` on all cores. The model's own output decoding (the
  normalisation, the root's integration, the 6D retargeting) is spread the same way. The character takes the
  finished frames (moved).
* **The endpoint correction's slerps from the identity** (`Quaternion().slerp(q, t)`, two per joint and frame) with
  each joint's angle worked out once: two trigonometric calls instead of five; the end's correction skipped when the
  target is root-only (it is the identity).
* **The joints the model drives.** Interpolation, crossfading and the skeleton's update cover the hips and the 25
  other joints the model drives (its four finger-tip end effectors do not move the fingers: the hands keep a relaxed
  pose); the other 51 joints keep the reference pose they have in every frame, their bones posed once. Below the hips
  only the rotations change, so only they are set.
* **No per-frame allocation or copying:** the history is a ring; the heading comes from the joints on the way to the
  legs only (`FellowMotionRig::heading_of_local`), the on-the-ground test from those on the way to the head
  (`hips_head_heights`); the crossfade's pose is a member.

## Verification

* One character's 12 s schedule on lavapipe (flush, deterministic), against the previous code: the joints within
  8.3e-6 m at every step (float rounding of the new slerp; the same tokens and segments); CHARACTER OK with the same
  speeds and heights. Its images at 2, 4 and 6 s: 2-6 pixels differ, by 1 level.
* GPU parity unchanged (the shaders are the same).

## Results

Motion Lab, 16 characters in flush mode for 6 s at 60 Hz (`tests/fmb_cpu_profile.gd`; lavapipe, the cloud's two
CPU cores), the characters' `advance()` in a step (the steering and the flush apart; the median of the steps of each
kind):

| step | before (`cpu_base`) | after (`cpu_new4`) |
|---|---:|---:|
| no request, no result | 0.385 ms (24.1 µs a character) | **0.135 ms (8.4 µs)** |
| a request made (1.4 a step) | 0.425 ms (26.6 µs) | **0.238 ms (14.9 µs)** |
| a result taken (1.4 a step) | 0.859 ms (53.7 µs) | **0.216 ms (13.5 µs)** |
| every step: median / 90th percentile | 0.611 / 1.901 ms | **0.204 / 0.369 ms** |

The decoding and correction now run on the worker threads: 0.23 ms a batch (1.4 requests) in `flush_requests()`,
spread over the cores; asynchronous (8 characters, 20 s, `crowd_async`), 1.07 ms a batch of 4.1 on the worker
threads, none on the main thread. Step by step (median of every step / 90th percentile): `cpu_new` (decoding moved,
the ring, partial transforms) 0.238 / 0.468 ms, `cpu_new2` (the driven joints only) 0.215 / 0.366, `cpu_new3` (the
encoding moved) 0.210 / 0.419, `cpu_new4` (moves, the context) 0.204 / 0.369.

The laptop's CPU (Core Ultra 9 275HX; Motion Lab in flush mode for 10 s, `m44_f325`, fellow243 = milestone 43
against fellow244), the characters' `advance()` a step:

| characters | milestone 43: median / 90th percentile | milestone 44 | the flush a step (mean; the GPU's batch, now with the encoding and decoding) |
|---|---:|---:|---:|
| 16 | 0.235 / 0.523 ms | **0.069 / 0.142 ms** | 2.08 → 2.16 ms |
| 64 | 0.888 / 1.453 ms | **0.212 / 0.313 ms** | 4.49 → 4.81 ms |

With 64 characters the main thread's share of a flush-mode step falls from 5.44 to 5.04 ms (the encoding and decoding,
0.24 ms a batch, now spread over the cores inside the flush); asynchronous, the 0.7 ms the characters saved leave the
main thread entirely.

Motion Lab on the RTX, 1080p, 2 runs each, milestone 43's fellow243 against fellow244 alternating in one session
(`m44_f325_motion`, 02:31-02:42, the laptop presumably idle), the mean frame:

| Motion Lab | fellow243 | fellow244 | change |
|---|---:|---:|---:|
| 16 characters, flush | 3.47 ms | 3.49 ms | = (the frame waits for the GPU's batch) |
| 64 characters, flush | 8.45 ms | **7.88 ms** | −6.7 % |
| 64 characters, asynchronous | 4.28 ms (99th percentile 10.4) | **3.59 ms (5.7)** | −16 % (the 99th percentile −45 %) |

32 characters for 40 s with fellow244 (`cz44_getup`): 15 falls of 9 characters, all back up (17 get-ups), no one down
for more than 3 s.

## Decision

**KEEP.**
