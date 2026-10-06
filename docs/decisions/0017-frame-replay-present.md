# 0017 — The end of the frame: parallel replay of draw lists, present thread

Status: **MEASURING** (`fellow51`) · Branch `fellow/geometry` · Toggles `rendering/fellow/frame/parallel_replay`,
`rendering/fellow/frame/present_thread` (runtime, default off until measured) · Code:
`servers/rendering/rendering_device_graph.{h,cpp}` (`_fellow_run_draw_list_parallel`, `_fellow_replay_segment`),
`servers/rendering/rendering_device.{h,cpp}` (`FellowPresentThread`, `_fellow_present_async`, `_fellow_present_wait`),
`servers/rendering/rendering_device_driver.h` and `drivers/vulkan/rendering_device_driver_vulkan.{h,cpp}`
(`fellow_command_queue_present`, `fellow_supports_parallel_replay`), `fellow_settings.*`.

## Problem

Decision 0016 shortened the render thread's scene work (cull, render lists). What the render thread does after the
scene is recorded was not measured: Godot records the frame into its render graph (`RenderingDeviceGraph`) and, in
`RenderingDevice::_end_frame()`, replays the whole graph into the frame's command buffer (barriers, then every draw
list instruction becomes a `vkCmd*` call), then submits and presents (`_execute_frame()`). New counters
(`rd_graph_end_usec`, `rd_execute_usec`, `rd_fence_wait_usec`, `rd_frames`) on the RTX laptop, per frame:

| scene (`m4_f50_*`) | graph replay ms | submit + present ms | waits for the GPU ms |
|---|---:|---:|---:|
| City Lab (CPU-bound, ~310 draws, instanced) | 0.19 | 0.10 | 0.05 |
| Bistro exterior (GPU-bound, ~2 600 draws per pass) | **1.38** | 0.09 | 1.37 |
| Bistro interior | 0.41 | 0.07 | 2.9 |
| Sponza | 0.40 | 0.08 | 6.0 |
| Lights Lab | 0.14 | 0.07 | 2.4 |
| PBR Lab | 0.19 | 0.08 | 2.2 |

In a draw-heavy scene the replay costs as much as half of the scene's own recording (Bistro exterior: render thread
2.1 ms, replay 1.4 ms more), all on the render thread, after the frame's recording and before its submission. Where
the GPU is the limit (Bistro) this is hidden; in a CPU-bound draw-heavy scene it is frame time.

## Change

* **Parallel replay** (`frame/parallel_replay`): a draw list with 192 or more draws is split into up to 12 segments of
  ~96 draws. A pre-pass over its instructions (no driver calls) records, for each segment start, the instructions that
  set the state in effect there: the last pipeline, vertex buffers, index buffer, viewport, scissor, blend constants,
  line width and push constant, and the last instruction that bound each descriptor set index. Worker threads
  (FellowEngine's shared loop, decision 0016) record each segment into its own secondary command buffer (one command
  pool per buffer, per frame slot, reused after the slot's fence): first those state instructions, in their original
  order, then the segment's instructions. The render pass is begun with secondary-command-buffer contents and executes
  the segments in order. Lists with subpasses or secondary command buffers of their own are replayed inline, as are
  smaller lists.
* **Present thread** (`frame/present_thread`): the frame is submitted signalling its semaphore (as Godot does for a
  separate present queue) and `vkQueuePresentKHR`, which waits on it, is issued by a dedicated thread through a
  presentation-only driver entry (no pending memory flushes, no acquisition semaphores: nothing the render thread uses
  meanwhile). The render thread goes on with the next frame; it waits for the presentation to have been issued before
  it acquires, resizes or frees a swap chain (Vulkan synchronizes swap chains externally) and before it hands over the
  next one.

## Why it is exact

Parallel replay: every draw sees the same pipeline, descriptor sets, buffers, dynamic state and push constants as
inline (the state instructions replayed at a segment start are the ones whose effect is in force there, in their
original order, so even the descriptor set compatibility and push constant rules see the same sequence); the draws
are executed in the same order within the same render pass instance. Present thread: the same submissions and the
same presentation of the same image; only which thread calls the driver, and when, changes.

## Verification

* lavapipe: City Lab draw-heavy variant (`--city_materials=3000`: the walls of each lot use one of 3000 materials;
  245 draw lists replayed in parallel over a capture run, 9.4 segments each) three cameras and the 60-frame `city_pan`
  sequence, Lights Lab (positional and cube shadows), Stress Lab (second viewport, reflection probe, MultiMesh), PBR Lab,
  with every CPU toggle of 0016, the present thread and parallel replay on: **0 differing pixels** against FellowEngine
  with them off.
* RTX: `m4_f51_verify`, `m4_f51_temporal` (pending).

## Result

(pending: `m4_f51_city_ab`, `m4_f51_city_perf`, `m4_f51_drawheavy_*`, `m4_f51_gpu_ab`)

## Decision

(pending)
