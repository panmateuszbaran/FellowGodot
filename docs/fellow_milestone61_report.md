# FellowEngine — milestone 61 report (what the CPU could still do without a GPU, then the RTX's measurements)

Status: **FINAL** (2026-11-09): checked in the cloud (lavapipe, headless), measured on the RTX laptop (jobs f364,
f365). Branch `fellow/geometry`. Decision records `docs/decisions/0167-late-pipelines.md`,
`0168-terrain-region-uploads.md`, `0169-kernel-spirv-cache.md`, `0170-thread-sanitizer.md`; RTX results added to
`0166-pipeline-cache-lock.md`.

## 0. Result

Work the cloud's CPU and lavapipe could check and measure while the RTX laptop's GPU was not to be used — what was
left of mid-game pipeline compilation, the terrain's editing cost, load-time compilation, the threading under a race
detector — then measured on the RTX laptop, whose soak also found a shader compiled at the first rain.

* **Fewer pipelines after the warm-up** (decision 0167): the sun's soft and contact shadow bits kept at night, tile
  culling's bit no longer flipping while autotune has it off, and the warmer's new shelf (a copy of every visible
  mesh, MultiMesh and terrain foliage kind drawn from a view 4 km up, hidden from other cameras by its range). The
  slice's 20-minute soak on the RTX compiles **88 pipelines after its warm-up instead of 151** (while it snowed 66 →
  8); on lavapipe the draws that waited for a pipeline 9 → 1. The same image.
* **The terrain's edits upload only what they changed** (decision 0168): on the RTX a 4097² map's frame while
  sculpting **63.3 → 0.9 ms**, while painting 29.2 → 1.0 ms (the update after a stroke 71.2 → 0.8 ms and 34.4 → 0.02
  ms), the same textures bit for bit.
* **Compiling less at load and in play** (decision 0169): the water's, rain's, shallow water's and the neural motion
  model's GLSL kept as SPIR-V after its first compilation (the motion model's load 26.4 → 0.86 s on the cloud's CPU);
  Godot's shader baker now bakes FellowEngine's effects and the materials its nodes make from code (baked exports of
  the fire and water scenes compile no shader at their first start instead of 3 and 1); and the rainbow, the one
  effect first used in the middle of a game, made ahead with its shader compiled on the worker threads — on the RTX
  its compilation at the first rain of a new build was the soak's one long frame, 89.6 ms.
* **ThreadSanitizer** (decision 0170): milestone 60's threading run under the sanitizer on lavapipe — three data races,
  all in stock Godot code (the RenderingServer's change counter written from the crowd's worker threads, project
  settings' flags set while the render thread lists them, Jolt's job timings), fixed; none in FellowEngine's own.
* **The soaks' memory growth explained** (decision 0166): the driver's pipeline cache stays flat (370.7 → 371.2 MB in
  20 minutes) while the process grows with the pipelines compiled — about 2.7 MB each, in f363's soak and f364's
  alike, level in the minutes without. Not a leak; fewer pipelines after the warm-up, less growth (+241 MB against
  +408).
* **The campfire's finest sweeps**: one multigrid sweep at the finest level (`fluid/gas_mg_fine_sweeps` 1) saves 0.08
  ms of the RTX's 0.65 ms pressure solve, its residual 20 times larger: the default stays 2.
* **Regression coverage**: a skinned crowd, the separate render thread's group and the crowd in process groups —
  each picture the default's, bit for bit.
* **No cost to the frame**: the slice on the RTX with milestone 60's and 61's templates interleaved (f365): 4.99 and
  5.05 ms, inside the runs' noise.

## 1. Development

* `servers/rendering/renderer_rd/forward_clustered/render_forward_clustered.{h,cpp}`: the directional soft-shadow bit
  and the sun's traced and contact masks' bits kept in frames without a directional light
  (`_fellow_stable_sun_masks()`), tile culling's kept bit only while tile culling is on (0167); the specialization log
  prints each shader's code once (`FELLOW_SPEC_SHADER`); the rainbow made when a rain node may show one (0169).
* `modules/fellow_world/fellow_pipeline_warmer_3d.{h,cpp}`, `FellowPipelineWarmer3D.xml`: `shelf` (0167).
* `servers/rendering/rendering_device.{h,cpp}` (`fellow_texture_update_region()`), `rendering_server.h`,
  `rendering_server_default.h`, `storage/texture_storage.h`, `renderer_rd/storage_rd/texture_storage.{h,cpp}`
  (`fellow_texture_2d_update_region()`), `modules/fellow_world/fellow_terrain_3d.{h,cpp}` (`_upload_texture()`, the
  splat maps' rectangles): decision 0168, `streaming/terrain_region_uploads`.
* `servers/rendering/fellow_kernel_cache.{h,cpp}`, used by `modules/fellow_fluid/fellow_shared_shaders.cpp` and
  `modules/fellow_motion/fellow_motion_model.cpp`: decision 0169, `frame/kernel_spirv_cache`; stats
  `cost_glsl_compile_usec`, `cost_glsl_compiles`, `cost_glsl_cached_usec`, `cost_glsl_cached`;
  `RendererSceneRenderRD::fellow_create_versions_for_baking()` (`renderer_scene_render_rd.{h,cpp}`,
  `render_forward_clustered.{h,cpp}`, `effects/fellow_gas.h`), called by `editor/export/shader_baker_export_plugin.cpp`;
  `servers/rendering/fellow_bake_shaders.h` and the nodes' `fellow_bake_shader_codes()` (water, rain, lightning, shallow
  water, burnable, terrain), registered by the modules; `ShaderRD::fellow_version_compile_start()`
  (`renderer_rd/shader_rd.{h,cpp}`), `effects/fellow_rainbow.cpp`, `effects/fellow_weather.h`
  (`State::rainbow_enabled`), `modules/fellow_fluid/fellow_rain_3d.cpp`.
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
  With the rainbow made ahead (0169): the 24 pictures again exact, editor build and release template.
* `pipeline_states_check.gd` (21 states, soft and contact shadows on): 21 of 21 pictures the same with
  `stable_scene_bits` on and off; on, 5 pipelines after the warm-up, none at night — on lavapipe and on the RTX.
* `pipeline_warmup_check.gd`: with the warmer (its shelf of 25 meshes) the camera's turn compiles 0 pipelines (7
  without).
* `terrain_edit_check.gd`: 19 of 19 checks, every texture and mip bit-exact with a rebuild's, on lavapipe and on the
  RTX (2049² and 4097²).
* Slice Lab's 4-minute soak on lavapipe with the specialization log: no engine error (a first version of the shelf
  drew a copy of the burning log with the material the log had freed: errors during the warm-up, fixed by copies that
  follow their sources). The slice's 20-minute soak on the RTX (f364): no engine error.
* The rainbow (lavapipe, the slice's soak with its weather every 15 s, from an empty shader cache, `--verbose`): its
  shader, before the one compiled in play (at the first rain), now compiles in the warm-up's first frames with the
  clouds' and the atmosphere's; no shader compiled in play, no engine error.
* Physics Lab headless (release template): every case at three or four scales, both engines, to the end.
* ThreadSanitizer (a debug template with `use_tsan`, lavapipe): the regression crowd in 4 process groups with the
  separate render thread, water, rain and fire with it, Jolt's and GodotPhysics' piles, the slice (40 minutes, in its
  warm-up). Three races in stock Godot code, fixed; then no report with an access in Godot's code (the rest are inside
  lavapipe, which is not instrumented). The image regression suite after the fixes: 24 pictures exact.

## 3. Measurements

**Pipelines after the warm-up** (decision 0167). Lavapipe, Slice Lab's soak, 4 minutes, weather every 10 s, fire
every 15 s, an 80 s day:

| | before | 0167 |
|---|---|---|
| the warm-up: frames; pipelines | 41; 81 | 43; 76 (17 meshes on its shelf) |
| specialized pipelines a draw waited for after the warm-up | 9 | **1** |
| pipelines compiled after the warm-up | 23 | 9 (8 of them autotune's, prepared ahead) |

The RTX laptop, the slice's 20-minute soak (1080p, the terrain and its foliage, the warm-up, the weather every minute,
the fire every two; release template):

| | f363 (milestone 60) | f364 (milestone 61) |
|---|---|---|
| pipelines compiled after the warm-up | 151 | **88** |
| of them while it snowed; while it rained | 66; 31 | 8; 14 |
| hitches after the warm-up | 4, all the GPU's fence | 10: 9 the GPU's fence (14-20 ms), 1 the rainbow's shader (89.6 ms) |
| the driver's pipeline cache, start → end | — | 370.7 → 371.2 MB |
| the process's memory after the warm-up → end | 2 169 → 2 577 MB (+408) | 1 348 → 1 589 MB (+241) |
| video memory; objects | 405 → 414 MB; +2 | 405 → 414 MB; +2 |

The 89.6 ms frame was the first rain's rainbow: its shader compiled when the frame drew the first bow, 77 ms of glslang,
because a new build's shaders all miss Godot's shader cache (f363's build had been run with rain before, in f362b); the
rainbow is now made ahead, its shader compiled on the worker threads (decision 0169). The first 3 minutes again with
the same build (f365, the shader cache warm): no hitch after the warm-up, the first rain's largest frame 9.8 ms. The memory grows by about 2.7 MB
a pipeline compiled in both soaks, level in the minutes without (decision 0166).

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
| 2 cycles, 1 sweep at the finest level | 1.8e-3 | 0.2 %; 0.2 %; 0.1 % | 0.71 |
| 3 cycles, 1 sweep | 2.2e-4 | < 0.05 % each | 1.06 |
| 1 cycle, 3 sweeps | 1.4e-3 | 1.1 %; 1.8 %; 1.0 % | 0.65 |
| SOR, 20 iterations | 1.7e-3 | 4.1 %; 9.6 %; 6.3 % | — |

On the RTX (f364, the slice at 1080p, clear weather, the GPU's passes timed): the pressure solve 0.654 ms with 2
sweeps, **0.572 ms** with 1 (−0.08 ms, 12 %); the GPU's frame 4.07 and 4.08 ms, the frame 5.25 and 5.18 ms — inside
the session's noise (6 interleaved runs each: 5.51 and 5.38 ms). Two sweeps stay the default: 0.08 ms is under 2 % of
the GPU's frame and the residual is 20 times larger with one; `fluid/gas_mg_fine_sweeps` 1 is there for a game that
wants it.

**The slice's frame, milestone 60 against 61** (f365, interleaved in one session; f364's frame was 15 % over f361b's,
its GPU passes broadly slower with a thermal slowdown reported): milestone 60's template 4.99 ms (the GPU's 3.84 ms),
milestone 61's 5.05 ms (3.89 ms), 3 runs each in the order ABBA, 0.05-0.08 ms between runs; with the GPU's passes
timed 5.03 against 5.05 ms, no pass apart by more than its own noise. No difference beyond the noise: f364's slower
frame was the laptop's that night — milestone 60's own template measured 4.99 ms in f365 against f361b's 4.58.

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

* The pipelines still compiled after the warm-up on the RTX (88 in 20 minutes): the release template's soak does not
  log their keys; omni lights (the lightning's), particles and skinned meshes are not on the warmer's shelf.
* The soak's remaining hitches are the GPU's fence: frames whose CPU waited 14-20 ms for the GPU, a few in 20 minutes,
  as in milestone 60.
* Not baked into exports: the particle process materials FellowEngine's nodes make (Godot writes their code from
  properties the node changes in play: the rain's 3), a burnable's burning material (written from its object's).
* A new build's first session compiles every shader Godot's cache does not have (its key holds the build's hash): the
  bench's first run of a build is not a player's first run of a baked export.
* ThreadSanitizer saw lavapipe, not a GPU's driver, and not the whole slice.
* The shelf's copies are lit by the directional lights only; skinned meshes, hidden nodes and particles are not on it.

## 5. Next

The pipelines left after the warm-up on the RTX (a soak with the specialization log, `FELLOW_SPECIALIZATION_LOG=1`, to
name them: the storm's and the blizzard's); the particle process materials in baked exports; the frame's GPU time on
the RTX, now that the laptop's thermal state moves it by more than most changes measure (a fixed clock, or the
GPU's own counters).
