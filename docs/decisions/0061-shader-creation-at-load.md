# 0061 — Shaders read from the shader cache: created in parallel, and when first asked for

Status: **KEEP**, both on (`fellow149`, `fellow150`) · Branch `fellow/geometry` · Settings `rendering/fellow/frame/parallel_shader_create`,
`rendering/fellow/frame/lazy_shader_variants` (both on) · Code: `renderer_rd/shader_rd.*` (`_load_from_cache`,
`_fellow_create_cached_variant`, `_fellow_create_lazy`, `version_get_shader`), `rendering_device.*`
(`shader_create_from_bytecode_with_samplers` without the device's lock where the driver allows it), `fellow_settings.*`
(stats `cost_*`); bench: `fellow_stats_scene_built`, `fellow_stats_first_frame`; `.tools/scripts/load_times.py`,
`load_costs.py`.

## Problem

FellowEngine takes longer than stock to load a scene: in `m11_f168_final` the time from the start of the scene's build
to its first frame was 0.1-1.0 s longer in every scene (Bistro exterior 4.62 against 3.62 s, Sponza 4.55 against 3.50 s,
PBR Lab 1.42 against 0.90 s). Building the scene took about as long as stock; the **first frame** took 1.2-2.1 times
stock's (Bistro exterior 1.55 against 0.80 s).

`fellow148` counts where loading spends its time (stats `cost_*`, recorded by the bench when the scene is built and
after its first frame; `m12_f174_load`, three runs each of stock, the defaults and FellowEngine disabled):

| scene | first frame: stock | defaults | FellowEngine disabled | of which shaders created from the shader cache (disabled) |
|---|---:|---:|---:|---|
| Bistro exterior | 735 ms | 1314 | 1161 | 240 shaders, 1070 ms |
| Bistro interior | 670 | 1131 | 1046 | 220, 989 ms |
| Sponza | 803 | 1544 | 1200 | 240, 1114 ms |
| PBR Lab | 440 | 1002 | 735 | 160, 693 ms |
| Stress Lab | 383 | 795 | 630 | 140, 563 ms |
| Lights Lab | 193 | 362 | 342 | 80, 304 ms |
| City Lab | 307 | 470 | 433 | 80, 300 ms |

Almost all of the first frame is the render thread creating shader objects (`shader_create_from_bytecode`: the SPIR-V
decompressed and decoded, its specialization data parsed, the shader modules and layouts created) one after another,
4-5 ms each for the scene shader's variants. The first frame enables the scene shader's advanced group (motion vectors,
for TAA), and `ShaderRD::enable_group()` then creates the group's 17 variants for every material's version, of which
a frame uses one or two. With FellowEngine disabled the time is the same: FellowEngine's code is in the scene shader
whatever its settings, and its variants are larger than stock's.

## Change

* **In parallel** (`frame/parallel_shader_create`): a version's variants read from the cache are created by the calling
  thread and the worker threads that start in time (`FellowParallelFor`; the caller never waits for a worker to become
  free, only for variants being created). The driver's part of creating a shader runs outside the device's lock where
  the driver allows it (Vulkan: its creation calls are thread-safe; the device's lock is taken to register the shader).
* **When first asked for** (`frame/lazy_shader_variants`): the variants' bytes are read from the cache as before (and
  kept, as stock keeps them), and each variant's shader is created by `version_get_shader()` the first time it is asked
  for. A variant whose group was disabled keeps its placeholder RID, filled when created.

The same shader modules: nothing reaches the GPU differently.

## Verification

lavapipe: City Lab captures with each setting on and off, 0 differing pixels; PBR Lab's other captures identical (its
overview differs between any two runs, ubershader frames during pipeline compilation); Vulkan validation with
synchronization validation: no messages. PBR Lab: 285 shaders created instead of 500 (lazy).

## Measured

`m12_f176_load` (`fellow150`; three runs each, after a pass that filled the shader caches of stock and `fellow150`;
`d` the defaults, `pl` parallel only, `off` both off, start-up overrides), time from the start of the scene's build to its
first frame, and the first frame alone:

| scene | stock | 0061 off | parallel only | **defaults** | defaults against stock |
|---|---:|---:|---:|---:|---:|
| Bistro exterior | 3.78 s (0.79) | 4.27 (1.28) | 3.21 (0.25) | **3.08 (0.28)** | **−18 %** |
| Bistro interior | 1.81 (0.67) | 2.39 (1.18) | 1.34 (0.20) | **1.27 (0.18)** | **−30 %** |
| Sponza | 3.78 (0.78) | 4.53 (1.29) | 3.34 (0.23) | **3.31 (0.23)** | **−12 %** |
| Foliage Lab | 0.91 (0.30) | 1.06 (0.47) | 0.71 (0.10) | **0.66 (0.09)** | **−27 %** |
| PBR Lab | 0.86 (0.48) | 1.21 (0.80) | 0.51 (0.14) | **0.51 (0.17)** | **−41 %** |
| Stress Lab | 0.53 (0.41) | 0.80 (0.65) | 0.25 (0.14) | **0.24 (0.16)** | **−55 %** |
| City Lab | 0.48 (0.34) | 0.63 (0.48) | 0.33 (0.19) | **0.32 (0.19)** | **−34 %** |
| Lights Lab | 0.28 (0.24) | 0.46 (0.41) | 0.14 (0.09) | **0.11 (0.07)** | **−62 %** |

(first frame in parentheses.) Shaders created during the first frame: 90-247 with either setting off, 20-55 with
lazy variants; the render thread's time in them during the first frame: 341-1245 ms off, 51-156 ms in parallel, 2-7 ms
lazy (the variants a frame needs are created by the threads compiling its pipelines). Created in parallel, the same
shaders take 1.5-1.6 times the CPU time in all (0.6-1.9 s per first frame across the threads): the lazy creation
removes that work instead.

With the shader cache cold (a new build's first run) the variants are compiled from source on the worker threads as
before; `m12_f174` (`fellow148`) measured those runs at 1.4-2.2 s for the first frame.

## Decision

KEEP, both on (`fellow149`, `fellow150`): FellowEngine now loads every scene faster than stock (−12 to −62 % to the first
frame), where it was 0.4-1.2 s slower. The images are the same (the same shader modules).
