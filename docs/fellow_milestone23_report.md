# FellowEngine — milestone 23 report (far objects: contribution culling, automatic HLOD)

Status: **lavapipe measured; RTX job `m23_f267` queued** (2026-10-28). Branch `fellow/geometry`. Decision records
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
    the directional shadow cache keeps them.

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

lavapipe, 480 x 270, per fixed camera, mean of two runs: see the tables in 0121 and 0122. RTX: job `m23_f267` (City
Lab stock / off / culling / HLOD / both, Foliage Lab, 1080p captures; also milestone 22's Burn Lab measurements).

## 4. Left for later

* Impostors for far vegetation (octahedral billboards baked at run time), as a further level of the instanced proxies.
* Far shading (fewer shadow taps in far cascades where the filter is below a texel).
