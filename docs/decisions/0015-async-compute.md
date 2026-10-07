# 0015 — Async compute: screen-space effects on a second queue

Status: **KEEP as an option** (`fellow38`-`fellow40`; off by default) · Branch `fellow/geometry` · Setting `rendering/fellow/frame/async_compute`
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
SDFGI/VoxelGI (they read the same normal buffer between the shadows), or for reflection probes. Uploads (transfer
workers): the prefix's submission submits and waits on those the work recorded so far uses, and the async part waits on
the prefix; the async part's own submission leaves the transfer workers and their texture barriers to the main queue's
next submission, unless the async part itself uses an upload not submitted yet or barriers of finished uploads are
pending (then they go with the async part, as before; `frame_async_transfers`). Until milestone 5 a frame did not use
async compute at all while any transfer worker was recording, and an upload nothing draws yet can keep one recording for
the rest of a run: in Sponza async compute ran in ~1.6 % of the frames of most runs (`m4_f65_gpu_auto`).

**Conflict check.** The concurrent part (step 3) runs at the same time as the async part, so it must not touch what the
async part uses. The render graph marks every resource the async part uses (with how); in step 3 a resource the async
part wrote, or uses with a different usage (layout), sets a conflict flag. On a conflict, step 3's submission also
waits on S2 — exactly the single-queue order its barriers assume — and the frame counts a conflict
(`frame_async_conflicts`); nothing is raced.

## Verification

* lavapipe (one queue: the second queue is a virtual queue on the same Vulkan queue, so the submissions run in order;
  this checks the split, the barriers across it and the moved recording), setting off vs on: PBR Lab 7 cameras, Lights
  Lab 60-frame walk, Stress Lab 4 cameras and both 60-frame sequences (moving objects, vertex-animated surface), Blend
  Lab 4 cameras: **0 differing pixels**; 92-366 async segments per session, **0 conflicts**.
* RTX 5070 Ti (`m3_f38_temporal`, six scenes, setting off vs on): Bistro exterior and Blend Lab 99 dB (bit-identical),
  the others at their run-to-run floor (the same values as `fellow35` vs `fellow38` with the setting off); 107-124
  async segments per run, **0 conflicts**.
* Milestone 5 (transfer workers left to the main queue): lavapipe with the Khronos validation layer, async compute
  (dedicated family) off vs on: Lights Lab walk (93 of 94 frames with an async part), Stress Lab temporal (120 frames,
  370 async parts), City Lab captures: **0 differing pixels**, 0 conflicts, no new validation message (Stress Lab
  shows stock's `srcAccessMask-02815` barrier message with the setting off as well).

## Result

Interim (`m3_f38_perf`, interrupted on 2026-10-04 at 20:20 at the user's request after the Sponza runs and one Bistro
exterior run): per-run pairs, setting on minus off, Sponza −0.19, +0.22, +0.15, +0.02, +0.14 ms (one more pair, −5.1 ms,
is a disturbed run), Bistro exterior +0.33 ms. No gain is visible: with a second queue of the main family the GPU does
not appear to run the two parts at the same time, and the extra submissions and the wait cost a little. Next: the
dedicated compute queue family (needs concurrent sharing or ownership transfers, and barriers restricted to compute
stages on that queue), or stop here.

### Dedicated compute family (`fellow39`, 2026-10-04)

The async part goes to the compute-only queue family (family 2 on the RTX); every image and buffer is created with
concurrent sharing between the main, compute and transfer families (no ownership transfers), and the barriers the
graph records into the compute queue's command buffers are restricted to compute stages and accesses (a source stage
of earlier graphics work becomes ALL_COMMANDS, which chains with the semaphore wait that already orders and makes
visible that work). RTX identity (`m3_f39_temporal`): compute family and main family both at every scene's floor,
Bistro exterior and Blend Lab bit-identical, 0 conflicts. A/B (`m3_f39_perf`, 4 ABBA runs, reference off):

| scene | off ms | compute family | Δ (paired) | pairs | GPU ms off → on | render thread ms off → on | sharing only |
|---|---:|---:|---:|---:|---|---|---:|
| Bistro exterior | 5.135 | 4.997 | **−2.7 %** (p 0.001) | 4/4 | 4.628 → 4.603 | 2.15 → 2.87 | −0.3 % |
| Sponza | 7.062 | 7.044 | −0.2 % (p 0.79) | 2/4 | 6.678 → 6.658 | 0.48 → 0.66 | −0.2 % |

The Bistro exterior frame gets faster but the GPU time hardly moves (−0.5 %): the gain is not two queues working at
once but the frame reaching the GPU in pieces (the pre-pass starts while the CPU still records the rest). The split
costs render-thread time (+0.2 to +0.7 ms: four flushes and submissions instead of one). Concurrent sharing alone
costs nothing measurable. `m3_f40_perf` separates the split from the concurrency (the same submissions on the main
queue itself, `async_compute_family = 2`).

### What the gain comes from (`fellow40`, `m3_f40_perf`, 2026-10-04, 4 ABBA runs, five scenes)

Three variants of one binary: off; async on the compute family; and the same split submissions on the main queue
itself (`async_compute_family = 2`: the same four submissions and semaphores, nothing concurrent).

| scene | off ms | async (compute family) | pairs | split only | GPU ms off / async / split | render thread ms off / async |
|---|---:|---:|---:|---:|---|---|
| Bistro exterior | 5.125 | **−2.1 %** (p 0.010) | 4/4 | −0.1 % | 4.62 / 4.62 / 4.73 | 2.12 / 2.88 |
| Bistro interior | 4.174 | **−1.3 %** (p 0.004) | 4/4 | −0.0 % | 3.80 / 3.74 / 3.80 | 0.66 / 1.01 |
| Lights Lab | 3.016 | **−1.0 %** (p 0.050) | 4/4 | +0.5 % | 2.67 / 2.64 / 2.68 | 0.21 / 0.36 |
| PBR Lab | 2.882 | −1.2 % (p 0.18) | 3/4 | +0.7 % | 2.47 / 2.49 / 2.49 | 0.30 / 0.50 |
| Sponza | 7.182 | −0.9 % (p 0.31) | 3/4 | +0.3 % | 6.79 / 6.73 / 6.81 | 0.49 / 0.83 |

The split alone is neutral or slightly slower (Bistro exterior: GPU time +0.11 ms, the batch boundaries leave the GPU
briefly idle); with the compute queue running the screen-space effects alongside the shadows, clusters and fog the
frame is faster in every scene (18 of 20 pairs). The gain is real concurrency, net of the split's cost.

Costs: the render thread spends 0.15-0.76 ms more per frame (four submissions instead of one; on lavapipe the
submissions themselves block, so the split cannot be timed there). None of these scenes is limited by the CPU, so it
does not show in their frame times; in a CPU-bound scene it would. Concurrent sharing of every image and buffer
(needed for the compute family) costs nothing measurable here (`m3_f39_perf`, sharing only: −0.3 % / −0.2 %).

### In a CPU-bound scene, with the submit thread (`fellow53`)

City Lab (CPU-bound: render thread 1.6-2.0 ms, GPU 1.8 ms), between processes (`m4_f53_city_perf`, 4 ABBA runs, vs
stock): candidate of 0016/0017 −14.8 %, + async compute −11.4 %, + submit thread −16.6 %, + both −14.2 %. In one
process (`m4_f53_city_ab_async`, under async compute): the submit thread takes the splits' submissions off the render
thread, −2.9 % frame. Async compute costs a CPU-bound frame about 3 % even then. Not in the graph replay: per frame in
City Lab the splits replay 0.18 ms and the frame's end 0.075 ms instead of 0.215 ms (+0.04 ms in total); their
submissions take 0.08 ms of the render thread, 0.016 ms with the submit thread (`m4_f53_city_perf` counters).

GPU-bound scenes (`m4_f53_gpu_perf`, 2 runs, frame vs the candidate without async compute): Bistro exterior −1.9 %,
PBR Lab −1.5 %, Sponza −0.8 %, Bistro interior −0.4 %, Lights Lab 0.0 %; the render thread +0.1 to +0.3 ms (hidden
there).

## Decision

**KEEP as an option, off by default.** Exact (bit-identical / at the floor on RTX in six scenes, 0 conflicts), and
1-2 % faster in GPU-bound frames on the RTX 5070 Ti. Not on by default because (1) it is measured on one GPU vendor
only (concurrent sharing may cost image compression on others, and the compute-only barriers are untested there),
(2) it adds render-thread time that a CPU-bound project would pay, and (3) it is a start-up setting. Without a
dedicated compute queue family it stays off (a second queue of the main family did not run concurrently and only
added the split's cost: `m3_f38_perf`, `m3_f40_perf` "split only").

Recommended for GPU-bound projects on NVIDIA: `rendering/fellow/frame/async_compute = true` in project.godot.
Follow-ups: a CPU-bound scene (10 k+ draws) to measure the render-thread cost, and fewer submissions (G2 and the rest
of the frame as two batches of one submission).
