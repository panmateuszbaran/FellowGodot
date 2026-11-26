# 0379 — Large surfaces drawn as a hierarchy of clusters (Nanite's level of detail)

Status: **KEEP as an option** (off: it changes the image, as LOD does) · Branch `fellow/geometry` · Setting
`rendering/fellow/geometry/cluster_dag` · Code: `storage_rd/fellow_cluster_dag.*` (build, `select`),
`fellow_cluster_dag_settings.h` (setting, counters, meshoptimizer's functions), `modules/meshoptimizer/register_types.cpp`,
`storage_rd/fellow_mesh_clusters.*` (the build task), `storage_rd/mesh_storage.*` (the hierarchy's index buffer),
`render_forward_clustered.*` (its draw)

## Problem

The gap analysis after milestone 230 (`docs/fellow_gap_analysis_m230.md`) put "geometric detail at scale" among the
three large gaps to Unreal Engine: Nanite draws film-quality meshes at about a triangle a pixel whatever their size.
Godot picks one LOD per instance surface from its nearest point, so a large surface the camera stands on or near (a
terrain, a cliff, a big scan) is drawn whole at full detail, its far end included. FellowEngine's mesh clusters
(0007) cull such a surface's k-d clusters against the view, and its cluster LOD (0063, an option) draws each cluster at
one of Godot's whole-mesh LODs — with its neighbours' finer levels drawn over it where they meet (no cracks, some
triangles twice), and only down to the LODs whose triangles stay near the cluster.

## Change

Nanite's scheme, drawn with ordinary indexed draws (no mesh shaders, no software rasteriser):

* **The hierarchy** (`fellow_cluster_dag_build`, on the mesh clusters' worker task, for the same surfaces: static,
  opaque, at least `cluster_min_surface_triangles`): the surface's triangles in clusters of up to 128
  (meshoptimizer's meshlets); neighbouring clusters grouped by about 8 (`meshopt_partitionClusters`); each group's
  triangles simplified to half with the group's border locked (`meshopt_simplifyWithAttributes`, the normals weighed
  as Godot's own LOD generation weighs them) and split into clusters again; and so on until one cluster is left or no
  group simplifies further. A group's error is the largest of its clusters' plus the simplification's, its bounds a
  sphere around theirs: both only grow going up. Every cluster carries its own (the group it was made from) and its
  parent's (the group it was simplified in). A level's groups are simplified on the WorkerThreadPool in parallel, their
  results taken in their order (the same hierarchy whatever the threads).
* **The derived data cache** (`geometry/cluster_dag_cache`, on): each hierarchy built is written to
  `user://fellow_dag_cache/<SHA-256 of the surface's vertex and index data, format and the build's version>.fdag`, and
  a surface seen again loads it instead of building it (Unreal's DDC; a header with the structures' sizes, so another
  build's layout is a miss; no eviction yet).
* **The cut** (`FellowClusterDag::select`): a cluster is drawn when its own error is fine enough on screen and its
  parent's is not — Godot's own LOD test (`mesh_surface_get_lod`: an error times the instance's LOD scale over the
  main camera's distance times the projection's LOD multiplier, against `mesh_lod_threshold`), with the cluster's error
  in place of a LOD's edge length and its sphere's nearest distance in place of the instance's. A group's clusters all
  share their parent and the clusters made from it all share it as their own: every point of the surface is drawn by
  exactly one level, and two levels meet along a border both kept. Each level has a tree over its clusters, walked with
  the view's planes; a subtree is skipped when every parent in it is already fine enough, or no own error in it can be.
* **The draw**: simplification only removes vertices, so every level indexes the surface's own vertex buffer; the
  levels' indices are one more index buffer (about twice the surface's), each level in its tree's leaf order, so a
  walk's clusters come in index order and adjacent ones are drawn as one range. The camera passes of one render share
  one walk (0063's cache); the shadow passes walk with their own planes and the main camera's eye, so the shadow casters
  are the geometry the camera sees.

Off, nothing is built and nothing is drawn differently.

## Checks (lavapipe; no GPU, the owner's request)

A test project (`dag_test.gd`, not committed: it makes its meshes): a 2 097 152-triangle terrain (a 1024² grid over
400 m, six octaves of noise, ±18 m) and a 640 000-triangle rock drawn 12 times (scale 1-3.75, 4-147 m away), a sun
with four cascades; three views (low over the terrain, at the nearest rock, high over it), 960×540, Godot's LOD
threshold (1 pixel). Triangles drawn per frame, every pass (the depth pre-pass, the colour pass, the shadow cascades):

| view | full detail (k-d clusters culled) | `cluster_lod` (0063, Godot's LODs) | `cluster_dag` |
|---|---:|---:|---:|
| ground | 22 554 624 | 7 957 072 | **5 150 928** (−77 %) |
| close | 15 980 784 | 9 025 580 | **4 946 916** (−69 %) |
| high | 10 170 368 | 1 962 274 | **2 091 376** (−79 %) |

The image against full detail (PSNR; pixels more than 2 levels apart):

| view | `cluster_lod` | `cluster_dag` |
|---|---|---|
| ground | 43.9 dB, 0.47 % | **47.5 dB**, 1.19 % |
| close | 43.0 dB, 1.26 % | **53.4 dB**, 0.25 % |
| high | 36.4 dB, 24.9 % | **36.9 dB**, 23.3 % |

**Correction (milestone 239, decision 0381):** these counts are of the clustered draws only. The rocks share their
mesh and material, so Godot drew most of them as one instanced run, which neither the k-d clusters nor the hierarchy
touched: those rocks were at full detail in every column and missing from the counts. Since 0381 such runs are drawn
instance by instance, each with its own cut. Against everything stock submits (its primitive count, every pass):
31.6 / 29.0 / 9.78 million triangles a frame against the hierarchy's 5.88 / 6.18 / 1.36 million (−81 / −79 / −86 %),
44.2 / 44.2 / 36.5 dB against full detail.

The differences are where LOD puts them: the far terrain's shading and its self-shadowing (side by side the images
are alike). Cracks: none — the pixels where the sky shows through in one image and not the other (0-29 a view) all lie
within 3 pixels of the horizon (silhouettes moved, as by any LOD).

The normals' weight: positions alone drew 2.11 / 2.26 / 0.85 million triangles but changed the shading (35.4 / 37.5
/ 26.7 dB: the far terrain's highlights and shadows), a weight of 0.25 2.91 / 2.96 / 1.18 million (37.4 / 41.6 /
30.1 dB); Godot's weight (1) keeps the image as Godot's LODs do.

The cost: the build 8.1 s on the worker threads for the two meshes (2.74 million triangles; 2.4 s of it the terrain's
first clusters, which meshoptimizer makes in one go; the surface is drawn as before meanwhile), 44 537 clusters,
62.7 MB of indices (1.9 times the surfaces'); from the cache, 0.5 s (the same hierarchy: the same triangles and the
same images bit for bit, as from the parallel build). About 4 800-5 000 draws a frame over the passes (k-d clusters:
180-350); the walks 2.3-2.7 ms a frame for 26-36 walks (70-90 µs each, an editor build on this container's CPU).
Timings of the frame mean something only on a GPU.

## Decision

KEEP as an option, off (it changes the image as LOD does). On the RTX, when the owner allows the GPU: the frame with
it on large scans and terrains, and whether its draws (a few thousand ranges) want one indirect draw per walk.
Next towards Nanite: the walks and the ranges on the GPU (a compute pass writing indirect draws, with culling against
the depth pyramid), and streaming the hierarchy's levels.

## Rejected

* **Positions alone in the simplification's error**: the fewest triangles but the shading of what is simplified
  changes (above).
* **Groups simplified in parallel with WorkerThreadPool's group tasks waited for from the build's task**: the build is a
  low-priority task and its groups were too; with two threads the pool allows one low-priority task at a time, so the
  groups never ran (the surface stayed at full detail). The groups are taken from a shared counter by high-priority
  helpers and by the build's own thread, which finishes them alone if no helper starts.
* **Groups that do not simplify carried to the next level** (meshoptimizer's demo keeps them): their clusters stay
  roots of the hierarchy instead — simpler, and their neighbours above still meet them along their locked borders.
