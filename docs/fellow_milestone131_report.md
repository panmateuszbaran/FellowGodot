# FellowEngine — milestone 131 report (milestones 124-130 on the RTX; terrain proxies by error)

Status: **FINAL** (2026-11-17). Branch `fellow/geometry`. The laptop's RTX 5070 Ti Laptop GPU (jobs f464-f469) and the
cloud's lavapipe.

## 0. Result

* **Measured on the RTX** what milestones 124-130 built without the GPU, each in its decision:
  * the hair (0248-0250): 10 000 strands at 1 m cost about 0.6 ms of GPU at 1080p; with its level of detail 0.1 ms at
    8 m (0.6 ms without) and nothing measurable from 16 m; 24 heads stepped on 24 threads 10.3-10.4 times faster than
    one after another (0.9 ms a frame on the main thread);
  * the textured proxies (0251): the village's pictures as on lavapipe (46 % of the picture off with vertex colours,
    10.4 % with 0.5 m texels), its 16 cells baked in 0.51 s; Frontier Lab's 999 proxies, 272 textured, 20 MB;
  * the groups of far cells (0252): World Lab 347 → 107 draws, a tenth of a millisecond of GPU and of the renderer's
    CPU at 1080p;
  * the HLOD bakes (0253, 0254): the town's build 1.7 s → 7.4 ms from its files; the forest's impostors there at once
    instead of after 68 frames.
* **Found on the RTX**: Frontier Lab's far views with groups showed fans of long thin triangles over the far terrain and
  a town 2 km away under the ground. A terrain region's proxy keeps its outer edges and was simplified to 5 % of its
  triangles — fewer than its locked edges need, so nothing was left inside it.
* **Fixed** (decision 0255): `bake_proxies()`'s `terrain_error` (2 pixels at a 1 080-line, 75° view, where the proxy
  is first drawn): a terrain is simplified apart, as far as that error allows. World Lab with ±60 m hills: the groups'
  ground 9.4 → 0.5 m off on average (50 → 3.9 m at most). Frontier Lab regenerated: its town back, its far views
  with groups and without 0.28 % apart (3.37 % before); with groups 468 draws instead of 755, the GPU 3.74 ms instead
  of 3.92 ms, the renderer's CPU 0.91 ms instead of 1.06 ms.

## 1. Frontier Lab (job f469)

The 4 km world generated again by the editor build (version 8: the terrain by error, baked at the distances it streams
at), with groups of 4 × 4 far cells and without:

| | regions' proxies | groups of regions | cells' proxies | groups of cells | proxies' bake |
|---|---:|---:|---:|---:|---:|
| groups of 4 × 4 | 50 164 triangles | 16, 19 004 triangles | 999, 1.23 M triangles | 64, 331 k triangles, 6.1 MB of textures | 122 s |
| none | 50 164 triangles | — | 999, 1.23 M triangles | — | 71 s |

The far views (captured with the atmosphere, settled, streaming waited for):

| pixels > 16 levels off | far view | overlook |
|---|---:|---:|
| groups against none, before (job f468: version 7) | 3.37 % | 1.7 % |
| **groups against none, now** | **0.28 %** | **0.40 %** |
| no groups, now against before (the regions' proxies by error) | 1.35 % | 1.13 % |

The town is back and the fans are gone; without groups the far regions' flat facets are gone too (the regions'
proxies had been the same fans, 256 m across).

Streamed with the atmosphere (release template, 1080p, 3 runs of 90 s, interleaved; job f469, the version-7 worlds in
job f468):

| | frame | GPU | renderer's CPU | draws | triangles | objects | video memory, peak | load |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| groups of 4 × 4 | **4.18 ms** | **3.74 ms** | **0.91 ms** | **468** | 5.55 M | 744 | 777 MB | 1 084 ms |
| none | 4.37 ms | 3.92 ms | 1.06 ms | 755 | 5.78 M | 1 031 | 742 MB | 888 ms |
| groups, version 7 (f468) | 4.31 ms | 3.74 ms | 0.97 ms | 455 | 5.47 M | 732 | 765 MB | 1 108 ms |
| none, version 7 (f468) | 4.43 ms | 3.96 ms | 1.08 ms | 755 | 5.77 M | 1 031 | 741 MB | 949 ms |

The groups take 4 % off the frame and the GPU and 14 % off the renderer's CPU here, for 35 MB of video memory and 0.2 s
of loading (their meshes and textures load with the level). The terrain by error costs 80 k triangles a frame with
groups (the GPU the same). The first run of the groups (the session's first) had 140 frames over the stutter line in
f468 and none in f469; the others none to two.

## 2. Checks

`bench/world_proxies_check.gd` (`--relief`, `--terrain_error`: the terrain proxies' heights against the terrain's,
sampled every 2 m) on lavapipe at 0, 1, 2 and 4 pixels and on the RTX at 0 and 2 (the same meshes; the view's GPU time
0.63-0.65 ms either way); World Lab as before (±12 m); the editor check (1920 × 1080; the proxies dialog with its new
error); the class reference. The image regression suite has no proxies (unchanged).

## 3. Next

The readiness assessment's open rows of world streaming: groups of groups for the farthest cells (a 4 km world has 80
groups, each a draw), proxies' textures shared between identical cells.
