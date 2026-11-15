# FellowEngine — milestone 120 report (asynchronous compute off by default; the driver's video memory budget)

Status: **DRAFT** (2026-11-15). Branch `fellow/geometry`. Milestone 119 placed the GPU's rare 21-23 ms frames in Sponza
streamed: all with asynchronous compute on (decision 0015, on by default on NVIDIA GPUs since `fellow71`).

## 0. Result

* **Asynchronous compute off by default** (decision 0241): measured again on four scenes with the passes timed by queue
  (job f461). On, the GPU's time is 1.5-3.7 % shorter in Bistro and Frontier Lab and no shorter in Sponza; and about once
  in two to five minutes where a scene uses it, the main queue waits 13-18 ms where it joins the asynchronous part — a
  missed frame at 60 Hz (Sponza: 8 in 30 minutes on, none in 15 off). A game wanting the throughput sets it.
* **The driver's video memory budget** (decision 0240): with FellowEngine on, `VK_EXT_memory_budget` where the driver has
  it (the RTX and lavapipe do); the memory allocator reads the operating system's budget and the process's usage every
  frame — FellowEngine's statistics and three new Performance monitors (`Video memory budget`, `Video memory used
  (driver)`, `Texture streaming budget`). `rendering/fellow/streaming/texture_budget_auto` (off): the streamed
  textures' budget follows it.

## 1. Asynchronous compute (decision 0241)

| scene | frame, on | frame, off | p99 on / off | long frames at the join, on | off |
|---|---:|---:|---:|---:|---:|
| Sponza streamed (30 min on, 15 off) | 6.13-6.21 ms | 6.15 ms | 8.63-8.74 / 8.55 ms | 8 | 0 |
| Bistro interior (3 min each) | 3.18 ms | 3.23 ms | 3.66 / 3.72 ms | 2 | 0 |
| Bistro exterior (3 min each) | 4.12 ms | 4.23 ms | 5.49 / 5.25 ms | 0 | 0 |
| Frontier Lab streamed (3 min each) | 3.90 ms | 4.06 ms | 5.24 / 5.43 ms | 0 | 0 |

Each long frame: the main queue's time after the volumetric fog 13-18 ms (0.02 ms otherwise), the asynchronous queue's
passes at their usual times, every thread of the process waiting on the fence.

## 2. The driver's video memory budget (decision 0240)

(pending)

## 3. Checks

(pending)

## 4. Next

(pending)
