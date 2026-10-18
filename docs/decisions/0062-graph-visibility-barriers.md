# 0062 — Render graph: a barrier for a read from a stage the last write was not made visible to

Status: **measuring** (`fellow151`) · Branch `fellow/geometry` · Setting `rendering/fellow/debug/visibility_barriers` (on;
off with FellowEngine disabled) · Code: `rendering_device_graph.{h,cpp}` (`_add_command_to_graph`,
`ResourceTracker::fellow_visible_stages`), `fellow_settings.*` (stat `rd_graph_visibility_barriers`).

## Problem

Vulkan synchronization validation on Lights Lab captures (lavapipe) reported `SYNC-HAZARD-READ-AFTER-WRITE`: light tile
culling's compute shader (0049) read the omni and spot light buffers after the copy that updated them, with no barrier
making the copy's writes visible to it. With FellowEngine disabled, or tile culling off, no message.

The render graph's state (`m12`, debug print): the dispatch's three buffers had the usage it asked for already
(storage buffer read), set by the light cluster pass's shaders, which read them first after the copy. Stock's usage
tracking adds a barrier only when a resource's usage changes; the barrier before the cluster pass made the copy visible
to that pass's stages only. A later reader with the same usage from another stage (here the compute stage) is ordered
after the copy (the graph makes it depend on the write) but gets no memory barrier: an execution dependency without the
memory dependency.

## Change

Each resource tracker records the stages the barriers since the graph's last write to it covered. A read with the usage
the resource already has, from a stage not yet covered, gets a memory barrier (source: memory writes; destination: the
read's access), and its stages are added. A write clears them. Stock's paths are unchanged with FellowEngine disabled.

## Verification

lavapipe with synchronization validation: Lights Lab captures 0 messages (4 before), City Lab with SSIL, SSR and async
compute emulated 0; captures against the setting off 0 differing pixels (Lights Lab, City Lab). 901 such barriers in a
Lights Lab capture run of 82 frames, 378 in City Lab's 62.

## Measured

`m12_f177_vb_ab`, `m12_f177_vb_warm` (pending).
