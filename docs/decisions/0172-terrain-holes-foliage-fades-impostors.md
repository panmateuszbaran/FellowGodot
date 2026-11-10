# 0172 — The terrain's holes, its foliage fading out, its trees' impostors baked into files

Status: **KEEP** (new properties and methods, all off by default: nothing changes without them) · Branch
`fellow/geometry` · Code: `modules/fellow_world/fellow_terrain_3d.{h,cpp}` (`hole_map`, `paint_holes()`,
`blit_hole_region()`, `is_hole()`, the patches over holes with their own material, the collision's NaN heights;
`FellowTerrainFoliage.fade_distance`, `impostor`, `impostor_distance`; `bake_foliage_impostors()`),
`modules/fellow_world/fellow_impostor_baker.{h,cpp}` (atlases as `PortableCompressedTexture2D` to be saved),
`modules/fellow_world/fellow_pipeline_warmer_3d.{h,cpp}` (the impostors on the warm-up's shelf),
`modules/fellow_editor/fellow_terrain_editor.{h,cpp}` (the Holes tool, Bake Impostors),
`servers/rendering/renderer_rd/shaders/effects/fellow_multimesh_cull.glsl` and `render_forward_clustered.{h,cpp}`
(the range's fade), `rendering_server.h`, `rendering_method.h`, `rendering_server_default.h`,
`renderer_scene_cull.{h,cpp}`, `renderer_geometry_instance.{h,cpp}` (`fellow_instance_set_instance_range_fade()`) ·
Checks: `benchmarks/project/bench/terrain_holes_check.gd`; the editor check's terrain brush (holes); the regression
suite's `terrain_holes`.

## Problem

Three gaps of the open-world ground (decisions 0158-0160) the readiness assessment lists: the terrain could not have a
hole (a cave's mouth, a tunnel's portal, a basement under a house: the ground drawn and colliding over it); the
foliage stopped at its range — every plant of a kind popped out on one ring around the camera, the grass's edge
visible as a line that walked with the player; and trees could only be meshes to their range (a forest to 400 m draws
a few hundred full trees) or impostors baked at run time by `FellowHLOD3D` for nodes, not for the terrain's scattered
kinds and not into files.

## Change

**Holes.** `hole_map` (an `Image`, the heightmap's size; a texel above half marks that vertex). A triangle touching a
marked vertex is gone, everywhere the same way:

* *Drawn*: the patches whose blocks touch a hole (a per-level pyramid of flags beside the heights' min/max) go to a
  second pair of MultiMeshes drawn with the terrain shader's hole variant — the fragment finds its triangle (the mesh's
  diagonal from (x + 1, z) to (x, z + 1)), reads its three vertices' flags (`texelFetch`, the hole texture R8) and
  discards. The other patches keep the material without a discard (early depth test, the depth prepass as before);
  without a hole map there is no second material and no second MultiMesh at all. The variant's code goes to the
  shader baker with the terrain's.
* *Collision*: those vertices' heights NaN in the height map shapes. Jolt (FLT_MAX inside) and Godot Physics both
  leave out the triangles touching them; the render mesh's triangles and the collision's share the same diagonal, so
  what is not drawn does not collide, triangle for triangle.
* *Foliage and queries*: a cell's copy of the heights has NaN at holes and a point whose height or slope reads one is
  not planted; `get_height()` returns NaN over a hole (the editor's brush ray passes through it), `is_hole()`.

`paint_holes(global_position, radius, hole)` and `blit_hole_region()` change a region and update only it (the flags'
blocks, the texture's texels at the frame's end, the foliage cells and collision shapes over it); the editor's toolbar
has a Holes tool (Shift fills), one undoable action a stroke, as sculpting.

**Foliage fading out.** `FellowTerrainFoliage.fade_distance`: the GPU's MultiMesh culling (decision 0034), which already
tests each instance against the range (0122), scales an instance about its origin by `(end - d) / fade` over the last
metres (the same path that grows decision 0121's thinned survivors), and drops it below a hundredth. The meshes and
materials are untouched — any plant shrinks into the ground. A new renderer call carries the distance
(`fellow_instance_set_instance_range_fade()`); 0 is the old behaviour.

**Impostors baked into files.** `FellowTerrainFoliage.impostor` + `impostor_distance`: each cell gets a second
MultiMesh of the same instances with the impostor's quad; the meshes draw to the distance, the impostors from it to
the range (and fade there), per instance on the GPU, and in the shadows per directional cascade as an HLOD's proxies
and members (0122). `bake_foliage_impostors(directory, frames, resolution)` renders each kind with a distance through
decision 0122's octahedral baker, one view a frame, and saves it as a `.res`: the quad, its shader material and its
two atlases as `PortableCompressedTexture2D` (lossless WebP, with their mips; a game decodes them at load and keeps no
compressed copy). The editor's Bake Impostors button saves them beside the scene (`impostors/`) in one undoable action.
The warm-up's shelf (0162) carries the impostors too.

## Verification

* `terrain_holes_check.gd` (lavapipe, 640 × 360, below) and the editor check (the Holes tool: a first stroke makes the
  hole map, undone and redone; a second hole and its filling, undone).
* The image regression suite: every existing picture exact (no hole map, no fade, no impostor: nothing changes); a new
  `terrain_holes` golden.

## Results

**Holes** (a 257 × 257 terrain at 1 m, a round hole of 8 m cut with the brush, grass at 3 per m², a red sky):

| | |
|---|---|
| hole vertices, patches drawn with the hole material | 197, 4 of 160 |
| the sky through the hole's disc (inside its rim) | 100 % of its pixels |
| red pixels elsewhere (cracks) | 0 |
| a ball dropped over the hole after 3 s | y = −33.8 m (fell through), Godot Physics and Jolt |
| a ball beside it | at rest 0.50 m (Godot Physics), 0.48 m (Jolt) over the ground (radius 0.5) |
| `is_hole()`, `get_height()` at the centre | true, NaN; a hole cut and stamped out again: false |

**Fading out** (grass at 2 per m² to 40 m, the camera 1.7 m over the ground; the grass's share of each band of 8 rows
from the horizon down — TAA moves single pixels, not these):

| fade_distance | 152 | 160 | 168 | 176 | 184 | 192-224 |
|---|---|---|---|---|---|---|
| 0 | 0.48 | 0.66 | 0.71 | 0.63 | 0.55 | 0.44, 0.39, 0.37, 0.32, 0.28 |
| 15 m | **0.34** | **0.49** | **0.59** | 0.62 | 0.55 | the same |

**Impostors** (323 trees of about 1,300 triangles to 400 m, impostors from 50 m; lavapipe's "GPU" time is its CPU
rasterizing, a relative measure of the work):

| | without impostors | impostors from 50 m |
|---|---|---|
| frame, no shadows | 78.9 ms | **49.2 ms** (−38 %) |
| frame, the sun's shadows | 214 ms | 191 ms (−11 %: the shadow passes draw a cell's meshes in the near cascades whatever their distance) |
| the far trees through a 15° lens | — | mean difference 0.0031 a channel from the meshes |
| bake (8 × 8 views of 128 px) | | 13.6 s on lavapipe (a view a frame), a 553 KB file |

**On the RTX** (job f372, 1920 × 1080, the release template): the holes the same (the ball through at −33.8 m, the
sky through all of the disc, no crack); 323 trees with impostors from 50 m: the GPU's frame 0.60 → 0.57 ms without
shadows, 0.78 → 0.75 ms with the sun's — little for this GPU to save with 323 trees; the far trees 0.0032 a channel
from the meshes. The fade (job f373, the check's rows now scaled to the picture's height): the grass's share of the rows near the
horizon 0.43, 0.62, 0.68 without, 0.30, 0.46, 0.57 with 15 m, the nearer rows the same.

## Decision

Keep. Holes cost nothing where there are none (no second material; the patches over holes only pay the discard),
fades and impostors are opt-in per kind.

## Rejected / not done

* **Holes as NaN vertices in the mesh** (the vertex shader collapsing a hole's triangles): coarse levels sample the
  heights' mips, so a hole's vertex would vanish in the distance and its triangles come back; the fragment's test is
  exact at every level.
* **The whole terrain with a discard**: the depth prepass and early depth test lost everywhere for a few holes.
* **A dithered cross-fade** at the range or the impostors' hand-over: needs the per-instance alpha the instances do not
  carry; shrinking needs nothing from the material. The hand-over is a switch, as an HLOD's.
* **The impostors' atlases VRAM-compressed** (S3TC/BPTC): smaller but per-platform formats and an editor-only
  compressor; lossless files decode on every platform. **Not yet**: terrain regions streamed with the world's cells
  (milestone 65), the range and its fade in the shadow passes (the shadow cache keeps casters independent of the
  camera).
