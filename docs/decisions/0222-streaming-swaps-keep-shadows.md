# 0222 — A streamed texture's swap keeps the shadow caches

Status: **KEEP**, on by default (exact) · Branch `fellow/geometry` · Setting
`rendering/fellow/streaming/texture_swaps_keep_shadows` · Code: `renderer_rd/storage_rd/texture_storage.cpp`
(`texture_2d_fellow_stream_swap()`), `renderer_rd/storage_rd/material_storage.{h,cpp}` (`_material_uniform_set_erased()`,
`ShaderData::fellow_shadow_texture_independent()`), `forward_clustered/scene_shader_forward_clustered.*`,
`renderer_scene_cull.{h,cpp}` (`_instance_queue_update()`, `_update_instance()`), `fellow_settings.*` · Checks:
`benchmarks/tools/lavapipe/streaming_check.sh`, the laptop's Sponza (job f434).

## Problem

Milestone 45's texture streaming swaps a texture's data for another range of its mips under the same RID; the old data's
uniform sets are erased, and every material using them tells its instances "the material changed". RendererSceneCull
then updates each such instance as for any change, and an update logs an event for decision 0004's directional shadow
cache: every cascade containing the instance is built again. On the RTX (job f433) Sponza's flythrough with streaming
(no budget) swapped textures all along (187 loads, 38 downgrades a run): 78 shadow cache invalidations against none
without, the cascades rebuilt 272 times against 174, the GPU's frame 6.27 → 6.62 ms (+5.6 %). With a 256 MiB budget
(343 downgrades) 194 invalidations.

A material's texture data cannot change the depth it casts unless the shadow pass reads the texture: alpha (scissor,
hash, antialiasing, prepass alpha), discard, or a vertex function (which may displace by a texture).

## Change

While `texture_2d_fellow_stream_swap()` frees the old data, a flag (render thread) is set; a material whose uniform set
is erased under it, and whose shader cannot read its textures in the shadow pass
(`ShaderData::fellow_shadow_texture_independent()`: the Forward+ scene shader without alpha, discard, depth prepass
alpha, vertex or position code, custom matrices, trails or z-clip scale; every other shader: no), notifies its users with
a second flag set. RendererSceneCull queues those instances' updates as before (their dependencies refreshed, the
renderer's caches rebuilt with the new uniform set) but marks the update as not shadow-relevant: `_update_instance()`
logs no shadow cache event for it. Any other cause of an update in the same frame marks it relevant again. The
material's own update after the swap (its uniform set made again, a second notification from
`_update_queued_materials()`) carries the same mark while nothing else queued the material.

## Verification

lavapipe, Streaming Lab (its four cameras, its sun's shadows; streaming on, `streaming_check.sh`'s settings: every
camera visited first, 48 frames each), the setting on against off:

* Without TAA (`--preset=high_noaa`, 480 × 270): **0 pixels differ** in the four views; the shadow cache's invalidations
  17 → 0 a run, the cascades built 49 → 33 times, the casters kept dynamic 258 → 158.
* With TAA: 145-311 pixels of two views differ by 1 — as much as two runs with the same setting differ (166: the loads
  landing at another frame stay in TAA's history, decision 0146); invalidations 25 → 0, cascades 58 → 33. A first try
  marked only the swap's own notification and still logged 25: the material's update after the swap notified again.
* The regression suite (the release template): `lights`, `shadows`, `materials`, `geometry`, `instances`, `terrain`,
  `crowd`, `fire`, `rain`, `destruction`, `structure`, `vehicle`, `vehicle_damage` the same as their goldens.

## Results

The RTX (job f434, the release template at 6c5ee4cf91, Sponza's flythrough at 1920 × 1080, streaming on without a budget,
three alternating runs each, the median GPU frame):

| | shadow cache invalidations a run | cascades built | objects drawn a frame (shadow passes included) | GPU frame |
|---|---:|---:|---:|---:|
| streaming off | 0 | 174 | 276.4 | 6.25 ms |
| on, without the decision | 71 | 262 | 292.4-294.4 | 6.37 ms |
| on, with it | 32 | 214 | 280.1-280.3 | 6.30 ms |

The invalidations left are the swaps of Sponza's alpha-tested materials (the plants, the curtains' edges: their shadows
do read their textures). The GPU's frame moved within the runs' spread (job f433 put streaming at +0.32 ms over off,
this job at +0.12 ms without the decision); the shadow passes' extra casters are what the decision removes.

## Decision

Keep, on by default: a swap's shadow cache invalidations only where a shadow can change.

## Rejected / not done

* **Skipping the instances' update altogether** on a swap: the renderer's surface caches hold the material's uniform
  set and must be rebuilt; GI's voxelisations may read the textures.
* **Per-texture knowledge** (which of a material's textures its shadow pass reads): an alpha-tested material is
  invalidated on any of its textures' swaps.
