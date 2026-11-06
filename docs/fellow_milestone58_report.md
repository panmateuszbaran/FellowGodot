# FellowEngine — milestone 58 report (sculpting and painting the terrain)

Status: **checked on lavapipe and headless** (2026-11-06); the RTX's measurement (job f359) is built and waits for
the GPU. Branch `fellow/geometry`. Decision record
`docs/decisions/0160-terrain-editing.md`.

## 0. Result

The rest of point 3 of the plan towards AAA: the terrain's editing tools. `FellowTerrain3D.sculpt()` raises, lowers,
smooths and flattens the ground under a brush, `paint()` paints a layer's weight, and both update only the region they
touched — the queries at once, the textures and collision shapes at the frame's end, the foliage over it scattered
again — leaving exactly the terrain a rebuild from the edited images makes (every texture and mip bit for bit). In the
editor, a selected terrain has a toolbar in the 3D view: Raise, Lower, Smooth, Flatten, Paint, the layer, the brush's
size and strength, the brush drawn on the ground, one undoable action a stroke. The FellowEngine menu adds a flat
256 m terrain ready to sculpt. A game can use the same methods (craters, a road's bed) and the blits for stamps.

On the cloud CPU a dab costs 0.02-0.08 ms of the main thread whatever the map's size, against 28-455 ms for building
a 1-4 km terrain again (what an edit cost before), and the frame's update of the collision shapes 0.3-0.5 ms; the
textures are uploaded whole each frame something was edited (8-134 MB with the heights' and normals' mips, by the
map's size), whose cost on a GPU is the measurement still to make. Two fixes came with it: a data rebuild freed the
patches' mesh under them, and the foliage cells' tasks now read their own copies of the ground, so edits never race
them and a rebuild never waits for them (a first paint stroke over grown foliage: 3.6 s → 9 ms on lavapipe).

## 1. Development

* `modules/fellow_world/fellow_terrain_3d.{h,cpp}`: `sculpt()` (`SculptMode`), `paint()`, `blit_heightmap_region()`,
  `blit_splat_region()`; the region updates (the CPU copy, the levels' bounds, the normals, the mipmaps over a region
  as `Image.generate_mipmaps()` makes them, the foliage cells), the frame-end update of the textures and collision
  shapes, `edit_flush_usec` in `get_stats()`; a data rebuild gives the patches' MultiMeshes the new mesh (it was freed
  under them before).
* `modules/fellow_editor/fellow_terrain_editor.{h,cpp}`: `FellowTerrainEditorPlugin` (the toolbar, the brush on the
  ground, strokes, undo and redo through the blits).
* `modules/fellow_editor/fellow_editor_plugin.cpp`: the FellowEngine menu's "Add Terrain (256 x 256 m, to sculpt)".
* `benchmarks/project/bench/terrain_edit_check.gd`; the editor check's terrain brush.
* The class reference.

## 2. Verification

* lavapipe and headless: `terrain_edit_check.gd` (every check; the textures against a rebuild and, after the blits,
  against the originals, bit for bit); the editor check (the toolbar, a mouse stroke, paint strokes, undo and redo, the
  setup); the image regression suite (bit-exact).
* RTX 5070 Ti Laptop: job f359 (the check at 1025², 2049² and 4097², strokes held 10 s with a dab a frame), built
  and ready, not run yet.

## 3. Measurements

`terrain_edit_check.gd` on the cloud CPU (headless unless marked; 12-16 m brushes at 1 m):

| | 1025² | 2049² | 4097² |
|---|---|---|---|
| a sculpting dab | 0.02-0.08 ms | 0.03-0.08 ms | 0.02-0.08 ms |
| a painting dab | 0.05 ms | 0.03 ms | 0.03 ms |
| the first paint stroke (making the splat map) | 6 ms | 34 ms (lavapipe) | 187 ms |
| the update at the frame's end | 0.3 ms | 6-9 ms (lavapipe: the textures too) | 0.5 ms |
| building the terrain again (before: every edit) | 28 ms | 150 ms (lavapipe) | 455 ms |
| the textures uploaded a frame with an edit (heights and normals; a splat map) | 8; 6 MB | 34; 22 MB | 134; 89 MB |

Every check passes, every texture bit-exact with a rebuild's (lavapipe, read back from the GPU), the foliage regrown
over the edits as a rebuild grows it (162 instances both); the editor check's stroke raised the idle terrain's middle
1.36 → 3.92 m, and its undo restored every height.

## 4. Limits

* The textures are uploaded whole each frame something was edited (the RenderingServer has no partial update of a
  texture): 8-134 MB a frame by the map's size; what it costs on a GPU is job f359's to measure.
* No brushes for holes, noise or erosion; no stamping from the editor (`blit_heightmap_region()` from code); no
  painting of the foliage's density apart from the layers' weights.
* A stroke's undo keeps a copy of the whole image it changes from the stroke's start.

## 5. Next

Pipeline pre-warming (point 4 of the plan); terrain regions streamed with the world's cells.
