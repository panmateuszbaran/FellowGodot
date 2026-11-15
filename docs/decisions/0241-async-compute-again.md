# 0241 — Asynchronous compute measured again: its rare stalls

Status: **CHANGE**: `rendering/fellow/frame/async_compute` off by default (it was on for NVIDIA GPUs) · Branch
`fellow/geometry` · Code: `servers/rendering/fellow_settings.cpp`, `servers/rendering/rendering_device.cpp`,
`drivers/vulkan/rendering_device_driver_vulkan.cpp` (the default when the project does not set it) · Checks: jobs f460,
f461 (the RTX 5070 Ti Laptop, Windows, NVIDIA's driver; autotune off, the hitch sampler on at 16.7 ms).

## Problem

Decision 0015 put the screen-space effects (SSAO, SSIL, SSR) on the GPU's dedicated compute queue beside the shadows,
the light clusters and the fog, on by default on NVIDIA GPUs, autotune deciding per scene. It was measured in milestones
3-5 on frame averages. Decision 0237's GPU side of the hitch sampler found, in Sponza streamed, a GPU frame of 18-24 ms
about once in two and a half minutes (the frames 6 ms otherwise): the main queue waiting 13-17 ms after the volumetric
fog, where it joins the asynchronous part, every thread of the process idle meanwhile.

## Measurements

The release template (job f461 at 9f82d66b58, the passes timed by queue), 1920 × 1080, alternating runs:

| scene (measured) | frame, on | frame, off | p99 on / off | GPU frames of 17-25 ms whose main queue waited at the join, on | off |
|---|---:|---:|---:|---:|---:|
| Sponza streamed (2 × 15 min, f460 and f461) | 6.13-6.21 ms | 6.15 ms (f460) | 8.63-8.74 / 8.55 ms | 8 in 30 min | 0 in 15 min |
| Bistro interior (3 × 60 s each) | 3.18 ms | 3.23 ms (−1.5 %) | 3.66 / 3.72 ms | 2 in 3 min | 0 |
| Bistro exterior (3 × 60 s each) | 4.12 ms | 4.23 ms (−2.6 %) | 5.49 / 5.25 ms | 0 | 0 |
| Frontier Lab streamed (2 × 90 s each) | 3.90 ms | 4.06 ms (−3.7 %) | 5.24 / 5.43 ms | 0 | 0 |

Every one of those frames looked the same: the main queue's time from "< Volumetric Fog" (or "Update Volumetric Fog")
to its next timestamp 13-18 ms against 0.02 ms, the asynchronous queue's passes at their usual times, every thread of the
process waiting on the frame's fence — the main queue waiting for the asynchronous part, whose passes took their usual
time: it ran late, or its signal came late; not the CPU, nor the work of either queue. Both lanes also had, a few times
each, a frame step of 17-19 ms whose GPU frame was 6.8 ms (a fence late without FellowEngine's work: the operating
system's or another program's).

## Decision

Off by default. With it on the GPU's time was 1.5-3.7 % shorter in Bistro and Frontier Lab and no shorter in Sponza, and
a frame missed its 60 Hz slot about once in two to five minutes where the scene used it — a hitch players see, against
a saving of 0.05-0.15 ms. A game that wants the throughput (or targets a GPU and driver where the stall does not happen)
sets 1 or 2; autotune then still decides per scene. Not tried: the asynchronous part on a second queue of the main
family (decision 0015 found no overlap there, so no saving), queue priorities (the driver's scheduling is not ours).
