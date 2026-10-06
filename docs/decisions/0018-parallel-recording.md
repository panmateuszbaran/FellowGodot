# 0018 — Parallel recording of draw lists

Status: **MEASURING** · Branch `fellow/geometry` · Toggle `rendering/fellow/cpu/parallel_recording` (runtime, default
off until measured) · Code: `servers/rendering/rendering_device.{h,cpp}` (`fellow_draw_list_fork`,
`fellow_draw_list_segment_begin/end`, `fellow_draw_list_join`, `FellowDrawSegment`),
`servers/rendering/rendering_device_graph.{h,cpp}` (`RenderingDeviceGraph::FellowDrawSegment`,
`fellow_draw_segment_set`, `fellow_draw_segment_append`), `servers/rendering/renderer_rd/pipeline_hash_map_rd.h`
(`fellow_find_pipeline`), `forward_clustered/render_forward_clustered.{h,cpp}` (`_fellow_render_list_parallel`,
`_fellow_record_chunk`, `_render_list_serial`), `fellow_settings.*`.

## Problem

Decisions 0016 and 0017 moved the building of the render lists and the replay of the render graph onto worker threads.
What stays on the render thread in a draw-heavy frame is the recording itself: `_render_list_template()` walks the
sorted list and calls the `RenderingDevice::draw_list_*` functions for every element (pipeline, uniform sets, vertex and
index arrays, push constant, draw), which append instructions to the render graph's draw list. In the draw-heavy City
Lab (`--city_materials=3000`, one material per lot's walls) the depth pre-pass and the opaque pass take 0.43 and 0.38 ms
of the render thread (`m4_f51_dh_profile`), the largest single items left.

Godot 4.0-4.2 recorded large lists on several threads (split draw lists into secondary command buffers); 4.3 removed it
with the render graph, which records every draw list instruction into one buffer and is not thread-safe.

## Change

A large render list (384 draws or more, at least 128 draws per segment; a run of instanced repeats counts as one draw) is recorded by the render thread and the worker
tasks of FellowEngine's shared loop (decision 0016) at once:

* **Fork** (`RenderingDevice::fellow_draw_list_fork`): the open draw list gets N segments. Each has its own draw list
  state (pipeline, uniform sets, vertex and index arrays, all starting as nothing bound) and its own instruction buffer
  and usage list in the render graph.
* **Segments**: segment *i* records the elements `[start_i, start_i+1)` (equal numbers of runs) with the stock template,
  on whichever thread takes it; the `draw_list_*` functions and the graph's `add_draw_list_*` functions write to the calling thread's
  segment (thread-local). Segment starts are run starts (the element before ends its run of instanced repeats): the
  serial loop visits every run start, so a segment records exactly the draws the serial loop records for its elements.
* **Join** (render thread, in segment order): the segments' instructions are appended to the draw list, their usages
  added (deduplicated as before), and the work that touches shared state done (shared-texture updates and pending
  clears of the uniform sets bound, transfer-worker waits of the vertex and index arrays): in serial recording that
  work happens at the bind or draw that needs it, while the draw list is open; either way before the draw list's
  command is added to the graph. The draw list's state after the join is what the last segment to set each part of it
  set, over the state before the fork, with every uniform set marked for binding again.
* **Pipelines**: the segments look pipelines up in the map without changing it. If one is not there yet (being compiled
  in the background, or never requested), the list is recorded again serially, which requests the compilation and waits
  for the ubershader as stock does (`cpu_record_fallbacks`). Nothing a discarded segment recorded reached the draw list
  or the device's state.
* Thread safety of the rest: `uniform_set_is_valid` without the device lock (`get_or_null` is lock-free); the mesh
  surface's vertex array lookup (a spin lock per draw) is reused while consecutive elements use the same surface;
  `redraw_request()` is called by the render thread after the join.

* **Replay in the recorded segments** (with parallel replay, decision 0017): the graph keeps where the appended
  segments start. A list recorded in parallel, with nothing recorded after its segments, is replayed into secondary
  command buffers split at those starts: each segment already binds what its draws use, so only the dynamic state set
  before the first segment (viewport, scissor, blend constants, line width) is replayed at the start of the others.
  The replay's two passes over the whole list (counting the draws, finding the state in effect at each split) are
  skipped (`frame_replay_recorded_lists`). A segment that sets dynamic state itself, or instructions recorded between
  two forks or after the last one, send the list back to the usual replay.

## Why it is exact

Each segment starts with nothing bound, so before its first draw it binds the pipeline, the uniform sets, the vertex
and index arrays and the push constant that draw uses: the same values the serial recording has in effect there (they
are what the template computes for that element). The instruction stream differs from the serial one only by these
re-binds of values already bound; every draw sees the same state, in the same order, in the same render pass.

## Verification

* lavapipe (2 worker threads: 3 segments per list), all CPU toggles off vs + parallel recording: draw-heavy City Lab
  3 cameras (312 lists in 936 segments, 6 lists recorded again serially in the first frames while pipelines compiled),
  Lights Lab, Stress Lab, PBR Lab: **0 differing pixels**. With the whole candidate (0016's toggles, hot workers with
  the gate off, present and submit threads, parallel replay) + parallel recording vs the CPU toggles off: draw-heavy
  City Lab `city_pan` (60 frames; 371 lists recorded, 374 replayed in parallel) and, under async compute, the Lights
  Lab walk: **0 differing pixels**.
* RTX (`fellow54`, recording by elements, with the submit thread): `cpu/verify` 0 mismatches (City Lab temporal: 502
  lists recorded in 5487 segments, all replayed in their segments; draw-heavy captures 793 lists; Bistro exterior
  captures 1169 lists). Identity (`m4_f54_temporal`, six scenes, and the draw-heavy `city_pan`): Bistro exterior
  bit-identical, the others at their floor (the same values as off vs off).

## Result

Draw-heavy City Lab, in one process (`m4_f54_dh_ab`, 2 runs, 480 pairs, reference: the candidate of 0016/0017):

| configuration | frame | render thread | depth pre-pass ms | opaque pass ms | shadows ms |
|---|---:|---:|---:|---:|---:|
| candidate | 3.625 ms | 2.427 ms | 0.454 | 0.396 | 0.171 |
| + parallel recording | **−15.5 %** | **−23.0 %** | 0.211 | 0.178 | 0.102 |
| + parallel recording + submit thread | **−17.0 %** | −22.9 % | 0.211 | 0.180 | 0.103 |

Between processes, draw-heavy City Lab (`m4_f54_dh_perf`, 4 ABBA runs, vs stock 4.7.2):

| variant | frame ms | Δ | render thread ms | Δ | pairs |
|---|---:|---:|---:|---:|---:|
| stock | 4.239 | | 3.198 | | |
| candidate | 3.495 | −17.5 % | 2.439 | −23.7 % | 4/4 |
| + parallel recording | **3.008** | **−29.0 %** | **1.933** | **−39.5 %** | 4/4 |
| + recording + submit thread | 3.019 | −28.8 % | 1.988 | −37.8 % | 4/4 |
| + recording + submit thread + async compute | 3.031 | −28.5 % | 2.342 | −26.8 % | 4/4 |

City Lab (instanced: ~310 draws, but lists of thousands of elements): `fellow54` counted elements, so its lists were
recorded in parallel too, for nothing: +3.0 % frame in one process (`m4_f54_city_ab`), −17.1 % instead of −19.6 % vs
stock between processes (`m4_f54_city_perf`). From `936b789` the threshold counts draws (runs): lists under 384 draws
stay serial (lavapipe: City Lab 1 list recorded, draw-heavy 281).

## Decision

(pending)
