# 0175 — Frontier Lab: an open world at AAA scale, measured on the RTX

Status: **KEEP** (a benchmark scene, and three fixes it found: the terrain layers' colour space, baked resources
referenced by their files, a bake's texture averages) · Branch `fellow/geometry` · Code:
`benchmarks/project/scenes/frontier_lab.gd` (new), `benchmarks/project/scenes/slice_lab.gd` (`_square()`),
`benchmarks/project/bench/main.gd` (the scene; captures wait for every streamer a scene lists),
`modules/fellow_world/fellow_terrain_3d.cpp` (layer colours linear in the shader and the proxies; impostors referenced
by file), `modules/fellow_world/fellow_world_streamer_3d.cpp` (proxies and their material referenced by file),
`modules/fellow_world/fellow_cell_proxies.cpp` (texture averages keyed by file),
`modules/fellow_editor/fellow_editor_plugin.cpp`, `showcase/tools/build_showcase.gd`,
`tools/fellow_regression/project/scripts/terrain.gd` (their layer colours converted, the same look) · Checks: Frontier
Lab's captures, perf runs and soak on the RTX (jobs f374-f376); the regression suite; the editor check.

## Problem

The readiness assessment's path to AAA (section 5, item 6) asks for "AAA-sized test levels (50-100 M triangles, 10 k+
objects, 100+ lights, all systems together)". The largest scene so far was Slice Lab's 1 km valley (milestone 55);
the streaming of milestones 45-46 and 64-66 had been measured piece by piece (World Lab's 768 m, a 4 km terrain
alone), never together, never at a world's scale on a GPU.

## Change

**Frontier Lab** (`--scene=frontier`), generated in code once and cached (`external/frontier_<size>/`, a version
stamp; `--frontier_generate=1` again):

* *The land*: 4 km x 4 km at 1 m (4097² heights from two noises, flattened for 52 settlements, a lake carved beside the
  first town), six layers (grass, rock on the slopes, paving, sand at the shore, meadows of grass, forests of trees
  with impostors past 140 m) on a tiling detail texture, split into 256 regions of 256 m under a
  `FellowWorldStreamer3D` (1000 m).
* *The cells*: 1024 of 128 m under another streamer (450 m): 180 town cells (blocks of 32 m, buildings of 2 m quads
  with windows of a modular kit as instances, a street light and post at each block's corner, props, crates), 116
  village cells (houses, lights, fences), 728 wild ones (rocks of about 1 000 triangles, logs). Proxies baked for the
  cells and the regions (decision 0174), the towns' buildings streamed by distance (160 m).
* *The slice's systems* in the first town: the campfire (gas, a burning log), the crowd of 48, the lake
  (`FellowWater3D`), `FellowWeather3D` with rain, storms and snow, wind, the sun's day; street lights on from dusk.
* *Cameras*: an overlook, a street, the square, the lake, a forest's edge, high over the centre; a 154 s flight from
  the overlook into the streets, the square, the lake, low over the land to another town, up over the centre and back.

**What it found** (fixed here):

* *A terrain layer's colour was drawn as linear*: documented as sRGB (as a material's), it reached the shader as it
  was, so layers drew brighter than their colour (a grass of 0.27 linear given as its sRGB 0.56 drew as 0.56). The
  shader and `make_proxy_arrays()` now take it linear. The editor's terrain preset, the showcase and the regression
  scene had chosen their colours for the old behaviour: converted, they look the same (the goldens 1 pixel 1 level
  apart, blessed).
* *Baked files copied into scenes*: `ResourceSaver`'s `FLAG_CHANGE_PATH` gives a resource its path only while it is
  saved, so the proxies, their material and the foliage's impostors kept none, and each scene saved with them held a
  copy (the level file 5.7 MB instead of 32 KB at 1 km). They take their file's path now.
* *A bake's texture averages read back again for each cell*: keyed by the texture object, they missed as soon as the
  previous cell's resources were freed (each read back the whole texture from the GPU): keyed by file, a region's bake
  2.3 s → 14 ms on lavapipe.

## Verification

Lavapipe: the 1 km world generated, its cameras captured (320 × 180), its level file, its directory (19 MB). The RTX
(release template from `1da1af9`): the 4 km world generated, captured from its six cameras, flown at three
resolutions and with FSR 3.1, profiled pass by pass, played for 20 minutes. The regression suite (editor build and
release template): every picture exact but the two terrain scenes (blessed, above). The editor check: every check
passes.

## Results

**The world** (generated on the laptop in 81 s; 291 MB on disk): 1024 cells — 6 728 buildings (2.4 M triangles of
their own), 145 142 windows, 17 354 rocks, 6 369 props (28.0 M instanced triangles), 2 123 street lights, 13 172 nodes
in the cells' scenes; 33.5 M terrain triangles at full resolution in 256 regions; trees and grass scattered around the
camera. In the tree at a time: 55 cells and 66 regions on average, 200 lights (364 at most), about 1 050 objects, 5.7 M
triangles drawn (12.8 M at most), 760 draw calls (1 524 at most).

**The flight on the RTX** (154 s, the release template, atmosphere, clouds, texture streaming; frame times over the
whole flight):

| | frame (mean / p99 / max) | GPU (mean / p99) | render thread | fps (mean / 1 % low) | video memory (peak) |
|---|---|---|---|---|---|
| 1920 × 1080 | 5.06 / 7.18 / 28.5 ms | 3.70 / 5.14 ms | 1.68 ms | 198 / 122 | 761 MB |
| 2560 × 1440 | 6.19 / 8.92 / 21.0 ms | 4.94 / 6.99 ms | 2.10 ms | 162 / 98 | 925 MB |
| 2560 × 1440, FSR 3.1 at 67 % | 5.38 / 8.87 / 22.1 ms | 4.06 / 5.17 ms | 2.07 ms | 186 / 102 | 891 MB |
| the 3D frame at 3840 × 2160 | 10.62 / 15.34 / 16.7 ms | 9.40 / 14.04 ms | 2.05 ms | 94 / 63 | 1 354 MB |

**Pass by pass** (1080p, timestamps): the campfire's gas simulation 0.93 ms — the largest pass, though the camera is
kilometres away for most of the flight —, the opaque pass 0.89 ms, the depth pre-pass 0.53, SSAO 0.46, the sun's
shadows 0.46, the clouds 0.27, TAA 0.23, volumetric fog 0.18; on the render thread the pre-opaque compositor effects
0.50 ms and the sun's fourth cascade's culling 0.21 ms.

**Twenty minutes of play** (1080p, the flight looping, the weather turning every minute, the log lit every two): 248
fps on average, 6.3 ms at the 99th percentile; 9 frames over 15 ms (0.4 a minute): two shaders created lazily at the
start and again when snow first fell (19-25 ms), a compute kernel compiled from GLSL on the main thread as the log
burned in the snow (92 ms: the kernel cache cold for a new build), two frames waiting 14-17 ms for the GPU, two with
17-20 ms of GPU and 17 ms of the main thread. Video memory 705 → 703 MB, buffers 93 → 96 MB, nodes and resources
flat: nothing grows.

## Decision

Keep the lab as the scale test of every later milestone; keep the fixes.

## Rejected / not done

* **A world from real assets** (a purchased city, scanned terrain): the bench project carries no third-party content;
  generated content is deterministic and of known size.
* **Not yet**: true 4K output (the laptop's screen is 2560 × 1600: the 3D frame was rendered at 3840 × 2160 in a
  1080p window); other GPUs; a drive slower than the laptop's NVMe.
