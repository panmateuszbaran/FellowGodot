# 0176 — What Frontier Lab found: a gas volume asleep when far, variants made ahead, a pool's stale members

Status: **KEEP** (a property off by default, a setting on by default that changes only when work is done, a kernel made
earlier, a lookup that no longer reports a false error: the same images) · Branch `fellow/geometry` · Code:
`modules/fellow_fluid/fellow_gas_volume_3d.{h,cpp}` (`sleep_distance`, `is_sleeping()`),
`modules/fellow_fluid/fellow_burnable_3d.cpp` (waits with its gas), `servers/rendering/renderer_rd/shader_rd.{h,cpp}`
(lazy variants filled in the background; `FELLOW_LOG_COMPILES`), `servers/rendering/fellow_kernel_cache.cpp`
(`FELLOW_LOG_COMPILES`), `servers/rendering/fellow_settings.{h,cpp}` (`frame/lazy_shader_variants_fill`,
`cost_shader_background_creates`), `modules/fellow_fluid/fellow_rain_3d.{h,cpp}` (the snow tracks' kernel and pipeline
at setup), `servers/rendering/renderer_rd/storage_rd/mesh_storage.cpp` (the MultiMesh pool's members checked before
they are looked up), `benchmarks/project/scenes/frontier_lab.gd` (the campfire asleep past 250 m, shared meshes as
ArrayMesh, the streamers' work per soak frame), `benchmarks/project/bench/main.gd` (`soak_after_frame()`) · Checks:
Frontier Lab on the RTX (jobs f377-f382); the regression suite.

## Problem

Frontier Lab (decision 0175) measured an open world on the RTX and left a list: the campfire's gas simulation was the
largest pass of the frame (0.93 ms of 4.5) wherever the camera was — kilometres away for most of the flight; in twenty
minutes of play, frames of 19-25 ms when two shader variants were made on the render thread at their first use (decision
0061 makes a cached shader's variants when first asked for), one of 92 ms when the snow tracks' kernel was compiled from
GLSL at the first footprint in the snow, and in some flights "Attempting to use an uninitialized RID" (3-7 times) and
"mesh_surface_set_material ... surface_count = 0" (once).

## Change

**A gas volume asleep when far.** `FellowGasVolume3D.sleep_distance` (0: never): while the viewport's camera is farther
than it from the box, the volume takes no steps — its smoke and fire keep their state, still drawn, and go on when the
camera returns; the `FellowBurnable3D`s burning in it wait with it. Frontier Lab's campfire sleeps past 250 m.

**Lazy variants made in the background** (`rendering/fellow/frame/lazy_shader_variants_fill`, on): when a shader version
is read from the shader cache with its variants left for later (decision 0061), it is queued; one worker task makes the
queued versions' variants one at a time, skipping a version another thread holds (tried again later), so that a variant
first used in play — the rainbow's when it starts to rain, another pass of a material in view — is made before it is
asked for. A variant is claimed under the version's lock and created with the lock released (the render thread takes
it whenever it asks for one of the version's shaders: held for the 5-7 ms of a creation, it made frames of 17-40 ms
after the load); asked for meanwhile, it is made by the asking thread as without the filling, and the background's copy
freed on the render thread. The loads stay as fast (the variants are still not made during the load's critical path);
the made ones are counted apart (`cost_shader_background_creates`). `FELLOW_LOG_COMPILES=1` prints each variant still made at its first
use and each kernel compiled from GLSL, with its frame — how the soak's hitches were attributed.

**The snow tracks' kernel at setup.** `FellowRain3D` makes the snow tracks' kernel and pipeline when it is set up,
with its own (the first footprint in the snow made them, compiling the kernel from GLSL when its cache was cold).

**The MultiMesh pool's freed members.** MultiMesh culling (decision 0034) copies each culled MultiMesh's instances
into one pool and lists the ones holding a slot; when the pool grows, the list is walked and the members freed since are
skipped. Looked up by `get_or_null()`, a freed member's RID is reported as "Attempting to use an uninitialized RID"
when its index has meanwhile been taken by a MultiMesh created on another thread — a cell's scene loading — whose
initialization is still queued for the main thread (`RenderingServer` creates such objects at once but initializes
them with the next flush of its queue): the pool grows while the frame is drawn, between two flushes. The lookup was
right (the member skipped), the error false; the members are now checked with `owns()` first. (An earlier guess, that
streamed content entered the tree before its objects existed, was tried and reverted: every call of the main thread to
the RenderingServer runs the queue first.)

**In the lab**: its shared props are ArrayMeshes (a `PrimitiveMesh` shared by cells loading on several threads builds
itself lazily, and a material set meanwhile met it without its surface); the soak records with each hitch what the
streamers did in that frame (`soak_after_frame()`).

## Verification

The regression suite (editor build and release template): every picture exact. The RTX: Frontier Lab's flight at 1080p
per pass, in clear weather and in rain; twenty minutes of play after each change (f377: the gas asleep; f378: the
variants and the kernel; f382: the final build); the flight five times at 1080p and twice with the 3D frame at
3840 × 2160 for the errors (f379, f380: printed in three of seven flights, six at a time; f381: the pool's fix).
Lavapipe: a gas volume steps near, not at 200 m with a sleep distance of 50 m, and again when the camera returns (30, 0
and 30 steps in 30 frames); Slice Lab captured three times with the shader cache warm (434-444 variants made in the
background, the same image, no error); a streamed world of 4 × 4 cells with proxies and streamed meshes
(`world_proxies_check.gd`).

## Results

**The gas asleep** (f377 against milestone 67's f374, 1080p, timestamps over the flight): the gas simulation 0.93 →
0.21 ms of GPU (0.18 in f378), the GPU's frame 4.50 → 4.25 ms; the campfire is simulated only while the flight is in
the first town.

**The flight** (release templates, 154 s at 1080p, the preset `high`, atmosphere, clouds, texture streaming): 5.06 ms
(198 fps) in milestone 67; with this record's changes 3.87-4.28 ms over seven flights of the final builds (f381-f382:
234-259 fps, GPU 3.3-3.8 ms, p99 5.9-7.4 ms — the laptop moving its power between the processor and the GPU as it warms
decides where in that range a flight falls); the 3D frame at 3840 × 2160 9.65-9.84 ms. "Attempting to use an
uninitialized RID": three of seven flights before the pool's fix (f379, f380), none of nine after (f381, f382).

**Twenty minutes of play** (1080p; the flight looping, the weather turning every minute, the log lit every two):

| | fps | p99 | p99.9 | largest frame (after the first minute) | frames over 15 ms (first minute / after) | made in play |
|---|---|---|---|---|---|---|
| milestone 67 (f376) | 248 | 6.33 ms | 8.82 ms | 92 ms | 2 / 7 | (not logged) |
| the gas asleep (f377) | 256 | 6.07 ms | 8.61 ms | 80 ms | 2 / 4 | 10 variants, a kernel (77 ms) |
| + the filling, the kernel (f378) | 272 | 5.68 ms | 7.76 ms | 19.9 ms | 5 / 2 | nothing (705 variants in the background) |
| + the pool's fix (f381) | 270 | 5.73 ms | 8.07 ms | 18.9 ms | 7 / 2 | nothing |
| + the filling outside the lock (f382) | 272 | 5.65 ms | 7.93 ms | 21.5 ms | 0 / 5 | nothing (530 in the background) |

The first minute's frames of f378 and f381 (17-40 ms) all fell in the 1.6 s after the load, the render thread waiting
for the lock the filling held: none with the lock released (the threads asking made 371 variants themselves during the
load, about 200 before). The frames after the first minute are of kinds no change here touched — none compiled
anything: a submission the driver held for 14 ms; the render thread 16-18 ms with no counter showing where (twice);
the main thread 15.6-16.7 ms (once inside the world streamer's update in a frame where it loaded, entered, released and
swapped nothing); the GPU 17-20 ms in snow. Video memory, buffers, nodes and resources flat over each soak.

## Decision

Keep all four. `sleep_distance` stays 0 by default (a volume is simulated wherever it is, as before); a level sets it
for volumes the player can be far from.

## Rejected / not done

* **Sleeping off-screen as well**: a plume behind the camera would freeze and jump when turned to; distance is what a
  level knows.
* **Every variant made at load** (decision 0061 off): the loads' 12-62 % back; made after the load, the same variants are
  ready without it.
* **Not found yet**: the rare frames of 17-22 ms above, two to five in twenty minutes; the steady rain adds 0.42 ms of
  GPU (wet opaque shading 0.16, the clouds 0.08, the depth pre-pass 0.07) with no spike. The stock
  `PipelineHashMapRD` can still wait for a compilation task twice ("Invalid Task ID", harmless) when the shadow filter
  quality is set while pipelines compile.
* **The streamer running the RenderingServer's queue** before streamed content enters the tree or is freed (parts 3-4,
  reverted): built on the guess that the error came from objects not created yet; every call of the main thread into
  the RenderingServer runs the queue first, and the errors stayed.
