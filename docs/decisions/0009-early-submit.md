# 0009 — Early submit: the frame's rendering no longer waits for the swap chain

Status: **REJECT, both experiments (`m3_pacing`, `m3_f21_pacing`)** · Branch `fellow/geometry` · Toggle
`rendering/fellow/frame/early_submit` (default **off** until measured) · Code: `servers/rendering/rendering_device.h/.cpp`
(`_fellow_submit_early`, `screen_prepare_for_drawing`, `_begin_frame`, frame setup/teardown), `fellow_settings.*`.
Benchmark harness: `run_benchmarks.py` variants with engine arguments (`^`) and start-up project settings (`%`,
written to the bench project's `override.cfg` for that run only).

## Problem

In every session since milestone 1 the frame time exceeds the GPU time of the rendered viewport by a nearly constant
0.37-0.44 ms, in every scene and configuration, and nvidia-smi reports 87-92 % GPU utilisation:

| session | scene, 1920×1080 | frame ms | viewport GPU ms | gap | render CPU ms |
|---|---|---:|---:|---:|---:|
| `godot-4.7.2_perf` (stock) | PBR Lab | 2.957 | 2.530 | 0.427 | 0.23 |
| | Sponza | 13.712 | 13.321 | 0.391 | 0.63 |
| | Bistro exterior | 5.962 | 5.579 | 0.383 | 1.43 |
| | Bistro interior | 4.529 | 4.161 | 0.368 | 0.59 |
| `m3_lc_perf` (FellowEngine) | Lights Lab | 3.379 | 3.046 | 0.333 | 0.17 |

The gap does not follow the CPU's recording time (0.2 ms in the PBR Lab, 1.4 ms in Bistro exterior: same gap) and
grows by only ~0.03-0.05 ms from 1920×1080 to 2560×1440. At 3-5 ms per frame it is 8-13 % of the frame rate.

The benchmark laptop (Acer Predator PHN16S-71, RTX 5070 Ti Laptop) drives its 2560×1600 panel from the Intel iGPU
(hybrid graphics, `hardware.display` in every session): every presented frame crosses from the NVIDIA GPU to the iGPU
and the desktop compositor. That is the common configuration of gaming laptops.

## Stock implementation

`RenderingDevice` records the whole frame (`RenderingDeviceGraph`) and submits it once, at the end, in
`swap_buffers()`. The swap chain image is acquired when the frame reaches the blit to the screen
(`screen_prepare_for_drawing()`, after every viewport is rendered), and the Vulkan driver attaches the acquisition's
semaphore to the **next submission**, i.e. the whole frame, with the wait at the colour-attachment-output stage
(`command_queue_execute_and_present`). So nothing of a frame can reach the GPU before the presentation engine has
handed out an image (if `vkAcquireNextImageKHR` blocks, the CPU waits before submitting anything), and on the GPU
every render pass with colour attachments — the depth pre-pass (normal/roughness), the opaque pass, sky, post
effects — waits on that semaphore. Godot already moves the screen blit into its own command buffer
(`split_swapchain_into_its_own_cmd_buffer`, "so that the whole rendering can start early"), but both command
buffers are submitted at the same point, after the acquisition, and the acquisition's semaphore lands on the first.

## Change

When the frame reaches the blit to the screen, before the image is acquired, everything recorded so far is
submitted (`_fellow_submit_early`: the graph is recorded into the frame's command buffer, which is ended and
submitted without a fence and without any acquisition semaphore). The rest of the frame — the blit and whatever
follows — is recorded into a second command buffer of the same frame (same command pool), which `swap_buffers()`
submits as before: it waits on the acquisition, signals the frame's fence (a fence signal covers every earlier
submission to the queue) and presents. The two command buffers are swapped back when the frame slot is reused
(after its fence). Vulkan only; the other drivers keep the single submission.

## Why it is exact

Same commands, same order, same queue. The render graph treats the split like a frame boundary: resource trackers
keep their usage and the following commands get the same transitions, and a pipeline barrier's first scope includes
every command submitted earlier to the queue. Only the time at which the GPU may start the frame changes.

## Verification

* lavapipe (960×540, the frame split active in every frame: `frame_early_submits` counts one per frame): PBR Lab
  7 fixed cameras, Stress Lab 4 fixed cameras (second viewport, reflection probe updated every frame, omni and spot
  shadows) and the Lights Lab 60-frame walk (TAA history, volumetric fog): **0 differing pixels** in every image.
* RTX 5070 Ti Laptop identity (`m3_f20_temporal`, 60-frame sequences, `fellow20` vs + early submit): Lights Lab
  95.4 dB (max |d| 3), Sponza 75.5 dB (max 3), Bistro interior and PBR Lab at their run-to-run floor — the same
  numbers as the null tests.

## Result: the hypothesis is wrong

`m3_pacing` (`fellow20`, 2 interleaved runs per variant; the split was active in every frame: 10-15 k early submits
per run):

| scene | variant | frame ms | viewport GPU ms | gap |
|---|---|---:|---:|---:|
| PBR Lab | FellowEngine defaults | 2.962 | 2.543 | 0.419 |
| | + early submit | 2.922 | 2.487 | 0.435 |
| | frame queue 3 | 2.943 | 2.529 | 0.414 |
| | 4 swap chain images | 3.020 | 2.569 | 0.452 |
| Bistro interior | FellowEngine defaults | 4.314 | 3.951 | 0.363 |
| | + early submit | 4.360 | 3.986 | 0.374 |
| | frame queue 3 | 4.392 | 4.020 | 0.372 |
| | 4 swap chain images | 4.463 | 4.092 | 0.370 |

Neither submitting the rendering before the acquisition, nor a third frame in flight, nor a fourth swap chain image
changes the gap (every frame-time difference is within noise, p ≥ 0.05). Whatever costs those ~0.4 ms per frame
does not wait on the acquisition or on the CPU running ahead: it is serial on the GPU queue after the frame
(the presentation path of the hybrid laptop — the copy to the iGPU and its synchronisation — is the remaining
suspect). Next experiment: present from a second queue (`rendering/fellow/frame/separate_present_queue`, a start-up
setting), so that whatever the driver does at presentation happens off the main queue.

## Decision

**REJECT** (off; the code stays behind the toggle for the separate-present-queue experiment, which it complements).

## Experiment 2: present from a second queue (`rendering/fellow/frame/separate_present_queue`, `fellow21`)

A start-up setting (the device is created before the rendering server's settings exist: project.godot or
override.cfg only). The Vulkan driver creates two queues per family instead of one, and `RenderingDevice` presents on
the second queue of the main family: the main queue's submission signals the frame semaphore (Godot's existing
separate-present path, until now used only where the main family cannot present), the present queue waits on it
and presents. The rendering is untouched (lavapipe: identical captures with and without, the split logged at
start-up). If the per-frame cost is something the driver serialises behind the present on the queue that presents,
the main queue no longer waits for it.

RTX (`m3_f21_pacing`, `fellow21`, 2 interleaved runs each; "separate queue" logged at start-up in every run):

| scene | variant | frame ms | viewport GPU ms | gap |
|---|---|---:|---:|---:|
| PBR Lab | defaults | 2.975 | 2.556 | 0.418 |
| | present from a second queue | 3.013 | 2.577 | 0.437 |
| | second queue + early submit | 2.998 | 2.664 | 0.335 |
| Bistro interior | defaults | 4.413 | 4.046 | 0.368 |
| | present from a second queue | 4.623 | **4.581** | **0.042** |
| | second queue + early submit | 4.651 | 4.605 | 0.046 |

The wait does not go away, it moves: presenting from the second queue, Bistro interior's gap vanishes and the same
time (more) appears inside the frame's own GPU time — the next frame's work on the main queue now waits for the
presentation of the previous one. Whatever the hybrid presentation path does (the copy to the iGPU and the
compositor's synchronisation), the GPU serialises it with the rendering, on whichever queue presents. Frame times:
+1 to +5 % (within noise with 2 runs). The interleaved A/B (`m3_f21_perf`, 3 runs each) confirms it in every
scene: Bistro interior +4.5 % (p 0.002), PBR Lab +3.1 %, Lights Lab +2.8 %, Bistro exterior +2.2 %, Sponza +1.7 %
(all p < 0.05, slower).

**REJECT** (off). The ~0.4 ms per frame is the presentation path of this hybrid laptop, not something the engine
waits for: it does not depend on the frame queue, the swap chain length, when the rendering is submitted, or which
queue presents. (On such laptops the system's dGPU-only display mode, where the panel is driven by the NVIDIA GPU, is
what removes it; that is a system setting, outside the engine and not changed here.)

