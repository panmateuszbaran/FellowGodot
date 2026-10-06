# 0019 — Frame plan for the parallel replay

Status: **MEASURING** · Branch `fellow/geometry` · Toggles `rendering/fellow/frame/replay_frame_plan` (runtime, default
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
  walk (60 frames), PBR Lab (7 cameras): **0 differing pixels**. (Further results below as they come.)

## Result

(to be measured: `fellow57`)

## Decision

(pending)
