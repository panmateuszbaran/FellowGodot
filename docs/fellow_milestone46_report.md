# FellowEngine — milestone 46 report (world streaming)

Status: **measured on lavapipe** (the cloud's CPU Vulkan driver, 2026-11-04), **and on the RTX** (milestone 49's jobs
f335 and f337, 2026-11-05).
Branch `fellow/geometry`. Decision record `docs/decisions/0147-world-streaming.md` (and 0146's revised pinning rule).

## 0. Result

`FellowWorldStreamer3D` and `FellowStreamingCell3D`: a level made of cells, each a PackedScene loaded on the
ResourceLoader's threads when the camera comes within its distance, instantiated on a worker thread, added to the tree
a cell a frame and freed when the camera has gone. In World Lab (144 cells of 64 m, 1.6 M triangles, 288 materials on
Streaming Lab's 96 textures of 2048²) at 1080p, with texture streaming too, a view needs **289-612 MiB of video memory instead of 740 MiB**
(mesh buffers 62-150 MiB instead of 158, textures 184-394 MiB instead of 547), the level starts in **2.55 s instead of
4.09 s**, and what the camera sees is the same as with the whole world loaded (bit for bit at 480 × 270; one pixel by
one level at 1080p). Over a flythrough the peak video memory falls from 708 to 321 MiB; a cell costs the main thread at
most 1.8-3.5 ms entering the tree (lavapipe's CPU) and its nodes are built on a worker thread.

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

lavapipe (`$DEMOTE`, without TAA), World Lab's three cameras, the whole world loaded at start against the streamed one
once settled, the same frames:

* World streaming, 480 × 270: 0 pixels differ (a load distance of 320 m for a 200-300 m far plane missed far corners of
  the view, 177 pixels: the far plane is a depth; 400 m for 200 m covers them).
* World and texture streaming, 480 × 270: 0 pixels; 1920 × 1080: 1 pixel by 1 level in one view, 0 in the others; world
  streaming alone: 0 in that view too (the pixel is texture streaming's estimate, one level short there).
* With the old pinning rule every streamed texture went to full resolution in World Lab (384 of 384 MiB); with the
  new one a cell's textures keep their tails until used.

## 3. Performance and memory

| World Lab, 1920 × 1080, each view settled | video memory | mesh buffers | textures |
|---|---:|---:|---:|
| the whole world loaded | 740.2 MiB | 157.9 MiB | 546.8 MiB |
| streamed, low over a corner | **289.2 MiB** | 62.4 MiB | 183.6 MiB |
| streamed, above the centre | **611.5 MiB** | 150.4 MiB | 393.8 MiB |
| streamed, the opposite corner | **322.1 MiB** | 65.6 MiB | 189.2 MiB |

The flythrough (960 × 540, two passes, 3 663 frames): peak video memory 707.6 MiB whole, 623.2 MiB with world
streaming, **321.4 MiB** with world and texture streaming; peak mesh buffers 141.1 → **56.9 MiB**; 323-340 cell loads
and 271-288 releases; adding a cell to the tree at most 1.5-3.5 ms of the main thread, instantiating it 0.31-0.34 ms
(on a worker thread, or on the main thread with `threaded_instancing` off); the streamer itself 74-123 µs a frame for
144 cells. Start to the first frame: 4.09 → 2.55 s at 1080p.

## 4. Limits

* The cells are authored by hand (or by a tool): nothing splits a level into cells, and FellowHLOD3D's proxies are
  built at run time from loaded meshes, so they cannot stand in for unloaded cells yet (a proxy has to be baked and
  saved with the cell's node).
* Entering the tree is on the main thread: a cell's `_enter_tree` and `_ready` (and its physics bodies) cost the frame
  it enters (`activation_usec_max`); a large cell should be split.
* Freed nodes are freed with `queue_free` on the main thread; resources (meshes, materials) are freed when the last
  cell using them goes.
* On the RTX (1080p, each view settled): video memory 799 MiB whole, 704-792 MiB with world streaming, 394-769 MiB
  with texture streaming too; the cells in range in the tree within 20-50 ms; the first frame 583 → 365 → 71 ms; the
  flythrough's frame time 0.98 → 0.97 → 0.96 ms. Its peak video memory 799 → 715 MiB with world streaming, 842 MiB
  with texture streaming too (the swaps' transient copies, no budget). Hitches over a long session on a real drive
  are still to measure.

## 5. Next

* Upscaling and frame generation (the agreed next milestone).
* For streaming: baked proxies (FellowHLOD3D's merged meshes saved with each cell), a tool to split a level into cells,
  DDS texture streaming, a cap on texture streaming's transient memory, Sponza with texture streaming on the RTX, a
  streamed world's hitches over a long session.
