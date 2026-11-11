# FellowEngine — milestone 70 report (terrain virtual texturing)

Status: **FINAL** (2026-11-11): built and checked on lavapipe only — the owner asked for no GPU run this time ("dawaj
to wirtualne teksturowanie ale na razie bez gpu"). Branch `fellow/geometry`. Decision record:
`docs/decisions/0178-terrain-virtual-texture.md`.

## 0. Result

The readiness assessment's open-world ground ended at "next virtual texturing"; this milestone does it.

* **`FellowTerrain3D.virtual_texture`** (off): the terrain's material — the splat weights, nine layers' textures from
  the top or three projections, the height blend, the macro variation — baked once by a compute kernel into pages of
  128 × 128 texels (`virtual_texture_texels_per_meter`, 64, at the finest mip), kept in an atlas every terrain shares
  (`rendering/fellow/terrain/virtual_texture_atlas_size`, 4080 texels: 900 slots, 127 MiB) and found through a page
  table per terrain; a fragment reads two mips' pages and blends them instead of blending the layers. The coarsest
  mip is each terrain's overview (128 × 128 with its own mips), read far away and until a page is baked.
* **Pages chosen on the CPU** each frame from the camera's distance to them, the missing ones baked within
  `rendering/fellow/terrain/virtual_texture_pages_per_frame` (16) for every terrain, coarsest and nearest first; the
  least recently wanted give their slots up; a nearly full atlas makes every terrain's pages coarser until it is not.
  Sculpting, painting and layer changes bake the pages over them again. Regions have their own pages.
* **Lavapipe**: five views of a 513 m terrain within 41.0-43.6 dB of the layers' pictures (regions, edits, a small
  atlas under pressure: 41.3-46.7 dB); a frame in software 19-36 % shorter (the fragments' work: a proxy; the GPU's
  answer is the next measurement); a flight of 301 frames with one frame missing pages (twenty with a small atlas and
  the default budget, 1 273 pages evicted).
* **Fixed on the way**: the terrain layers' colours were converted from sRGB twice since milestone 67 (a `Color` in a
  `vec4` uniform is converted by the renderer), so layers drew darker than their colour; they reach the shader as
  vectors now. The scenes milestone 67 had converted to compensate have their colours back (the same look); Slice Lab
  and Frontier Lab, which wrote their colours as linear, draw them so again.

## 1. Development

* `modules/fellow_world/fellow_terrain_vt.{h,cpp}` (new): `FellowTerrainVT` — the page geometry, the CPU quadtree,
  the shared atlas's slots and their ages, the pressure bias, the page table (an RGBA8 `ImageTexture` with a level per
  mip, uploaded by changed rectangles), the overview (two `Texture2DRD` with eight mips), the batches dispatched on the
  render thread (one dispatch for a frame's pages, one per overview level through views of that level).
* `modules/fellow_world/fellow_terrain_vt.glsl` (new): the bake kernel — `FellowTerrain3D`'s layer arithmetic with a
  texel's footprint as the gradients; pages with their borders into the atlas, or an overview level without border.
* `modules/fellow_world/fellow_terrain_3d.{h,cpp}`: the shader's virtual texture variant (no layer textures: the
  table, the atlas, the overview; the mip from the gradients, 4:1 anisotropy within the border, the two mips blended,
  the footprint kept within the border); the pages a camera wants each frame (its frustum and a pixel's angle with the
  viewport's 3D scaling); invalidation from the edits, the layers, the height blend, the macro variation and a
  region's offset; `debug_view` Virtual Texture; `get_virtual_texture_stats()`; a configuration warning without the
  RenderingDevice; the layers' colours as vectors.
* `servers/rendering/fellow_settings.{h,cpp}`: `terrain/virtual_texture_atlas_size`,
  `terrain/virtual_texture_pages_per_frame`; the counters `terrain_vt_pages_baked`, `terrain_vt_dispatch_usec`.
  `doc/tools/fellow_settings_docs.py`, `doc/classes/ProjectSettings.xml`, the class reference.
* `benchmarks/project/bench/terrain_vt_check.gd` (new); `tools/fellow_regression/project/scenes/terrain_vt.tscn` and
  `scripts/terrain_vt.gd` (new: the terrain scene with the pages), `suite.json`.
* The colours: `tools/fellow_regression/project/scripts/terrain.gd`, `modules/fellow_editor/fellow_editor_plugin.cpp`
  (the terrain preset), `showcase/tools/build_showcase.gd` — their colours as before milestone 67.

## 2. Verification

* `terrain_vt_check.gd` twice: a budget of 64 pages a frame with the default atlas; the default budget with an atlas
  of 144 slots (evictions, the pressure bias).
* The regression terrain against its layers at 512 × 288 and 2048 × 1152; a single-colour layer against its colour
  (the double conversion).
* The regression suite with the editor build and the release template: every picture exact but the two terrain
  scenes (one pixel one level off: the colours' round trip of milestone 67 undone; blessed) and the new `terrain_vt`
  (blessed); the release template's pictures the editor build's.
* The editor check: every check passes; an untouched editor showing a terrain with its virtual texture draws no frame
  (`--perf-idle`: 0 of 1 450, as without it).
* The `terrain_vt` scene with the Compatibility and Mobile renderers: no error (Compatibility draws the layers).

## 3. Measurements

See decision 0178 for the tables. In short (lavapipe, 640 × 360, a 513 m terrain at 64 texels a metre):

| | default atlas, 64 pages a frame | 144 slots, 16 a frame |
|---|---|---|
| the five views against the layers | 41.0-43.3 dB | 41.8-43.3 dB (pressure bias up to 1.25) |
| regions (across, down) | 42.2, 43.6 dB | 43.8, 41.3 dB |
| after a sculpt and a paint stroke | 46.6 dB, 29 pages baked again | 46.7 dB, 59 |
| the flight: frames with pages missing | 1 of 301 | 20 of 301 (1 273 evicted) |

A frame on lavapipe: near the ground 85.4 → 69.0 ms, across the land 126.2 → 80.4 ms.

## 4. Next

The RTX: a fragment's cost with and without the pages (Frontier Lab's regions with `virtual_texture`), the bakes'
cost in a flight, the atlas's size and budget for 1 080p and 4K.
