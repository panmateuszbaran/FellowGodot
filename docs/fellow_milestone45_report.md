# FellowEngine — milestone 45 report (texture streaming)

Status: **measured on lavapipe** (the cloud's CPU Vulkan driver, 2026-11-04); the RTX not used (the user's GPU was
busy). Branch `fellow/geometry`. Decision record `docs/decisions/0146-texture-streaming.md`.

## 0. Result

TEXTURE_RESULT

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

VERIFICATION

## 3. Performance and memory

MEMORY

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
