# 0021 — Instance data buffers in system memory

Status: **REJECT** (option kept, off) · Branch `fellow/geometry` · Toggle `rendering/fellow/cpu/instance_buffer_memory` (runtime: 0
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

RTX (`fellow64`): `cpu/verify` 0 mismatches in both modes (`m4_f67_verify_*`: City Lab, Lights, Stress temporal; City,
PBR, Bistro exterior captures); identity against mode 0 (`m4_f67_temporal`, six scenes): City Lab and Bistro exterior
bit-identical, the others at their floor.

## Result

In one process (`m4_f67_*_mem_ab`, blocks of 30 frames, 480 block pairs per mode, reference: mode 0):

| | City Lab, 1 / 2 | draw-heavy City Lab, 1 / 2 |
|---|---:|---:|
| render thread | **−4.3 % / −4.6 %** | **−7.1 % / −6.6 %** |
| GPU time | **+2.6 % / +2.4 %** (pre-pass +0.022 ms, opaque +0.015 ms) | **+3.4 % / +3.3 %** |
| frame | +0.9 % / +0.7 % | −2.2 % / −1.8 % |
| instance data writers (opaque, shadow lists) | 0.177 → 0.094 ms, 0.138 → 0.060 ms | 0.179 → 0.092 ms, 0.106 → 0.043 ms |

The writes get twice as fast in system memory, but the GPU reads the instance data over PCIe in every pass that draws
(the vertex shaders of the depth pre-pass and the opaque pass read an element's data for each of its draws), and that
costs the GPU about as much as the render thread saves. The data has to stay in device memory and be written less:
decision 0022 (persistent instance data).

Between processes (`fellow64`, ABBA, `m4_f67_*_mem`, 2026-10-07 09:42-10:50; reference: mode 0):

| scene | 1 (system memory) | 2 (cached) |
|---|---:|---:|
| City Lab (4 runs) | +0.3 % (n.s.) | +1.0 % (n.s., 0/4) |
| draw-heavy City Lab (4 runs) | −1.9 % (p 0.09, 3/4) | −0.8 % (n.s.) |
| Bistro exterior (3 runs) | +1.4 % (n.s., 0/3; GPU +1.6 %) | +1.1 % (n.s.; GPU +1.2 %) |
| Bistro interior (3 runs) | +0.3 % (n.s.) | −0.3 % (n.s.) |
| Sponza (3 runs) | +0.5 % (n.s.) | −1.1 % (n.s., 3/3) |

## Decision

**REJECT (kept as an option, off)**: exact, but what the render thread saves the GPU spends reading the data over PCIe;
only the CPU-bound draw-heavy City Lab gains (−1.9 %), the GPU-bound scenes lose up to 1.4 %. The way forward is to write
less (decision 0022, persistent instance data, which keeps the data in device memory).
