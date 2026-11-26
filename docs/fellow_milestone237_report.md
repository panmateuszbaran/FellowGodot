# FellowEngine — milestone 237 report (a hierarchy of clusters)

Status: **FINAL** (2026-11-26). Branch `fellow/geometry`. Decision 0379. No GPU work: checked on lavapipe.

## 0. Result

The third of the gaps the owner wanted closed (after the shadows' cache, 231, and the people, 232-236): geometric
detail at scale. Large static surfaces can now be drawn the way Nanite draws them — a hierarchy of small clusters
simplified from each other, and in every frame the cut through it whose every cluster is just fine enough on screen —
with ordinary indexed draws. On a 2-million-triangle terrain with twelve 640 000-triangle rocks the frame draws 69-79 %
fewer triangles than at full detail, without cracks, with the image as Godot's own LODs keep it; against FellowEngine's
earlier cluster LOD (0063) it draws fewer triangles near the camera and stays closer to full detail.

It is an option (`rendering/fellow/geometry/cluster_dag`, off): like any LOD it changes the image.

## 1. Changes

* `servers/rendering/renderer_rd/storage_rd/fellow_cluster_dag.{h,cpp}` (new): the build (meshoptimizer's meshlets,
  partitions and simplification with locked borders and the normals weighed as Godot weighs them) and the walk.
* The build in parallel (a level's groups) and kept in `user://fellow_dag_cache` (`geometry/cluster_dag_cache`, on):
  a surface seen again loads its hierarchy (8.1 s → 0.5 s on the test's meshes, the same images bit for bit).
* `servers/rendering/fellow_cluster_dag_settings.h` (new): the settings, their counters (`fellow_get_stats()`'s
  `geometry_cluster_dag_*`), the meshoptimizer functions (set by `modules/meshoptimizer`).
* The mesh clusters' build task builds the hierarchy too when the setting is on; the mesh storage gives the surface its
  index buffer of all the levels; the Forward+ renderer draws the cut in the depth pre-pass, colour and motion passes
  (one walk shared) and the shadow passes (the main camera's cut).
* `FellowFaceAnimator.follower_paths` bound as an array of NodePaths (it was untyped in the inspector and the docs).
* Docs: decision 0379, `FELLOWENGINE_OPTIONS.md`, `doc/classes/ProjectSettings.xml`, the changelog.

## 2. Measurements (lavapipe, 960×540, every pass of a frame)

| view | full detail | cluster LOD (0063) | cluster DAG |
|---|---:|---:|---:|
| ground | 22.55 M triangles | 7.96 M, 43.9 dB | **5.15 M, 47.5 dB** |
| close | 15.98 M | 9.03 M, 43.0 dB | **4.95 M, 53.4 dB** |
| high | 10.17 M | 1.96 M, 36.4 dB | **2.09 M, 36.9 dB** |

(dB: PSNR against full detail. Correction from milestone 239: these counts are of the clustered draws only — most of
the rocks were drawn as one instanced run, untouched and uncounted; milestone 239 cuts such runs too: 5.88 / 6.18 /
1.36 million triangles against stock's 31.6 / 29.0 / 9.78 million.) No cracks: the few pixels where the sky shows through in one image and not in the
other all lie on the horizon. The build: 8.1 s on the worker threads for 2.74 million triangles, 0.5 s from the cache;
1.9 times the surfaces' indices in GPU memory. The walks: 70-90 µs each on this CPU.

## 3. Open

* On the RTX (when the GPU is allowed): frame times; the draws (a few thousand ranges a frame over the passes) may
  want one indirect draw per walk.
* Towards Nanite: the walk on the GPU with culling against the depth pyramid; streaming the hierarchy's levels;
  the first level's clusters in parallel (2.4 s of the terrain's build); the cache's eviction.
