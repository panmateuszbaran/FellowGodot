# FellowEngine — milestone 67 report (Frontier Lab: an open world at AAA scale on the RTX)

Status: **FINAL** (2026-11-10): the lab checked on lavapipe (1 km) and measured on the RTX (4 km, jobs f374-f376).
Branch `fellow/geometry`. Decision record: `docs/decisions/0175-frontier-lab.md`.

## 0. Result

The first item the owner chose once the GPU could be used again (2026-11-10, "kontynuuj z GPU"): the readiness
assessment's scale test — an AAA-sized level with every system at once, measured on a GPU.

* **Frontier Lab**: 4 km × 4 km at 1 m in 256 streamed terrain regions with forests and meadows, 1 024 streamed cells
  (180 of towns, 116 of villages, 728 of the wild): 6 728 buildings, 145 142 windows of a modular kit, 17 354 rocks,
  2 123 street lights, 13 172 nodes, about 64 M triangles in the cells and the terrain (plus the trees); proxies for
  what is not loaded; the slice's fire, crowd, lake and weather in the first town. Generated once in 81 s (291 MB).
* **On the RTX 5070 Ti Laptop**: the flight at **198 fps at 1080p** (5.06 ms; GPU 3.70 ms), **162 at 1440p**, 186 at
  1440p with FSR 3.1 at 67 %, 94 with the 3D frame at 3840 × 2160; 761-1 354 MB of video memory. Twenty minutes of play:
  248 fps, 6.3 ms at the 99th percentile, 0.4 frames over 15 ms a minute, no memory growth.
* **Fixed on the way**: terrain layer colours drawn as linear though documented sRGB; baked proxies, their material and
  impostors copied into the scenes saved with them instead of referenced; a bake's texture averages read back from the
  GPU again for every cell.
* **Found for next**: the campfire's gas simulation costs 0.93 ms of GPU at any distance (the largest pass); shaders
  created lazily when snow first falls; a compute kernel compiled on the main thread (92 ms, cold cache); spikes of
  17 ms on the main thread with 18-20 ms of GPU.

## 1. Development

* `benchmarks/project/scenes/frontier_lab.gd` (new, extends Slice Lab): sites, heights, splat maps, the terrain and its
  foliage (impostors baked), the regions, 1 024 cells (towns, villages, the wild), proxies and streamed meshes, the
  level saved as a scene of streamers, cells and proxies; at run time the slice's systems in the first town, cameras
  and the flight, street lights by the time of day, the streamers' counts per frame.
* `benchmarks/project/scenes/slice_lab.gd`: the square's position as `_square()` (Frontier Lab's is its first town's).
* `benchmarks/project/bench/main.gd`: `--scene=frontier`; captures wait for every streamer in a scene's `streamers`.
* `modules/fellow_world/fellow_terrain_3d.cpp`: layer colours to linear for the shader and `make_proxy_arrays()`; a
  baked impostor takes its file's path. `fellow_world_streamer_3d.cpp`: proxies and their material take their files'
  paths. `fellow_cell_proxies.cpp`: texture averages keyed by file.
* `modules/fellow_editor/fellow_editor_plugin.cpp`, `showcase/tools/build_showcase.gd`,
  `tools/fellow_regression/project/scripts/terrain.gd`: their layer colours converted to sRGB (the same look); the two
  terrain goldens blessed.
* `benchmarks/README.md`: the lab.

## 2. Verification

* Lavapipe: the 1 km world generated (64 cells, 16 regions, 19 MB), captured from its cameras at 320 × 180; the level
  file 32 KB (5.7 MB before the path fix); a region's proxy bake 14 ms (2.3 s before).
* The RTX (release template from `1da1af9`): the 4 km world generated and captured from its six cameras; perf runs at
  1080p, 1440p, 1440p with FSR 3.1, the 3D frame at 3840 × 2160 (a 3840 × 2160 window does not fit the laptop's
  2560 × 1600 screen); timestamps per pass at 1080p; a 20-minute soak with FellowEngine's counters per frame.
* The image regression suite (editor build, release template): every picture exact but `terrain` and
  `terrain_holes` (the layer colours: 1 pixel 1 level apart, blessed). The editor check: every check passes.

## 3. Measurements

The flight (154 s; frame times over all of it):

| | frame (mean / p99 / max) | GPU (mean / p99) | render thread | fps (mean / 1 % low) | video memory |
|---|---|---|---|---|---|
| 1920 × 1080 | 5.06 / 7.18 / 28.5 ms | 3.70 / 5.14 ms | 1.68 ms | 198 / 122 | 761 MB |
| 2560 × 1440 | 6.19 / 8.92 / 21.0 ms | 4.94 / 6.99 ms | 2.10 ms | 162 / 98 | 925 MB |
| 2560 × 1440, FSR 3.1 67 % | 5.38 / 8.87 / 22.1 ms | 4.06 / 5.17 ms | 2.07 ms | 186 / 102 | 891 MB |
| 3D frame at 3840 × 2160 | 10.62 / 15.34 / 16.7 ms | 9.40 / 14.04 ms | 2.05 ms | 94 / 63 | 1 354 MB |

In the tree on average: 55 cells, 66 regions, 200 lights, 1 050 objects; 5.7 M triangles and 760 draw calls a frame;
texture streaming kept 51 MB of the buildings' 193 MB of textures resident. The main thread's own work 1.15 ms a frame.

GPU passes at 1080p: gas simulation 0.93 ms, opaque 0.89, depth pre-pass 0.53, SSAO 0.46, shadows 0.46, clouds
0.27, TAA 0.23, volumetric fog 0.18, transparent 0.11, shadow pyramids 0.10, water 0.10.

The soak (20 minutes): 4.03 ms on average, p99 6.33 ms, p99.9 8.82 ms; 9 frames over 15 ms — lazily created shaders
(19-25 ms, three), a kernel compiled from GLSL (92 ms), GPU waits (16-20 ms, two), GPU and main-thread spikes (20-22 ms,
two). Video memory, buffers, nodes and resources flat over the 20 minutes.

## 4. Limits

* One GPU, one drive (the laptop's NVMe); true 4K output not measured (the screen).
* Content generated: boxes with windows, not production assets; no physics bodies beyond the terrain's collision.
* The stock `PipelineHashMapRD` can wait for a compilation task twice when the shadow filter quality is set while
  pipelines compile at load ("Invalid Task ID", harmless); an error "mesh_surface_set_material on a mesh without
  surfaces" appears once in some runs (to find).

## 5. Next

Milestone 68, the lab's findings: a gas volume that sleeps when far or unseen, shaders and kernels made ahead of the
weather and the fire's stages, the spikes of the main thread and the GPU, the surfaceless mesh.
