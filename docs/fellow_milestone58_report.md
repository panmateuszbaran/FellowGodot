# FellowEngine — milestone 58 report (sculpting and painting the terrain)

Status: **checked on lavapipe**, __F359_STATUS__ (2026-11-06). Branch `fellow/geometry`. Decision record
`docs/decisions/0160-terrain-editing.md`.

## 0. Result

The rest of point 3 of the plan towards AAA: the terrain's editing tools. `FellowTerrain3D.sculpt()` raises, lowers,
smooths and flattens the ground under a brush, `paint()` paints a layer's weight, and both update only the region they
touched — the queries at once, the textures and collision shapes at the frame's end, the foliage over it scattered
again — leaving exactly the terrain a rebuild from the edited images makes (every texture and mip bit for bit). In the
editor, a selected terrain has a toolbar in the 3D view: Raise, Lower, Smooth, Flatten, Paint, the layer, the brush's
size and strength, the brush drawn on the ground, one undoable action a stroke. The FellowEngine menu adds a flat
256 m terrain ready to sculpt. A game can use the same methods (craters, a road's bed) and the blits for stamps.

__F359_SUMMARY__

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
* RTX 5070 Ti Laptop: __F359_VERIFY__

## 3. Measurements

__F359_TABLE__

## 4. Limits

* The textures are uploaded whole each frame something was edited (the RenderingServer has no partial update of a
  texture): __F359_UPLOAD__
* No brushes for holes, noise or erosion; no stamping from the editor (`blit_heightmap_region()` from code); no
  painting of the foliage's density apart from the layers' weights.
* A stroke's undo keeps a copy of the whole image it changes from the stroke's start.

## 5. Next

Pipeline pre-warming (point 4 of the plan); terrain regions streamed with the world's cells.
