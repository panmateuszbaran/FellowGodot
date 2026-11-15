# 0237 — The hitch sampler's GPU side

Status: **KEEP** (with `rendering/fellow/debug/hitch_sampler`, off by default: a diagnostic) · Branch `fellow/geometry` ·
Code: `servers/rendering/fellow_hitch_sampler.{h,cpp}` (`gpu_wanted()`, `gpu_frame()`, `gpu_report()`),
`servers/rendering/rendering_server_default.cpp` (`_draw()`), `servers/rendering/rendering_device.{h,cpp}`
(`fellow_get_captured_begin_wait_usec()`, `fellow_get_captured_timestamp_async()`) · Checks: a lavapipe scene with a long GPU frame every 40
frames; the laptop's Sponza streamed (job f457).

## Problem

Decision 0231's sampler sees the CPU's threads. Milestone 118 left Sponza streamed with rare frames over 16.7 ms whose
render thread waited 19-22 ms for the GPU's fence (about one a 20-second flythrough, both with and without decision
0234), the GPU's frame 6-7 ms otherwise: either the frame's passes took that long, or the GPU was busy before it began
them — a transfer worker's copies, the driver, another program. The sampler cannot see into the GPU; Godot's visual
profiler shows a pass's time frame by frame in the editor, not the one frame in a thousand that was long.

## Change

With the sampler on, the renderer's pass timestamps (`RENDER_TIMESTAMP`, as `profiling/pass_timestamps` captures them)
are captured every frame and handed to the sampler as they are read back. A pass's time is from its timestamp to the
next one written on the same queue (RenderingDevice marks the timestamps written while decision 0015's asynchronous
compute part is recorded; their passes are named "(async queue)", their time overlapping the main queue's), summed when a
name recurs in a frame; a frame's, from its earliest timestamp to its latest; and the gap, from the previous frame's
latest timestamp to its earliest. Ordinary frames keep running averages (about the last 32) of each. A
gap is the GPU's only beyond what the CPU explains: the CPU's own time between recording the previous frame's last
timestamp and this one's first, less the render thread's wait for the GPU's fence when this frame began (RenderingDevice
keeps it with the frame's timestamps) — a frame the CPU recorded late leaves the GPU idle, which is the CPU's hitch. A
frame whose passes, or the unexplained part of its gap, exceed the threshold is a GPU hitch: up to 64 are reported at exit after the CPU's, with when
the frame was recorded (on the CPU sampler's clock), its passes' time and gap against their averages, and the six passes
longest past their averages. FellowEngine's statistics count `gpu_hitches`. On every platform; with V-Sync the gap
includes the wait for the display.

## Verification

Lavapipe (Mesa's CPU Vulkan driver), threshold 150 ms, 170 frames of a full-screen quad whose shader loops 400 times
every 40th frame: 4 GPU hitches, each "Render Opaque Pass" 188-259 ms against its average 2.3-2.8 ms, the gap before it
1-4 ms (the release template; the editor build alike, plus the first frame's 1 s of pipelines). A box lit by the sun, with a
compute dispatch of 20 000 iterations an invocation recorded on the render thread before every 40th frame's draw (outside
its passes): 4 GPU hitches whose passes took 4.4-9.7 ms (their average 4.9-5.5) and which began 2.6-2.8 s after the previous frame's last
timestamp, against 0.7-11 ms of the CPU's own time. A 250 ms sleep in a script every 60 frames: its frames' 247 ms gaps
explained by the CPU (259 ms), no GPU hitch (before the CPU's share was taken off, each was one).

The RTX, job f458 (the release template at 5ea9de57e3, before the CPU's share of a gap was taken off; Sponza streamed at
1920 × 1080, eight alternating runs each, 7 683 frames timed a run):

| | frame (run means) | GPU frame | p99 (pooled) | longest frame measured |
|---|---:|---:|---:|---:|
| the defaults | 5.97-6.57 ms | 5.90-6.49 ms | 8.72 ms | 14.5 ms |
| the sampler on (CPU and GPU) | 5.99-6.72 ms | 5.92-6.64 ms | 8.82 ms | 14.0 ms |

The sampler's GPU hitches, 2-6 a run, all but one in the run's first 3.3 s (the level loading, the camera's first
moves): frames whose shadow pass took 4.4-9.0 ms against 0.5-2.5, and gaps of 17-52 ms while the streamer's first loads
and pipelines were being made (some of them the CPU's: this template did not yet take its share off). The one later (run 7, 15.4 s in, as the measurement began): passes of 19.1 ms
against 6.7, the main thread waiting 21.4 ms for its fence meanwhile, the time between "Prepare Depth for SSAO/SSIL"
(13.7 ms) and the volumetric fog — where the asynchronous compute queue's work overlaps the main queue's, so that a
pass's time there was the queues' together (this template timed every pass to the next timestamp of either queue: the
reason for timing them by queue). No frame over 14.5 ms in the measured flythroughs of either lane.

Frontier Lab (job f459, the same template; the 4 km open world streamed, the flight at 45 m/s, three runs of 90 s,
27 780 frames measured each; 3.9 ms frames, p99 5.2 ms, p99.9 6.0 ms): GPU hitches only in the first 0.5 s after the
level's load (its first frames: "Update Volumetric Fog" 23-25 ms against 2.5) but one, 78 s into run 2: passes of 19.5
ms against 4.1, "Update Volumetric Fog" 15.6 ms and "FellowEngine Light Tile Depth Bounds" 13.4 ms (each a few hundredths
of a millisecond otherwise) — the same 13-15 ms beside the asynchronous queue's work as Sponza's.

Job f460 (the release template at d05b6e0ae6; Sponza streamed, three alternating runs of 300 s each, the sampler on):

| | frame (run means) | GPU frame | p99 | p99.9 | GPU frames over 16.7 ms after the first 4 s |
|---|---:|---:|---:|---:|---:|
| asynchronous compute on (the default on NVIDIA) | 6.12-6.28 ms | 6.05-6.21 ms | 8.74 ms | 11.18 ms | 3 (21.2, 21.6, 23.4 ms) |
| off | 6.11-6.19 ms | 6.04-6.12 ms | 8.55 ms | 10.86 ms | 0 |

The three were alike: "Prepare Depth for SSAO/SSIL" 14.2-15.5 ms and "< Volumetric Fog" 14.6-16.5 ms (a tenth and two
hundredths of a millisecond otherwise), every thread of the process waiting meanwhile — the GPU stalled about 15 ms
where decision 0015's asynchronous part runs beside the main queue. Both lanes had one frame step of 19-22 ms whose GPU
frame was 6.8 ms (a fence late without the GPU's passes: the operating system's or another program's work), and every
loop of the flight a 13 ms GPU frame where the camera's path jumps back to its start.

## Decision

Keep, with the sampler: a long GPU frame's passes, or the work before them, named, at no measurable cost (the runs
overlap). With asynchronous compute on, a pass next to the asynchronous queue's work can take the other queue's time.
