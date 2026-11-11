# FellowEngine — milestone 71 report (the terrain's virtual texture on the RTX)

Status: **FINAL** (2026-11-11): the owner said "działaj" after milestone 70's report proposed measuring the virtual
texture on the RTX. Branch `fellow/geometry`. Decision record: `docs/decisions/0179-terrain-vt-on-the-rtx.md`.

## 0. Result

* **What the pages buy on the GPU** (Frontier Lab, the RTX 5070 Ti Laptop GPU, every terrain region drawn from its
  pages): the GPU's frame 5-10 % shorter at the six still cameras at 1080p; in the flight 2-7 % at 1080p and 5 % at
  4K (92 → 98 fps, 1 % low 62 → 67); the bakes 0.015 ms of the GPU a frame (5.6 µs a page).
* **What they cost the CPU**, first (f386): more than they saved where the frame is the CPU's (1080p at 230 fps): the
  flight's frames over 7 ms 6-10 → 45-75, 1 % low 149-156 → 132-137 fps — a region entering and the bakes. Three
  changes brought it to about even: the bakes reuse each terrain's uniform sets and buffer (the render thread 18 →
  3.6 µs a page); regions enter with their pages set up; a terrain keeps its page choice while its view barely changes
  (the main thread 0.32 → 0.14 ms a frame in the fast flight, 0.27 → 0.08 ms in play).
* **Play** (fifteen minutes, real time, 1080p): off 235 fps, p99 6.15 ms, three frames over 15 ms after the first
  minute; with the pages (f389) 236 fps, p99 6.20 ms, two (one the GPU's own 22 ms frame); the page choice 0.08 ms a
  frame; video memory flat, about 145 MB more (the atlas, the overviews); no error.
* **`rendering/fellow/terrain/virtual_texture_all`**: every terrain drawn from its pages (to compare a project with and
  without); Frontier Lab's `--frontier_vt=1`. Off by default: for projects whose frame is the GPU's.

## 1. Development

* `modules/fellow_world/fellow_terrain_vt.{h,cpp}`: each terrain's kernel inputs kept on the render thread (the uniform
  sets writing the atlas and each overview level, the level views, a storage buffer updated in place; made again when
  an input changes or a set is invalidated; freed with the terrain); GPU timestamps around the bakes; `keep()` — the
  last page choice kept while the view barely changes (the choice again every 4 frames near the camera, 8 past 256 m,
  16 past 1 km, or when the camera moved, pages are missing or stale, the bias changed, a page was evicted); the
  page table's upload skipped when clean; the atlas's wanted count as `atlas_wanted`.
* `modules/fellow_world/fellow_terrain_3d.{h,cpp}`: `virtual_texture_all`; entering, the pages set up without a second
  material update; the camera's frustum and pixel angle once a frame for every terrain; `updates`, `visits` in the
  stats; the counters.
* `servers/rendering/fellow_settings.{h,cpp}`: the setting; `terrain_vt_update_usec`, `terrain_vt_setup_usec`.
  `doc/tools/fellow_settings_docs.py`, `doc/classes/ProjectSettings.xml`, `FellowTerrain3D.xml`.
* `benchmarks/project/scenes/frontier_lab.gd`: `--frontier_vt=1` (`--frontier_vt_texels`), the shared atlas sampled
  per frame in perf mode and in the soak's frame state.

## 2. Verification

* The RTX (release templates): f386 (milestone 70's code: the flight at 1080p off / pages / pages with 3 600 slots,
  twice, with the still cameras and GPU timestamps; 4K twice; the cameras captured; fifteen minutes of play); f387 (the
  sets kept: each region's entry timed; the flight three times each; fifteen minutes off and with the pages); f388 (the
  choice kept: the flight three times each, 4K, fifteen minutes); f389 (far terrains less often: the flight three times
  each, fifteen minutes).
* Lavapipe: `terrain_vt_check.gd` (a small atlas and the default budget) after each change — the same views, edits and
  regions to the dB, the flight's pages within 2 of 1 341; the regression scenes `terrain`, `terrain_holes`,
  `terrain_vt` exact after each; the full suite (editor build and release template) and the editor check at the end;
  Frontier Lab captured and soaked with `--frontier_vt=1`.

## 3. Measurements

See decision 0179. The flight at 1080p (means of three runs each, the frame the CPU's):

| | off | f387 (sets kept) | f388 (choice kept) | f389 (far less often) |
|---|---|---|---|---|
| frame mean | 4.28 / 4.27 / 4.35 ms | 4.30 ms | 4.29 ms | 4.35 ms |
| p99 | 5.89 / 5.81 / 6.00 ms | 6.13 ms | 6.03 ms | 6.16 ms |
| 1 % low | 156 / 161 / 156 fps | 148 fps | 154 fps | 150 fps |
| GPU mean | 3.69 / 3.68 / 3.72 ms | 3.54 ms | 3.62 ms | 3.61 ms |
| main thread choosing pages | | 0.317 ms | 0.160 ms | 0.138 ms |

## 4. Next

The page choice on a worker thread (the last CPU cost); a mid-range GPU, where the layers are a larger share of the
frame.
