# FellowEngine — milestone 69 report (the rare frames attributed, an hour of play)

Status: **FINAL** (2026-11-11): checked on lavapipe and measured on the RTX (jobs f383-f385). Branch `fellow/geometry`.
Decision record: `docs/decisions/0177-rare-frames.md`.

## 0. Result

The third item of the order set when the GPU could be used again ("kontynuuj z GPU"; the owner's "ok działaj" for
this one): the frames of 17-22 ms milestone 68 left in Frontier Lab's play, and the readiness assessment's hour-long
play.

* **Attributed**: new counters for the RenderingServer's queue where it runs, the draw by phase, the RenderingDevice's
  frees and uploads, the world streamer's update by step. An hour of play with them (f383): of fourteen frames over
  15 ms, eight were terrain regions entering (6.4-8.7 ms on the main thread), four waits for the GPU while its own
  frames were normal, one held submission, two lost scheduling quanta.
* **Terrain regions entering cheaply**: a region's neighbours found once per border refresh, and only the border
  facing a changed neighbour refreshed — an entry on the RTX p50 2.6 → 0.78 ms, p99 4.4 → 1.5 ms. Frontier Lab's
  flight at 1080p: p99 6.5 → 5.3 ms, p99.9 8.2 → 6.2 ms, 1 % low 135 → 175 fps, frames over 8 ms 15 → 0-1.
* **An hour of play** (the final build): 254 fps, p99 5.9 ms, p99.9 6.5 ms (10.2 before), 0.15 frames over 15 ms a
  minute (0.24 before), none a region entering — of the nine, eight the GPU or its driver (waits of 13-47 ms with the
  GPU's frames normal, GPU frames of 19-21 ms, an async submission held), one a lost quantum; nothing growing.
* **Found on the way**: a hang — terrain regions instanced on pool threads waited for their normals' group task
  there, and with every pool thread waiting the rows never ran (two threads); built inline on pool threads now.

## 1. Development

* `core/templates/command_queue_mt.h`: `fellow_ticks`, `fellow_flush_usec`, `fellow_flush_commands` (set by the
  RenderingServer: the commands `_flush()` runs and their time).
* `servers/rendering/rendering_server_default.cpp`: the hook set; `_draw()` timed by phase. `main/main.cpp`:
  `RenderingServer::sync()` timed. `servers/rendering/rendering_device.cpp`: pending frees, buffers created with data.
  `servers/rendering/renderer_rd/storage_rd/mesh_storage.cpp`: mesh surfaces added, MultiMesh buffers set.
  `servers/rendering/fellow_settings.{h,cpp}`: the counters.
* `modules/fellow_world/fellow_world_streamer_3d.{h,cpp}`: the update timed by step and its largest (`get_stats()`;
  the class reference). `benchmarks/project/scenes/frontier_lab.gd`: the steps per soak frame.
* `modules/fellow_world/fellow_terrain_3d.{h,cpp}`: normals built inline on a pool thread;
  `_region_neighbors()` (the eight neighbours and the transforms into their maps, once per refresh),
  `_refresh_border_normals(dx, dz)` (one border, or none for a diagonal neighbour).

## 2. Verification

* Lavapipe: Frontier Lab (1 km) soaked two minutes with the counters (the hang found, then gone);
  `terrain_regions_check.gd`: the region pictures, the shared edges (within 3.8 µm) and the brushes identical before
  and after parts 2-3, an activation 1.11 → 0.67 ms; Slice Lab's capture with the counters.
* The regression suite (editor build and release template): every picture exact.
* The RTX (release templates): f383 (the counters: two flights, an hour of play), f384 (part 2: the flight with each
  region entry timed before and after, twenty minutes of play), f385 (part 3: the flight timed and twice measured, an
  hour of play).

## 3. Measurements

A terrain region entering (the flight, 482-530 entries, the RTX; `FELLOW_TERRAIN_TIMING`):

| | p50 | p90 | p99 | its neighbours' borders (mean) | collision (mean) | material, instances (mean) |
|---|---|---|---|---|---|---|
| before (f384, m69a) | 2.60 ms | 3.41 ms | 4.43 ms | 2.15 ms | 0.22 ms | 0.29 ms |
| neighbours found once (part 2) | 0.96 ms | 1.22 ms | 1.53 ms | 0.51 ms | 0.25 ms | 0.25 ms |
| only the facing border (part 3) | 0.78 ms | 1.15 ms | 1.46 ms | 0.22 ms | 0.32 ms | 0.36 ms |

The flight at 1080p (154 s; the preset `high`, atmosphere, clouds, texture streaming):

| | fps | 1 % low | p99 | p99.9 | largest | frames over 8 ms | GPU |
|---|---|---|---|---|---|---|---|
| milestone 68 (f382) | 247 | 152 | 5.94 ms | 7.38 ms | 8.8 ms | 3 | 3.54 ms |
| the counters (f383, two flights) | 227-233 | 131 | 6.70-6.84 ms | 8.5-8.7 ms | 10.9-20.9 ms | 20-25 | 3.72-3.84 ms |
| before (f384, m69a) | 237 | 135 | 6.49 ms | 8.22 ms | 19.3 ms | 15 | 3.67 ms |
| part 2 (f384) | 235 | 164 | 5.80 ms | 6.56 ms | 8.4 ms | 1 | 3.72 ms |
| part 3 (f385, two flights) | 255-256 | 174-175 | 5.33-5.34 ms | 6.13-6.38 ms | 6.8-10.0 ms | 0-1 | 3.41-3.43 ms |

(The mean follows the laptop's power state, 3.4-3.8 ms of GPU; the tail follows the region entries.)

Play (1080p, the flight looping, the weather turning every minute, the log lit every two):

| | minutes | fps | p99 | p99.9 | largest | frames over 15 ms (after the first minute) |
|---|---|---|---|---|---|---|
| milestone 68 (f382) | 20 | 272 | 5.65 ms | 7.93 ms | 21.5 ms | 5 |
| the counters (f383) | 60 | 239 | 6.93 ms | 10.24 ms | 23.1 ms | 14 |
| part 2 (f384) | 20 | 257 | 5.66 ms | 6.28 ms | 12.1 ms | 0 |
| part 3 (f385) | 60 | 254 | 5.92 ms | 6.50 ms | 51.6 ms | 9 |

The hour with the counters (f383) and the final one (f385), its frames over 15 ms after the first minute by kind:

| kind | f383 | f385 |
|---|---|---|
| a terrain region entering (6.4-8.7 ms on the main thread) | 8 | 0 |
| waiting for the GPU while its frames were normal (13-47 ms) | 4 | 4 |
| the GPU's frame itself 19-21 ms | 0 | 3 |
| a submission held by the driver (11-16 ms in `vkQueueSubmit` or an async split) | 1 | 1 |
| the main thread losing about 16 ms where nothing counted | 2 | 1 |

In the final hour video memory swung between 681 and 903 MB with the flight (textures 453-669 MB), buffers 90-104 MB
(-0.1 MB an hour), nodes, objects and resources flat, no orphans; 148 pipelines compiled after the first minute,
none in a frame over 15 ms; no shader variant or kernel made in play.

## 4. Limits

* The GPU waits (13-47 ms while the GPU's own frames were normal), GPU frames of 19-21 ms and a submission the driver
  held: no engine work in those frames stands out; the laptop's other GPU clients and power management are the
  suspects, not measured from inside the process.
* The main thread losing one Windows scheduling quantum (about 16 ms) at a point nothing counted, twice in an hour:
  not attributable from inside the process.
* An exit in the middle of streaming on lavapipe reports one mesh freed after the RenderingServer (a load not taken).

## 5. Next

The readiness assessment's open rows with the GPU at hand: the terrain's virtual texturing; the main thread's lost
quanta with a system trace.
