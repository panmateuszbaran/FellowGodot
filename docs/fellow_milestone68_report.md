# FellowEngine — milestone 68 report (what Frontier Lab found)

Status: **FINAL** (2026-11-10): checked on lavapipe and measured on the RTX (jobs f377-f382). Branch `fellow/geometry`.
Decision record: `docs/decisions/0176-frontier-findings.md`.

## 0. Result

The second item of the order set when the GPU could be used again (2026-11-10, "kontynuuj z GPU"): the list Frontier
Lab (milestone 67) left.

* **A gas volume asleep when far**: `FellowGasVolume3D.sleep_distance` (0: never) — beyond it from the camera the
  volume takes no steps, its smoke and fire kept and drawn, and goes on when the camera returns. Frontier Lab's
  campfire sleeps past 250 m: the gas simulation over the flight 0.93 → 0.18 ms of GPU, the frame at 1080p 5.06 →
  3.9-4.3 ms (198 → 234-259 fps over seven flights, with the laptop's power state).
* **No shader made in play**: the variants a shader-cache load leaves for later (decision 0061) are made on a worker
  thread after the load (`frame/lazy_shader_variants_fill`, on), and the snow tracks' kernel when the rain node is set
  up — claimed under the version's lock, created with it released. Twenty minutes of play: nothing compiled in play,
  the largest frame 92 → 21.5 ms, frames over 15 ms 9 → 5 (none in the first minute).
* **A false error found**: "Attempting to use an uninitialized RID", printed in some flights, came from the MultiMesh
  culling pool looking up members freed since (decision 0034): checked first now — in none of nine flights since
  (three of seven before).
* **Attributed**: `FELLOW_LOG_COMPILES=1` prints each shader variant still made at its first use and each kernel
  compiled from GLSL, with its frame.

## 1. Development

* `modules/fellow_fluid/fellow_gas_volume_3d.{h,cpp}`: `sleep_distance`, `is_sleeping()` (the viewport's camera to
  the box's corners); `fellow_burnable_3d.cpp`: a burning object waits with its volume.
* `servers/rendering/renderer_rd/shader_rd.{h,cpp}`: versions read from the cache with variants left for later queued
  for one serial worker task, which makes one variant per step — claimed under the version's lock, created with it
  released, kept unless the render thread made it meanwhile — and skips a version another thread holds;
  `FELLOW_LOG_COMPILES` (also in `servers/rendering/fellow_kernel_cache.cpp`).
* `servers/rendering/fellow_settings.{h,cpp}`: `frame/lazy_shader_variants_fill`, the counter
  `cost_shader_background_creates`; `doc/tools/fellow_settings_docs.py`, `doc/classes/ProjectSettings.xml`.
* `modules/fellow_fluid/fellow_rain_3d.{h,cpp}`: the snow tracks' kernel and pipeline made at setup.
* `servers/rendering/renderer_rd/storage_rd/mesh_storage.cpp`: the pool's members checked with `owns()` before they
  are looked up.
* `benchmarks/project/scenes/frontier_lab.gd`: the campfire's `sleep_distance` (`--frontier_fire_sleep`, 250 m), its
  shared props as `ArrayMesh`es, the streamers' work per soak frame; `benchmarks/project/bench/main.gd`:
  `soak_after_frame()`.
* The class reference (`FellowGasVolume3D.xml`), `docs/FELLOWENGINE_OPTIONS.md`.
* Tried and reverted (parts 3-4): the streamer running the RenderingServer's queue before streamed content enters the
  tree or is freed — every main-thread call into the RenderingServer runs the queue first, and the errors stayed.

## 2. Verification

* Lavapipe: a gas volume with `sleep_distance` 50 m took 30, 0 and 30 steps in 30 frames (near, at 200 m, near
  again); `world_proxies_check.gd` (4 × 4 cells) with the final build: the streamed world with proxies 0.7 % of its
  pixels off the whole world's (3.1 % without), streamed meshes swapped, no error.
* The regression suite (editor build and release template): every picture exact.
* The RTX (release templates): f377 (the gas asleep) and f378 (the variants and the kernel): the flight per pass, in
  clear weather and in rain, and twenty minutes of play each; f379-f380 (the reverted streamer change) and f381 (the
  pool's fix): the flight five times at 1080p and twice with the 3D frame at 3840 × 2160 for the errors; f381 and f382
  (the final build): twenty minutes of play, f382 two flights more.

## 3. Measurements

The flight at 1080p (154 s, the preset `high`, atmosphere, clouds, texture streaming), GPU passes from timestamps:

| | frame | GPU (timestamps) | gas simulation | opaque | depth pre-pass | shadows | SSAO |
|---|---|---|---|---|---|---|---|
| milestone 67 (f374) | 5.06 ms | 4.50 ms | 0.93 ms | 0.89 | 0.53 | 0.46 | 0.46 |
| the gas asleep (f377) | 4.37 ms | 4.25 ms | 0.21 ms | 1.02 | 0.59 | 0.54 | 0.56 |
| + variants, kernel (f378) | 3.90 ms | 4.01 ms | 0.18 ms | 1.00 | 0.58 | 0.53 | 0.50 |
| f378 in rain | 4.55 ms (profiled) | 4.43 ms | 0.18 ms | 1.15 | 0.64 | 0.52 | 0.50 |

(The other passes vary by about 10 % between jobs with the GPU's clocks. Rain adds 0.42 ms of GPU: wet opaque shading
0.16, the clouds 0.08, the depth pre-pass 0.07, the rainbow 0.04, the drops' particles 0.04.)

Five flights at 1080p and two with the 3D frame at 3840 × 2160 (the release template of each job; f382 two flights):

| | flights printing the error | 1080p frame (mean; range) | p99 (range) | GPU | 3D frame at 3840 × 2160 (2) |
|---|---|---|---|---|---|
| the streamer's queue run (part 3, f379) | 3 of 7 | 3.88 ms (3.85-3.91) | 7.42 ms (7.22-7.67) | 3.34 ms | 9.78 ms |
| also before frees (part 4, f380) | 3 of 7 | 4.02 ms (3.86-4.15) | 6.55 ms (5.92-7.52) | 3.51 ms | 9.74 ms |
| the pool's members checked (f381) | **0 of 7** | 4.08 ms (3.87-4.28) | 6.70 ms (6.14-7.43) | 3.56 ms | 9.84 ms |
| final: variants made outside the lock (f382) | 0 of 2 | 4.09 ms (4.04-4.14) | 5.97 ms (5.94-5.99) | 3.59 ms | — |

Within each job the first flights ran with the GPU slower (3.6-3.8 ms) and a p99 of 5.9-6.5 ms, the last ones with the
GPU at 3.3 ms and a p99 of 7.0-7.5 ms — the laptop moving its power between the processor and the GPU as it warms; the
differences between the jobs are inside that range.

Twenty minutes of play (1080p, the flight looping, the weather turning every minute, the log lit every two):

| | fps | p99 | p99.9 | largest frame (after the first minute) | frames over 15 ms: first minute / after | shaders made in play |
|---|---|---|---|---|---|---|
| milestone 67 (f376) | 248 | 6.33 ms | 8.82 ms | 92 ms | 2 / 7 | not logged (3 frames of variants, 1 kernel) |
| the gas asleep (f377) | 256 | 6.07 ms | 8.61 ms | 80 ms | 2 / 4 | 10 variants, 1 kernel (77 ms) |
| + variants, kernel (f378) | 272 | 5.68 ms | 7.76 ms | 19.9 ms | 5 / 2 | none (705 made in the background) |
| + the pool's fix (f381) | 270 | 5.73 ms | 8.07 ms | 18.9 ms | 7 / 2 | none (692 in the background) |
| final: variants made outside the lock (f382) | 272 | 5.65 ms | 7.93 ms | 21.5 ms | 0 / 5 | none (530 in the background) |

In f378 and f381 the frames over 15 ms of the first minute (17-40 ms) all fell in the 1.6 s after the load: the render
thread waiting for a shader version's lock while the background filling created one of its variants (5-7 ms each).
Made with the lock released (part 6), none (the threads asking made 371 variants themselves during the load, about 200
before). The frames after the first minute (two to five) are of kinds no change here touched (Limits).

## 4. Limits

* Distance only: a volume behind the camera and near keeps simulating (a plume turned to would jump otherwise).
* Rare frames of 17-22 ms remain, two to five in twenty minutes, none from compiling: a submission the driver held for
  14 ms; the render thread 16-18 ms with no counter showing where (twice); the main thread 15.6-16.7 ms (once inside the
  world streamer's update in a frame where it loaded, entered, released and swapped nothing); the GPU 17-20 ms in snow.
* The stock `PipelineHashMapRD` can still wait for a compilation task twice ("Invalid Task ID", harmless) when the
  shadow filter quality is set while pipelines compile at load.

## 5. Next

The rare frames above (the streamer's update timed step by step, the render thread's unaccounted time), hour-long
play of the open world, and the readiness assessment's open rows with the GPU at hand (the terrain's virtual
texturing).
