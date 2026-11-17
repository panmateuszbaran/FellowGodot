# 0258 — Streamed cells cross over with their proxies

Status: **KEEP** (`FellowWorldStreamer3D.crossfade`, 0 s by default: a switch, as before) · Branch `fellow/geometry` ·
Code: `servers/rendering/rendering_server.h` and the scene cull (`fellow_instance_set_dither_alpha()`),
`servers/rendering/renderer_rd/forward_clustered/render_forward_clustered.cpp` (an instance's requested share dithered
as decision 0256's fades), `modules/fellow_world/fellow_world_streamer_3d.{h,cpp}` (`crossfade`, the cells' fades and
their content kept while it fades out) · Checks: `benchmarks/project/bench/dithered_fade_check.gd` (the cell's
crossing), the image regression suite, the editor check.

## Problem

A streamed cell (decision 0147) switches between its proxy (0174) and its content in one frame when it loads and
when it unloads: the far buildings change their shape, colour and detail at once — the pop-in every open world shows
at its streaming distance. The hand-overs by distance now fade (decisions 0256, 0257), but a cell's is not by distance:
it happens when the content has loaded, a frame the streamer decides.

## Change

* **`fellow_instance_set_dither_alpha(instance, alpha, other_share)`** (RenderingServer, not bound): an instance keeps
  `alpha` of its pixels in the opaque passes, dithered as decision 0256's fades (the same noise, moving with TAA's
  frames), `other_share` the other pixels — whatever `geometry/dithered_fade` says (a node asked for it). Its shadows
  whole.
* **`FellowWorldStreamer3D.crossfade`** (seconds, 0): when a cell loads, its content enters keeping nothing and its
  proxy stays; over `crossfade` the content's share grows from 0 to 1 and the proxy keeps the other pixels, then the
  proxy is hidden. When a cell unloads, its content stays as a ghost (the cell unloaded, a reload starting at once)
  while the proxy's share grows and the ghost keeps the others, then the ghost is freed. A cell unloading while it
  fades in turns back from where it is; a cell loading while its ghost fades out drops the ghost, the new content taking
  the share it had. `is_settled()` waits for the fades.
* The geometry instances below the content take part (meshes, MultiMeshes, particles), and a terrain region, which
  draws its patches and foliage itself, through `FellowTerrain3D.fellow_set_dither_alpha()` (its patches, holes and
  foliage cells, the cells made later too). A MultiMesh whose instances fade by themselves (decision 0257) keeps its
  share times theirs.

## Verification

`dithered_fade_check.gd` (lavapipe, 960 × 540, without TAA): a streamer with one cell of 16 boxes on a terrain of 32 m
(a scene file) and its proxy (baked, vertex colours, half the triangles), `crossfade` 0.5 s at 60 frames a second, the
camera 40 m away: the load distance moved past the camera, then short of it.

* Loading: 30 frames from the content's entering to settled; halfway, no pixel left to the sky where the content and
  the proxy would both cover (0 %), the picture between the two (1.06 % of it more than 16 levels off the content
  alone, which is 2.27 % off the proxy).
* Unloading: 30 frames; halfway no hole either; settled, the picture the proxy's, exactly, and the ghost freed.
* The image regression suite exact (no streamer in it fades); the editor check (a level streamed back).

## Results

On the RTX (jobs f473, f474: 1920 × 1080, the release template, three runs each): the cell's sections as on lavapipe
(no hole, 30 frames each way, the ghost freed). Frontier Lab streamed with the atmosphere, its cells and regions
crossing over in 0.5 s (3 runs of 90 s interleaved; f469's world):

| Frontier Lab | frame | GPU | the renderer's CPU | draws | p99 |
|---|---:|---:|---:|---:|---:|
| switching (job f473) | 4.22 ms | 3.79 ms | 0.91 ms | 468 | 5.57 ms |
| crossing over, the cells' content (f473) | 4.14 ms | 3.70 ms | 0.92 ms | 479 | 5.49 ms |
| switching (job f474) | 4.21 ms | 3.78 ms | 0.91 ms | 469 | 5.61 ms |
| crossing over, the terrains and streamed meshes too (f474) | 4.26 ms | 3.82 ms | 0.93 ms | 479 | 5.62 ms |

Within the runs' spread either way: a dozen more draws (the cells crossing over drawn twice for half a second), no
stutter.

## Decision

Keep, 0 by default: with `crossfade` and TAA or FSR an open world's cells no longer pop in or out at the streaming
distance — their content dissolves into and out of their proxies — for no measurable cost. Off by default because it
changes what the streaming distance looks like and holds a cell's content half a second longer when it unloads.

## Rejected / not done

* **Blending** (the content made transparent while it enters): a cell's content in the transparent pass, sorted,
  one draw per node, see-through over its proxy.
* **The fade in the renderer by time** (a start and a length per instance, the renderer computing the share): the
  renderer's clock is not the game's (a paused or slowed game); the streamer sets the shares each frame of a fade —
  a call per node of the few cells crossing over.
* **The streamed meshes' swaps** (a stand-in and its full mesh, decision 0174): still a switch, near the camera.
