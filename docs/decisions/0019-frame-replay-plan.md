# 0019 — Frame plan for the parallel replay

Status: **KEEP as an option (off)** · Branch `fellow/geometry` · Toggles `rendering/fellow/frame/replay_frame_plan` (runtime, default
off until measured), `rendering/fellow/frame/replay_plan_min_draws` (16) · Code:
`servers/rendering/rendering_device_graph.{h,cpp}` (`_fellow_plan_replay`, `_fellow_plan_execute`,
`_fellow_split_draw_list`, `_fellow_assign_secondaries`), `servers/rendering/fellow_parallel.{h,cpp}`
(`begin_async`, `help`, `finish`), `fellow_settings.*`.

## Problem

At the end of the frame the render graph replays its commands into the primary command buffer (`RenderingDeviceGraph::
end()`): barriers, compute lists, copies and the draw lists, one after the other, on the render thread. With the
parallel replay (0017) a large draw list (192 draws or more, or recorded in parallel, 0018) is split into segments that
the workers replay into secondary command buffers, but the render thread still stops at each such list until its
segments are done, and every other draw list is replayed inline. In the RTX measurements of `fellow54` the replay of the
draw lists (`rd_graph_draw_nsec`) took 94 µs per frame in City Lab (12 lists, none large enough) and 355 µs in the
draw-heavy City Lab (12 lists, 3.6 of them replayed in parallel): the largest part of the graph's end (214 and 471 µs).

## Change

Once the graph's commands are sorted and before the primary command buffer is recorded, `_fellow_plan_replay()` walks
the sorted commands and splits every draw list worth replaying in secondary command buffers:

* the large ones as before (0017: segments of equal numbers of draws, each starting with the state set before it; 0018:
  the segments they were recorded in);
* the others with at least `frame/replay_plan_min_draws` draws as one segment (the whole list: it sets all of its state
  itself); smaller lists, lists with subpasses or secondary command buffers of their own, and lists that start a new
  command buffer stay inline.

All the segments of the frame (in the order the lists will be recorded) are published as one asynchronous loop of the
shared worker pool (`FellowParallelFor::begin_async()`), and the render thread goes on recording the primary command
buffer. At a planned draw list it begins the render pass for secondary command buffers, waits until that list's segments
are done (running segments nobody has taken yet itself meanwhile), executes them and ends the render pass. Lists left
out of the plan are replayed inline. The loop is closed (`finish()`) when the primary command buffer is complete.

Only while the parallel replay itself runs (`frame/parallel_replay`: CPU-bound frames, hot workers' gate), Vulkan.

## Why it is exact

Each secondary command buffer holds the same commands, in the same order, that the inline replay records into the
primary command buffer for that part of the list, preceded by the state commands in effect at its start (dynamic state
is not inherited by secondary command buffers, so it is set again; the values are the same). The primary command buffer
is the same as with the parallel replay of 0017, with more render passes made of secondary command buffers. The render
pass and framebuffer of a planned list are looked up before the primary command buffer is recorded instead of during;
the lookup (`_get_draw_list_render_pass_and_framebuffer`) is a cache keyed by the list's load and store operations, which
the replay does not change.

Thread safety: each segment has its own command pool and buffer; the render thread records only the primary command
buffer while the workers record; a segment's completion is published (acquire/release counter) after its
`vkEndCommandBuffer`, and its buffer is executed only after that.

## Verification

* lavapipe, frame plan off vs on (defaults otherwise; lavapipe's gate never closes): City Lab captures (545 of 1103
  draw lists from the plan), draw-heavy City Lab captures (498 lists, 281 of them recorded in parallel), Lights Lab
  walk (60 frames), PBR Lab (7 cameras), Blend Lab (4 cameras), Stress Lab temporal (120 frames), and under async
  compute the Lights Lab walk and draw-heavy City Lab captures: **0 differing pixels**.
* RTX (`fellow57`, gate off, plan on): `cpu/verify` 0 mismatches in every scene (City Lab temporal 839 planned lists,
  Bistro exterior captures 2033 lists in 13286 segments, draw-heavy City Lab captures 1371 lists in 8530 segments).
  Identity plan vs off (`m4_f57_temporal`, six scenes, draw-heavy `city_pan`, captures): Bistro exterior bit-identical
  (99 dB, max |d| 0), every other scene at its floor (the same values as off vs off).

## Result

In one process (gate off, so every frame uses it; 2 runs, 480 block pairs per configuration, reference: plan off):

| session | min draws 4 | 16 | 64 |
|---|---:|---:|---:|
| `m4_f57_dh_plan_ab`, draw-heavy City Lab, frame | −2.4 % (p 0.04) | **−2.7 %** (p 0.01) | −2.3 % (p 0.04) |
| `m4_f57_city_plan_ab`, City Lab, frame | +0.3 % | 0.0 % | −0.2 % |
| `m4_f57_city_plan_ab`, City Lab, GPU time | +0.3 % (p 0.04) | +0.4 % (p 0.02) | +0.1 % |

The render thread waits 49 µs per frame for planned lists in the draw-heavy City Lab (4 µs in City Lab): its replay
goes on, it is only ordered differently. City Lab's twelve lists are too small for the plan to matter (the graph's end,
209 µs per frame, is spread over 130 commands: barriers 51 µs, sort 14 µs, draw lists 70 µs).

Minimum draws for a list to be planned (`m4_f61_dh_plan_ab`, draw-heavy City Lab, in one process): 16 / 64 / 128
draws −2.1 / −2.3 / −2.2 % frame, GPU time +0.4 / +0.5 / +0.7 %; Bistro exterior (`m4_f61_bistro_plan_ab`, gate off,
GPU-bound): +0.2 / +0.1 / +0.1 % frame, from the GPU time of the secondary command buffers (in real use the gate turns
the replay off there).

Between processes (`fellow59` defaults vs defaults + plan, ABBA): City Lab 2.321 vs 2.325 ms (`m4_f61_city_perf`, 5
runs), draw-heavy City Lab 2.531 vs 2.526 ms (`m4_f61_dh_perf`, 4 runs): **no difference** (−0.2 % and +0.2 %).

## Decision

**KEEP as an option, off** (`frame/replay_frame_plan`): exact and verified, but the −2.7 % it gives the draw-heavy City
Lab in one process does not show between processes, and in City Lab it adds a little GPU time (more render passes made
of secondary command buffers). The asynchronous loop of `FellowParallelFor` stays (no cost when unused).
