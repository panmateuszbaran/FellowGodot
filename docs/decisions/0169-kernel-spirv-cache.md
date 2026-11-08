# 0169 — FellowEngine nodes' compute kernels compiled once per machine, not once per session

Status: **KEEP, on** (exact: the same SPIR-V) · Branch `fellow/geometry` · Setting
`rendering/fellow/frame/kernel_spirv_cache` (on) · Code: `modules/fellow_fluid/fellow_shared_shaders.cpp`
(`FellowSharedShaders::file()`), `servers/rendering/fellow_settings.{h,cpp}` (the stats `cost_glsl_compile_usec`,
`cost_glsl_compiles`, `cost_glsl_cached_usec`, `cost_glsl_cached`).

## Problem

Part of milestone 61's look at load time and cold shader compilation. Godot's own shaders (`ShaderRD`) keep their
SPIR-V in the shader cache, and an exported game can ship them baked; but the compute kernels of FellowEngine's water,
rain (its ripples and the snow's tracks) and shallow water are GLSL text compiled with glslang
(`RDShaderFile::parse_versions_from_text()`) the first time a node of the kind is set up in a session (decision 0154
made it once per session, not once per node) — on the thread adding the node, every session, the editor's and the
game's alike: when the scene loads, or in the middle of a game when the first water or rain of the session is added.

## Change

`FellowSharedShaders::file()` keeps the compiled file as a resource in `user://fellow_kernel_cache/`, named by the
kernel and the MD5 of its source with the engine build's hash: a later session of the same build reads it instead of
compiling (a source or build that changed is a new name). Only a file whose every version and stage compiled is kept;
a cached file that does not load, or holds an error, is compiled again. FellowEngine's stats count the compilations and
the cache's reads with their time.

## Verification

lavapipe, the editor build (the cloud's CPU), a water, a rain and a shallow water added to an empty scene, the user
directory empty and then again: the first session compiles 3 files in 28.1 ms, the second reads 3 in 0.5 ms. The image
regression suite's water, rain and snow scenes (the waves, the ripples, the snow's tracks), their kernels compiled and
then read from the cache: the same pictures as their goldens, bit for bit, no error. (The kernels' share of a load is
small on this CPU; it is the main thread's, and it fell where the first such node of a session is added during play.)

## Decision

**KEEP, on.** Not done: the neural motion model's kernels (`FellowMotionModel`, compiled with the GPU's subgroup size
in their defines) — the same cache would apply; shipping the files with an exported game (a game's first session still
compiles).
