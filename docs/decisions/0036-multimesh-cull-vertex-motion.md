# 0036 — MultiMesh culling for materials that move vertices (wind), within the node's cull margin

Status: **MEASURING** (option, off) · Branch `fellow/geometry` · Toggle
`rendering/fellow/geometry/multimesh_cull_vertex_motion` (runtime, off; works with `geometry/multimesh_culling`, 0034) ·
Code: `render_forward_clustered.cpp` (`_fellow_multimesh_cull_collect`), `shaders/effects/fellow_multimesh_cull.glsl`
(the margin), `renderer_geometry_instance.{h,cpp}` and `renderer_scene_cull.cpp` (the node's extra cull margin passed to
the renderer) · Bench: Foliage Lab `--foliage_wind=1`.

## Problem

MultiMesh culling (0034) leaves out every material that writes `VERTEX`: the instance's box then no longer bounds its
vertices. Vegetation in games nearly always sways in the wind, in exactly such a vertex shader, so in real content the
culling would rarely apply.

Stock Godot has the same problem for whole nodes and gives projects one tool for it:
`GeometryInstance3D.extra_cull_margin`, a distance added to the node's box (in the node's space) for culling and LOD.
A project whose vegetation sways sets it to cover the sway (or the sway pops at the screen's edges and in shadows).

## Change

* Every job's instance box is grown by the node's extra cull margin, on every axis in the node's space, as stock grows
  the node's box (also for per-instance LOD's distance, 0035, as stock's LOD uses the grown box). Growing a box can only
  cull less, so this part is exact for every material.
* Under the option, a material that writes `VERTEX` no longer excludes a MultiMesh whose node has a margin > 0: the
  margin is taken as the bound of the displacement. Writes to `POSITION`, the model-view or projection matrices, point
  size, particle trails, the clip-space scale and reads of `INSTANCE_ID` still exclude it.
* `RenderGeometryInstance::set_fellow_extra_margin` passes the node's margin to the renderer (when the geometry
  instance is created and when the margin changes); the base implementation keeps it, the other renderers ignore it.

The condition for an identical image is that every vertex stays within its instance's box grown by the margin. Stock
assumes less of the project: that the vertices stay within the whole node's box grown by the margin. A material that
moves vertices far from their instance (more than the margin) but stays within the node's box would lose instances
under the option; that is why it is an option.

## Verification (lavapipe, 960 × 540, fixed 60 fps so that `TIME` advances the same in both processes)

Foliage Lab with `--foliage_wind=1`: grass, bushes and tree crowns sway by `TIME` (up to 1.82 × the strength in the
mesh's space: 0.14 m for grass, 0.10 m for bushes, 0.36 m for trees in the node's space at the largest instance
scales); the nodes' margins 0.2, 0.15 and 0.5 m bound it with room to spare. Culling off against culling with the
option:

| | images | differing pixels |
|---|---:|---:|
| one MultiMesh per type, captures | 3 | 0 |
| chunks, captures | 3 | 0 |
| one MultiMesh per type, `foliage_walk` (TAA) | 60 | 0 |
| one MultiMesh per type, captures, with the "nothing to cull, no job" rule (0034, `fellow93`) | 3 | 0 |

The swaying MultiMeshes are culled: 56.8 M instances tested and 29.1 M drawn over the captures, as without wind.

What the margin is for: with the margins scaled to 1 % (0.002-0.005 m) only 0.05 M more instances were culled over
the captures (27.77 M against 27.72 M; the boxes are mostly the meshes' own), still without a differing pixel. A
stronger sway is needed to show what too small a margin loses:

| sway (`--foliage_wind_strength`) | margins (`--foliage_wind_margin`) | differing pixels |
|---|---|---:|
| × 8 (grass up to 1.1 m, trees 2.8 m in the node's space) | × 0.01 | **1 591** (meadow, 55.4 dB, max \|d\| 72) |
| × 8 | × 8 | 0 |

The lost pixels are swaying grass at the view's left and right edges and shadows of instances culled from the
cascades: the test sees what a margin that does not cover the sway loses, and a margin that does loses nothing.

RTX (`m7_f103_wind_temporal`, `fellow93`, 1080p, `foliage_walk`, 60 frames, TAA): culling off against culling with the
option 78.14 dB mean, 74.54 min, max |d| 12, 0.000 % of pixels > 2 — the floor of these sequences (78.27 / 74.93 in
`m7_f101`).
