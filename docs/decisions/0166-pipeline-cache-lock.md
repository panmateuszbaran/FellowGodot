# 0166 — The pipeline cache saved without the device's lock, its copies not kept

Status: **measuring** (a fix: the same pipelines and frames) · Branch `fellow/geometry` · Code:
`servers/rendering/rendering_device.{h,cpp}` (`update_pipeline_cache()`, `_save_pipeline_cache()`,
`fellow_pipeline_cache_mutex`), `drivers/vulkan/rendering_device_driver_vulkan.{h,cpp}` (`_fellow_keep_cache_header()`).

## Problem

The slice's soak on the RTX kept a few hitches of 170-400 ms that compiled nothing on the main thread (decisions 0156,
0159; f358: three in 30 minutes). Milestone 60's split of the main loop's step (f360, 20 minutes, the warm-up and
decision 0161's stable bits on): five such hitches, each in a frame during which the worker threads spent 0.6-4.2 s
compiling pipelines in the background; the main thread was held 167-241 ms in the scene's process (three) or 241-396 ms
in the RenderingServer's draw (two), while every step FellowEngine times inside the frame (the render graph, the
submissions, the fence) was ordinary.

Stock Godot updates its Vulkan pipeline cache after every pipeline it makes: `update_pipeline_cache()` asks the driver
for the cache's size and, every 3 MB it has grown (`pipeline_cache/save_chunk_size_mb`), starts a task that copies the
whole cache out of the driver (`vkGetPipelineCacheData`) and checksums it (`hash_murmur3_buffer`) for its file. Both
hold the RenderingDevice's lock (`_thread_safe_`), which most of the device's calls take — creating a buffer, a texture,
a uniform set, a framebuffer, freeing one — and FellowBench's cache file is hundreds of MB (decision 0050 met one of
2 GB). While a burst of background compilations grows the cache, a save of hundreds of MB runs under that lock, and the
main thread's next device call waits for it: in a node's process (a mesh, a multimesh's buffer, a material's uniform
set) or in the frame's rendering.

The same code kept two copies of the cache in the process's memory: the file as loaded (the driver's cache has its
data from then on) and the largest copy made for a save, until the next — part of the process's resident memory that
grew in the soaks (1.7 → 2.2 GB in f360's 20 minutes) while FellowEngine's own allocations and video memory stayed flat.

## Change

* `update_pipeline_cache()` and the save take a mutex of their own (`fellow_pipeline_cache_mutex`) instead of the
  device's. A pipeline made while a save or another query holds it does not wait (`try_lock`): the next pipeline
  queries again, as the stock code already did while a save task ran. At exit a save still running is waited for
  before the mutex is taken (which it needs), and the last save made as before.
* The driver keeps only the cache's header after creating the cache from the file and after each save's copy (the
  copy goes to the file, then is freed).

The cache's contents, the pipelines and the file are the same: the driver's pipeline cache is internally synchronized
(Godot does not create it externally synchronized), and pipelines were already made on several threads at once.

## Verification

* lavapipe: runs and exits clean (lavapipe's pipeline cache does not grow, so its saves are not reached there).
* RTX (job f363): f360's 20-minute soak again with the fix — the hitches during background compilation, the process's
  memory.

## Decision

Pending f363.
