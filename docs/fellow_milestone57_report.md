# FellowEngine — milestone 57 report (terrain and foliage)

Status: **measured on the RTX** (2026-11-06) and checked on lavapipe. Branch `fellow/geometry`. Decision records
`docs/decisions/0158-terrain.md` (the terrain), `0159-terrain-foliage.md` (its foliage); the slice's remaining hitches
in `0156-vertical-slice-soak.md` ("Afterwards").

## 0. Result

Point 3 of the plan towards AAA: FellowEngine's own terrain, and the foliage on it. `FellowTerrain3D` draws a height
field with continuous levels of detail — patches whose vertices morph onto the next coarser grid before it takes over, and
read a coarser mip of the heights as they do, so that levels meet without cracks and change without popping — as the
instances of one MultiMesh that the GPU culls one by one, written into a second, hidden MultiMesh when the selection
changes so that TAA and FSR always see a static ground. Up to 9 materials blend by two splat maps and their heights,
triplanar on cliffs; it collides (height map shapes, the finest level's triangles) and answers height, normal and
material queries; FellowEngine's rain and snow reach it as any material. `FellowTerrainFoliage` scatters plants and
rocks over its layers in cells around the camera, each cell made once on a worker thread into a MultiMesh of its own.

On the RTX, Slice Lab's 1 km valley as a FellowTerrain3D at 1 m (16 times the vertices of its 4 m chunks near the
camera, with collision) costs 0.23 ms of GPU time a frame (+6.7 % at 1080p; the render thread 0.16 ms shorter); its trees and grass scattered by the terrain instead of
placed by its script cost the same. No crack in any view of a 1 km or a 4 km map; physics rays meet `get_height()`
within 0.08 mm. Also: the slice's first snow and a relit log no longer hitch (51 → 10 ms, 17-22 → 6-9 ms).

## 1. Development

* `modules/fellow_world/fellow_terrain_3d.{h,cpp}`: `FellowTerrain3D` (heightmap, splat maps, layers, spacing, patch
  size, level-of-detail distance and morph, height blend, macro variation, shadows, render layers, collision, debug
  view; `get_height`, `get_normal`, `get_layer_weights`, `get_aabb`, `get_stats`, `update_heightmap`,
  `heightmap_from_image`), `FellowTerrainLayer`; icons; the class reference.
* `benchmarks/project/bench/terrain_check.gd`: a noise terrain looked at from four heights: pictures, cracks, physics
  rays against `get_height()`, statistics (`--snow` under FellowEngine's snow cover, `--debug_view=1`).
* `FellowTerrainFoliage` and `FellowTerrain3D.foliage`, `foliage_cells_per_frame` (decision 0159).
* `servers/rendering/renderer_scene_cull.cpp`: the directional shadow cache skips the changes of instances that cast
  no shadows.
* Slice Lab `--slice_terrain=node` (`--slice_terrain_spacing`, `--slice_terrain_lod`, `--slice_terrain_patch`),
  `--slice_foliage=node`.
* Decision 0156's remaining hitches: `FellowRain3D` (the flakes' draw material on a hidden quad, their own process
  materials, the process materials run once at setup), `FellowBurnable3D` (its mesh's arrays read once);
  `bench/weather_pipelines_check.gd`.
* The image regression suite's `terrain` scene; the editor check's icons and idle terrain scene.

## 2. Verification

* lavapipe: `terrain_check.gd` at 513² and 1025² (with `--foliage=1`, `--snow=0.8`, `--debug_view=1`); the regression
  scene (bit-exact, blessed; the whole suite bit-exact after the shadow cache's change); the editor's idle frames with
  a terrain and its foliage; Slice Lab's cameras with chunks, with the node and with its foliage.
* RTX 5070 Ti Laptop: `terrain_check.gd` at 1080p, a 1 km and a 4 km map, Slice Lab's cameras on the node (f350);
  Slice Lab's frame time, chunks against the node, frame-locked and interleaved (f352-f355); a 10-minute soak on the
  terrain with its foliage (f355); the slice's hitch fixes (f349, f350).

## 3. Measurements

`terrain_check.gd` on the RTX at 1080p (f350), four cameras from 3 m to 600 m above the ground:

| map | patches | triangles | selection | cracks | physics rays against `get_height()` |
|---|---|---|---|---|---|
| 1025² (1 km at 1 m) | 64-415 | 33 k-212 k | 3-12 µs | 0 | mean 0.026 mm, largest 0.076 mm |
| 4097² (4 km at 1 m) | 235-667 | 120 k-342 k | 5-32 µs | 0 | mean 0.025 mm, largest 0.072 mm |

Slice Lab on the RTX, 1920 × 1080, clear weather, atmosphere and clouds, frame-locked, interleaved, three runs each
(`run_benchmarks.py`; f356, the code as shipped but for the foliage's two hitch fixes, which change no frame's cost):

| | its 4 m chunks | FellowTerrain3D at 1 m | the same, its trees and grass scattered by the terrain |
|---|---|---|---|
| frame time; 99th percentile | 4.426; 5.894 ms | 4.663; 6.325 ms | 4.672; 6.265 ms |
| GPU | 3.361 ms | 3.587 ms (+0.23) | 3.583 ms |
| GPU per camera: overview, street, square, lake, forest | 3.83, 2.91, 2.62, 2.74, 3.49 | 4.16, 3.05, 3.12, 3.18, 3.93 | 4.15, 3.04, 3.11, 3.18, 3.93 |
| render thread | 1.315 ms | 1.155 ms | 1.036 ms |
| main thread's process time | 5.316 ms | 5.406 ms | 5.353 ms |
| the terrain's own time a frame (its levels, its foliage) | — | 17 µs (380 patches) | 23 µs (90 cells, 10 600 instances) |

The node's cost by pass (f354, one run each with GPU timestamps): opaque +0.09 ms, shadows +0.16 ms, depth pre-pass
(normals and roughness for SSAO) +0.07 ms. Through the milestone (decision 0158's table): the first shader +0.36 ms,
the unrolled blend and textures not read −0.10 ms; at the chunks' 4 m the node costs 0.21 ms less than at 1 m (f352).
A second, stable set of patches casting the shadows gained nothing in the moving view (removed). The main thread's
process time had risen 0.31 ms with the terrain in f355: the campfire's gas volume copied the terrain's height map
shape every step (fixed: +0.09 ms in f356).

Soaks on the RTX, 10 minutes each on the terrain with its scattered foliage (the weather every minute, the log lit again
every two): before the foliage's two fixes (f355) 10 hitches — 6 surface pipelines when the first grass cell appeared
(21.5 ms), four of 139-274 ms that compiled nothing on the main thread; with them (f357) 1 hitch (17 ms, the GPU's
fence), the largest frame 17 ms, 236 fps, video memory 400 → 401 MB, objects +2. The process's resident memory rose
338 MB from the first to the tenth minute (the chunks' slice: 163 MB in 19 minutes; our own allocations, headless, do
not grow): __F358__

## 4. Limits

* No sculpting or painting in the editor yet: the heights and splat maps are images made elsewhere (or in code).
* The foliage appears and disappears at its range (no fade; a kind's material can fade itself), has no impostors, and
  does not collide.
* The terrain's shadows: the camera's patches change as it moves, so the directional shadow cache draws them into
  every cascade every frame; a stable set of shadow casters gained nothing in a moving view (decision 0158).
* One heightmap in memory (a 4 km map at 1 m: 64 MB of heights, 85 MB with mips on the GPU); no streaming of regions,
  no holes, no virtual texturing.
* The splat maps blend weights (two RGBA8 images): 9 layers at most.

## 5. Next

Sculpting the heights and painting the layers in the editor; terrain regions streamed with the world's cells.
