# 0233 — The staging trim off by default: a freed memory block holds the frame up

Status: **KEEP** (`rendering/fellow/frame/transfer_staging_trim` off by default; decision 0225's option stays) · Branch
`fellow/geometry` · Code: `servers/rendering/fellow_settings.cpp`; the driver's allocations counted in
`drivers/vulkan/rendering_device_driver_vulkan.cpp` (`_initialize_allocator()`),
`servers/rendering/fellow_memory_counters.h` · Checks: the laptop's Sponza streamed (jobs f447-f451), the hitch sampler
(decision 0231).

## Problem

Sponza streamed on the laptop's RTX had a frame of 25-36 ms in about one 20-second flythrough of four, the GPU's 5-9 ms,
in every variant of milestones 114-116 — none of FellowEngine's counters in those frames. Decision 0231's hitch sampler
found the render thread off the CPU inside `vkQueueSubmit()` (the async compute split's submission), waiting for a lock
in the NVIDIA driver, while a worker thread was in the kernel freeing device memory (`NtGdiDdDDIDestroyAllocation2`).

## Measurement

FellowEngine's statistics now count the driver's device memory allocations and frees as the memory allocator (VMA) makes
them, with their time (`memory_allocations`, `_bytes`, `_usec`, `_max_usec`, `memory_frees`, `_bytes`, `_usec`).
Recorded frame by frame (job f450, six runs): every long frame freed one 256 MiB block of the allocator, 28-30 ms in
`vkFreeMemory()`, and its `vkQueueSubmit()` took as long; the frame before it decision 0225 had released idle transfer
workers' staging buffers, the last ones in that block of host memory (the allocator frees a second empty block). No
other frees during the flythroughs; one allocation of a 256 MiB block a run, 0.3 ms.

Job f451 (the release template at 412a919365, Sponza streamed at 1920 × 1080, eight alternating runs each; a stall: a
frame over 16.7 ms whose GPU work is under 60 % of it):

| | frames over 29 ms | stalls (all) | longest frame | frees of a block (their time) | staging at the end | the driver's memory at the end |
|---|---:|---:|---:|---:|---:|---:|
| the trim on (decision 0225) | 3 (31.7-36.2 ms) | 6 | 14.5-36.2 ms | 3 of 256 MiB (30-34 ms each), 1 of 64 MiB (0.9 ms) | 128-257 MiB | 2 470-2 598 MiB |
| the trim off | 0 | 0 | 14.4-20.2 ms | 0 | 512 MiB | 2 853 MiB |

Each frame over 29 ms came with the free of a 256 MiB block, its `vkQueueSubmit()` as long (29-65 ms); the trim's other
stalls (20-22 ms) were frames waiting for the GPU. The GPU's frame the same either way: 6.6-6.8 ms, except in runs
during which the GPU was busier with something else (7.2-10.9 ms in both variants; between jobs `nvidia-smi` found it
26-29 % busy with no FellowEngine running, then 7 %).

## Decision

The trim off by default: no freed block holds a frame up. It keeps up to 512 MiB of host memory the driver counts as
video memory (four workers' staging after Sponza's streaming). A game short of memory can turn it on, knowing that a
release emptying one of the allocator's blocks costs a frame about 30 ms on this driver. Not done: staging buffers of
their own memory (a 64 MiB block took 0.9-1.4 ms to free here) or uploads split into chunks so that staging stays small.
