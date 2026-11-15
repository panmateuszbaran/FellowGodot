# 0237 — The hitch sampler's GPU side

Status: **KEEP** (with `rendering/fellow/debug/hitch_sampler`, off by default: a diagnostic) · Branch `fellow/geometry` ·
Code: `servers/rendering/fellow_hitch_sampler.{h,cpp}` (`gpu_wanted()`, `gpu_frame()`, `gpu_report()`),
`servers/rendering/rendering_server_default.cpp` (`_draw()`) · Checks: a lavapipe scene with a long GPU frame every 40
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
next (summed when a name recurs in a frame); a frame's, from its first timestamp to its last; and the gap, from the
previous frame's last timestamp to its first. Ordinary frames keep running averages (about the last 32) of each. A
frame whose passes or gap exceed the threshold is a GPU hitch: up to 64 are reported at exit after the CPU's, with when
the frame was recorded (on the CPU sampler's clock), its passes' time and gap against their averages, and the six passes
longest past their averages. FellowEngine's statistics count `gpu_hitches`. On every platform; with V-Sync the gap
includes the wait for the display.

## Verification

Lavapipe (Mesa's CPU Vulkan driver), threshold 150 ms, 170 frames of a full-screen quad whose shader loops 400 times
every 40th frame: 4 GPU hitches, each "Render Opaque Pass" 188-259 ms against its average 2.3-2.8 ms, the gap before it
1-4 ms (the release template; the editor build alike, plus the first frame's 1 s of pipelines). A box lit by the sun, with a
compute dispatch of 20 000 iterations an invocation recorded on the render thread before every 40th frame's draw (outside
its passes): 4 GPU hitches whose passes took 4.4-9.7 ms (their average 4.9-5.5) and which began 2.6-2.8 s after the previous frame's last
timestamp. The CPU's report alongside: the main thread's steps of the same frames waiting for the GPU.

The RTX, job f458 (Sponza streamed, the sampler on at 16.7 ms against the defaults): below.

## Decision

Keep, with the sampler: a long GPU frame's passes, or the work before them, named.
