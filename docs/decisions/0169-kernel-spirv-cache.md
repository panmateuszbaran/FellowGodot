# 0169 — Compiling less at load: FellowEngine's kernels cached, its effects and node materials baked into exports, the rainbow made ahead

Status: **KEEP, on** (exact: the same SPIR-V) · Branch `fellow/geometry` · Setting
`rendering/fellow/frame/kernel_spirv_cache` (on) · Code: `modules/fellow_fluid/fellow_shared_shaders.cpp`
(`FellowSharedShaders::file()`); `RendererSceneRenderRD::fellow_create_versions_for_baking()`
(`renderer_scene_render_rd.{h,cpp}`, `forward_clustered/render_forward_clustered.{h,cpp}`, `effects/fellow_gas.h`),
called by `editor/export/shader_baker_export_plugin.cpp`; `servers/rendering/fellow_bake_shaders.h`, `fellow_kernel_cache.{h,cpp}`,
`modules/fellow_motion/fellow_motion_model.cpp` and the nodes'
`fellow_bake_shader_codes()` (water, rain, lightning, shallow water, burnable, terrain); the rainbow made ahead:
`renderer_rd/shader_rd.{h,cpp}` (`fellow_version_compile_start()`), `effects/fellow_rainbow.cpp`, `effects/fellow_weather.h`
(`State::rainbow_enabled`), `modules/fellow_fluid/fellow_rain_3d.cpp`; `servers/rendering/fellow_settings.{h,cpp}` (the stats
`cost_glsl_compile_usec`, `cost_glsl_compiles`, `cost_glsl_cached_usec`, `cost_glsl_cached`).

## Problem

Milestone 61's look at load time and cold shader compilation, two gaps:

* **The nodes' kernels compiled every session.** Godot's own shaders (`ShaderRD`) keep their SPIR-V in the shader
  cache; the compute kernels of FellowEngine's water, rain (its ripples, the snow's tracks) and shallow water are GLSL
  text compiled with glslang (`RDShaderFile::parse_versions_from_text()`) the first time a node of the kind is set up
  in a session (decision 0154 made it once per session, not once per node): on the thread adding the node, every
  session, at load or in the middle of a game when the first water or rain of the session is added.
* **The effects missing from baked exports.** Godot 4.5's shader baker compiles an export's shaders into it, so that a
  game's first start compiles none: it bakes the `ShaderRD` versions that exist in the editor when the export starts,
  and FellowEngine makes most of its effects when they are first used. A Linux export of the regression suite's fire
  scene with the baker on, run on lavapipe with an empty user directory, still missed the cache for the gas volume's
  simulation and rendering and for light tile culling (3 shaders compiled at its first start; 72 with the baker off).

## Change

* `FellowKernelCache::parse()` (`servers/rendering/fellow_kernel_cache.{h,cpp}`), used by `FellowSharedShaders::file()`
  and by the neural motion model (`FellowMotionModel`, its kernels compiled with the GPU's subgroup size in their
  defines), keeps the compiled file as a resource in `user://fellow_kernel_cache/`, named by the kernel and the MD5 of
  its source and defines with the engine build's hash: a later session of the same build reads it instead of
  compiling (a changed source, define or build is a new name). Only a file whose every version and stage compiled is kept; one
  that does not load, or holds an error, is compiled again. FellowEngine's stats count the compilations and the reads
  with their time.
* `RendererSceneRenderRD::fellow_create_versions_for_baking()`, called by the shader baker when an export starts (after
  it enables the renderer's features): FellowEngine's effects made if they do not exist yet — motion blur, exposure,
  bloom, atmosphere, clouds, rainbow, contact shadows, the gas volumes' simulation (both precisions), rendering and
  burning, light tile culling, occlusion and MultiMesh culling — so that their versions are baked. Not the ray-traced
  effects without ray tracing, nor adaptive VRS without VRS.
* The spatial shaders FellowEngine's nodes make from code (`servers/rendering/fellow_bake_shaders.h`): each module
  registers a function giving them — the water's surface with and without screen-space reflections, the rain's drops
  and snowflakes, the lightning's bolt and glow, the shallow water's surface, a burnable's ash heap, the terrain — and
  the same hook creates them, preprocessed as a Shader resource's code is, as embedded shaders, which the baker bakes as
  it bakes the engine's own (freed with the renderer).
* **The rainbow made ahead** (found by the RTX's soak, job f364): FellowEngine's effects are made when first used, and
  in a game all are used from its first frames but the rainbow, made at the first bow — in the middle of play, its
  shader compiled on the drawing thread (`FellowRainbow`'s constructor waited for it). With the shader cache it is read
  without a hitch (f363's first rain: no frame over 6 ms); without — the first session of a new build, whose shaders all miss Godot's shader cache (its key
  holds the build's hash), or a player's first without a baked export — glslang ran 77 ms on the RTX laptop: f364's
  frame of 89.6 ms at the first rain. Now the renderer makes the rainbow as soon as an active `FellowRain3D` with a
  `rainbow` is in the scene (`FellowWeather::State::rainbow_enabled`), its shader compiled on the worker threads
  (`ShaderRD::fellow_version_compile_start()`: a version's compilation, or its read from the cache, started without
  waiting for it), its pipeline made at the first bow.

## Verification

* lavapipe, the editor build (the cloud's CPU): a water, a rain and a shallow water added to an empty scene, the user
  directory empty and then again: the first session compiles 3 files in 28.1 ms, the second reads 3 in 0.5 ms.
* The neural motion model (`modules/fellow_motion/tests/fmb_gpu_run.gd`, MotionBricks' f16 weights, lavapipe): its
  load 26.4 s with the kernels compiled, **0.86 s** with them read from the cache; every output and activation the
  same bit for bit.
* The image regression suite's water, rain and snow scenes (the waves, the ripples, the snow's tracks), their kernels
  compiled and then read from the cache: the same pictures as their goldens, bit for bit, no error.
* Exports (Linux, release template, the baker on) of the regression suite's scenes, each run twice on lavapipe from an
  empty user directory (`--verbose`: "Shader cache miss" lines), the shaders compiled at the first start:

| export | baker off | baker on, before | 0169 |
|---|---|---|---|
| fire (gas, burning) | 72 | 3 (the gas's simulation and rendering, light tile culling) | **0** |
| water | 70 | 1 (the water's surface, made from code) | **0** |
| rain, lightning, terrain | — | — | 3 (the rain's particle process materials); 0; 0 |

  None at the second start. The image regression suite: 24 pictures exact.
* The rainbow (lavapipe, the slice's soak with its weather every 15 s, from an empty shader cache, `--verbose`): before,
  its shader was the one compiled after the soak had started — at the first rain (with the cache, read there; f364's
  89.6 ms on the RTX without); with the change it compiles in the warm-up's first frames with the clouds' and the
  atmosphere's, and no shader is compiled in play. The regression suite's rain scene (with a rainbow): exact. On the
  RTX (f365) the same build's next soak, its shader cache warm, had its first rain's largest frame at 9.8 ms, no
  hitch: the 89.6 ms was the cold cache's compilation, which the change moves to the worker threads at the start.

## Decision

**KEEP, on.** Not done: the particle process materials FellowEngine's nodes make (the rain's drops, splashes and
flakes: Godot's ParticleProcessMaterial writes their code from properties the node changes at run time); a burnable's
burning material (written from its object's own material); shipping the kernel cache with an export (a game's first session
still compiles its kernels, 28 ms here).
