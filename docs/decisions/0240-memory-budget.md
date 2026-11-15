# 0240 — The driver's video memory budget, and a texture budget that follows it

Status: **KEEP** (the budget's figures with FellowEngine on; `rendering/fellow/streaming/texture_budget_auto` off by
default) · Branch `fellow/geometry` · Code: `drivers/vulkan/rendering_device_driver_vulkan.{h,cpp}`
(`fellow_memory_budget()`, the allocator's `VMA_ALLOCATOR_CREATE_EXT_MEMORY_BUDGET_BIT`),
`servers/rendering/fellow_texture_streaming.{h,cpp}` (`_budget_bytes()`), `servers/rendering/fellow_settings.cpp` ·
Checks: lavapipe (a 512 MiB buffer, Streaming Lab), the RTX (job f461; f462 for the figures), the editor check (the
monitors).

## Problem

A game shares the GPU's memory with the operating system and other programs; on Windows a process over its budget has
its allocations paged out to system memory and back, which stalls the GPU for tens of milliseconds. AAA engines size
their texture pools from what the driver says is available (Unreal's texture streaming pool from the video memory
available, D3D12's `QueryVideoMemoryInfo`, Vulkan's `VK_EXT_memory_budget`). FellowEngine's texture streamer had a
fixed budget in megabytes or none, and its statistics counted the memory the allocator holds, not the budget it must
stay within; Godot does not enable `VK_EXT_memory_budget`, so the memory allocator's budget is its own estimate (80 % of
each heap).

## Change

* With FellowEngine on, the device is created with `VK_EXT_memory_budget` when the driver has it, and the memory
  allocator (VMA) reads the operating system's budget and the process's usage from it, again every frame. FellowEngine's
  statistics: `memory_budget_bytes`, `memory_budget_usage_bytes` (the device's own heaps), `memory_budget_from_driver`.
* `rendering/fellow/streaming/texture_budget_auto` (0, off): a share of that budget the process may use; the streamed
  textures' budget is the share less what the process uses besides them, at least 64 MiB, in steps of 32 MiB, raised
  only by 64 MiB or more (the targets are recomputed when the budget changes), capped by `texture_budget_mb` when set.
  Without the driver's figures it does nothing.

## Verification

Lavapipe has the extension: the budget 8 024 MiB, the usage tracking the process (lavapipe's device memory is the
host's: a 512 MiB buffer filled with data raised it by 1 005 MiB, the buffer and its copy in the script). Streaming Lab
with `texture_budget_auto` 0.2: the budget in force 64 MiB (the floor: the share less the process's other use was below
it), the cameras' streamed textures 1.5-42.9 MiB resident, the same as without the option (their wanted bytes fit).
The editor check reads the three monitors (lavapipe: budget 8 024 MiB, used 3 161 MiB).

The RTX (job f461, Sponza streamed with `texture_budget_auto` 0.9): the driver's figures read every frame; the
streamed textures' budget 8 192 MiB in force, their resident bytes 1.13 GiB (wanted 1.07): no texture coarsened; the
frame 6.12 ms as in the lane without it (6.05-6.25). Job f462 recorded the figures at the end of each run: the budget
11 176 MiB (of the GPU's 12 GiB), the process's usage 2 963 MiB, the allocator's own device memory 2 341 MiB.

## Decision

Keep: the budget a game must stay within, from the operating system, in FellowEngine's statistics and monitors with
FellowEngine on (an extension the device asks for where the driver has it, read by the allocator every frame; no frame
changes); `texture_budget_auto` for a game whose textures should give way to what else the process and the machine use,
off by default.
