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
  (`centre`), the others 0.
* A texture loaded with a cell before the cell enters the tree stays at its tail until an instance uses it (with the
  old rule all 96 went to full resolution: 384 of 384 MiB resident).

RESULTS_PENDING
