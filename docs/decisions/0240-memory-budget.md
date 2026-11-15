# 0240 — The driver's video memory budget, and a texture budget that follows it

Status: **KEEP** (the budget's figures with FellowEngine on; `rendering/fellow/streaming/texture_budget_auto` off by
default) · Branch `fellow/geometry` · Code: `drivers/vulkan/rendering_device_driver_vulkan.{h,cpp}`
(`fellow_memory_budget()`, the allocator's `VMA_ALLOCATOR_CREATE_EXT_MEMORY_BUDGET_BIT`),
`servers/rendering/fellow_texture_streaming.{h,cpp}` (`_budget_bytes()`), `servers/rendering/fellow_settings.cpp` ·
Checks: (pending).

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

(pending)

## Decision

(pending)
