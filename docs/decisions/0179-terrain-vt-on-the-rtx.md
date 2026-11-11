# 0179 — The terrain's virtual texture on the RTX: the GPU's frame shorter, its CPU cost brought down to its gain

Status: **KEEP** (the measurement; the bakes reusing each terrain's kernel inputs; `virtual_texture_all`; the
counters) · Branch `fellow/geometry` · Code: `modules/fellow_world/fellow_terrain_vt.{h,cpp}` (each terrain's uniform
sets, overview views and buffer kept on the render thread; GPU timestamps around the bakes),
`modules/fellow_world/fellow_terrain_3d.{h,cpp}` (`rendering/fellow/terrain/virtual_texture_all`; a terrain entering
sets its pages up without a second material update; the counters), `servers/rendering/fellow_settings.{h,cpp}`,
`benchmarks/project/scenes/frontier_lab.gd` (`--frontier_vt=1`, the atlas sampled per frame) · Checks: the RTX 5070 Ti
Laptop GPU (jobs f386-f389); lavapipe (`terrain_vt_check.gd`, the regression suite).

## Problem

Milestone 70 built the terrain's virtual texture on lavapipe only; whether a fragment reading two pages beats blending
the layers, what the bakes cost, and whether the atlas's default size suits an open world were the RTX's questions.

## Measured (f386, milestone 70's code)

Frontier Lab (4 km, 66 terrain regions of 256 m loaded around the camera, the flight at 45 m/s; `--profile=1`: the GPU's
timestamps), off, with the pages (`--frontier_vt=1`, the default atlas: 900 slots), and with an atlas of 8160 texels
(3 600 slots), in turn, twice:

| f386, 1080p (two runs each) | off | pages (900 slots) | pages (3 600 slots) |
|---|---|---|---|
| GPU, the flight's mean | 3.85 / 3.63 ms | 3.59 / 3.38 ms | 3.41 / 3.53 ms |
| the six fixed cameras still (GPU p50) | 2.83-5.12 ms | 5-10 % less | 3-11 % less |
| the frame's mean (the CPU's: 230 fps) | 4.44 / 4.23 ms | 4.33 / 4.39 ms | 4.40 / 4.50 ms |
| p99 / frames over 7 ms | 6.25 / 5.96 ms, 6 / 10 | 6.47 / 6.77 ms, 45 / 65 | 6.88 / 6.82 ms, 75 / 62 |
| 1 % low | 149 / 156 fps | 137 / 134 fps | 132 / 136 fps |
| the bakes (GPU, a frame on average) | | 0.015 ms (2.7 pages) | 0.019 ms (3.9 pages) |
| the atlas | | full, the pressure bias up to 1.0 (0.33 on average) | 92 % used, no pressure |

At 3840 × 2160 (the 3D frame scaled by 2, GPU-bound): the GPU's frame 10.57 / 10.22 → 10.01 / 9.86 ms (−4.5 %), the
frame's p99 16.2 / 15.4 → 14.7 / 14.8 ms, 1 % low 59 / 63 → 66 / 66 fps. Fifteen minutes of play with the pages
(real time, 234 fps): one frame over 15 ms after the first minute (19.7 ms, no page work in it), video memory flat
(849 → 850 MB), no error. The fixed cameras' pictures against the layers' differ where the scene moves (the lake's
waves, the trees in the wind), not on the ground.

**The flight's frames got worse on the CPU**: the frames over 7 ms were where a region entered (a new terrain: its
pages set up, the material switched to the pages' shader after the layers', the first bake making nine uniform sets,
sixteen views and a buffer) or where pages were baked (each bake making and freeing a uniform set of 27 textures and a
buffer per terrain: 18 µs of the render thread a page).

## Change

* **The bakes reuse each terrain's kernel inputs**: the render thread keeps, per terrain, the uniform sets writing the
  atlas and each overview level, the views of those levels, and a storage buffer updated in place (`buffer_update`);
  made again only when an input texture changes or a set is invalidated (a resource it names freed). The same pages,
  the same pictures (`terrain_vt_check.gd`, the regression suite).
* **`rendering/fellow/terrain/virtual_texture_all`** (off): every `FellowTerrain3D` drawn from its pages as if its
  `virtual_texture` were on — to compare a project with and without; Frontier Lab's `--frontier_vt=1` uses it, so a
  region enters with its pages set up (as a region saved with `virtual_texture` would) instead of switching after.
  Entering, a terrain sets its pages up without a second material update.
* **A terrain keeps its page choice while its view barely changes**: the quadtree runs when pages are missing or stale,
  the bias changed, the camera moved more than 1 m (or 3 % of its distance to the map), and at least every 4 frames
  within 256 m of the camera, every 8 past it, every 16 past 1 km (staggered by terrain; f389); otherwise its wanted
  slots are only kept recent. An eviction of its pages makes it choose
  again. The camera's frustum and pixel angle are computed once a frame for every terrain.
* **Counters**: `terrain_vt_update_usec` (the main thread choosing pages and updating tables), `terrain_vt_setup_usec`;
  `get_virtual_texture_stats()` gains `updates`, `visits`, and the atlas's own count is `atlas_wanted` (it was `wanted`,
  overwritten by the terrain's own). The bakes are bracketed by GPU timestamps (`FellowTerrain VT bake`), so
  FellowBench's `--profile=1` reports them.

## Results (f387-f389)

The flight at 1080p, off and with the pages in turn, three times each (the means of the three; the frame is the CPU's
here, about 230 fps, the GPU's 3.6 ms):

| | off | the bakes' sets kept (f387) | and the choice kept (f388) | and far terrains less often (f389) |
|---|---|---|---|---|
| the frame's mean | 4.277 / 4.265 / 4.350 ms | 4.298 ms | 4.294 ms | 4.349 ms |
| p99 / p99.9 | 5.89 / 7.18, 5.81 / 6.84, 6.00 / 7.15 ms | 6.13 / 7.34 ms | 6.03 / 7.06 ms | 6.16 / 7.24 ms |
| frames over 7 ms (of 9 260) | 14, 7, 11 | 17 | 12 | 16 |
| 1 % low | 156 / 161 / 156 fps | 148 fps | 154 fps | 150 fps |
| GPU mean | 3.69 / 3.68 / 3.72 ms | 3.54 ms | 3.62 ms | 3.61 ms |
| the main thread choosing pages | | 0.317 ms a frame | 0.160 ms | 0.138 ms |
| the render thread recording bakes | | 0.010 ms a frame (3.6 µs a page; f386: 18 µs) | 0.010 ms | 0.010 ms |

**A region entering** (f387, `FELLOW_TERRAIN_TIMING`, 494 and 516 entries in the flight): off p50 871 µs, p99 1 607 µs;
with the pages p50 913 µs, p99 1 693 µs — setting the pages up adds 225 µs to the data's part (the table, the heights'
ranges), the pages' material 185 µs less than the layers' (fewer parameters).

**At 3840 × 2160** (f388): the GPU's frame 10.38 → 9.75 ms, the frame 10.85 → 10.25 ms (92 → 98 fps), 1 % low 62 → 67
fps; with f386's runs, the GPU's frame 5.0 % shorter on average (10.39 → 9.87 ms).

**Fifteen minutes of play** (1080p, real time):

| | fps | p99 | p99.9 | largest | frames over 15 ms after the first minute | main thread choosing pages |
|---|---|---|---|---|---|---|
| off (f387) | 235 | 6.15 ms | 6.67 ms | 19.9 ms | 3 | |
| pages, sets kept (f387) | 234 | 6.22 ms | 6.80 ms | 13.1 ms | 0 | 0.27 ms a frame |
| pages, choice kept (f388) | 230 | 6.39 ms | 6.98 ms | 20.0 ms | 1 (GPU 2.5 ms: not the pages) | 0.11 ms |
| pages, far terrains less often (f389) | 236 | 6.20 ms | 6.84 ms | 22.4 ms (the GPU's own frame 22.1 ms) | 2 (one the GPU's) | 0.08 ms |

No error in any run; video memory flat in each (the pages: about 145 MB more, the atlas and the overviews); pipelines
specialized after the first minute 91 off and 91-115 with the pages, none in a frame over 15 ms.

## Decision

Keep the three changes, and `virtual_texture` off by default. On the RTX the pages shorten the GPU's frame where the
ground fills the screen (5-10 % at Frontier Lab's still cameras at 1080p, 5 % in the flight at 4K: 92 → 98 fps), cost
the GPU almost nothing to bake (0.015 ms a frame), and after these changes cost the CPU about what they save it where
the frame is the CPU's (1080p at 230 fps: the same mean, a slightly longer tail in the fast flight, the same play). A
project whose frame is the GPU's — 4K, many triplanar layers, large terrains in view — turns them on; the default atlas
(900 slots) holds Frontier Lab's 1080p view with a little pressure (the pages a quarter of a mip coarser on average).
Still worth doing: the page choice on a worker thread, and a measurement on a mid-range GPU, where the layers' cost is
a larger share.

## Rejected / not done

* **A larger default atlas** (8160 texels: 3 600 slots, 508 MiB): no pressure in Frontier Lab, the GPU's frame about
  as short, more pages missing at once (the budget the same) and four times the memory; projects that need it set it.
* **Choosing pages on the GPU** (feedback from the fragments, read back): a frame or more late and a readback, where
  the CPU's choice now costs 0.14 ms a frame in a fast flight and 0.08 ms in play.
* **Turning the pages on by default**: where the frame is the CPU's they gain nothing measurable, and each terrain costs
  its page choice and its pages' memory.
* **Fewer pages a frame** to shorten the frames that bake: the bakes cost 3.6 µs of the render thread and 5.6 µs of
  the GPU a page; the slow frames left are a region entering (its setup, 0.3 ms) and the streaming itself, as off.
