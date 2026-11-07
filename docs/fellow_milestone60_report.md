# FellowEngine — milestone 60 report (the main thread's CPU, and the hitches it waited through)

Status: **FINAL** (2026-11-07): checked in the cloud, measured on the RTX laptop (jobs f361, f361b, f362b, f363).
Branch `fellow/geometry`. Decision records `docs/decisions/0163-skeletal-animation-shortcuts-2.md`,
`0164-crowd-thread-groups.md`, `0165-separate-render-thread.md`, `0166-pipeline-cache-lock.md`.

## 0. Result

Work first done without a GPU — the main thread's share of a frame, a crowd's skeletal animation first — then measured
on the RTX laptop, where the new split of the main loop's step also found the cause of the slice's last long hitches.

* **The slice's long hitches** (decision 0166): Godot's pipeline cache was saved under the RenderingDevice's lock —
  hundreds of MB copied and checksummed while pipelines compiled in the background — and the main thread's next device
  call waited for it. Off that lock, the slice's 20-minute soak has its largest frame at **20 ms instead of 397**, 4
  hitches instead of 16, none over 30 ms, with more pipelines compiled than before.
* **Two more exact shortcuts in the skeletons' update** (decision 0163): the deprecated global pose without overrides
  copied where it equals the global pose, the compatibility physical-bone simulator's pose copies made only when a
  physical bone needs them. The same bone poses bit for bit; on the RTX laptop the scene's process −8.5 % in Crowd Lab
  (256 characters; −5.4 % blending), −9 % in the slice. A third shortcut (the keys' part of each slerp kept per thread)
  saved instructions but cost CPU time: rejected and removed.
* **Crowds on worker threads** (decision 0164): characters in Godot's sub-thread process groups, the same poses bit
  for bit. Crowd Lab's 256 characters in 8 groups: the scene's process **3.07 → 1.04 ms**, the frame **−33 %**
  (blending: 5.25 → 1.40 ms, −53 %); the frame is then the GPU's.
* **The separate render thread usable** (decision 0165): water, rain and shallow water freed their GPU resources on
  the main thread (errors and leaks at exit), and Godot's shader compilation could deadlock when a version's waiter
  held its mutex with every worker blocked; both fixed. Crowd Lab with it: the frame −32 %.
* **A measurement fixed**: `Performance.TIME_PROCESS`, which the bench reported as "the main thread's process time",
  is the largest of each second's main-loop steps with the RenderingServer's sync and draw — the slice's 5.3 ms on the
  RTX was mostly rendering and waiting for the GPU. FellowEngine's stats now split the step: the slice's scene process
  is 0.66 ms a frame.

## 1. Development

* `servers/rendering/rendering_device.{h,cpp}`, `drivers/vulkan/rendering_device_driver_vulkan.{h,cpp}`: the pipeline
  cache's queries and saves under a mutex of their own, the loaded file and the save's copy not kept (0166).
* `scene/3d/skeleton_3d.{h,cpp}`, `scene/3d/physics/physical_bone_simulator_3d.{h,cpp}`: decision 0163's shortcuts;
  `servers/rendering/fellow_settings.{h,cpp}`, `doc/tools/fellow_settings_docs.py`: `animation/shared_no_override_pose`,
  `animation/lazy_simulator_poses`.
* `main/main.cpp`, `fellow_settings.{h,cpp}`: the main loop's step in the stats (`main_frames`, `main_process_usec`,
  `main_draw_usec`); `benchmarks/run_benchmarks.py` (`scene_process_ms`, `main_draw_ms`), `bench/metrics.gd`.
* `modules/fellow_fluid/fellow_water_3d.*`, `fellow_rain_3d.*`, `fellow_shallow_water_3d.*`: `_rt_destroy()`;
  `servers/rendering/renderer_rd/shader_rd.{h,cpp}`: a version group's variants claimed by its tasks or its waiter.
* `benchmarks/project/scenes/crowd_lab.gd`, `crowd_group.gd`, `slice_lab.gd`: `--crowd_groups`,
  `--slice_crowd_groups`; `bench/animation_check.gd`: `--groups`, `scene_ms`; `bench/main.gd`: the soak's render times
  read on the render thread with `--rs_metrics=render_thread`; `benchmarks/tools/soak/run_soak.py`: `--engine-args`;
  the editor check: its layout recorded (it needs a 1920 × 1080 virtual screen).

## 2. Verification

* `animation_check.gd`: the same hashes with every animation setting off and on, with 0, 2 and 8 process groups, with
  AnimationPlayers and with AnimationTrees blending (editor build: no error).
* lavapipe: Crowd Lab with 4 groups against none, 0 pixels differ; the image regression suite (exact, 17 scenes, the
  release template against the editor build), with its shader cache warm and cold; Fire Lab with the multigrid's small
  levels in one workgroup at the slice's 48 cells, 0 pixels differ.
* lavapipe with the separate render thread: the slice with crowd groups without an error; its water and rain freed at
  exit without an error or a leak; with the shader compilation fix four runs of four to the end (before: two hung), and
  three minutes with every milestone-60 feature at once without an engine error.
* The editor check (a 1920 × 1080 virtual screen): passed.
* The cloud: CPU time with `perf stat -e task-clock`, instructions with callgrind (its frame timings vary ±40 %).
* RTX 5070 Ti Laptop, release template, 1080p: Crowd Lab (f361) and Slice Lab (f361b) frame-locked and interleaved,
  six runs each; the slice's 20-minute soak with decision 0166 (f363, against f360's); 10 minutes of the slice's soak
  with the separate render thread and its crowd in 8 groups (f362b): no engine error.

## 3. Measurements

The scene's process a frame (FellowEngine's `main_process_usec`) and the frame (medians), RTX laptop:

| | scene process | frame |
|---|---|---|
| Crowd Lab, 256 characters, AnimationPlayers: 0163's settings off → on | 3.35 → 3.07 ms | 5.05 → 4.79 ms |
| the same, AnimationTrees blending | 5.55 → 5.25 ms | 7.32 → 7.09 ms |
| Crowd Lab in 8 process groups (24: the same) | **1.04 ms** | **3.19 ms** |
| the same, blending | **1.40 ms** | **3.32 ms** |
| Crowd Lab, the separate render thread; with 8 groups | 3.01; 0.93 ms | 3.28; 3.10 ms |
| Slice Lab (48 characters, the terrain and its foliage): settings off → on | 0.731 → 0.664 ms | 4.549 → 4.497 ms |
| Slice Lab, its crowd in 8 groups | 0.490 ms | 4.555 ms |
| Slice Lab, the separate render thread | 0.642 ms | 4.597 ms |

The slice's frame is its GPU's (3.5 ms; the gas simulation's pressure solve alone 0.54 ms): what the main thread saves
there does not shorten it. The campfire's multigrid levels of at most 2048 or 512 cells in one workgroup
(`fluid/gas_mg_tail_cells`, exact): the frame −1.6 % and −2.4 %, the GPU's time the same, the pressure group 0.535 →
0.536 ms — the fine levels are the cost at 48 cells; still off (decision 0119).

The slice's 20-minute soak (decision 0166):

| | f360 | f363 (0166) |
|---|---|---|
| pipelines after the warm-up | 122 | 151 |
| hitches; over 100 ms | 16; 5 (167-396 ms) | **4; 0** |
| the largest frame | 397 ms | **20.3 ms** |

With the separate render thread and the crowd in 8 groups (f362b, 10 minutes, decision 0166 in): 106 pipelines after the
warm-up, 3 hitches, all the GPU's fence (the largest frame 22.4 ms), 234 frames a second, video memory 404 → 406 MB,
objects +2, no orphans, no error.

Crowd Lab headless on the cloud (decision 0163): instructions −10.4 % (blending −6.4 %), CPU time a frame −7.3 %
(−4.4 %).

## 4. Limits

* What remains of a crowd's animation is its work proper (interpolation, poses, global poses, skins); further exact
  savings are small. Parallelism is the scene's choice (process groups), not automatic.
* The process's memory still grows in the soaks (about 1.1 GB an hour, start 1.7-2.2 GB): likely the driver's pipeline
  cache, which the bench's cache file fills with every build's pipelines; not measured directly.
* The slice is bound by its GPU on the RTX: the CPU work of this milestone shows in CPU-bound scenes (crowds), not in
  its frame.

## 5. Next

The slice's GPU frame: the campfire's pressure solve (0.54 ms of 3.5: its fine levels), shadows, SSAO; the terrain's
partial texture uploads for editing large maps (M58); other GPUs when the hardware is at hand.
