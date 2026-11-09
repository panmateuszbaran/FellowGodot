# FellowEngine — milestone 61 report (what the CPU could still do without a GPU)

Status: **FINAL for the cloud's part** (2026-11-08): checked in the cloud (lavapipe, headless); the RTX laptop's
measurements (job f364) prepared, not run — the GPU was not to be used. Branch `fellow/geometry`. Decision records `docs/decisions/0167-late-pipelines.md`,
`0168-terrain-region-uploads.md`, `0169-kernel-spirv-cache.md`, `0170-thread-sanitizer.md`.

## 0. Result

Work the cloud's CPU and lavapipe could check and measure, while the RTX laptop's GPU was not to be used: what is left
of mid-game pipeline compilation, the terrain's editing cost, load-time compilation, and measurements prepared for the
GPU (the campfire's solver, the process's memory), with the regression suite extended to milestone 60's threading.

* **Fewer pipelines after the warm-up** (decision 0167): the slice's soak on lavapipe asked for 1 pipeline after its
  warm-up that a draw had to wait for, instead of 9 — the sun's soft and contact shadow bits kept at night, tile
  culling's bit no longer flipping while autotune has it off, and the warmer's new shelf: a copy of every visible
  mesh, MultiMesh and terrain foliage kind drawn from a view 4 km up, hidden from other cameras by its range. The same
  image.
* **The terrain's edits upload only what they changed** (decision 0168): on the RTX a 4097² map's frame while
  sculpting 63.3 → 0.9 ms, while painting 29.2 → 1.0 ms (the update after a stroke 71.2 → 0.8 ms and 34.4 → 0.02 ms),
  the same textures bit for bit.
* **Compiling less at load** (decision 0169): the water's, rain's, shallow water's and the neural motion model's GLSL
  kept as SPIR-V after its first compilation (the motion model's load 26.4 → 0.86 s on the cloud's CPU, the others' 28 →
  0.5 ms; on the thread adding the first such node, at load or in play); and Godot's
  shader baker now bakes FellowEngine's effects, which are made when first used, and the materials its nodes make from
  code: baked exports of the fire and water scenes compile no shader at their first start instead of 3 and 1 (70-72
  unbaked).
* **ThreadSanitizer** (decision 0170): milestone 60's threading run under the sanitizer on lavapipe — three data races,
  all in stock Godot code (the RenderingServer's change counter written from the crowd's worker threads, project
  settings' flags set while the render thread lists them, Jolt's job timings), fixed; none in FellowEngine's own.
* **Prepared for the GPU**: the campfire's multigrid with one sweep at its finest level (`fluid/gas_mg_fine_sweeps`)
  — the flame within 0.2 % of the reference, about 29 % less of the solve's cell updates; the driver's pipeline cache
  in the stats beside the process's memory (the soaks' growth); job f364.
* **Regression coverage**: a skinned crowd, the separate render thread's group and the crowd in process groups —
  each picture the default's, bit for bit.

## 1. Development

* `servers/rendering/renderer_rd/forward_clustered/render_forward_clustered.{h,cpp}`: the directional soft-shadow bit
  and the sun's traced and contact masks' bits kept in frames without a directional light
  (`_fellow_stable_sun_masks()`), tile culling's kept bit only while tile culling is on (0167); the specialization log
  prints each shader's code once (`FELLOW_SPEC_SHADER`).
* `modules/fellow_world/fellow_pipeline_warmer_3d.{h,cpp}`, `FellowPipelineWarmer3D.xml`: `shelf` (0167).
* `servers/rendering/rendering_device.{h,cpp}` (`fellow_texture_update_region()`), `rendering_server.h`,
  `rendering_server_default.h`, `storage/texture_storage.h`, `renderer_rd/storage_rd/texture_storage.{h,cpp}`
  (`fellow_texture_2d_update_region()`), `modules/fellow_world/fellow_terrain_3d.{h,cpp}` (`_upload_texture()`, the
  splat maps' rectangles): decision 0168, `streaming/terrain_region_uploads`.
* `modules/fellow_fluid/fellow_shared_shaders.cpp`: decision 0169, `frame/kernel_spirv_cache`; stats
  `cost_glsl_compile_usec`, `cost_glsl_compiles`, `cost_glsl_cached_usec`, `cost_glsl_cached`;
  `RendererSceneRenderRD::fellow_create_versions_for_baking()` (`renderer_scene_render_rd.{h,cpp}`,
  `render_forward_clustered.{h,cpp}`, `effects/fellow_gas.h`), called by `editor/export/shader_baker_export_plugin.cpp`;
  `servers/rendering/fellow_bake_shaders.h` and the nodes' `fellow_bake_shader_codes()` (water, rain, lightning, shallow
  water, burnable, terrain), registered by the modules.
* Decision 0170: `servers/rendering/rendering_server_default.{h,cpp}` (`changes` atomic),
  `core/config/project_settings.cpp` (the flag setters locked), `modules/jolt_physics/spaces/jolt_job_system.cpp`
  (`flush_timings()` under the jobs' lock).
* `servers/rendering/renderer_rd/effects/fellow_gas.cpp`: `fluid/gas_mg_fine_sweeps` (2: as before).
* `servers/rendering/fellow_settings.{h,cpp}`, `rendering_device.cpp`: `pipeline_cache_bytes`;
  `bench/main.gd`: the soak's `pipeline_cache_mb` a second and its growth; `tools/soak/soak_report.py`: in the chart.
* Checks: `bench/pipeline_states_check.gd` (`--sun_size`, `--contact`), `bench/terrain_edit_check.gd`
  (`--region_uploads`), `bench/gas_check.gd` (`--fine_sweeps`), Slice Lab `--slice_warmup_shelf`; the regression
  suite's `crowd` scene (`scripts/crowd.gd`, `crowd_group.gd`; the project setting `fellow_regression/crowd_groups`) and
  its `render_thread` and `crowd_groups` groups.

## 2. Verification

* The image regression suite on lavapipe (editor build): 24 pictures exact, with the new groups (the separate render
  thread: lights, water, rain, fire, the crowd; the crowd in 4 process groups) the same as the default's bit for bit,
  no error; its water, rain and snow scenes again with their kernels read from the new cache: exact.
  The release template built from the same tree against the editor build: the 24 pictures the same, bit for bit.
* `pipeline_states_check.gd` (21 states, soft and contact shadows on): 21 of 21 pictures the same with
  `stable_scene_bits` on and off; on, 5 pipelines after the warm-up, none at night.
* `pipeline_warmup_check.gd`: with the warmer (its shelf of 25 meshes) the camera's turn compiles 0 pipelines (7
  without).
* `terrain_edit_check.gd` on lavapipe: 19 of 19 checks, every texture and mip bit-exact with a rebuild's.
* Slice Lab's 4-minute soak on lavapipe with the specialization log: no engine error (a first version of the shelf
  drew a copy of the burning log with the material the log had freed: errors during the warm-up, fixed by copies that
  follow their sources).
* Physics Lab headless (release template): every case at three or four scales, both engines, to the end.
* ThreadSanitizer (a debug template with `use_tsan`, lavapipe): the regression crowd in 4 process groups with the
  separate render thread, water, rain and fire with it, Jolt's and GodotPhysics' piles, the slice (40 minutes, in its
  warm-up). Three races in stock Godot code, fixed; then no report with an access in Godot's code (the rest are inside
  lavapipe, which is not instrumented). The image regression suite after the fixes: 24 pictures exact.

## 3. Measurements

**Pipelines after the warm-up** (lavapipe, Slice Lab's soak, 4 minutes, weather every 10 s, fire every 15 s, an 80 s
day; decision 0167):

| | before | 0167 |
|---|---|---|
| the warm-up: frames; pipelines | 41; 81 | 43; 76 (17 meshes on its shelf) |
| specialized pipelines a draw waited for after the warm-up | 9 | **1** |
| pipelines compiled after the warm-up | 23 | 9 (8 of them autotune's, prepared ahead) |

**The terrain's edits** (decision 0168), the setting off → on: on lavapipe at 2049² the frame's update 8.78 → 0.36 ms
after a raise stroke, 4.62 → 0.03 ms after a paint stroke. On the RTX laptop (f364, 1920 × 1080):

| | 2049² | 4097² |
|---|---|---|
| the update after a raise stroke (heights and normals) | 12.83 → 0.65 ms | 71.16 → **0.81 ms** |
| the update after a paint stroke (a splat map) | 8.81 → 0.02 ms | 34.42 → **0.02 ms** |
| a frame while raising; while painting (means) | 12.31 → 0.98; 8.06 → 1.11 ms | 63.27 → **0.87**; 29.16 → **0.98 ms** |

**The campfire's pressure solve** (lavapipe, `gas_check.gd`, 32 × 48 × 32 cells, sparse, 180 steps; the residual's RMS
after the last step, and the state's difference from 8 V-cycles'; the work is the solve's cell updates, the finest level
dominating):

| multigrid | residual (RMS) | velocity; smoke; temperature vs 8 cycles | work |
|---|---|---|---|
| 2 cycles, 2 sweeps (decision 0117, the default) | 9.1e-5 | 0.2 %; 0.1 %; 0.1 % | 1 |
| 3 cycles, 2 sweeps | 4.9e-6 | < 0.05 % each | 1.5 |
| 1 cycle, 2 sweeps | 3.0e-3 | 1.6 %; 2.1 %; 1.3 % | 0.5 |
| **2 cycles, 1 sweep at the finest level** | 1.8e-3 | 0.2 %; 0.2 %; 0.1 % | **0.71** |
| 3 cycles, 1 sweep | 2.2e-4 | < 0.05 % each | 1.06 |
| 1 cycle, 3 sweeps | 1.4e-3 | 1.1 %; 1.8 %; 1.0 % | 0.65 |
| SOR, 20 iterations | 1.7e-3 | 4.1 %; 9.6 %; 6.3 % | — |

One sweep at the finest level keeps the flame as close to the reference as the default after 180 steps, for about
29 % less work by this count; the RTX decides (f364: the slice's frame and its gas passes, with 2 and 1).

**Physics Lab headless** (the cloud's two vCPUs, release template; ms a tick, the counts scaled in a fixed world, so
contacts and overlaps grow faster than the counts):

| case (at scale 1) | 0.5 | 1 | 2 | 4 |
|---|---|---|---|---|
| pile (2000 bodies): Jolt; GodotPhysics3D | 5.6; 17.7 | 13.7; 42.4 | 34.5; 105.4 | — |
| chars (250 characters) | 3.8; 7.2 | 7.7; 15.7 | 11.7; 23.7 | 14.7; 28.5 |
| rays (1500 rays, 150 shape casts) | 6.7; 4.3 | 14.0; 10.5 | 31.7; 24.6 | 82.3; 62.9 |
| areas (300 areas, 1000 spheres) | 1.5; 2.0 | 4.0; 5.5 | 14.5; 19.1 | — |

Per doubling: piles ×2.4-2.5 (deeper piles, more contacts a body), areas ×2.8-3.6 (overlaps ∝ areas × spheres), rays
×2.1-2.6 (more queries into more boxes); characters less than ×2 (more of them blocked). Nothing grows faster than its
case's contacts or overlaps. Jolt is 3 times faster than GodotPhysics3D on piles and slower on the query case, where
`perf` puts 70 % of Jolt's time in the 150 ShapeCast3D nodes' bisection of collision tests against the terrain's
trimesh (EPA, GJK) — decision 0095's fast mode (one shape cast per body, off: not stock's results) is the option there.
The two vCPUs show no thread scaling; the RTX laptop's 24 cores would.

**Load time** (decision 0169; lavapipe, the cloud's CPU): a water, a rain and a shallow water's kernels compiled in
28.1 ms, read from the cache in 0.5 ms. Linux exports of the regression suite's scenes (release template), each started
twice from an empty user directory, the shaders compiled at the start ("Shader cache miss"):

| export | baker off: first start; second | baker on, before | baker on, 0169 |
|---|---|---|---|
| fire (gas, burning) | 72; 0 | 3 (the gas's simulation and rendering, light tile culling) | **0** |
| water | 70; 0 | 1 (the water's surface, a material made from code) | **0** |
| rain; lightning; terrain | — | — | 3 (the rain's particle process materials); 0; 0 |

Without the baker the first start took 1.3-1.6 s longer on the cloud's CPU (the water 15.2 against 13.6 s, the fire
12.3 against 11.0 s, lavapipe's own pipeline compilation in both): the GLSL compiled at start, which a GPU does not
shorten.

## 4. Limits

* The process's memory growth in the RTX soaks (about 1.1 GB an hour from 1.7-2.2 GB) is still not explained: the
  driver's pipeline cache is now in the stats and the soak's rows (`pipeline_cache_mb`) for f364 to show whether it
  follows the pipelines compiled. The bench's Forward+ cache file on the laptop is 364.5 MB (saved at f363's end), so
  the cache alone is not most of the process's 2.2 GB at start, as decision 0166 supposed. The cloud cannot: lavapipe's cache does not grow, and the editor build's own
  allocations stayed flat in the lavapipe soaks (static memory 112 → 106 MB in 4 minutes).
* Not baked into exports: the particle process materials FellowEngine's nodes make (Godot writes their code from
  properties the node changes in play: the rain's 3), a burnable's burning material (written from its object's).
* ThreadSanitizer saw lavapipe, not a GPU's driver, and not the whole slice.
* The shelf's copies are lit by the directional lights only; skinned meshes, hidden nodes and particles are not on it.

## 5. Next

Job f364 on the RTX laptop, when the GPU may be used: the terrain's region uploads at 2049² and 4097², the sun's bits
with soft and contact shadows, the campfire's finest sweeps (2 against 1), the slice's 20-minute soak with the shelf and
the pipeline cache's size beside the process's memory.
