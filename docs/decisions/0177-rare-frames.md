# 0177 — The rare frames attributed: the draw by phase, the queue where it runs, terrain regions entering cheaply

Status: **KEEP** (counters, and two changes that give the same terrain: the same images) · Branch `fellow/geometry` ·
Code: `core/templates/command_queue_mt.h` (a counter hook in `_flush()`), `servers/rendering/rendering_server_default.cpp`
(the queue's commands and time where they run; `draw()` by phase), `main/main.cpp` (`RenderingServer::sync()`),
`servers/rendering/rendering_device.cpp` (pending frees, buffers created with data),
`servers/rendering/renderer_rd/storage_rd/mesh_storage.cpp` (mesh surfaces, MultiMesh buffers),
`servers/rendering/fellow_settings.{h,cpp}` (the counters), `modules/fellow_world/fellow_world_streamer_3d.{h,cpp}`
(the update by step), `modules/fellow_world/fellow_terrain_3d.{h,cpp}` (normals inline on pool threads; a region's
neighbours found once per border refresh, and only the border facing a changed neighbour refreshed),
`benchmarks/project/scenes/frontier_lab.gd` (the streamers' steps per soak frame) · Checks: Frontier Lab on the RTX
(jobs f383-f385, an hour of play twice); `terrain_regions_check.gd`; the regression suite.

## Problem

Milestone 68 left Frontier Lab's play with frames of 17-22 ms, two to five in twenty minutes, none from compiling, and
for most of them no counter that showed where the time went: the render thread 16-18 ms in `draw()` with every
counted part small, the main thread 15.6-16.7 ms in the scene's process (once inside the world streamer's update with
nothing loaded, entered, released or swapped). The readiness assessment also asks for hour-long play.

## Change

**Counted** (`Fellow::get_stats_dictionary()`, per frame in the soak): the RenderingServer's queue — the commands
other threads pushed, run by the thread that flushes it (the main thread when the server is not threaded) — and its
time (`rs_queue_flush_usec`, `rs_queue_flush_commands`); the main loop's `RenderingServer::sync()`
(`main_sync_usec`); `RenderingServer::draw()` by phase (`draw_begin_frame_usec`, `draw_setup_usec`,
`draw_particles_probes_usec`, `draw_viewports_usec`, `draw_end_frame_usec`, `draw_post_usec`); the RenderingDevice's
pending frees (`rd_free_pending_usec`); buffers created with data (`cost_buffer_init_usec`, `_inits`, `_bytes`), mesh
surfaces added, MultiMesh buffers set. `FellowWorldStreamer3D.get_stats()`: its update by step
(`update_instanced_usec`, `update_meshes_usec`, `update_loaded_usec`, `update_requests_usec`) and its largest.

**What the hour showed** (f383, before the fixes): of fourteen frames over 15 ms, eight were the ground streamer
activating a terrain region (6.4-8.7 ms on the main thread); four waited 15-17 ms for the GPU while the GPU's own
frames were 2.6-5.4 ms; one spent 11.3 ms in `vkQueueSubmit`; two
lost about 16 ms — one Windows scheduling quantum — at a point where nothing counted took it (once in the scene's
process, once drawing the viewports).

**Terrain regions entering cheaply.** Entering, a region and its eight neighbours refreshed their border normals; a
border texel's height across the edge looked its neighbour up by walking every region of the group — with Frontier
Lab's 66 regions loaded, about 4 000 walks of 66 per refresh, nine refreshes. The neighbours and the transforms into
their maps are now found once per refresh; and since a border texel's normal reads its four direct neighbours' heights,
a neighbour across an edge refreshes that edge only (normals, mips, partial uploads) and a diagonal one nothing. The
heights read are the same; the pictures identical.

**Found on the way**: a terrain region instanced on a pool thread built its normals as a group task and waited for it
there; a pool thread waits for a group on a semaphore, so with every pool thread waiting the rows never ran (a hang at
exit on a machine of two threads). On a pool thread the rows are built inline.

## Verification

Lavapipe: Frontier Lab (1 km) soaked two minutes with the counters (the hang found, then gone);
`terrain_regions_check.gd`: the region pictures, the shared edges (within 3.8 µm) and the brushes identical before and
after the two terrain changes; Slice Lab captured with the counters (the draw's phases add up to the draw). The
regression suite (editor build and release template): every picture exact. The RTX (release templates): f383 (the
counters: two flights, an hour of play), f384 (the neighbours found once: the flight with each region's entry timed
before and after, twenty minutes of play), f385 (the facing border only: the flight timed and twice measured, an
hour of play).

## Results

**A terrain region entering** (Frontier Lab's flight on the RTX, 482-530 entries, `FELLOW_TERRAIN_TIMING`): p50 2.60 →
0.96 (neighbours found once) → 0.78 ms (the facing border only), p99 4.43 → 1.53 → 1.46 ms; the neighbours' borders
2.15 → 0.51 → 0.22 ms of it.

**The flight at 1080p** (154 s): before, p99 6.49 ms, p99.9 8.22 ms, the largest 19.3 ms, 15 frames over 8 ms, 1 %
low 135 fps; after both, p99 5.33-5.34 ms, p99.9 6.13-6.38 ms, the largest 6.8-10.0 ms, 0-1 frames over 8 ms, 1 % low
174-175 fps (255-256 fps on average; milestone 68: 247, 1 % low 152).

**An hour of play** (1080p, the flight looping, the weather turning every minute, the log lit every two):

| | fps | p99 | p99.9 | largest | frames over 15 ms (after the first minute) |
|---|---|---|---|---|---|
| the counters (f383) | 239 | 6.93 ms | 10.24 ms | 23.1 ms | 14 (0.24 a minute) |
| both terrain changes (f385) | 254 | 5.92 ms | 6.50 ms | 51.6 ms | 9 (0.15 a minute) |

By kind (f383 / f385): a region entering 8 / 0; waiting 13-47 ms for the GPU while its frames were normal 4 / 4 (the
hour's largest frame one of them); the GPU's frame itself 19-21 ms 0 / 3; a submission held by the driver 1 / 1; the
main thread losing about 16 ms where nothing counted 2 / 1. Video memory and textures swing with the flight (681-903
MB), buffers, nodes, objects and resources flat; no shader or kernel made in play. Twenty minutes with the neighbours
found once (f384): no frame over 15 ms after the first minute, the largest 12.1 ms.

## Decision

Keep the counters (a few clock reads a frame) and both terrain changes.

## Rejected / not done

* **The GPU waits, the GPU's slow frames and the driver's submission**: no engine work in those frames stands out to
  change; the laptop's other GPU clients and its power management are the suspects, outside the engine.
* **The main thread's lost quantum** (twice an hour): not attributable from inside the process; raising the main
  thread's priority, or fewer pool threads, are the candidates, to be measured with a trace.
