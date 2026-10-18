# 0061 — Shaders read from the shader cache: created in parallel, and when first asked for

Status: **measuring** (`fellow149`, `fellow150`) · Branch `fellow/geometry` · Settings `rendering/fellow/frame/parallel_shader_create`,
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

`m12_f176_load` (pending).
