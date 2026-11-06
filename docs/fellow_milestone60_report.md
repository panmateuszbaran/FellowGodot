# FellowEngine — milestone 60 report (the main thread's CPU)

Status: **checked in the cloud** (2026-11-06); the RTX laptop's measurements wait for the GPU (job f361). Branch
`fellow/geometry`. Decision records `docs/decisions/0163-skeletal-animation-shortcuts-2.md`,
`0164-crowd-thread-groups.md`, `0165-separate-render-thread.md`.

## 0. Result

Work that needs no GPU: the main thread's share of a frame, first of all a crowd's skeletal animation, which the
slice's profile without rendering puts first.

* Two more exact shortcuts in the skeletons' update (decision 0163): the deprecated global pose without overrides
  copied where it equals the global pose, the compatibility physical-bone simulator's pose copies made only when a
  physical bone needs them. Crowd Lab headless (128 characters): the main thread's instructions −10.4 % (blending
  AnimationTrees −6.4 %), its CPU time a frame −7.3 % (−4.4 %), the same bone poses bit for bit. A third shortcut — the
  keys' part of each rotation track's slerp kept per thread — saved instructions but cost CPU time (its table's cache
  misses): rejected and removed.
* Crowds on worker threads (decision 0164): Crowd Lab's and Slice Lab's characters can be put in Godot's sub-thread
  process groups; FellowEngine's animation code gives the same poses there bit for bit.
* The separate render thread (decision 0165), which overlaps the main thread's scene work with the rendering: water,
  rain and shallow water freed their GPU resources on the main thread (errors and leaks at exit); fixed. A deadlock in
  Godot's own shader compilation (a waiter holding a version's mutex while every worker is blocked) fixed: the waiter
  compiles the variants no task has started.
* A measurement fixed: `Performance.TIME_PROCESS`, which the bench reported as "the main thread's process time", is the
  largest of each second's main-loop steps with the RenderingServer's sync and draw — with the default thread model the
  frame's rendering and its waits for the GPU. The slice's 5.3 ms on the RTX was not its scene's cost. FellowEngine's
  stats now split the step (`main_process_usec`, `main_draw_usec`), and `run_benchmarks.py` reports
  `scene_process_ms` and `main_draw_ms`.

## 1. Development

* `scene/3d/skeleton_3d.{h,cpp}`, `scene/3d/physics/physical_bone_simulator_3d.{h,cpp}`: decision 0163's shortcuts;
  `servers/rendering/fellow_settings.{h,cpp}`, `doc/tools/fellow_settings_docs.py`: `animation/shared_no_override_pose`,
  `animation/lazy_simulator_poses`.
* `main/main.cpp`, `fellow_settings.{h,cpp}`: the main loop's step in the stats; `benchmarks/run_benchmarks.py`,
  `bench/metrics.gd`: reported.
* `benchmarks/project/scenes/crowd_lab.gd`, `crowd_group.gd`, `slice_lab.gd`: `--crowd_groups`,
  `--slice_crowd_groups`; `bench/animation_check.gd`: `--groups`, `scene_ms`.
* `modules/fellow_fluid/fellow_water_3d.*`, `fellow_rain_3d.*`, `fellow_shallow_water_3d.*`: `_rt_destroy()`;
  `servers/rendering/renderer_rd/shader_rd.{h,cpp}`: a version group's variants claimed by its tasks or its waiter.

## 2. Verification

* `animation_check.gd`: the same hashes with every animation setting off and on, with 0, 2 and 8 process groups, with
  AnimationPlayers and with AnimationTrees blending (editor build: no error).
* lavapipe: Crowd Lab with 4 groups against none, 0 pixels differ; the image regression suite (exact, 17 scenes, the
  release template against the editor build); Fire Lab with the multigrid's small levels in one workgroup
  (`fluid/gas_mg_tail_cells=2048`) at the slice's 48 cells, 0 pixels differ (for job f361).
* lavapipe with the separate render thread: the slice with crowd groups for two minutes without an error; its water
  and rain freed at exit without an error or a leak.
* The editor check (milestone 60's editor build, a 1920 × 1080 virtual screen): passed — the water, rain and gas
  nodes added, dragged and freed in the editor, the presets added and undone.
* CPU time measured with `perf stat -e task-clock` and instructions with callgrind (the cloud's frame timings vary
  ±40 % between runs).

## 3. Measurements

Crowd Lab headless, release template, the cloud's CPU (decision 0163):

| | instructions, AnimationPlayer | instructions, AnimationTree | CPU time a frame, AnimationPlayer | AnimationTree |
|---|---|---|---|---|
| both off | 553.3 M | 903.3 M | 1.85 ms | 3.20 ms |
| both on | −10.4 % | −6.4 % | −7.3 % | −4.4 % |

The slice's main thread headless (`perf`, 1 minute): the crowd's animation and skeletons about half of it, the rest
the bench's own scripts (the flythrough, the weather's schedule, the street lights), the terrain's selection and
foliage (under 1 %), transforms.

To measure on the RTX (job f361): Crowd Lab and Slice Lab with the two settings off against on; Crowd Lab with 8 and
24 groups, the slice's crowd with 8; the slice's scene process and draw split; the campfire's gas solver with its
small levels in one workgroup (the gas simulation is 0.84 ms of the slice's 3.6 ms GPU frame, f354).

## 4. Limits

* What remains of a crowd's animation is its work proper (interpolation, poses, global poses, skins); further exact
  savings are small. Parallelism is the scene's choice (process groups), not automatic.
* The cloud's two vCPUs show 12 % with 2 groups; the scaling on many cores is the laptop's to measure.

## 5. Next

The RTX's numbers; the gas simulation's cost in the slice (its multigrid's dispatches).
