# 0160 — Sculpting and painting the terrain: brushes that update only what they touch

Status: **KEEP** (new methods of FellowTerrain3D's and an editor plugin: nothing changes without them) · Branch
`fellow/geometry` · Code: `modules/fellow_world/fellow_terrain_3d.{h,cpp}` (`sculpt`, `paint`, `blit_heightmap_region`,
`blit_splat_region`, the region updates), `modules/fellow_editor/fellow_terrain_editor.{h,cpp}` (the toolbar and its
brush), `modules/fellow_editor/fellow_editor_plugin.cpp` (the Terrain setup) · Checks:
`benchmarks/project/bench/terrain_edit_check.gd`; the editor check's terrain brush and setups.

## Problem

Decision 0158's terrain draws heights and splat maps made elsewhere: a person had nothing to shape the ground with in
the editor, and a game nothing to change it with while it runs (a crater, a road's bed). The only way to change it was
to edit the images and call `update_heightmap()`, which builds everything again — the CPU copy, the levels' bounds,
the normals, every texture and mip, every collision shape, every foliage cell: 28 ms for a 1 km map at 1 m on the
cloud CPU, 150 ms for 2 km, 455 ms for 4 km — far too slow for a brush that dabs every frame.

## Change

**Brushes on the terrain itself** (usable in a game and testable headless):

* `sculpt(global_position, radius, strength, mode, target_height)`: `SCULPT_RAISE`, `SCULPT_LOWER` (by `strength`
  metres at the centre), `SCULPT_SMOOTH` (towards each texel's 3 × 3 mean), `SCULPT_FLATTEN` (towards
  `target_height`); the falloff `(1 − d²)²` of the distance as a share of the radius. A heightmap that is not
  `FORMAT_RF` without mipmaps becomes one first (the heights as drawn).
* `paint(global_position, radius, layer, strength)`: every layer's weight towards 0 and the painted one's towards 1, so
  they still add up to 1 (layer 0 is what the others leave). The splat maps are made (RGBA8, the heightmap's size)
  when there are none; the second when a layer above 4 is painted.
* `blit_heightmap_region()` and `blit_splat_region()`: a rectangle written back (the editor's undo; a game's stamps).

**Only the region is updated.** At once: the heights' CPU copy (the queries answer from it), the levels' bounds (the
blocks over the region and their parents), the normals over it and a texel around, the mipmaps of the height, normal
and splat images over the region — the same 2 × 2 box and rounding as `Image.generate_mipmaps()`, so an edited terrain
is the one a rebuild from its images makes, texel for texel — and the foliage cells over it (dropped, scattered again
from the new ground). At the frame's end, once for all the frame's dabs: the textures uploaded and the collision
shapes over the region rebuilt. The first paint stroke on a terrain without splat maps makes them and their textures
only (no weight changes, so nothing else is built again).

**Two fixes the editing found.** A data rebuild now gives the patches' MultiMeshes the new mesh (the old one was freed
under them: errors in the console and nothing drawn, whenever a drawn terrain's data changed). And the foliage cells'
tasks read copies of their own square of the heights and the splat maps (two texels around), made on the main thread
when they start, not the terrain's: a brush never races a task reading the heights it writes, and a rebuild retires the
cells whose tasks have not run instead of waiting for them (decision 0159's rule; a wait for a queued task may run
other queued work, pipeline compilations among them: a first paint stroke over grown foliage waited 3.6 s on
lavapipe). The copies are read with the same arithmetic as the whole maps: the same instances.

**The editor** (`FellowTerrainEditorPlugin`, through the terrain's bound methods: no fellow_world header): a selected
terrain shows a toolbar in the 3D view's menu — Raise, Lower, Smooth, Flatten (to the height where the stroke starts),
Paint, the layer to paint, the brush's size (diameter, 0.5-256 m) and strength. The brush is drawn on the ground under
the mouse (its circle and its half); dabs are a tenth of the size apart along a stroke; Shift inverts Raise and Lower.
A stroke is one undoable action: the touched rectangle of each image before and after it, written back with the blits
(an image the terrain replaced — made editable, or made — is swapped back as the property). The FellowEngine menu adds a
flat 256 m terrain with a grass and a rock layer, ready to sculpt.

## Verification

* `terrain_edit_check.gd` (lavapipe and headless; 513² heights with collision and foliage): a raise at a texel adds the
  strength exactly; smoothing lowers the roughness; flattening reaches its target; painted weights reach their layers
  and add up to 1; physics rays meet `get_height()` over the edits; the foliage over painted grass grows again, as many
  instances as a terrain built from the edited images; every texture of the edited terrain (heights, normals, both
  splat maps, all mips, read back from the GPU) equals that rebuilt terrain's bit for bit; blits of the saved
  rectangles restore the original textures bit for bit. Then a dab's cost and the frame's update on larger maps.
* The editor check (lavapipe, a window): the toolbar shows for a selected terrain; a Raise stroke dragged with the
  mouse across the idle terrain raises it; undo restores every height, redo gives the stroke's; a first paint stroke
  makes the splat map and its undo removes it; a second writes a rectangle both ways; the Terrain setup adds without a
  configuration warning and undoes.
* RTX 5070 Ti Laptop (job f359, built and ready; not run yet: the GPU was not available): the same check at 1025²,
  2049² and 4097² with a raise and a paint stroke held for 10 seconds, a dab a frame, the frame times meanwhile.

## Results

Every check passes on lavapipe and headless; every texture of the edited terrain is bit-exact with a rebuild's, and the
foliage over it the same (162 instances both). On the cloud CPU, a dab's own time (the CPU's copy, bounds, normals,
mipmaps of the region): 0.02-0.08 ms for a 12-16 m brush at 1 m, whatever the map's size (513² to 4097²); painting
0.03-0.06 ms (the first stroke, making the splat maps: 6 ms at 1025², 34 ms at 2049², 187 ms at 4097²); the update at
the frame's end without a GPU (headless: the collision shapes) 0.3-0.5 ms, and on lavapipe (the textures copied into
its memory too) 6-9 ms at 2049²; undoing a 340 × 230 rectangle 10-13 ms headless. Building the terrain again, what an
edit cost before: 28 ms at 1025², 150 ms at 2049², 455 ms at 4097². (Headless, a stroke's first dab also copies the
height image the dummy renderer holds: 2-60 ms, by the map's size; a GPU's renderer holds no copy.)

The textures' upload is the open cost: the RenderingServer updates a texture whole, so a frame with an edit uploads the
heights' and normals' textures with their mips (8 MB at 1025², 34 MB at 2049², 134 MB at 4097²) and the edited splat
map's (6, 22, 89 MB). What that costs on a GPU, and what sculpting and painting feel like on the larger maps, is job
f359's measurement, still to run.

## Decision

**KEEP.** The terrain can be shaped and painted in the editor with undo, and changed by a game, at the cost of a few
milliseconds a dab on the main thread, leaving exactly the terrain its images make.

Not done: brushes for holes, noise or erosion; stamping a heightmap from the editor (the blits do it from code);
painting the foliage's density apart from the layers; a partial upload of the textures (above: the whole texture is
uploaded again each frame something was edited); the RTX's measurement (job f359); the heightmap's undo snapshot is a
copy of the whole image a stroke.
