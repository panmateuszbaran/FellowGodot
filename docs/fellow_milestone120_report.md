# FellowEngine — milestone 120 report (asynchronous compute off by default; the driver's video memory budget; a GPU time line)

Status: **FINAL** (2026-11-15). Branch `fellow/geometry`. Milestone 119 placed the GPU's rare 21-23 ms frames in Sponza
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
passes at their usual times, every thread of the process waiting on the fence. Job f462 (the asynchronous part's start
reported): 4 more in 10 minutes on — three whose part began 12.9-17.7 ms into the frame instead of 1.3-1.9, one whose
part ran on time and whose finish reached the main queue 15 ms late — none off. The driver's and the operating system's
scheduling of two queues; Sponza's frame either way within the jobs' spread.

## 2. The driver's video memory budget (decision 0240)

With FellowEngine on the device asks for `VK_EXT_memory_budget` where the driver has it and the memory allocator reads
the operating system's budget and the process's usage every frame: FellowEngine's statistics (`memory_budget_bytes`,
`memory_budget_usage_bytes`, `memory_budget_from_driver`) and Performance monitors (`Video memory budget`, `Video
memory used (driver)`, `Texture streaming budget`). `rendering/fellow/streaming/texture_budget_auto` (off; a share): the
streamed textures' budget is that share of the driver's budget less what the process uses besides them, at least 64 MiB,
in 32 MiB steps, raised only by 64 MiB or more, capped by `texture_budget_mb`.

Lavapipe: the budget 8 024 MiB, the usage following the process; Streaming Lab at 0.2 held to the 64 MiB floor (its
cameras' textures fit). The RTX (Sponza streamed at 0.9): 8 192 MiB in force for 1.13 GiB of streamed textures, the frame
unchanged; at the end of a run the driver's budget 11 176 MiB, the process's usage 2 963 MiB.

## 3. A long GPU frame on a time line (decision 0242)

The debugger's FellowEngine Hitches tab (milestone 119) draws a selected GPU hitch's passes on a time line, a row per
queue, each pass reddening as it runs past its average, with tooltips; the frame's passes travel with the hitch's
message.

## 4. Checks

The regression suite (36 scenes, the release template against the editor build, lavapipe): the same as its goldens
(lavapipe has no asynchronous queue: the default's change does not show there); the editor check passes and reads the
new monitors; the class reference and the settings' descriptions regenerated.

## 5. Next

The fences late without FellowEngine's work (a frame step of 17-22 ms whose GPU frame was 6-7 ms, a few times in each
lane, async compute or not: the compositor or another program); hair for characters (the readiness assessment's open
item); a frame's passes on a time line for any frame, not only the long ones.
