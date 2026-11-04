# 0147 — World streaming: cells of a level loaded and freed around the camera

Status: **KEEP** (new nodes; nothing changes without them) · Branch `fellow/geometry` · Module `modules/fellow_world`
(`FellowWorldStreamer3D`, `FellowStreamingCell3D`: `fellow_world_streamer_3d.{h,cpp}`) · Texture streaming's pinning
rule revised (`servers/rendering/fellow_texture_streaming.cpp`, RD material and texture storage; decision 0146) ·
Bench: `scenes/world_lab.gd` (`--scene world`).

## Problem

A Godot level is a scene: everything in it is loaded when it loads and stays until it is freed. An open world or a
large level does not fit that way — its meshes, materials and textures for every corner at once — and loading it is
the time to load all of it. Godot has the pieces (threaded resource loading, nodes built off the main thread) but no
system that uses them for a level; the assessment (`docs/fellow_aaa_readiness.md`) listed cell streaming among the
first gaps after texture streaming.

## Change

**Cells.** A `FellowStreamingCell3D` stands for a part of the level: a `PackedScene` file (`scene_path`), its box
(`bounds`, in its space) and optionally its own load distance. Its own children are its proxy (an HLOD mesh, a stand-in):
shown while the content is not in the tree, hidden while it is.

**The streamer.** A `FellowWorldStreamer3D` above the cells measures, every frame, the distance from the viewport's
camera (`use_camera`), its `targets` and `prefetch()` points to each cell's box. A cell within its distance
(`load_distance`, 200 m) is requested from the ResourceLoader's threads (`load_threaded_request`), the nearest first,
at most `max_loading` (2) loading or being instantiated at once; when its resource has loaded, its nodes are created on
a WorkerThreadPool thread (`threaded_instancing`; nodes outside the tree may be built off the main thread), and it
enters the tree, the nearest first, at most `max_activations_per_frame` (1) a frame. A cell beyond its distance plus
`unload_margin` (25 m) leaves the tree and is freed, the farthest first, `max_releases_per_frame` (2) a frame; one
that was still loading is dropped when its load finishes. With `streaming` off every cell loads. `load_now()` loads
what is in range synchronously (a loading screen), `is_settled()` says that everything in range is in the tree and
nothing is loading, `get_stats()` counts cells by state, loads, releases, failures, the main thread's time adding and
removing cells (total, largest), the worker threads' instantiation time, the latency from request to the tree (total,
largest) and the streamer's own time. In the editor nothing loads.

**Texture streaming with it.** A cell's textures load with the cell, before it enters the tree; decision 0146's first
rule sent a texture no instance had used for two cycles to full resolution (so that a 2D texture was not left blurred),
and every streamed world's textures with it. Now a texture not used yet keeps its small mips, and the uses the
evaluation cannot estimate pin a texture at full resolution when they happen: drawn in 2D (`canvas_texture_get_info`),
copied into the decal or area light atlas, or in the uniform set of a material that is not a BaseMaterial3D-style 3D
shader (sky, canvas item, fog, particles, a custom spatial shader; `MaterialData::update_textures`).

**A .ctex where a Texture2D is expected.** A scene saved with a direct reference to a `.ctex` file asks for a
`Texture2D`; stock's loader for `.ctex` handles only `CompressedTexture2D` ("No loader found"). It now handles the
classes `CompressedTexture2D` derives from.

## Verification

lavapipe (`$DEMOTE`, without TAA), World Lab's three cameras, the whole world loaded at start against the streamed one
settled (`is_settled()`), the same frames:

* World streaming alone, 480 × 270: **0 pixels** differ in the three views (with the load distance at 320 m, 177 pixels
  did: the far plane is a depth, and at the corners of the view a visible point can be farther than it — hence 400 m
  for a 200 m far plane).
* World and texture streaming, 480 × 270: 0 pixels; 1920 × 1080: **1 pixel by 1 level** of 2 073 600 in one view
  (`centre`), the others 0 — texture streaming's: world streaming alone gives 0 in that view at 1080p (a texel level
  estimated one short at one pixel, within decision 0146's margin's reach).
* A texture loaded with a cell before the cell enters the tree stays at its tail until an instance uses it (with the
  old rule all 96 went to full resolution: 384 of 384 MiB resident).

## Results

World Lab (144 cells of 64 m, 1.6 M triangles, 288 materials on 96 textures of 2048²; lavapipe):

| 1920 × 1080, settled in each view (`w6`) | video memory | mesh buffers | textures |
|---|---:|---:|---:|
| the whole world loaded at start | 740.2 MiB | 157.9 MiB | 546.8 MiB |
| streamed (world and textures): low over a corner | **289.2 MiB** | 62.4 MiB | 183.6 MiB |
| above the centre (most of the world in range) | **611.5 MiB** | 150.4 MiB | 393.8 MiB |
| the opposite corner | **322.1 MiB** | 65.6 MiB | 189.2 MiB |

The level's start (to the first frame): 4.09 → 2.55 s (1080p; 10.3 → 2.4-2.8 s at 960 × 540 in perf mode, where the
start includes the flythrough's first pose).

The flythrough (960 × 540, two passes over the world at 25 m, 3 663 frames; `wp`):

| | peak video memory | peak mesh buffers | peak textures | loads / releases | main thread: adding a cell (largest) | instantiating a cell (worker) | request to tree (mean / largest) |
|---|---:|---:|---:|---:|---:|---:|---:|
| whole world | 707.6 MiB | 141.1 MiB | 531.0 MiB | — | — | — | — |
| world streaming | 623.2 | **56.9** | 531.0 | 336 / 284 | 3.5 ms | 0.34 ms | 0.88 / 5.7 s |
| world and texture streaming | **321.4** | 56.9 | **197.2** | 323 / 271 | 1.8 ms | 0.31 ms | 0.85 / 4.8 s |
| world streaming, instancing on the main thread | 624.2 | 56.9 | 531.0 | 340 / 288 | 1.5 ms | 0.32 ms (main thread) | 1.8 / 10.8 s |

The streamer's own work: 74-123 µs a frame for 144 cells on the cloud's CPU (shared with lavapipe's rasterizer);
freeing a cell at most 1.9-2.4 ms. The latencies are lavapipe's (a frame takes 0.39 s: a load finishes, then waits for
its frame to be instantiated and its turn to enter the tree, one a frame); the frame times are the CPU rasterizer's
(392 ms whole, 390 ms streamed; 408 ms with texture streaming's uploads competing for the same two cores).

## Decision

**KEEP.** A level split into cells costs the memory of what is around the camera — 57 instead of 141 MiB of meshes on
the flythrough, and with texture streaming 321 instead of 708 MiB of video memory — starts in a fraction of the time,
and shows the same image as when it is all loaded. Next: baked proxies for unloaded cells (FellowHLOD3D's merged meshes
saved with each cell), a tool to split a level into cells, and the RTX: a streamed world's hitches on a GPU and a real
drive.
