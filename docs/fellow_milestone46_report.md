# FellowEngine — milestone 46 report (world streaming)

Status: **measured on lavapipe** (the cloud's CPU Vulkan driver, 2026-11-04; the RTX not used, at the user's request).
Branch `fellow/geometry`. Decision record `docs/decisions/0147-world-streaming.md` (and 0146's revised pinning rule).

## 0. Result

`FellowWorldStreamer3D` and `FellowStreamingCell3D`: a level made of cells, each a PackedScene loaded on the
ResourceLoader's threads when the camera comes within its distance, instantiated on a worker thread, added to the tree
a cell a frame and freed when the camera has gone. In World Lab (144 cells of 64 m, 1.6 M triangles, 48 materials'
2048² textures) at 1080p, with texture streaming too, a view needs **289-612 MiB of video memory instead of 740 MiB**
(mesh buffers 62-150 MiB instead of 158, textures 184-394 MiB instead of 547), the level starts in **2.55 s instead of
4.09 s**, and what the camera sees is the same as with the whole world loaded (bit for bit at 480 × 270; one pixel by
one level at 1080p). PERF_SUMMARY

## 1. Development

* `modules/fellow_world/fellow_world_streamer_3d.{h,cpp}`: the cell (its scene, box, distance, state, proxy children)
  and the streamer (distances to the camera, targets and prefetch points; requests, instancing tasks, activations and
  releases with their limits per frame; `load_now`, `is_settled`, `get_stats`); class reference in `doc_classes`.
* Texture streaming's pinning rule: a texture not used yet keeps its tail (it used to go to full resolution after two
  cycles unused, so every streamed cell's textures did); 2D, the decal and area light atlases and materials whose UVs
  the streamer cannot estimate (sky, canvas, fog, particles, custom shaders) pin a texture at full resolution.
* A `.ctex` loadable where a `Texture2D` is expected (stock: "No loader found" for a scene saved with a direct
  reference to one).
* World Lab (`--scene world`, `--world_streaming=1`): 12 × 12 generated cells saved as `.scn` into
  `res://external/world/` on its first run (a ground tile, ten buildings as one mesh of 1.5 m quads, forty crates in a
  MultiMesh; materials on Streaming Lab's textures); the camera's far plane at 200 m and the load distance 400 m.
  FellowBench waits for `is_settled()` before a capture and records the streamer's state.

## 2. Verification

VERIFICATION

## 3. Performance and memory

MEMORY

## 4. Limits

* The cells are authored by hand (or by a tool): nothing splits a level into cells, and FellowHLOD3D's proxies are
  built at run time from loaded meshes, so they cannot stand in for unloaded cells yet (a proxy has to be baked and
  saved with the cell's node).
* Entering the tree is on the main thread: a cell's `_enter_tree` and `_ready` (and its physics bodies) cost the frame
  it enters (`activation_usec_max`); a large cell should be split.
* Freed nodes are freed with `queue_free` on the main thread; resources (meshes, materials) are freed when the last
  cell using them goes.
* Not measured on the RTX or a real drive: lavapipe's frame times are the CPU rasterizer's.

## 5. Next

* Upscaling and frame generation (the agreed next milestone).
* For streaming: baked proxies (FellowHLOD3D's merged meshes saved with each cell), a tool to split a level into cells,
  DDS texture streaming, and the RTX measurements (Sponza with texture streaming, a streamed world's hitches on a GPU).
