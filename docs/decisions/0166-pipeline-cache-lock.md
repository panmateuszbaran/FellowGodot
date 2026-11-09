# 0166 — The pipeline cache saved without the device's lock, its copies not kept

Status: **KEEP** (a fix: the same pipelines and frames) · Branch `fellow/geometry` · Code:
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
* RTX (job f363): f360's 20-minute soak again with the fix, the same arguments (the terrain with its foliage, the
  warm-up, the weather every minute, the fire every two):

| the slice's 20-minute soak, 1080p | f360 (before) | f363 (the fix) |
|---|---|---|
| pipelines compiled after the warm-up | 122 | 151 |
| hitches (twice the median and 10 ms over it) | 16 | **4** |
| hitches over 100 ms | 5 (167-396 ms, in frames whose workers compiled 0.6-4.2 s) | **0** |
| the largest frame | 397 ms | **20.3 ms** |
| 99th, 99.9th percentile | 7.71, 8.81 ms | 6.68, 7.57 ms |
| video memory; objects | 405 → 414 MB; +2 | 405 → 414 MB; +2 |
| the process's memory, start → end (largest) | 1 706 → 2 175 MB (2 178) | 2 176 → 2 577 MB (2 995) |

More pipelines compiled in the background than before, and none held the main thread: every remaining hitch is the
GPU's fence (16-21 ms waited in `rd_fence_wait_usec`), but for one frame of 29 ms drawing in the first half minute.

The memory did not fall. Likely most of the process's memory is the driver's pipeline cache, which FellowBench's
cache file fills at start and which only grows: between the soaks two more builds' pipelines were compiled (f360's
soak, f361's runs), and the start rose from 1.7 to 2.2 GB; the saves' copies now come and go (the largest sample
3.0 GB) instead of one staying. The file's size is not in this measurement (it lives outside the folders the jobs
read; read later, milestone 61: 364.5 MB for Forward+, saved at f363's end — so the cache is a part of the process's
memory at start, not most of it). A
game's cache holds its own pipelines only; the bench's holds every build's (decision 0050's limit, 1 GB, starts it
again).

**Milestone 61 (job f364):** FellowEngine's stats now give the driver's pipeline cache (`pipeline_cache_bytes`) and the
soak writes it each second. The same 20-minute soak with milestone 61's build: the cache 370.7 MB at the start and
371.2 MB at the end — flat — while the process grew 1 348 → 1 589 MB after the warm-up (video memory 405 → 414 MB and
the objects flat, as before). The growth follows the pipelines compiled: 88 after the warm-up, +241 MB (2.7 MB each);
f363's 151, +408 MB (2.7 MB each); in the minutes without pipelines compiled the process stayed level (from the 6th to
the 16th minute 5 pipelines, the process between 1 499 and 1 562 MB). So it is not a leak nor the cache file: each pipeline the driver compiles
keeps its own memory in the process (the cache's data hardly grows: they were in it already). Fewer pipelines after the
warm-up (decision 0167) is less growth. The process's memory at the start is not the build's: 1.25 GB in f362b and
1.98 GB in f363 with the same build, 1.24 GB in f364 (what else the laptop ran: the GPU's memory in use 3.8 GB in f363,
2.7 GB in f364).

## Decision

**KEEP.** The slice's long hitches during background compilation are gone: the largest frame of 20 minutes 397 → 20
ms, with more pipelines compiled than before. The soaks' memory growth is the pipelines compiled, about 2.7 MB each on
the RTX (milestone 61). Not done: a smaller limit for the bench's ever-growing cache file
(`frame/pipeline_cache_max_mb`), a save that does not copy the whole cache (the driver gives it whole).
