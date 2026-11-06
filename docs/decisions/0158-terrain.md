# 0158 — A terrain node: FellowTerrain3D, a height field with continuous levels of detail

Status: **KEEP** (a new node: nothing changes without it) · Branch `fellow/geometry` · Code:
`modules/fellow_world/fellow_terrain_3d.{h,cpp}` (`FellowTerrain3D`, `FellowTerrainLayer`), icons, class reference ·
Checks: `benchmarks/project/bench/terrain_check.gd`, the image regression suite's `terrain` scene, the editor check's
idle terrain; Slice Lab `--slice_terrain=node`.

## Problem

Point 3 of the plan towards AAA (`docs/fellow_aaa_readiness.md`): open worlds stand on terrain, and FellowEngine had
none. Its labs built their ground in GDScript as chunks of meshes at one resolution (Slice Lab: 64 chunks of a 4 m
grid, 131 k triangles, the same detail at 5 m and at 900 m), with a flat collision plane under them. A game had to
write its own terrain or use a plugin: Terrain3D (MIT, a GDExtension for Godot 4.4-4.6, not yet 4.7, and its binaries
from outside) is the candidate the assessment named; it knows nothing of FellowEngine's weather, wind or MultiMesh
culling.

## Change

**`FellowTerrain3D`** (module `fellow_world`): a height field from an `Image` of heights in metres (`FORMAT_RF`, one
texel a vertex, `vertex_spacing` apart; `heightmap_from_image()` converts a grey image).

* **Levels of detail (CDLOD).** A quadtree over the map: a node at level L is `2 × patch_size` cells of
  `vertex_spacing × 2^L`; the finest level reaches `lod_distance` (by default three times its node's size: 96 m for
  16-quad patches at 1 m), each coarser level twice as far. Every frame the camera moves, the selection walks the tree
  against each level's range (3D distance to the node's box, its heights from a min/max pyramid) and draws each chosen
  area as quadrants: patches of `patch_size` × `patch_size` quads, one grid mesh for all. Between `morph_ratio` (0.7)
  of the way from the finer level's range and its own range, a patch's odd vertices slide onto the coarser grid
  (fully at the range, where the coarser level takes over), and its heights are read from the heightmap's mip
  `level + morph`: the two sides of a level's edge are the same vertices at the same mip, so there are no cracks, and
  a level changes without popping. Selection: 1-10 µs.
* **One MultiMesh, two buffers.** The patches are the instances of one MultiMesh (each its origin, cell and level), one
  draw a pass; their shader writes `VERTEX` but stays within the grid mesh's box (the terrain's height range), so
  FellowEngine's MultiMesh culling (decision 0034) culls them one by one in the camera's and the shadows' passes
  (`fellow_instance_set_instance_range(…, vertices_bounded)`, decision 0122's flag). A MultiMesh whose buffer is
  rewritten gets motion vectors from its previous buffer — instance i's old patch against its new one, a wrong motion
  for every patch that changed (TAA and FSR would smear the ground at every level change). So a new selection is
  written into a second MultiMesh, hidden, and shown in the old one's place three drawn frames later, when its
  previous buffer is its current one: the ground's motion vectors are always those of a static surface. The morph's
  camera is the selection's (an instance uniform), the same in both of a frame's vertex passes.
* **Materials.** Up to 9 `FellowTerrainLayer`s: layer 0 the base, `splat_map`'s channels the weights of layers 1-4,
  `splat_map_2`'s of 5-8 (any resolution); a layer's albedo and normal textures repeat every `texture_size` metres,
  seen from above or, with `triplanar`, from three sides (cliffs without stretching); `height_blend` lets a layer with
  a height in its albedo's alpha show where it is higher; `macro_variation` (two octaves of noise) against tiling at a
  distance. A layer is sampled only where its weight is above 0.002, with explicit gradients, only the textures it
  has, and a triplanar layer only the projections that weigh 1 % or more; the blend of the 9 layers is generated
  unrolled (no local array indexed at run time). Normals come from a normal map built from the heights (RG8,
  mipmapped). FellowEngine's weather (wetness, puddles, snow cover) applies
  as to any material (the scene shader's pass after the material).
* **Collision**: a static body of height map shapes, one per `collision_region_size` (256) cells, scaled by the
  spacing: the finest level's triangles. **Queries**: `get_height()` (the same triangles), `get_normal()`,
  `get_layer_weights()`, `get_aabb()`, `get_stats()`. `update_heightmap()` after editing the images in place.
* **Editor**: the levels follow the 3D editor's last used viewport's camera; patches are pickable; `debug_view` colours
  the levels; icons, configuration warnings (no heightmap, too small, too many layers).
* The shader's code is one for every terrain (`FellowSharedShaders`: compiled once a session).
* **FellowGasVolume3D** no longer reads the data of concave, height map and custom shapes in its box (they are not
  obstacles of the gas, as before; their data is large): a FellowTerrain3D's height map shape under Slice Lab's
  campfire was copied, 66 000 heights, every step (the main thread's process time +0.31 ms with the terrain in f355,
  +0.09 ms in f356; the terrain's own frame 17 µs on the laptop's CPU).
* **The directional shadow cache** (decision 0004) no longer logs changes of instances that cast no shadows: they are
  in no shadow map (one that starts casting is logged when it does). Exact: the regression suite's 17 scenes stay
  bit-exact. Found with the terrain: while its camera's patches cast no shadows (the stable shadow casters tried in
  Results), they still invalidated every cascade as they changed.

Slice Lab `--slice_terrain=node` samples its valley's height function at 1 m (`--slice_terrain_spacing`) into a
FellowTerrain3D with the chunks' four colours as layers (rock on the slopes, paving in the town, sand at the shore) and
its collision instead of the plane.

## Verification

* `bench/terrain_check.gd` (lavapipe, 640 × 360): a noise terrain of 513² and 1025² heights, four cameras from 3 m to
  600 m over it: no crack (a pure red sky behind the ground; a crack is a thin red line with ground on both sides),
  and 1 600 physics rays down against `get_height()`: mean error 0.03 mm, largest 0.08 mm.
* The image regression suite's `terrain` scene (257², five levels in view, four layers, triplanar, height blend):
  bit-exact between runs; golden blessed.
* The editor check: the two classes' icons; the idle terrain scene: an untouched editor draws 1 frame in 10 s.
* Slice Lab's five cameras with chunks and with the node (lavapipe): the town, trees and lake on the same ground.
* RTX 5070 Ti Laptop: `terrain_check.gd` at 1080p on a 1 km and a 4 km map, Slice Lab's five cameras on the node
  (job f350); Slice Lab's frame time on the chunks against the node, frame-locked, interleaved, three runs each, with
  the GPU's per-pass timings (jobs f352-f355, `benchmarks/run_benchmarks.py`).

## Results

`terrain_check.gd` on the RTX at 1920 × 1080 (job f350), four cameras from 3 m to 600 m above the ground:

| map | patches | triangles | selection | cracks | rays against `get_height()` |
|---|---|---|---|---|---|
| 1025² (1 km at 1 m) | 64-415 | 33 k-212 k | 3-12 µs | 0 | mean 0.026 mm, largest 0.076 mm |
| 4097² (4 km at 1 m) | 235-667 | 120 k-342 k | 5-32 µs | 0 | mean 0.025 mm, largest 0.072 mm |

Slice Lab's flythrough on the RTX, 1920 × 1080, clear weather, atmosphere and clouds, frame-locked, the variants
interleaved, three runs each (`run_benchmarks.py`): the GPU's time a frame, its chunks (a 4 m grid, 131 k triangles,
vertex colours, a plane to collide with) against FellowTerrain3D (its layers coloured as the chunks, rock triplanar):

| job: the terrain's state | chunks | the node at 1 m | the node at 4 m |
|---|---|---|---|
| f352: the first shader | 3.385 ms | 3.742 ms (+0.36) | 3.594 ms (+0.21) |
| f353: the blend unrolled, the cache's non-casters | 3.388 ms | 3.728 ms (+0.34); stable shadow casters 3.667 | — |
| f354: textures a layer has not got not read | 3.435 ms | 3.653 ms (+0.22) | — |
| f355: as shipped (one set of patches) | 3.357 ms | 3.617 ms (+0.26) | — |
| f356: the gas volume's fix (below) | 3.361 ms | 3.587 ms (+0.23) | — |

Per pass (f352, one run each with GPU timestamps; chunks → node at 1 m → at 4 m): opaque 1.05 → 1.17 → 1.13 ms,
shadows 0.44 → 0.60 → 0.47 ms, depth pre-pass (normals and roughness for SSAO: the material runs in it too) 0.42 →
0.51 → 0.53 ms; everything else the same. The node draws 16 times the chunks' vertices near the camera at 1 m; at
the chunks' 4 m it costs 0.21 ms more for its material and morphing vertex shader. The render thread's time is the
same (1.0 ms): one draw a pass instead of 64 chunks', the selection 3-30 µs on the main thread.

Shadows: the camera's patches change whenever it moves, so the directional shadow cache takes the terrain for a
dynamic caster. A second, stable set of patches cast the shadows (chosen again after a tenth of `lod_distance`):
3.667 against 3.728 ms (f353), within the runs' spread, and the shadow pass 0.603 ms in both: during the flythrough
the cascades are unstable or uneconomic and drawn stock either way (cascade-frames cached: chunks 4 586, the node
3 952 with either set; stock 24 000-24 636, of them 13 688 unstable). Doubling the stable set's ranges (so that it
would be finer than the camera's anywhere it was used) put false shadows on the distant ground: casters sharper than
the mip-smoothed ground they shadowed. Removed; the cache's skip of non-casters stays (exact).

## Decision

**KEEP.** FellowEngine has a terrain: Slice Lab's valley at four times the chunks' resolution (16 times their vertices near
the camera), with levels of detail to the horizon, layered materials, collision and height queries, for 0.23 ms of GPU
a frame at 1080p (+6.7 %; frame time 4.43 → 4.66 ms), a render thread 0.16 ms shorter and 17 µs of the main thread;
no crack in any view of a 4 km map; FellowEngine's weather on it as on any material. The gas volume's and the shadow
cache's fixes are exact and general.

Rejected or left for later: geometry clipmaps with snapped rings (Terrain3D's layout: a mesh that moves by whole cells
gets the same wrong motion vectors without the second buffer, and its rings cannot be culled one by one); per-patch
draws (hundreds of draws a pass instead of one); index-based control maps (Terrain3D's 32 materials in one RGBA32
texel) — weights in two RGBA8 maps are simpler to paint and blend, at 9 layers; textures packed into arrays (would
force one size and format on every layer). Not done: sculpting and painting in the editor, foliage scattered by layer,
holes, streaming the heightmap in regions, virtual texturing.
