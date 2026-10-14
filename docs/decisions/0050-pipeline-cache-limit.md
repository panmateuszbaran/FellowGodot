# 0050 — The pipeline cache file: a size limit, and no crash at 2 GiB

Status: **KEEP, on** (`fellow125`; a robustness fix, no effect on images or frame times) · Branch `fellow/geometry` ·
Setting `rendering/fellow/frame/pipeline_cache_max_mb` (start-up, 64-2000, default 1024) · Code:
`servers/rendering/rendering_device.cpp` (`_load_pipeline_cache`), `drivers/vulkan/rendering_device_driver_vulkan.cpp`
(`pipeline_cache_serialize`), `fellow_settings.*`.

## Problem

`m9_f141` crashed in every run, in every scene and build variant (0xC0000005 in `hash_murmur3_buffer`, called from
`RenderingDeviceDriverVulkan::pipeline_cache_serialize` on the worker thread that saves the pipeline cache). The bench
project's cache file (`%APPDATA%\FellowBench\vulkan\pipelines.forward_plus.*.cache`) had reached 2 146 666 778 bytes.

Stock Godot keeps the Vulkan pipeline cache in one file that it reloads at start-up and saves as it grows; nothing is
ever evicted, so the file collects the pipelines of every shader every build has compiled (here: 125 engine builds and
their experiment variants). The save checksums the data with `hash_murmur3_buffer(const void *, int length)`: once the
size passes `INT32_MAX` the length overflows and the hash reads past the buffer. Any engine whose cache grows that far
(many builds, many materials, long-lived projects) crashes at its next save.

## Change

* At start-up a cache file larger than `frame/pipeline_cache_max_mb` (1024 MiB) is ignored: the cache starts empty and
  the next save replaces the file (a message says so).
* A cache whose data would not fit the checksum's `int` is not saved (warned once); it keeps working in memory.

The pipelines themselves are unchanged; the NVIDIA driver keeps its own cache as well, so a new Godot cache costs the
first frames some compilations, which the bench's warm-up and heat soak absorb.

## Decision

**KEEP** (`fellow125`). `m9_f143` repeats `m9_f141`'s measurement with it.
