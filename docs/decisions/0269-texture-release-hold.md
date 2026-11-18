# 0269 — Streamed textures: Bistro's longer rare frames are its releases and reloads

Status: **MEASURED** (the default unchanged: a rare hang under investigation) · Branch `fellow/geometry` · Code:
`servers/rendering/renderer_rd/storage_rd/material_storage.cpp` and `fellow_settings.{h,cpp}` (FellowEngine's statistics:
`material_uniform_sets_created`) · Checks: Bistro exterior streamed on the RTX (`m145_f496`-`m145_f501`: 55 runs),
Streaming Lab on lavapipe (under Vulkan's core and synchronization validation too).

## Problem

Milestone 143 measured Bistro exterior's DDS textures streamed on the RTX (decision 0264): half the load time and
1.4 GB less video memory, but its rare long frames longer — the 99th percentile 5.3 → 7.4 ms, the render thread's own
time at its 99th percentile 2.4 → 3.8 ms, the largest main-thread step of each second 5.4 → 7.8 ms.

## What it is

* **Not the swaps' bursts.** A finished load takes its texture's place on the render thread (a swap), and the old
  data's uniform sets go with it: every material using the texture makes its set again (counted now:
  `material_uniform_sets_created`, about 5 600 a run — one for each swap or so). Swaps capped at 1 or 2 a frame
  (a setting tried, `streaming/texture_max_swaps_per_frame`, `m145_f498`): the 99th percentile 7.1-7.5 ms as with every
  swap in its frame. Removed.
* **Not the evaluation.** The streamer's evaluation of each instance's texel density (3.6 s of a run's render thread)
  halved with 512 instances a frame instead of 2 048 (`m145_f499`): the 99th percentile unchanged.
* **The churn.** In one 30-second flight the 320 streamed textures were loaded 4 800 times and released 2 200 times
  (`texture_release_hold_s` 2 s: a texture needing fewer mips gives them up two seconds later, and the camera needs them
  again a few seconds after). Released 10 s later (`m145_f499`-`f501`, 17 runs):

| Bistro exterior streamed (RTX, 1080p) | loads / releases | uniform sets made | frame | p99 frame | render thread p99 | video memory (peak) |
|---|---:|---:|---:|---:|---:|---:|
| releases held 2 s (as now) | 4 800 / 2 200 | 5 600 | 4.34 ms | 7.3 ms | 3.6 ms | 1 620 MB |
| **held 10 s** | **1 400 / 360** | **1 110** | **4.24 ms** | **5.9 ms** | **2.7 ms** | 1 670 MB |
| (whole, milestone 143) | — | — | 4.28 ms | 5.3 ms | 2.4 ms | 3 080 MB |

  Streaming Lab on lavapipe (its street flown at 320 × 180): loads 1 592 → 1 033, releases 531 → 262, uniform sets
  1 531 → 939; the textures' peak residency 302 → 339 MB (+12 %; at the end 195 → 339 MB) — a budget releases at once
  whatever the hold (`texture_budget_mb`: over it, nothing waits).

## The hang

Three of the 28 runs held 10 s hung (`m145_f499`, `f500`'s heat soak, `f501`): the main thread waiting for a frame's
fence that the GPU never signalled (`RenderingDevice::_stall_for_frame()` → `fence_wait()`), the GPU idle, FellowEngine's
present thread idle with nothing to issue, every worker thread idle (FellowEngine's hang reports, decision 0206: the
main thread's frames; their minidumps' stacks scanned with the linker map). None in the 27 runs held 2 s; with the
present and submit threads off and with large uploads not submitted on their own (`m145_f501`, 6 runs each) none either
— too few to tell. Streaming Lab streamed under Vulkan's validation on lavapipe: no message. A frame's submission
waiting on a semaphore that is never signalled is what leaves a GPU idle with its fence unsignalled; which one is next
(the stalled frame's waits and their origins reported when its fence is late).

## Decision

The cause found; `texture_release_hold_s` stays 2 s until the hang is understood (a hang is worse than 1.4 ms on one
frame in a hundred). The uniform sets' counter kept (FellowEngine's statistics); the swap cap rejected and removed.

## Rejected / not done

* **Capping swaps per frame** (`texture_max_swaps_per_frame`): the cost is per swap, not per burst; capped, fewer loads
  finish (the cap holds the streamer's job slots) and the long frames stay.
* **Fewer instances evaluated a frame**: the evaluation's time halves, the long frames stay.
* **Keeping the old data's uniform sets across a swap** (a texture's RD texture replaced in place): a Vulkan image's
  size and mips are its own; the sets must point at the new image.
