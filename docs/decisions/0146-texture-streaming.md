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

lavapipe (`tools/lavapipe/streaming_check.sh`, `$DEMOTE`), Streaming Lab's four cameras, streaming off against on with
no budget, the same frames (`--streaming_fixed_wait=48`):

* Without TAA (`--preset=high_noaa`, 960 × 540): **0 pixels differ** in all four views at the default margin -1; at
  -0.5, 1 358 pixels of one view (`street_start`) by 1 level (a facade where the margin was short).
* With TAA (the default preset): **0 pixels** at 960 × 540 and at 1920 × 1080 when every camera is visited before the
  captures (`--streaming_prepass=48`). Without the pre-pass 4 761-7 413 pixels differ by 1-6 levels in the views that
  needed new levels, the same after 720 frames as after 48 and the same with every texture loaded at full resolution
  (`texture_lod_bias=-20`): the frames rendered before the loads landed, which TAA's half-float history keeps (a
  difference smaller than half a unit in the last place at its blend factor never decays). The streamed textures
  themselves sample the same values.
* With a budget (32 MiB, 960 × 540): the targets fit (27.8-29.3 MiB in the views that wanted 35.9 and 45.9 MiB, a bias
  of 1.0 and 3.0); within it the views are coarser, as intended.
* Off: nothing changes (no texture is registered, no camera recorded; the meshes' texel densities are measured only
  when it is on).

## Results

Streaming Lab (48 materials, 96 textures of 2048², 384 MiB with their mip chains), lavapipe:

| 1920 × 1080, each view after its loads (`st13`) | streamed textures resident | all texture memory (RD) |
|---|---:|---:|
| streaming off (any view) | 384.0 MiB | 602.2 MiB |
| street level, looking down the street | **29.3 MiB** | 247.6 MiB |
| mid-street, a facade 7 m away | **52.7 MiB** | 270.9 MiB |
| 1.2 m from a wall | **77.1 MiB** | 295.3 MiB |
| above the street (90 m) | **3.1 MiB** | 221.3 MiB |

(The rest of the texture memory, 218 MiB, is the render targets, shadow maps and sky: not streamed.) All four views
visited and kept (the release hold at 600 s): 124.4 MiB. Loading the 96 textures: **460 → 7-9 ms** (the tails
only); the first frame 4.2 → 3.8 s (lavapipe's shader compilation dominates it).

The flythrough (960 × 540, 30 s down the street, over the roofs and up; two passes, 3 683 frames, `sp5`): 2 019 loads
and 1 462 releases, 3.58 GB read (1 MB a frame), at the end 25.9 MiB resident, at most 205.8 MiB (low over the roofs,
with the release hold); with a 48 MiB budget (`sp4`) at most 48.0 MiB, 1 751 loads, 2.71 GB read. The render
thread's cost: the evaluation 79 µs and the rest of the update (swaps, schedule, report) 59 µs a frame for 97
instances and 96 textures — a cycle every frame in a scene this small — on the cloud's CPU shared with lavapipe; the
frame setup 0.033 → 0.223 ms. With 20 097 instances (`--streaming_props=20000`): 0.146 µs an instance surface, a cycle
of 3.0 ms spread over 10 frames (2 048 instances a frame: 0.30 ms a frame).

## Decision

**KEEP**, opt-in. Off, nothing changes; on, a level's textures cost what its cameras can see, and load in a fraction of
the time, with the same images once loaded. Next: DDS streaming (Bistro), a prefetch at a position, GPU feedback for
custom shaders, and on the RTX: Sponza's textures (imported as VRAM-compressed `.ctex`) and the swaps' cost on a GPU.
