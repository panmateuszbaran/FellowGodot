# FellowEngine — milestone 45 report (texture streaming)

Status: **measured on lavapipe** (the cloud's CPU Vulkan driver, 2026-11-04); the RTX not used (the user asked
for no GPU load for now). Branch `fellow/geometry`. Decision record `docs/decisions/0146-texture-streaming.md`.

## 0. Result

Texture streaming (`rendering/fellow/streaming/textures`, opt-in): a VRAM-compressed texture loads only its mips at or
below 128 px; the RenderingServer works out, from the instances that use it and the cameras that see them, the finest
level each texture can need, loads those levels on worker threads, swaps them in under the same RID and releases them
when they are no longer needed, within an optional budget. In Streaming Lab at 1080p (96 textures, 384 MiB with their
mip chains) a view holds **3-77 MiB** of them instead of 384 MiB — all texture memory 221-295 MiB instead of 602 MiB —
the textures load in **7-9 ms instead of 460 ms**, and once the loads have landed the images are **the same bit for
bit** as without streaming (lavapipe; with TAA once each camera has been visited, since TAA keeps the frames rendered
before). A 48 MiB budget held at 48.0 MiB over the whole flythrough.

## 1. Development

* **The tail load** (`CompressedTexture2D`): a VRAM-compressed `.ctex` with its full mip chain is read from its first
  level at or below 128 px; the rest stays in the file, its position registered with the RenderingServer.
* **The streamer** (`servers/rendering/fellow_texture_streaming.{h,cpp}`): per texture its file, levels and byte
  offsets; per material (cached by a version the RD material storage bumps) its streamed textures, `uv1_scale`,
  triplanar and anisotropic flags; per frame the last frame's cameras; per cycle each texture's finest needed level, the
  targets within the budget, the loads on worker threads, the swaps on the render thread.
* **The evaluation** (`RendererSceneCull::fellow_texture_streaming_update`, before the scene's update): a slice of each
  scenario's instances a frame — meshes, MultiMeshes, particles' draw passes — against the cameras' nearest view depth
  of the instance's box.
* **Texel density** (`FellowTextureStreaming::compute_uv_density`, RD `mesh_add_surface`): the 99th percentile by
  area of each triangle's largest UV stretch, from up to 8 192 triangles, positions and UVs decoded from the surface's
  own (possibly compressed) buffers.
* **The swap** (RD `texture_2d_fellow_stream_swap`): the RD texture replaced under the same RID, its size override,
  path, callbacks, proxies and users kept.
* **Streaming Lab** (`--scene streaming`) and `scripts/make_streaming_textures.py`: 48 materials of 2048² BC1 albedo
  and BC5 normal maps written as Godot's importer writes them (a NumPy BC1/BC5 encoder, 41-47 dB), generated, never
  committed; FellowBench's waits (`settled`, `--streaming_fixed_wait`, `--streaming_prepass`) and the streamer's state
  in every result.
* **Found on the way**: the estimate must include the samplers' own bias — TAA's -0.5 and FXAA's -0.25 are added in
  `RenderSceneBuffersRD::update_samplers`, not in the viewport's bias; and A/B captures need the same frame count (the
  jitter phase).

## 2. Verification

lavapipe (`tools/lavapipe/streaming_check.sh`, `$DEMOTE`), Streaming Lab's four cameras, streaming off against on, the
same frames in both runs:

* Without TAA (960 × 540): 0 pixels differ in all four views with the default margin -1 (at -0.5: 1 358 pixels of one
  view by 1 level, so the default is -1); after the evaluation's rewrite and the schedule's: 0 again.
* With TAA, every camera visited before the captures (`--streaming_prepass=48`): 0 pixels at 960 × 540 and at
  1920 × 1080. Without the pre-pass, 4 761-7 413 pixels by 1-6 levels in the views that needed new levels, unchanged
  after 720 frames and present also when every texture ends at full resolution (`texture_lod_bias=-20`): TAA's
  half-float history keeps the frames before the loads landed.
* Budgets: 32 MiB (960 × 540, captures) at most 31.2 MiB, the views that wanted 43.6 and 49.5 MiB coarsened by 1.5 and
  3.5 levels; 48 MiB over the flythrough at most 48.0 MiB.
* A scene without streamed textures (PBR Lab, procedural textures), streaming on: the same 7 images.
* The streamer's report in every FellowBench result (per capture and per run).

## 3. Performance and memory

| Streaming Lab, 1920 × 1080 | streamed textures resident | all texture memory |
|---|---:|---:|
| off | 384.0 MiB | 602.2 MiB |
| street level | **29.3 MiB** | 247.6 MiB |
| mid-street | **52.7 MiB** | 270.9 MiB |
| 1.2 m from a wall | **77.1 MiB** | 295.3 MiB |
| above the street | **3.1 MiB** | 221.3 MiB |

Loading: the 96 textures in 7-9 ms instead of 460 ms. The flythrough (960 × 540, two passes, 3 683 frames): 2 019 loads,
1 462 releases, 1 MB read a frame, at most 205.8 MiB resident (low over the roofs, with the 2 s release hold), with a
48 MiB budget at most 48.0 MiB. The render thread: 0.19 ms a frame more in frame setup on the cloud's CPU (the
evaluation 79 µs, swaps and schedule 59 µs; a full cycle every frame in a scene of 97 instances), and 0.146 µs an
instance surface at 20 097 instances (0.30 ms a frame at the default 2 048 instances a frame). Frame times on lavapipe
are the CPU rasterizer's: 135.6 ms off, 138.5 ms on, within its spread.

## 4. Limits

* Only `CompressedTexture2D` with a VRAM-compressed full mip chain streams: Bistro's DDS textures (loaded as
  `ImageTexture`), lossless and uncompressed textures, texture arrays, cubemaps and 3D textures load whole.
* The estimate knows BaseMaterial3D's UVs (`uv1_scale`, triplanar); a ShaderMaterial's textures stay at full
  resolution, and UV2 textures (detail on UV2, lightmaps) follow UV1's density.
* A texture never used by 3D geometry in its first two cycles stays at full resolution, so is one first shown later
  (spawned after the start). A texture shared by a far mesh and a sky, decal or 2D control follows the mesh.
* The first frames after a view needs new levels show the coarser ones until the loads land (pop-in); with TAA they
  stay in its history at 1-2 levels. A game can wait for `settled` behind a loading screen, or pre-visit its cameras.
* Measured on lavapipe only: load times on an NVMe drive and the swaps' cost on a GPU are for the RTX.

## 5. Next

* World streaming: cells of a level loaded and freed by distance on worker threads (the next milestone).
* On the RTX (when free): Sponza with streaming (its glTF textures are imported as VRAM-compressed `.ctex`), the swap's
  cost in frame time, NVMe load times.
* DDS streaming (Bistro), a pre-fetch API (warm a position before the camera gets there), GPU feedback for custom
  shaders.
