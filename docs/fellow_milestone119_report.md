# FellowEngine — milestone 119 report (downloads returned whole; the GPU's hitches, scripts' frames, and hitches in the editor)

Status: **FINAL** (2026-11-15). Branch `fellow/geometry`. Milestone 118 made texture downloads' staging as tight as
uploads' and left the GPU's rare long frames (19-22 ms fence waits in Sponza streamed) out of the sampler's sight.

## 0. Result

* **Downloads returned whole** (decision 0236, with FellowEngine on): checking milestone 118's download staging found
  two faults of stock Godot's in `RenderingDevice.texture_get_data_async()` and `buffer_get_data_async()` — a texture
  that is not square came back without its last mips, and a download larger than what the download staging ring had
  left lost the parts copied before the ring's wait (a 144 MiB buffer 89 % wrong). Fixed; the same on the RTX and on
  lavapipe (`bench/readback_check.gd`, every byte).
* **The GPU's hitches** (decision 0237): with the hitch sampler on, the renderer's pass timestamps every frame; a GPU
  frame whose passes, or the gap before them beyond what the CPU's own time explains, exceed the threshold is reported
  with the passes longest past their averages. Within the runs' noise on the RTX.
* **Hitches in the editor's debugger** (decision 0238): a game run from the editor lists its hitches as they happen in a
  FellowEngine Hitches tab, named, with the full text; Record starts the sampler in a running game.
* **Scripts' frames** (decision 0239): a CPU hitch in a debug build names the GDScript functions and lines it was in
  (`_sleepy_function (res://main.gd:25)`), not only the interpreter's native frames.
* **Frontier Lab** (the 4 km open world, streamed, at 45 m/s; job f459): no frame step over 16.7 ms in three 90-second
  flights, one GPU frame (19.5 ms).
* **The GPU's rare long frames placed** (job f460): 21-23 ms frames, about one in five minutes of Sponza streamed, all
  with asynchronous compute on (about 15 ms where its part runs beside the main queue); none in 15 minutes with it off,
  whose frames were also 1 % shorter.

## 1. Downloads returned whole (decision 0236)

(see the decision) The readback check's results:

| | synchronous | asynchronous, 30 in a frame | larger than the ring |
|---|---:|---:|---:|
| FellowEngine off (stock; lavapipe and the RTX alike) | 30 of 30 | 12: the 18 not square wrong (17 in their last 1-16 bytes, BC5 4096 × 2048 63 %) | the buffer 89 % wrong, a texture 40 % |
| on | 30 of 30 | 30 of 30 | 3 of 3 |

## 2. The GPU's hitches (decision 0237)

With `rendering/fellow/debug/hitch_sampler` on, the renderer's pass timestamps are captured every frame and handed to
the sampler as they are read back. A pass is timed to the next timestamp of its own queue (the asynchronous compute
part's marked by RenderingDevice), a frame from its earliest timestamp to its latest; the gap before a frame counts
beyond the CPU's own time between recording the two frames (less the render thread's fence wait), which leaves the GPU
idle. A frame over the threshold either way is reported at exit with the passes longest past their running averages.

On lavapipe: a looping full-screen shader every 40 frames named "Render Opaque Pass" 188-259 ms against 2.3-2.8; a compute
dispatch recorded before the frame's passes a 2.4-2.8 s gap against 0.7-11 ms of the CPU's own time; a script's 250 ms
sleep no GPU hitch (its gap the CPU's).

| the RTX, job f458 (Sponza streamed, 8 alternating runs) | frame (run means) | GPU frame | p99 | longest measured |
|---|---:|---:|---:|---:|
| the defaults | 5.97-6.57 ms | 5.90-6.49 ms | 8.72 ms | 14.5 ms |
| the sampler on, CPU and GPU | 5.99-6.72 ms | 5.92-6.64 ms | 8.82 ms | 14.0 ms |

Its GPU hitches came in the first 3.3 s (the shadow atlas's first fill: 4.4-9.0 ms against 0.5-2.5; the streamer's first
loads), and one at 15.4 s: 19.1 ms of passes against 6.7, 13.7 ms between "Prepare Depth for SSAO/SSIL" and the
volumetric fog. Frontier Lab (job f459, three 90-second flights at 45 m/s, 3.9 ms frames, p99.9 6.0 ms) had one: 19.5 ms
against 4.1, "Update Volumetric Fog" 15.6 ms and "FellowEngine Light Tile Depth Bounds" 13.4 ms — 13-15 ms in both,
beside the asynchronous compute queue's work (that template timed passes across the queues: hence the timing by queue).

Job f460 tested that: Sponza streamed for 15 minutes each way, the sampler on. With asynchronous compute (decision 0015,
on by default on NVIDIA) three GPU frames of 21-23 ms, each "Prepare Depth for SSAO/SSIL" and "< Volumetric Fog" about
15 ms, every thread of the process waiting; with it off none, and the frames slightly shorter (6.15 against 6.21 ms on
average, p99 8.55 against 8.74 ms, p99.9 10.86 against 11.18 ms). The GPU's rare long frames milestones 116-118 could
not place are the asynchronous queue's.

## 3. Hitches in the editor's debugger (decision 0238), scripts' frames (decision 0239)

A game run from the editor sends each hitch as it happens: a CPU hitch named on a low-priority thread of the game's own
(decision 0231's `symbolize()`; Windows' linker map now read once), a GPU hitch's text made where it is found; the main
thread sends them at its next frame step. The debugger's **FellowEngine Hitches** tab lists them (when, CPU or GPU, how
long, where) with the full text of the one selected; **Record** starts the sampler in the running game and the next.

In GDScript's debug builds the sampler also reads the sampled thread's call levels (the list GDScript keeps for the
debugger's stack traces): in the stack's copy on Windows, in the signal handler on Linux; the functions are named when the
step ends, against GDScript's list of live functions. A hitch's report adds the script call path and innermost
functions; the tab shows the innermost one of the hottest path.

| lavapipe, a project played from the editor, Record at 150 ms (25 s) | the tab's rows |
|---|---|
| a long GPU frame every 40 frames | GPU, "Render Opaque Pass", 405-467 ms |
| the main thread waiting for those frames | CPU, `RenderingDevice::_fellow_present_wait` |
| a 250 ms sleep in `_sleepy_function()` every 60 frames | CPU, `_sleepy_function (res://main.gd:24)` |
| the first frame's shader compilation | CPU, `compute_pipeline_create` |

## 4. Checks

The regression suite (36 scenes, the release template against the editor build, lavapipe): the same as its goldens;
the editor check passes; the class reference and the settings' descriptions regenerated.

## 5. Next

Asynchronous compute measured again on the current engine (it was chosen in milestones 15-21 on other scenes): its
gains against its rare 15 ms stalls, scene by scene, with the passes timed by queue; the operating system's video memory
budget read from the driver (`VK_EXT_memory_budget`) and a texture streaming budget that follows it.
