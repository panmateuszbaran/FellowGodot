# 0257 — Hand-overs per instance on the GPU dithered

Status: **KEEP** (opt-in per node: `FellowTerrainFoliage.impostor_fade`, `FellowHLOD3D.fade`; nothing changes
without them) · Branch `fellow/geometry` · Code: `servers/rendering/rendering_server.h` and the scene cull
(`fellow_instance_set_instance_range_margins()`), `servers/rendering/renderer_rd/shaders/effects/fellow_multimesh_cull.glsl`
(each compacted instance's fade), `servers/rendering/renderer_rd/forward_clustered/render_forward_clustered.{h,cpp}`
(the jobs' bands, the compacted stride, a specialization bit per draw),
`servers/rendering/renderer_rd/shaders/forward_clustered/scene_forward_clustered{,_inc}.glsl` (the instance's fade read
in the vertex stage and dithered as decision 0256's), `modules/fellow_world/fellow_terrain_3d.{h,cpp}`
(`impostor_fade`), `modules/fellow_world/fellow_hlod_3d.{h,cpp}` (the instanced proxies under `fade`) · Checks:
`benchmarks/project/bench/dithered_fade_check.gd` (the foliage's hand-over), the image regression suite.

## Problem

Decision 0256 dithered the fades Godot's visibility ranges make per node. FellowEngine's largest hand-overs are not per
node: a terrain's foliage hands each tree over to its impostor (decision 0172) and an HLOD's repeated meshes to their
instanced proxy (decision 0122) instance by instance, in the MultiMesh culling's compute pass (decision 0034) — a
switch per instance, the most visible pop of an open world (a forest's edge of impostors moving with the camera).

## Change

* **`fellow_instance_set_instance_range_margins(instance, begin_margin, end_margin)`** (RenderingServer, not bound):
  a MultiMesh's per-instance range (`fellow_instance_set_instance_range`) is drawn over a band of that width on each
  side of its start and end, and the culling's compaction writes each instance's fade after its data — smoothstep over
  the band, as Godot's fades by visibility range, by the distance to the instance's box's centre — with the side it is
  on. The job's compacted stride is 4 floats longer; nothing else changes for MultiMeshes without margins.
* **The scene shader**: a draw of such a job has one more specialization bit (`fellow_mm_instance_fade`): the vertex
  stage reads the instance's fade and passes it, with its side, in the high bits of the instance index (8 and 1 bits;
  23 remain); the fragment stage dithers it as decision 0256 does (the start's side keeping the other pixels). So a
  MultiMesh ending where another starts with the same margin — a foliage kind's meshes and impostors — hands each
  instance over without a hole, and a node fading out (decision 0256) where a MultiMesh's instance fades in, too.
* **`FellowTerrainFoliage.impostor_fade`** (0 m): the meshes and the impostors dithered over that on each side of
  `impostor_distance` (at most half the distance to the ground and to the range's end).
* **`FellowHLOD3D.fade`** (decision 0256) now also covers the instanced proxies: the members (nodes) fade out by
  themselves (`FADE_SELF`) over `margin`, the proxy's instances fade in over it, and the proxy's instances hand over to
  their impostors the same way.

Shadows switch per cascade as before (decisions 0122, 0172).

## Verification

`dithered_fade_check.gd` (lavapipe, 960 × 540, without TAA for these sections: the dither stands still):

* **The foliage**: a flat terrain (blue) with 1 820 balls as foliage (a sphere of 64 × 32 faces), their "impostor" the
  far ball (10 × 5 faces, darker; any mesh can stand in), from 40 m; the camera 1.6 m over the ground looking along
  it. With `impostor_fade` 6 m against the impostors alone: no pixel of an impostor's silhouette lost (0 %), every
  ball within the band partly mesh, partly impostor; against the switch, 0.16 % of the picture differs (the band's
  balls). The meshes' rims outside the impostors' silhouettes are dithered out too (0.04 % of the balls' pixels): the
  levels' shapes, not a gap.
* **FellowHLOD3D**, merged and instanced (16 boxes, `instance_threshold` 4: one MultiMesh proxy), `fade` with
  `geometry/dithered_fade`, the camera 40 m away and the node's distance at it: the picture the boxes', no pixel off —
  the members (nodes, fading by themselves) and the proxy's instances (fading in on the GPU) cover every pixel once.
* Decision 0256's sections as before; the image regression suite exact (no MultiMesh in it has margins).

## Results

On the RTX (job f472: 1920 × 1080, the release template, three runs): the foliage's and the instanced HLOD's sections
as on lavapipe (no hole; the foliage's band 0.16 % of the picture). Decision 0256's costs as before (4 000 balls
mid-fade dithered 3.90-4.05 ms of GPU).

Frontier Lab streamed with the atmosphere (1080p, 3 runs of 90 s interleaved; f469's world: its forests' trees handed
over to their impostors at 140 m):

| the trees at 140 m | frame | GPU | the renderer's CPU | p99 |
|---|---:|---:|---:|---:|
| switching (as before) | 4.25 ms | 3.81 ms | 0.90 ms | 5.68 ms |
| **dithered over 20 m on each side** (`impostor_fade`) | **4.21 ms** | **3.77 ms** | 0.91 ms | 5.58 ms |

The same within the runs' spread: the band's trees are drawn twice (mesh and impostor), a few hundred of the
forest's thousands, and the compaction writes a vec4 more for each of the kind's instances.

## Decision

Keep, opt-in per node: a forest's edge of impostors no longer moves with the camera as a line of switching trees, and
an HLOD's repeated meshes hand over to their proxy as its merged ones do (decision 0256), for no measurable cost in
Frontier Lab. Like decision 0256's dither, it wants TAA or FSR.

## Rejected / not done

* **A buffer of fades beside the instances** (a binding of its own): a new uniform in the instances' set for every
  draw; four floats more per compacted instance of the few MultiMeshes that fade cost nothing elsewhere.
* **A varying for the fade**: the vertex stage's outputs are at their guaranteed limit (16 locations); the instance
  index's high bits were free (23 bits left: 8 million instance records a frame).
* **The fades in the shadows**: as decision 0256 (switching per cascade).
* **Not yet**: a streaming cell's proxy and its content (they switch when the content enters).
