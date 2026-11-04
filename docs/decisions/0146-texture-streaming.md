# 0146 — Texture streaming: only the mips the cameras can see

Status: **KEEP** (opt-in: `rendering/fellow/streaming/textures`, off by default; on and loaded, the same images bit
for bit on lavapipe) · Branch `fellow/geometry` · `servers/rendering/fellow_texture_streaming.{h,cpp}`,
`scene/resources/compressed_texture.cpp`, `renderer_scene_cull.cpp` (`fellow_texture_streaming_update`), RD storage
(`texture_2d_fellow_stream_swap`, `material_get_fellow_stream_info`, `mesh_surface_get_fellow_uv_density`) · Settings
`rendering/fellow/streaming/*` · Bench: `scenes/streaming_lab.gd` (`--scene streaming`),
`scripts/make_streaming_textures.py`, `tools/lavapipe/streaming_check.sh`.

## Problem

Stock Godot loads every texture whole: each level of each mip chain is resident from the moment its resource loads
until it is freed. A texture 200 m away that covers 30 pixels holds the same 2.7 MB (a 2048² BC1 map) as one a metre
from the camera. Sponza needed 2.5 GB of VRAM (milestone 3), most of it textures; an AAA level's textures do not fit in
8-12 GB that way, and the assessment (`docs/fellow_aaa_readiness.md`) put memory and streaming first among what an AAA
project would hit.

## Change

**Loading the tail.** With streaming on (read at start-up), a `CompressedTexture2D` whose file holds a VRAM-compressed
image with its full mip chain (`DATA_FORMAT_IMAGE`, BCn/ETC2/ASTC: how Godot imports 3D textures) larger than
`texture_resident_size` (128 px) reads only the chain from its first level at or below that size — for a 2048² map the
last 8 of 12 levels, 1/256 of the bytes. The texture keeps its full size (`texture_set_size_override`), so materials,
UVs and `get_width()` are unchanged; `get_image()` reads the whole chain from the file. The file's position of mip 0 is
registered with the RenderingServer (`texture_2d_fellow_set_stream_source`). Lossless (PNG/WebP) textures, textures
without mips and small ones load as before.

**What each texture needs.** Every frame, the render thread evaluates a slice (`texture_instances_per_frame`, 2048) of
the instances of every scenario a camera rendered in the previous frame; when all of them have been seen, a cycle ends.
For an instance: the finest pixels per world unit any camera can have on it — the camera's half render height times
its projection's vertical scale, over the nearest view depth of the instance's box (the distance to the box when it is
behind the camera); its smallest scale; and, for each surface, the surface's texel density: the 99th percentile, by
area, of the largest stretch (singular value) of each triangle's UV mapping, measured from up to 8 192 triangles when
the surface is added (only while streaming is on). For each texture of the surface's materials (the override, the
instance's, the mesh's; the overlay; next passes) that is streamed: the level
`log2(size × density × uv1_scale / scale / pixels_per_unit) + the samplers' bias (TAA -0.5, FXAA -0.25, the viewport's)
+ texture_lod_bias (-1)`, the finest over the cycle. Triplanar materials (`uv1_blend_sharpness`) use `uv1_scale` per
unit; anisotropic samplers one level finer. A shader without `uv1_scale` (not a BaseMaterial3D: its UVs are unknown)
asks for full resolution; a texture no 3D instance used in its first two cycles (2D, sky, decals) goes to full
resolution and stays there.

The margin `texture_lod_bias = -1`: the estimate is the level along the UV mapping's largest stretch at the nearest
depth, the level a fronto-parallel surface shows there; a GPU's level is the larger of the screen axes' derivatives,
up to half a level finer when the stretch is diagonal on screen, and half a level more where the derivatives' lengths
are approximated by their largest component (Vulkan allows it). At -0.5 lavapipe showed 1-level differences on 1 358
pixels of one view (0.3 %); at -1 none.

**The budget.** A cycle's end sets each texture's target: its level (floored, clamped to the resident tail), all
coarsened by the same bias in steps of 0.5 until the targets fit `texture_budget_mb` (0: no limit).

**Loads.** Each frame the streamer starts jobs (`texture_max_jobs` 4 at once, `texture_upload_mb_per_frame` 64):
finer first (the finest target, then the largest step), within the budget; releases (a coarser target) after
`texture_release_hold_s` (2 s) unless over the budget, the largest saving first; a texture no longer referenced keeps
its levels `texture_unreferenced_hold_s` (5 s), then falls back to its tail. A job reads the chain from its target
level (one `seek` and `get_buffer`), builds the `Image` and creates the RD texture on a WorkerThreadPool thread
(`texture_2d_initialize`: the RD creates resources off the render thread); the render thread then swaps it in under the
same RID before the materials update (`texture_2d_fellow_stream_swap`: the old RD texture is freed, its uniform sets are
invalidated and rebuilt with the new one; size override, path, callbacks, proxies and users stay).

**Reporting.** `RenderingServer.fellow_get_texture_streaming_info()`: the streamed textures (resident, target, wanted
and full bytes, the budget's bias, pending, jobs, loads, releases, cycles, evaluation time, `settled`), with
`texture_info_details` a list per texture (path, size, levels, resident and target level, estimate, bytes). FellowBench
records it per capture and per run, and waits for `settled` before a capture (`--streaming_fixed_wait=<n>` for A/B
captures with the same frame count, `--streaming_prepass=<n>` to visit every camera first).

## Verification

RESULTS_PENDING
