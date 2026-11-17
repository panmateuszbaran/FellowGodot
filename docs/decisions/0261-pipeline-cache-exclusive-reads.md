# 0261 — The pipeline cache read only while no pipeline is created (Intel's driver)

Status: **KEEP** (a fix; `rendering/fellow/frame/pipeline_cache_exclusive`, 2: Intel GPUs) · Branch
`fellow/geometry` · Code: `drivers/vulkan/rendering_device_driver_vulkan.{h,cpp}` (a reader-writer lock around the
pipeline cache), `servers/rendering/rendering_device.cpp` (`update_pipeline_cache()` with no size yet),
`servers/rendering/fellow_settings.cpp` · Checks: `benchmarks/project/bench/dithered_fade_check.gd` on the laptop's
Intel GPU (jobs f476-f478).

## Problem

FellowEngine had run on one GPU, the laptop's RTX. The laptop's Core Ultra 9 275HX has an Intel GPU as well (it drives
the panel; Godot lists it as Vulkan device 1, Intel's Windows driver 32.0.101.6629). The first run on it (job f476:
`dithered_fade_check.gd`'s shadow section, the release template, an empty pipeline cache) crashed after a few seconds,
inside Intel's driver (`igvk64.dll`, a read at 0x160): in `vkGetPipelineCacheData`, called by
`RenderingDeviceDriverVulkan::pipeline_cache_serialize()` from `RenderingDevice::_save_pipeline_cache()` on a worker
thread — the periodic save of the pipeline cache every 3 MB it grows — while the scene's pipelines were being created
on other threads. The same run again did not crash (f477): a race.

Godot creates pipelines on several threads at once without the device's lock (the Vulkan pipeline cache is internally
synchronized for creation), and reads the cache's data for the save on another; stock Godot's save held the device's
lock, which creation does not take, and FellowEngine's (decision 0166) a mutex of its own: neither keeps the read apart
from a creation. NVIDIA's driver takes it; Intel's does not, at least while the cache is filling — a game's first
minutes on that GPU.

## Change

`rendering/fellow/frame/pipeline_cache_exclusive` (0 off, 1 on, 2 on Intel GPUs — the default): pipelines are created
under the read side of a reader-writer lock of the driver's (many at once, as before), and the cache's data — its size,
queried after each new pipeline, and its copy for the file — read under the write side, which is only ever taken when
free: a size query while a pipeline is being created returns the size known (the next one sees it), a save waits on its
worker thread for a moment no pipeline is being created. A creation never waits for a save to start; it waits only
while one copies (the copy is of the cache's few MB on Intel, where the setting is on by default).
`update_pipeline_cache()` returns when the driver has no size yet instead of reporting an error.

## Verification

On the laptop's Intel GPU (`--gpu-index 1`, the release template):

* **The race provoked** (job f482; `bench/pipeline_cache_stress.gd`, new): a compute shader whose code a
  specialization constant changes, 8 threads creating 150 pipelines each with constants no run used before, the cache
  saved after almost every one (`save_chunk_size_mb` 0.001), each run from an empty cache, ten rounds interleaved:

| `pipeline_cache_exclusive` | runs | crashed | pipelines a second |
|---|---:|---:|---:|
| 0 (off: as before) | 10 | **10** (each within 5 s, Intel's driver at the same address, a read at 0x160) | — |
| 2 (the default: on for Intel) | 10 | **0** (1 200 pipelines each) | 45.8-52.2 |
| stock Godot 4.7.2's template (job f484) | 5 | **5** (an access violation within 5 s; stock has no crash report) | — |

Stock Godot crashes the same way: the race is stock's (its save under the device's lock, which creation does not take),
not decision 0166's.

* **The scenes' own saves** (jobs f478, f479: `dithered_fade_check.gd`'s shadow section from an empty cache, saves
  every 3 MB or 0.05 MB the cache grows, with and without Frontier Lab's settings): 24 runs of milestone 135's build and
  24 of the fix's, none crashed — the race is rare in a scene's start (f476's one crash), certain under the stress.
* On the RTX with the reads forced exclusive (1): the check as before (f478).

## Decision

Keep, on Intel GPUs by default: no crash where the race crashed, nothing waiting where it did not; NVIDIA's driver
keeps the lock-free reads (decision 0166's measurements).

## Rejected / not done

* **The device's lock or a plain mutex around both**: a creation would wait for a whole save, the hitches decision
  0166 removed; and every creation would serialize with the others.
* **No saves until exit on Intel**: a crash or a killed game would lose the cache; the saves are kept, between
  creations.
* **On every GPU**: NVIDIA's driver reads the cache safely while creating (decision 0166's soaks: hours of saves during
  background compilation); its saves of hundreds of MB would make creations wait.
