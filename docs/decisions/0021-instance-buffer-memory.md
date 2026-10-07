# 0021 — Instance data buffers in system memory

Status: **MEASURING** · Branch `fellow/geometry` · Toggle `rendering/fellow/cpu/instance_buffer_memory` (runtime: 0
stock, 1 system memory, 2 system memory cached; default 0 until measured) · Code: `multi_uma_buffer.h`
(`fellow_set_memory`), `rendering_device.{h,cpp}` and `rendering_device_driver.h` (creation and usage bits),
`drivers/vulkan/rendering_device_driver_vulkan.cpp` (`buffer_create`), `render_forward_clustered.cpp`
(`grow_instance_buffer`), `fellow_settings.*`.

## Problem

Every frame the forward renderer writes each render list element's instance data (176 bytes; 112 for the batched shadow
lists, decision 0016) into a buffer the GPU reads in the scene shaders. Stock creates these buffers as dynamic
persistent buffers, which the Vulkan driver puts in device-local memory that the CPU can map where the GPU offers such
memory (resizable BAR, or the 256 MB window): on a discrete GPU every byte the render thread and its workers write
crosses PCIe as it is written. In City Lab this costs 0.17 ms of render thread per frame (`m4_f57_city_scratch_ab`:
written to system memory instead, a debug mode with wrong images, the render thread is 10.7 % faster; the frame 2.1 %).

## Change

`cpu/instance_buffer_memory` chooses where the forward renderer's four instance data buffers (opaque, motion, alpha,
secondary lists) are created: 0 as stock; 1 system memory, write-combined (VMA "prefer host", sequential writes); 2
system memory, cached (VMA "prefer host", random access). The CPU then writes into RAM at memory speed and the GPU
reads the data over PCIe when it draws (once per element and pass; the GPU caches it). When the setting changes, a
buffer is freed and created again in the other memory the next time its list starts over (as when it grows).

## Why it is exact

The same bytes are written at the same offsets and read by the same shaders; only the memory backing the buffer
differs. Both memories are host-coherent (no flush needed; stock's `buffer_flush` is a no-op on them).

## Verification

* lavapipe (one memory type there, so this checks the code path, not the memory): mode 0 vs 1, City Lab captures; mode
  0 vs 2, Lights Lab walk (60 frames): **0 differing pixels**. Modes switched at run time (0, 1, 2 in blocks in one
  process, City Lab) under the Khronos validation layer: buffers created again in the new memory, no validation message.

## Result

(pending)

## Decision

(pending)
