# FellowEngine — milestone 23 report (far objects: contribution culling, automatic HLOD)

Status: **measured** (lavapipe; RTX jobs `m23_f267`, `m23_f268`, 2026-10-28). Branch `fellow/geometry`. Decision records
`docs/decisions/0121` (contribution culling) and `0122` (automatic HLOD); user-facing summary
`docs/FELLOWENGINE_OPTIONS.md` ("Far objects: automatic HLOD", and the two culling options under "Options that change
the image").

## 0. Result

The user asked (2026-10-27) for "a smart mechanism that changes the renderer or its parameters for far objects".
Two mechanisms, both measured on City Lab (18 800 static meshes, a sun with four cascades):

* **Contribution culling** (`geometry/contribution_cull_pixels`, `shadows/contribution_cull_texels`, off by default):
  the camera skips a mesh or MultiMesh node — and, with MultiMesh culling, each MultiMesh instance on the GPU — whose
  bounding sphere would cover fewer than N pixels of the viewport's width (perspective-correct: the sphere's
  silhouette at its view depth; no square root per node); a directional cascade skips casters spanning fewer than N
  of its texels. Nodes with `ignore_occlusion_culling` are always kept. At about 4 px of 1080p: 300-1 400 nodes per
  view not drawn, 7-13 % fewer primitives, the render thread 1-7 % shorter; 62.9-78.4 dB against the full image.
  Small MultiMesh instances are **thinned**, not culled: a share is kept and grown to the threshold's size, so a far
  meadow keeps its coverage (culled, Foliage Lab's meadow turned 3.7 levels lighter; thinned, its mean colour stays
  within 0.05 levels).
* **Automatic HLOD** (`FellowHLOD3D`, module `fellow_world`, a new node): the static meshes below it are replaced
  beyond a distance by proxies:
  * meshes repeated many times become **instanced proxies** — one MultiMesh per kind, its instances culled one by one
    on the GPU and drawn only from the distance on, each mesh node up to it. The same image bit for bit (lavapipe),
    the render thread **23-42 % shorter**, the GPU unchanged; 18 800 nodes in 14 proxies, built in 44 ms;
  * the others are **merged per cell** and material into meshes with generated LODs (for unique meshes; merging
    repeated ones undoes Godot's instancing — measured);
  * **shadows decided per cascade** (cascades starting beyond the distance draw the proxies), camera-independent, so
    the directional shadow cache keeps them;
  * **impostors** (an option): far instances of heavy meshes drawn as octahedral impostors baked at start —
    measured slower than Foliage Lab's light trees, so only for meshes of 2 000 triangles and more (section 4).

## 1. Development

* **Contribution culling first** (the cheapest lever): the test in the scene cull before the occlusion test, the
  viewport's threshold handed to the culler for each camera (`RenderingMethod::fellow_set_contribution_fraction`, as
  `mesh_lod_threshold` relative to the viewport's width), per instance in `fellow_multimesh_cull.glsl` (MultiMeshes
  wholly inside the view become jobs too), casters per cascade by `shadow_texel_size` (nodes) and by the pass's
  projection and atlas region (instances). Off is bit for bit the old image.
* **HLOD, first version: merge everything per cell.** 18 814 meshes into 196 proxies (2 301 surfaces, 600 k
  triangles): a third of the triangles, but 1 500 draws instead of 200 and the render thread no faster — City Lab's
  meshes are a dozen meshes and materials Godot already draws instanced. With detail levels 47-69 dB (the spheres'
  silhouettes coarsen, the project's LOD trade-off), without them 69-95 dB.
* **HLOD, instanced proxies.** Repeated meshes become one MultiMesh per kind, culled on the GPU (0034), drawn from
  the distance on (a per-instance distance test in the cull job; such jobs are never left whole); each member's own
  range ends there, judged on the same point (its box's center), so the switch is invisible and needs no hysteresis.
  Two things kept the image from matching at first:
  * **normals**: stock MultiMeshes transform normals by the instance's basis, nodes by its inverse transpose under a
    non-uniform scale (City Lab's prisms and spheres: 29.7 dB). HLOD proxies' instances use the inverse transpose (a
    flag in their instance data; stock MultiMeshes unchanged) → 0 differing pixels;
  * **shadows**: the far cascade drew more casters than with nodes (lavapipe GPU +6-20 %, all in the directional
    shadows) — the light culler's planes (casters whose shadow cannot reach the view) apply to nodes, not to GPU-culled
    instances. They are now passed to the proxies' cull jobs (`FellowShadowCacheOp.caster_planes`) → shadows cost what
    they cost without the node.
* **Shadows per cascade** (`RenderingServer::fellow_instance_set_hlod`): proxies and members carry a flag; a cascade
  starting beyond the distance draws the proxies, the others the members, whatever the camera's distance to them.
  Without it the shadow cache could keep none of them (stock's visibility dependencies follow the camera).

## 2. Verification (lavapipe)

* Off: City Lab and Foliage Lab (chunks, one MultiMesh per type) bit for bit the images of `c39f6d2`, after every
  engine change of the milestone (four rounds).
* Contribution culling: City Lab 2 px of 960 against off 62.94 / 78.35 / 71.70 dB; casters at 2 texels 0 pixels (City
  Lab has no caster smaller than 6 texels of its far cascade). Foliage Lab aerial (one MultiMesh per type): culled
  instances 36.77 dB (the meadow lighter), thinned 41.07 dB (its mean colour unchanged); chunks 67.6 dB.
* HLOD instanced: 0 differing pixels (three cameras, with and without per-instance LOD); merged: as above; without
  MultiMesh culling the node falls back to merging (as merged).

## 3. Performance

lavapipe, 480 x 270, per fixed camera, mean of two runs: see the tables in 0121 and 0122.

RTX 5070 Ti Laptop, 1080p, autotune off, 3 runs each (`m23_f267`, fellow200):

| City Lab flythrough | frame | render thread | GPU |
|---|---:|---:|---:|
| stock 4.7.2 | 2.705 ms | 2.066 ms | 1.789 ms |
| fellow200 | 2.053 ms | 1.592 ms | 1.711 ms |
| + contribution culling 4 px | 2.020 ms (−1.6 %) | 1.569 ms | 1.695 ms |
| + FellowHLOD3D 100 m | **1.813 ms (−11.7 %)** | **1.172 ms (−26.4 %)** | 1.746 ms |

Per camera the HLOD's render thread is 21-35 % shorter, the GPU 4-7 % less; captures with the node are identical to
those without it (aerial: 3 pixels off by 2 levels). Foliage Lab with contribution culling: the aerial view 10.5 %
faster (the far meadow thinned, 40.3 dB), the near views 3-4 % slower (the per-instance test where nothing is small).
With the compaction growing only the instances the cull kept (`m23_f268`, fellow201): aerial −10.5 %, forest −1.2 %,
meadow −2.4 % — the near views' penalty is gone.

Milestone 22's Burn Lab measurements ran in the same job (see that report).

## 4. Impostors

Added after the RTX job: `FellowHLOD3D.impostor_distance` hands far instances over to octahedral impostors baked at
run time (8 x 8 views of 128 px, albedo with coverage and normals, about 70 frames per mesh), per instance on the GPU
and per cascade in the shadows. Foliage Lab's trees (420 triangles, 6 000 instances in one MultiMesh) beyond 60 m,
against the same scene with the impostors baked but not shown: lavapipe 28.3 dB aerial, 49.7 forest, 32.2 meadow; RTX
1080p 27.4 / 49.0 / 32.0 dB — per pixel the far trees are pictures (a little softer, trunks fainter), the mean colour
within 1.4 levels.

**They do not pay for trees this light** (`m23_f268_foliage`, RTX, 3 runs): flythrough 4.805 → 4.671 ms (−2.8 %), but
aerial +5.1 % and forest +6.2 % (lavapipe forest +85 %): an alpha-tested quad over the bounding sphere blending four
views costs more fragments than 420 triangles cost vertices. Impostors stay as an option for heavy meshes; the default
`impostor_min_triangles` is now 2 000 (the bench passes 256), so turning them on leaves light meshes as meshes.

## 5. Left for later

* Far shading (fewer shadow taps in far cascades where the filter is below a texel): not done — FellowEngine's
  min/max early-outs (0005) already skip uniform regions, and the far cascades' PCSS radii are several texels.
* Impostors with depth (parallax-correct views, depth offset), per-instance colour; a cheaper impostor (a tighter
  quad per view, fewer views blended) before trying them again on light meshes; a heavy-mesh bench (scanned trees).
