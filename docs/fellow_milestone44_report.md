# FellowEngine — milestone 44 report (neural motion: the CPU side and the tensor-core products; readiness for AAA)

Status: **measured** — lavapipe and the cloud's CPU, then the RTX 5070 Ti Laptop (`m44_f325` to `m44_f333`,
2026-11-04, at night). Branch `fellow/geometry`. Decision records `docs/decisions/0144-motion-cpu.md` and
`docs/decisions/0145-motion-gemm-vec4.md`; assessment `docs/fellow_aaa_readiness.md`.

## 0. Result

* The characters' main-thread work in Motion Lab is a third to a quarter of what it was (16 characters on the cloud's
  CPU: the median step 0.611 → 0.204 ms, the 90th percentile 1.901 → 0.369 ms; 64 on the laptop's: 0.888 → 0.212 ms):
  the service now encodes the requests and decodes and corrects the results on the worker threads (off the main thread
  when asynchronous), and a character's frame touches only the 26 joints the model drives, with no allocation or
  69 KB history shift. The same motion (within
  8.3e-6 m over 12 s) and the same images (within 1 level).
* The tensor-core products 13-22 % faster from 2 requests at once on (16: 15.23 → 12.60 ms, 64: 51.55 → 40.95 ms on
  the RTX): the activations loaded four at a time (vec4) and the epilogue worked out a row at a time — the same
  results bit for bit. Six other kernel designs measured and not kept (packed stores, wide tiles, smaller tiles, two
  shared-memory stages, per-subgroup epilogues, more split-K).
* Motion Lab on the RTX with both (`m44_f333`, milestone 43's build alternating with this one): 64 characters
  8.52 → 7.33 ms a frame in flush mode (−14 %), 4.30 → 3.56 ms asynchronous (−17 %, the 99th percentile 11.6 →
  6.1 ms); 16 characters 3.51 → 3.43 ms. 32 characters for 40 s: 15 falls, all back up, no one down for 3 s.
* An assessment of FellowEngine's readiness for AAA production after 44 milestones: not AAA-ready as an engine — a
  renderer and simulation close to AAA-class on Windows/Vulkan/NVIDIA, with streaming, platforms, tools, animation,
  upscaling and validation breadth the gaps (`docs/fellow_aaa_readiness.md`).

## 1. Development

* `tests/fmb_cpu_profile.gd`: Motion Lab's characters stepped at 60 Hz in flush mode, the steering, the characters'
  `advance()` and the flush timed separately per step.
* Where the time went (16 characters, cloud CPU): 24 µs a character a frame without results (interpolating 77
  joints, the 69 KB history `memmove`, world transforms of all 77 joints for the heading, scratch allocations), 54 µs
  in a frame with results (each decoded into poses and endpoint-corrected: 58 frames × 77 joints, two slerps each).
* In order (each measured, `cpu_new` to `cpu_new4`): the decoding and the endpoint correction moved to the service
  and spread over the worker threads, the correction's slerps with each joint's angle worked out once, the model's
  output decoding spread too, the history a ring, partial world transforms for the heading and the on-the-ground test
  (`cpu_new`); only the joints the model drives interpolated and posed, the positions below the hips set once
  (`cpu_new2`); the encoding moved to the service too (`cpu_new3`); requests moved instead of copied, the context
  interpolated for the driven joints only (`cpu_new4`).
* The tensor-core products (decision 0145): the activations' A tile loaded as `vec4` through a read-only vec4 view of
  the activation buffer where rows and K are 4-aligned (`gemmd16/32/64`, on where the subgroup is 32 or less;
  `FELLOW_MOTION_GEMM=scalar` the diagnostic for the scalar loads); the epilogue's addresses and mask a row at a time
  in shared memory instead of per element. Tried and removed, each measured on the RTX with the same results:
  packed shared stores (`m44_f327`, no change), 128 columns a workgroup (`f328`, 2× slower), 32 or 16 rows a workgroup
  (`f329`, no consistent change), two shared-memory stages (`f330`, 14-30 % slower), each subgroup's own epilogue
  (`f331`, 2× slower), split-K to 192 workgroups (`f326`, no gain).

## 2. Verification

* One character's 12 s schedule on lavapipe (flush), each step against the previous code: the joints within 8.3e-6 m
  (the new slerp's rounding; the same tokens and segments), CHARACTER OK with the same speeds; `cpu_new3` and
  `cpu_new4` bit for bit `cpu_new2`'s motion.
* Images (640 × 360, the mannequin at 2, 4 and 6 s) against the previous code: 2-6 pixels differ by 1 level.
* Asynchronous (8 characters for 20 s on lavapipe): 62 requests in 15 batches decoded on the worker threads, no one
  down for long (1 fall), a clean exit.
* The products: the RTX's results for the clip requests on tensor cores and in fp32 bit for bit milestone 43's
  (`m44_f333`); on lavapipe (no cooperative matrices) every shader version compiles at load and the fp32 products'
  results are bit for bit milestone 43's.

## 3. Performance

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

The tensor-core products (`fmb_gpu_run.gd`, fp16 weights, the clip requests at batches of 1 to 64, the medians of
runs 2-12; milestone 43's kernel from `m44_f326`, where it ran alternating with the vec4 loads, the final code from
`m44_f333`, 03:22; the laptop presumably idle both times):

| requests at once | milestone 43's kernel | milestone 44 | change |
|---|---:|---:|---:|
| 1 | 4.12 ms | **4.03 ms** | −2 % |
| 2 | 5.09 | **4.44** | −13 % |
| 4 | 6.46 | **5.57** | −14 % |
| 8 | 9.37 | **7.76** | −17 % |
| 16 | 15.23 | **12.60** | −17 % |
| 32 | 26.65 | **20.88** | −22 % |
| 64 | 51.55 | **40.95** | −21 % |

The GPU's own time at 16 requests 12.59 → 9.52 ms. In steps (`m44_f326`, `f332`): the vec4 loads −10 to −19 % from 2
requests on, the row-at-a-time epilogue a further −6 % at 32 and 64 (−1 % at 8 and 16). The fp32 products (GPUs
without cooperative matrices; unchanged): 1 request 5.50 ms, 16 26.28, 64 82.28 — 1.4× the tensor cores' time at 1,
2.0-2.1× from 16 on.

The milestone's final code in Motion Lab (`m44_f333_motion`, 1080p, 2 runs each, milestone 43's fellow243 against
fellow252 alternating, 03:22-03:33, the laptop presumably idle), the mean frame (the 99th percentile):

| Motion Lab | milestone 43 | milestone 44 | change |
|---|---:|---:|---:|
| 16 characters, flush | 3.51 ms (8.1) | **3.43 ms (7.6)** | −2 % |
| 64 characters, flush | 8.52 ms (12.7) | **7.33 ms (10.7)** | −14 % |
| 64 characters, asynchronous | 4.30 ms (11.6) | **3.56 ms (6.1)** | −17 % (the 99th percentile −48 %) |

In flush mode the frame waits for the batch (64 characters: 2.6-2.7 requests a batch, 5.73 → 5.17 ms with the
encoding and decoding); asynchronous, the service's batches take about 41 ms either way, and in them it now answers
28.4 requests instead of 23.4 — 22 % more a second. 32 characters for 40 s with fellow252 (`cz44f_getup`): the same
as fellow244's run — 15 falls of 9 characters, 17 get-ups, no one down for 3 s.

## 4. Readiness for AAA

`docs/fellow_aaa_readiness.md`: a scorecard of 14 areas (frame time, lighting, environment, geometry, streaming,
shader compilation, animation, physics, threading, upscaling, platforms, tools, middleware, validation), what an AAA
project would hit first (memory and streaming, platforms, GPU breadth, 4K and upscaling, animation, team scale), what
FellowEngine is ready for now (PC indie and AA games with a high-end look) and a path in seven steps.

## 5. Next

* The products: an epilogue that stores straight from the accumulators, fewer and larger dispatches for a batch of one
  (a request is 251 dispatches, 13 µs each on average at a batch of one — most of them near a dispatch's fixed cost), fp16 activations.
* Neural motion beyond locomotion (crouching, jumping, interacting: target poses from a gameplay layer), uneven
  ground, and a GPU without cooperative matrices measured (the fp32 path 1.4-2.1× slower on the RTX).
* From the assessment: the first AAA blockers are not neural motion's — measurement on AMD and Intel GPUs, texture and
  world streaming, an upscaler interface (`docs/fellow_aaa_readiness.md`, section 5).
