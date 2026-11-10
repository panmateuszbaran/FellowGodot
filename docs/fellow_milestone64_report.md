# FellowEngine — milestone 64 report (the terrain's holes, foliage fading out, tree impostors in files)

Status: **FINAL** (2026-11-10): checked on lavapipe; the RTX's holes and impostors in section 3a (job f372). Branch
`fellow/geometry`. Decision record: `docs/decisions/0172-terrain-holes-foliage-fades-impostors.md`.

## 0. Result

The second of the three milestones agreed after the showcase was set aside (63: recorded pipelines; 64-65: the
open-world ground; 66: proxies for unloaded cells, mesh LOD streaming). Its terrain half that does not need streaming:

* **Holes in the terrain** (`hole_map`, `paint_holes()`, the editor's Holes tool): a triangle touching a marked vertex
  is not drawn, does not collide and has no foliage. Only the patches over holes draw with a material that discards;
  the rest of the terrain keeps its material. A ball dropped over a hole falls through it with Godot Physics and with
  Jolt; one beside it rests on the ground.
* **Foliage fading out** (`FellowTerrainFoliage.fade_distance`): instead of popping out on a ring around the camera,
  plants shrink into the ground over their range's last metres, scaled one by one by the GPU's MultiMesh culling —
  the meshes and materials unchanged.
* **Tree impostors baked into files** (`FellowTerrainFoliage.impostor`, `impostor_distance`,
  `FellowTerrain3D.bake_foliage_impostors()`, the editor's Bake Impostors): octahedral impostors saved as `.res`, drawn
  instead of the meshes from a distance, per instance on the GPU. 323 trees to 400 m on lavapipe: 78.9 → 49.2 ms
  without shadows.

Terrain regions streamed with the world's cells moved to milestone 65: it changes how the terrain's data is held, not
what it draws.

## 1. Development

* `modules/fellow_world/fellow_terrain_3d.{h,cpp}`: the hole map (CPU flags, a per-level pyramid of blocks touching a
  hole, an R8 texture), the shader's hole variant (`_terrain_shader_code(true)`, also given to the shader baker), the
  second pair of MultiMeshes and its material, the split of the patches in `_write_patches()`, `paint_holes()`,
  `blit_hole_region()`, `is_hole()`, holes in `get_height()`, the collision's data (`_collision_region_data()`, NaN at
  holes) and the foliage's copies; `FellowTerrainFoliage.fade_distance`, `impostor`, `impostor_distance`; the cells'
  impostor MultiMeshes with their ranges and HLOD roles; `bake_foliage_impostors()` and the signal
  `foliage_impostors_baked`; configuration warnings (a hole map of another size, an impostor distance without an
  impostor or beyond the range or without MultiMesh culling); `get_stats()`: `hole_vertices`, `hole_patches`,
  `foliage_impostor_instances`.
* `servers/rendering/renderer_rd/shaders/effects/fellow_multimesh_cull.glsl`, `render_forward_clustered.{h,cpp}`: the
  range's fade (`range_fade_scale()`, the job's `contribution_mode.z`); `rendering_server.h`, `rendering_method.h`,
  `rendering_server_default.h`, `renderer_scene_cull.{h,cpp}`, `renderer_geometry_instance.{h,cpp}`:
  `fellow_instance_set_instance_range_fade()`.
* `modules/fellow_world/fellow_impostor_baker.{h,cpp}`: `queue_bake(..., portable)` — the atlases as
  `PortableCompressedTexture2D` (lossless, their mips kept) for saving.
* `modules/fellow_world/fellow_pipeline_warmer_3d.{h,cpp}`: the foliage's impostors on the warm-up's shelf.
* `modules/fellow_editor/fellow_terrain_editor.{h,cpp}`: the Holes tool (undo with `blit_hole_region()`), the Bake
  Impostors button (beside the scene, one undoable action).
* `benchmarks/project/bench/terrain_holes_check.gd` (new); the editor check's terrain brush cuts, fills and undoes
  holes; the regression suite's `terrain_holes` scene (new golden).
* The class reference (`FellowTerrain3D.xml`, `FellowTerrainFoliage.xml`), `docs/FELLOWENGINE_OPTIONS.md`.

## 2. Verification

* `terrain_holes_check.gd` on lavapipe (640 × 360): the results below.
* The editor check (1920 × 1080): every check passes; the Holes tool's first stroke makes the hole map (undone: no
  map; redone), a second hole and its filling with Shift, both undone back to the same image.
* The image regression suite (editor build and release template): every existing picture exact; `terrain_holes`
  blessed (its picture looked at: a round hole, a trench and a rectangle, their rims along the triangles).
* `xmllint` and `make_rst.py --dry-run`: no errors, no warnings.

## 3. Measurements

**Holes** (257 × 257 at 1 m, a round hole of 8 m, grass at 3 per m², a red sky seen through it): 197 hole vertices; 4
of 160 patches drawn with the hole material; 100 % of the hole's disc shows the sky, no red pixel elsewhere; a ball over
the hole falls through (−33.8 m after 3 s), one beside it rests 0.50 m over the ground (radius 0.5; Jolt 0.48 m).

**Fading out** (grass at 2 per m² to 40 m, seen from 1.7 m; the grass's share of the rows near the horizon): 0.48, 0.66,
0.71 without a fade, 0.34, 0.49, 0.59 with 15 m; the nearer rows the same.

**Impostors** (323 trees of about 1,300 triangles to 400 m, impostors from 50 m): 78.9 → 49.2 ms a frame without
shadows, 214 → 191 ms with the sun's; the far trees through a 15° lens differ from the meshes by 0.0031 a channel on
average. The bake: 8 × 8 views of 128 px, 13.6 s (a view a frame), a 553 KB file.

## 3a. On the RTX (job f372, 1920 × 1080, the release template from `56490d7`)

* **Holes**: the same — the ball over the hole at −33.8 m after 3 s, the one beside it at rest 0.51 m over the
  ground; the sky through all of the hole's disc, no red pixel elsewhere.
* **Impostors** (323 trees to 400 m): the GPU's frame 0.60 → 0.57 ms without shadows, 0.78 → 0.75 ms with the sun's
  (the CPU's 0.07-0.15 ms either way); the far trees 0.0032 a channel from the meshes. 323 trees are little work for
  this GPU: the impostors pay off with many more (lavapipe's −38 % is the work saved).
* **Fading out**: not measured — the check counted the grass in rows of a 360-row picture, at 1080 rows the sky (the
  check now scales its rows to the picture; for the next job).

## 4. Limits

* The shadow passes draw a cell's meshes in the near cascades whatever their distance (the range and its fade are the
  camera's): a shrinking plant's shadow stays until its cell is dropped (15 % beyond the range), and the impostors save
  little in the shadows. The range in the shadow passes would make the cached cascades depend on the camera.
* The impostors' hand-over is a switch at the distance (no cross-fade: the instances carry no alpha).
* A hole's edge follows the triangles (1 m steps at 1 m spacing).
* The RTX's measure of the fade is for its next job.

## 5. Next

Milestone 65: terrain regions streamed with the world's cells. Milestone 66: proxies baked offline for unloaded cells,
mesh LOD streaming.
