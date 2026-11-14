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
logs no shadow cache event for it. Any other cause of an update in the same frame marks it relevant again.

## Verification

PENDING.

## Results

PENDING.
