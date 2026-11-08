# 0169 — Compiling less at load: FellowEngine's kernels cached, its effects baked into exports

Status: **KEEP, on** (exact: the same SPIR-V) · Branch `fellow/geometry` · Setting
`rendering/fellow/frame/kernel_spirv_cache` (on) · Code: `modules/fellow_fluid/fellow_shared_shaders.cpp`
(`FellowSharedShaders::file()`); `RendererSceneRenderRD::fellow_create_versions_for_baking()`
(`renderer_scene_render_rd.{h,cpp}`, `forward_clustered/render_forward_clustered.{h,cpp}`, `effects/fellow_gas.h`),
called by `editor/export/shader_baker_export_plugin.cpp`; `servers/rendering/fellow_settings.{h,cpp}` (the stats
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

* `FellowSharedShaders::file()` keeps the compiled file as a resource in `user://fellow_kernel_cache/`, named by the
  kernel and the MD5 of its source with the engine build's hash: a later session of the same build reads it instead of
  compiling (a changed source or build is a new name). Only a file whose every version and stage compiled is kept; one
  that does not load, or holds an error, is compiled again. FellowEngine's stats count the compilations and the reads
  with their time.
* `RendererSceneRenderRD::fellow_create_versions_for_baking()`, called by the shader baker when an export starts (after
  it enables the renderer's features): FellowEngine's effects made if they do not exist yet — motion blur, exposure,
  bloom, atmosphere, clouds, rainbow, contact shadows, the gas volumes' simulation (both precisions), rendering and
  burning, light tile culling, occlusion and MultiMesh culling — so that their versions are baked. Not the ray-traced
  effects without ray tracing, nor adaptive VRS without VRS.

## Verification

* lavapipe, the editor build (the cloud's CPU): a water, a rain and a shallow water added to an empty scene, the user
  directory empty and then again: the first session compiles 3 files in 28.1 ms, the second reads 3 in 0.5 ms.
* The image regression suite's water, rain and snow scenes (the waves, the ripples, the snow's tracks), their kernels
  compiled and then read from the cache: the same pictures as their goldens, bit for bit, no error.
* Exports (Linux, release template, the baker on) of the regression suite's fire and water scenes, each run twice on
  lavapipe from an empty user directory (`--verbose`: "Shader cache miss" lines): fire 3 → **0** shaders compiled at
  the first start (72 without the baker), water 1 (70 without): the water's surface, a material its node makes from
  code.

## Decision

**KEEP, on.** Not done: materials FellowEngine's nodes make from code (the water's surface, the rain's drops) — not
resources of the export, the baker does not see them; the neural motion model's kernels (`FellowMotionModel`, compiled
with the GPU's subgroup size in their defines); shipping the kernel cache with an export (a game's first session
still compiles its kernels, 28 ms here).
