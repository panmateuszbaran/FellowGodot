# 0170 — ThreadSanitizer over milestone 60's threading: three data races fixed

Status: **KEEP** (fixes: the same frames; the image regression suite exact) · Branch `fellow/geometry` · Code:
`servers/rendering/rendering_server_default.{h,cpp}` (`changes`), `core/config/project_settings.cpp`,
`modules/jolt_physics/spaces/jolt_job_system.cpp` (`flush_timings()`).

## Problem

Milestone 60 put FellowEngine's work on more threads: a crowd's characters posed in process thread groups (decision
0164), the separate render thread (0165), shader compilation's claims and the pipeline cache's own lock (0165, 0166),
on top of Jolt's job system (0090-0092). They were checked by their results — the same poses and pictures bit for bit,
runs to the end — not by a race detector.

## Method

A debug template built with ThreadSanitizer (`use_tsan=yes`, GCC 13's libtsan; symbols from a relink of the same
objects without `-s`), run on lavapipe under `setarch -R`:

* the regression suite's crowd in 4 process groups with the separate render thread, and its water, rain and fire scenes
  with the separate render thread (150 frames each);
* Physics Lab's pile headless with Jolt (its jobs in the WorkerThreadPool) and with GodotPhysics3D;
* Slice Lab with its crowd in 8 groups, the separate render thread and the warm-up (stopped after 40 minutes, still in
  its warm-up, which takes under a minute with the editor build on lavapipe).

Reports whose two accesses are inside lavapipe (Mesa's driver, not instrumented: its own locks are invisible to the
sanitizer) are set aside: about 60 over a set of runs, all in its pipeline creation and fences.

## Found and fixed

| race | threads | fix |
|---|---|---|
| `RenderingServerDefault::changes`, the counter every server call increments (stock: a plain `int`) | two worker threads posing their groups' skeletons (`fellow_skeleton_set_bone_transforms`) | `std::atomic<int>`, incremented relaxed |
| `ProjectSettings::set_as_basic()`, `set_restart_if_changed()` (and `set_initial_value()`, `set_as_internal()`, `set_ignore_value_in_docs()`), which did not take the settings' lock | the main thread defining a setting while the render thread lists the settings (`global_shader_parameters_load_settings()`) | the class's lock, as its other methods take |
| `JoltJobSystem::flush_timings()` resetting the jobs' times without their lock (debug builds) | the main thread, and a job thread adding its time after its job's barrier | the times read and reset under the lock, the profiler called after |

All three are in stock Godot 4.7.2's code; the first two show with process groups or a separate render thread, the
third whenever Jolt's jobs run. None was in FellowEngine's own threading: no report in decision 0165's shader
compilation claims, 0166's pipeline cache lock, 0164's groups or 0092's job queue.

## Verification

The same runs with the fixes: no report with an access in Godot's code (the crowd, rain and fire with the separate
render thread; Jolt's pile: none at all). The image regression suite: 24 pictures exact.

## Decision

**KEEP.** Not done: a sanitizer run on a GPU's driver (lavapipe's reports hide whatever it would show of
synchronization through the driver), the whole slice under the sanitizer (40 minutes did not finish its warm-up).
