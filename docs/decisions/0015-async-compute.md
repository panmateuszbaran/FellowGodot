# 0015 — Async compute: screen-space effects on a second queue

Status: **in test** (`fellow38`) · Branch `fellow/geometry` · Setting `rendering/fellow/frame/async_compute`
(start-up only, default **off**) · Code: `rendering_device.{h,cpp}` (`fellow_async_compute_begin/end/join`),
`rendering_device_graph.{h,cpp}` (conflict tracking), `drivers/vulkan/rendering_device_driver_vulkan.cpp` (second
queue of the family), `forward_clustered/render_forward_clustered.{h,cpp}` (`_pre_opaque_render`), `fellow_settings.*`.

## Problem

Between the depth pre-pass and the opaque pass, Forward+ runs two independent chains one after the other on the
GPU's single queue:

* the shadow maps (raster: vertices, depth writes), the shadow pyramids, the light cluster bake and the volumetric
  fog — 0.3-0.6 ms in Sponza and Bistro exterior with FellowEngine's cached cascades;
* the screen-space effects, which need only the pre-pass (depth and normal-roughness) and the previous frame: depth
  downsample, SSAO, SSIL, SSR and their blur/interleave passes — 0.9-1.1 ms in the same scenes (`m3_f33_profile`).

Neither reads what the other writes; the opaque pass needs both. The first chain is bound by rasterisation and memory
writes, the second by texture fetches and ALU: on a GPU that can run work from two queues at once they can overlap.

## Change

With the setting on (read at start-up: the Vulkan device then creates two queues in each family), the screen-space
effects are recorded first and submitted to a **second queue of the main queue family** (no queue-family ownership
transfers: exclusive sharing is per family), while the main queue renders the rest:

1. `fellow_async_compute_begin()`: the work recorded so far (pre-pass and everything before it) is submitted to the
   main queue and signals semaphore S1.
2. The screen-space effects are recorded (stock's block, unchanged, moved before the shadows) and
   `fellow_async_compute_end()` submits them to the second queue, waiting on S1 (all stages) and signalling S2.
3. Shadows, pyramids, cluster bake, fog are recorded as usual and `fellow_async_compute_join()` submits them to the main
   queue, without waiting.
4. The next main-queue submission (the opaque pass and the rest of the frame) waits on S2.

Each submission is a render-graph flush (as in early submit, 0009): resource trackers carry their state across, so the
barriers and layout transitions are the ones a single submission would have, split at those points. Not used with
SDFGI/VoxelGI (they read the same normal buffer between the shadows), for reflection probes, or when a transfer worker
has uploads in flight.

**Conflict check.** The concurrent part (step 3) runs at the same time as the async part, so it must not touch what the
async part uses. The render graph marks every resource the async part uses (with how); in step 3 a resource the async
part wrote, or uses with a different usage (layout), sets a conflict flag. On a conflict, step 3's submission also
waits on S2 — exactly the single-queue order its barriers assume — and the frame counts a conflict
(`frame_async_conflicts`); nothing is raced.

## Verification

* lavapipe (one queue: the second queue is a virtual queue on the same Vulkan queue, so the submissions run in order;
  this checks the split, the barriers across it and the moved recording), setting off vs on: PBR Lab 7 cameras, Lights
  Lab 60-frame walk: **0 differing pixels**; 210 and 92 async segments, **0 conflicts**. Stress Lab, Blend Lab: pending.
* RTX: pending (`m3_f38_temporal`).

## Result

Pending (`m3_f38_perf`, `m3_f38_profile`).

## Decision

Pending.
