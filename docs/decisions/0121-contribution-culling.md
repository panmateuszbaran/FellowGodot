# 0121 — Contribution culling: tiny far objects and shadow casters

Status: **KEEP as an option** (changes the image, off) · Branch `fellow/geometry` · Toggles `rendering/fellow/geometry/contribution_cull_pixels`
(runtime, 0 = off), `rendering/fellow/shadows/contribution_cull_texels` (runtime, 0 = off) · Code:
`servers/rendering/renderer_scene_cull.{h,cpp}` (nodes, directional cascades), `renderer_viewport.cpp` and
`rendering_method.h` (the viewport's threshold), `render_forward_clustered.{h,cpp}` and
`shaders/effects/fellow_multimesh_cull.glsl` (MultiMesh instances), `fellow_settings.*`.

## Problem

Far away, most of what a scene contains covers a pixel or less: crates, benches, rocks, grass clumps, the parts of
buildings. Godot draws every node whose box meets the view frustum (and is not occluded, when occlusion culling is
on), however small it is on screen, and every caster whose box meets a shadow cascade, however few of the cascade's
texels it covers. Each costs the same per-object CPU work (culling, light pairing, render list, instance data, a draw
or a share of one) as a near one, and its triangles go through the GPU's front end to cover a pixel or none.

Engines cull such objects by screen size (Unreal's per-primitive screen-size and distance culling, Unity's LOD group
"culled" level). Godot's only tools are visibility ranges (a distance per node, set by hand) and mesh LOD (which
coarsens the mesh but still draws it).

## Change

**Camera.** A threshold in pixels of the viewport's width (as `mesh_lod_threshold`, so the result does not depend on
the 3D resolution scale). The viewport hands its fraction of the width (`threshold / width`) to the culler for each
camera it renders (`RenderingMethod::fellow_set_contribution_fraction`); reflection probes are not affected. In the
scene cull, a mesh or MultiMesh node whose bounding sphere (radius: half its box's diagonal) would cover less than
that is not drawn:

* perspective: the sphere's silhouette at view depth z has half-angle `asin(r / d)`; with `k = fraction / P[0][0]`
  (the projection's x scale), it is too small when `r / sqrt(z^2 - r^2) < k`, i.e. `r^2 < z^2 k^2 / (1 + k^2)` — one dot
  product and a few multiplications, no square root (z: depth along the camera's view direction, so objects at the
  edges of a wide view are judged by their projected size, not their distance);
* orthogonal: `r < k`.

The test runs before the occlusion test (it is cheaper) and only for meshes and MultiMeshes (not particles: their
processing follows their visibility). Nodes with `ignore_occlusion_culling` or `ignore_all_culling` are always kept —
the project's way to keep a tiny far object that matters (a beacon, a light bulb). A culled node is skipped like a
frustum-culled one: no light, probe or decal pairing, no render list element.

**MultiMesh instances.** With MultiMesh culling (0034) every instance is tested on the GPU anyway: the same test per
instance (its box in world space, the camera's view direction as a plane) in the camera's passes. A MultiMesh whose
node box lies inside the view is then a job too (it may hold instances to cull).

**Thinning instead of culling** (`geometry/contribution_cull_instances` = Thin, the default, for MultiMesh instances).
Culling tiny instances one by one removes coverage that adds up: a far meadow of grass clumps, each a pixel, turns to
bare ground (Foliage Lab aerial: 36.8 dB, the meadow visibly lighter). So below the threshold an instance at a
fraction f = r² / r_min² of the threshold's area is kept with probability f (a stable hash of its index: the same
instances in every pass and frame) and grown about its origin by 1 / sqrt(f) to the threshold's size, in the compaction
(both halves for motion vectors): the expected coverage is the same, drawn by fewer, larger instances, and as the
camera approaches, the kept share and their size change continuously. HLOD proxies (0122) are culled, not thinned (they
stand in for nodes, which are culled whole).

**Directional shadow casters.** A threshold in texels of the cascade: a caster whose box's diagonal spans fewer
texels of a cascade (`cascade.shadow_texel_size`) is not drawn into it — per node in the scene cull (stock path and
the shadow cache's paths), per instance on the GPU for culled MultiMeshes (the cascade's texel from its projection and
atlas region). The far cascades, whose texels are large, lose the most casters; the near ones none.

## Verification

* Off (both 0): bit for bit the same images as before (`c39f6d2`) on lavapipe: City Lab 3 captures, Foliage Lab
  chunks and one MultiMesh per type, 0 differing pixels.
* City Lab captures (960 x 540), 2 px (of 960, about 4 px of 1920) against off: aerial 62.94 dB (235 pixels differ,
  at most 36 levels), roofs 78.35 dB (35), street 71.70 dB (22): crates, benches and lamp heads at the far end of the
  city, a pixel or two each, gone.
* Shadow casters at 2 texels against off: 0 differing pixels — City Lab's smallest casters (lamp heads, crates, 0.9-1.4
  m) span 6 texels or more of its far cascade (300 m, 2048 texels per cascade).

* Foliage Lab, one MultiMesh per type (150 k grass clumps over the map), 2 px of 960, aerial view (the forest and
  meadow views are not changed):
  * instances culled: 36.77 dB, 38 102 pixels differ — the far meadow loses its grass, its mean colour 3.7 levels
    lighter (83.5 / 101.3 / 62.3 → 87.3 / 104.1 / 65.9);
  * thinned (the default): 41.07 dB, 30 095 pixels differ, the meadow's mean colour within 0.05 levels of the full
    image's (83.58 / 101.32 / 62.25) — fewer, larger clumps where the culled ones were, the same coverage;
  * chunks layout (grass already limited to 90 m by its visibility range), both thresholds: 67.6 dB (248 pixels).
* Shadow casters at 2 texels, Foliage Lab one MultiMesh per type: 0 differing pixels (a grass clump spans 6 texels of
  the far cascade).

## Performance (lavapipe, City Lab, 480 x 270)

1 px of 480 (the same fraction of the width as 4 px of 1920); per fixed camera, 40 frames, the mean of two runs (ABAB
with the other variants of the session):

| camera | nodes not drawn | objects | primitives | render thread | lavapipe GPU |
|---|---:|---:|---:|---:|---:|
| aerial | 1 421 | 19 636 → 18 215 | −12.7 % | 6.72 → 6.25 ms (−7.0 %) | 281.6 → 271.0 ms (−3.8 %) |
| street | 503 | 16 294 → 15 791 | −8.0 % | 5.22 → 5.19 ms (−0.6 %) | 233.1 → 232.2 ms (−0.4 %) |
| roofs | 292 | 12 235 → 11 943 | −7.4 % | 4.68 → 4.56 ms (−2.6 %) | 239.1 → 215.8 ms (−9.7 %) |

The draws hardly change (201 → 199): City Lab draws its many objects instanced, so a culled node saves its per-object
work (culling, pairing, its element and instance data) and its triangles, not a draw. The saving grows with the share
of the view that is far clutter and with the threshold; it is the project's choice how many pixels it can lose.

## RTX 5070 Ti Laptop (`m23_f267_city`, fellow200, 1080p, 4 px, casters 2 texels, autotune off, 3 runs)

City Lab flythrough: frame 2.053 → 2.020 ms (−1.6 %), render thread 1.592 → 1.569 ms, GPU 1.711 → 1.695 ms; 963
objects and 11 % of the primitives fewer per frame. Per camera the render thread is within its run-to-run spread
(aerial −1.4 %, street +3.6 %, roofs +5.4 %: the test costs about what the few culled objects saved there), the GPU
2-4 % less. City Lab's objects are mostly large; the option is for scenes with far clutter.

Foliage Lab (`m23_f267_foliage`, the same settings, thinning): one MultiMesh per type, flythrough 4.776 → 4.749 ms
(−0.6 %); per camera aerial 6.456 → 5.778 ms (**−10.5 %**, GPU-bound: far grass thinned), forest 1.819 → 1.897 ms
(+4.3 %) and meadow 4.008 → 4.126 ms (+2.9 %) — where nothing is small, the per-instance test and the MultiMeshes that
are wholly in view becoming culling jobs cost a little. Chunks: 2.953 → 3.005 ms (+1.8 %), aerial −3.0 %.

Captures at 1080p on the RTX (`m23_f267_*_capture`): City Lab aerial 59.55 dB (442 pixels differ), roofs 72.37,
street 70.96; Foliage Lab aerial 40.28 dB (3.9 % of the pixels by more than 2 levels: the thinned far meadow), forest
and meadow identical.

## Decision

**KEEP as an option** (off): it changes the image, by design and by the project's threshold. The test is cheap (a dot
product per node in the cull, a few multiplications per instance on the GPU) and off is bit for bit the same image.
