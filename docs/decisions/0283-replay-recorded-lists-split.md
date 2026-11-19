# 0283 — Lists recorded in parallel replayed as any other (the RTX's rare hang)

Status: **PENDING the RTX** (`f515`; off by default from milestone 151) · Branch `fellow/geometry` · Code:
`servers/rendering/rendering_device_graph.cpp` (`_fellow_split_draw_list()`), `fellow_settings.{h,cpp}`
(`frame/replay_recorded_segments`) · Checks: the RTX's jobs `m147_f510`, `m151_f512`, `m151_f514`; lavapipe (a
scene of 1 200 transparent meshes, the image regression suite's draw-heavy scenes).

## Problem

Decision 0270's hang: about one run in 20 of Bistro exterior streamed (releases held 10 s) on the RTX, the GPU stops
inside a command of the frame and its fence is never signalled. Milestone 147's checkpoints caught it once inside
command 69, a draw list. `f510` (milestone 151, 40 runs) caught it three times, each inside command 69: a draw list at
1920 × 1080, about 64 KB of commands, recorded on worker threads. In Forward+'s frame that is **the transparent pass**,
after the opaque pass (65), the sky (66) and the screen and depth textures' copies (67, 68). It was never the opaque
pass, which is recorded the same way.

Two of FellowEngine's parallel paths meet in such a list:

* `cpu/parallel_recording`: the render thread's helpers record a long list in segments (decision 0016).
* `frame/parallel_replay`: the graph replays a long list into secondary command buffers, recorded by worker threads
  (decision 0017). A list recorded in segments is replayed in those segments. Each segment is trusted to bind its own
  pipeline, uniform sets, buffers and push constant before its first draw, and is given only the dynamic state set
  before the first one. Any other list is split by its draws, and each part starts with the state the instructions
  before it set.

| job (40 runs each) | parallel recording | parallel replay | hangs | frame | render thread | p99 |
|---|---|---|---:|---:|---:|---:|
| `m147_f510` | on | on (recorded lists in their segments) | **3** | 4.47 ms | 1.47 ms | 6.32 ms |
| `m151_f512` | off | on (every list split by its draws) | 0 | 4.56 ms | 2.07 ms | 7.14 ms |
| `m151_f514` | on | off (every list replayed whole) | 0 | 4.47 ms | 1.43 ms | 6.01 ms |

The commands recorded in segments do not hang the GPU when they are replayed whole (`f514`). Lists split by their draws
do not hang it either (`f512`). Only lists replayed in their recorded segments did. A check of every recorded segment's
first draw against the state set before it found nothing missing on lavapipe: a scene of 1 200 transparent meshes, 24
lists recorded in parallel. So the fault is not identified; the replay of recorded segments is avoided.

## Change

`frame/replay_recorded_segments` (off): a list recorded in parallel is split by its draws for the replay, as any other
list is, each part starting with the state the instructions before it set. On restores the replay in its recorded
segments. The same commands in the same order: the same images.

## Checks

* Lavapipe: the scene of 1 200 transparent meshes. Its 24 recorded lists are replayed split (`frame_replay_recorded_lists`
  0, 348 segments in 29 lists, against 98 in their recorded segments). The image regression suite's draw-heavy scenes
  (`instances`, `crowd`, `stress`, `geometry`, `materials`, the render thread's and the crowd groups' crowds) are
  identical.
* The RTX (`f515`, pending): Bistro exterior streamed as `f510`, with this build's defaults, 60 runs. If the hang were
  as frequent as in `f510`, 60 runs without one would happen once in 100; at one in 20 runs, once in 20.

## Decision

Off from milestone 151, pending `f515`. `f512` and `f514` already show that neither parallel path alone hangs.

## Rejected / not done

* **Parallel replay off** (`f514`: the same frame, the render thread 0.04 ms shorter in Bistro). It is measured faster
  in the CPU-bound labs (decision 0017); it stays.
* **Parallel recording off** (`f512`): the render thread 0.6 ms longer, p99 0.8 ms worse.
