# 0063 — Large surfaces: each cluster at its own mesh LOD

Status: **KEEP as an option** (off; `fellow152`: Sponza GPU −3.1 %, render thread +0.21 ms; the image as LOD changes it) · Branch `fellow/geometry` · Setting
`rendering/fellow/geometry/cluster_lod` (runtime) · Code: `storage_rd/fellow_mesh_clusters.*` (`cull_lod`,
`lod_level_for_distance`, the build's `union_bounds`, `max_level`, neighbours), `render_forward_clustered.*` (the
clusters' draw, `fellow_cluster_lod`, `fellow_lod_eye`), `fellow_settings.*` (stat `geometry_cluster_lod_coarser_ranges`).

## Problem

Godot chooses a mesh LOD per instance surface, from the camera's distance to the instance's bounds. A large surface
that the camera stands inside or near (Sponza's ivy, 4.9 million triangles over a wall; a terrain) is always drawn at
full detail, its far end included: 0.15 ms per million triangles in Sponza's depth pre-pass alone (0058). A project
that splits such a mesh into chunks gets per-chunk LOD from stock; one that does not, does not.

## Change

The mesh clusters (0007) already hold, for every node of a large static surface's k-d tree, its range and bounds in
every LOD's index buffer (each reordered by leaf). With the setting, the clusters' draw walks the tree and gives each
leaf in view the level stock would give it if it were an instance of its own: Godot's test (the LOD's edge length times
the instance's LOD scale over the distance, against the screen's LOD threshold), with the main camera's distance to the
leaf (a lower bound in world units: the smallest axis scale of the instance), never finer than the instance's own level.
Each level's ranges are drawn from that level's index buffer.

Two things keep the result whole:

* **Cracks.** Each level's mesh is closed, but a leaf at one level next to a leaf at another leaves gaps where their
  triangles do not meet. Each leaf in view is therefore drawn at its own level and at every finer level of its
  neighbours (leaves whose bounds intersect); every point of the surface is then covered by the finest level of the
  leaves around it. Overlaps are drawn twice (the depth pre-pass keeps the nearest; the colour pass's equal test draws it
  once more where they coincide).
* **The coarsest LODs.** Godot generates LODs down to a few dozen triangles for the whole surface; a leaf holding one of
  those triangles would neighbour every other leaf. A leaf's usable levels end where its triangles leave its base bounds
  grown by a quarter of their size.

Not for blended surfaces (their order-preserving clusters), shaders that move vertices, orthogonal views and orthogonal
shadow cameras (stock's LOD distance is constant there), MultiMesh (0035 has its own per-instance LOD).

## Verification

lavapipe, a 524 288-triangle terrain with 8 LODs seen from one edge (`/tmp` test project): 86 % fewer triangles drawn;
against off 49.3 dB, the differences only at the horizon's silhouette (0.023 % of pixels > 2 levels). Without the
neighbours' levels: cracks across the surface (42.1 dB, the background visible through the gaps).

## Measured

In one process, autotune off, off against on in blocks of 30 frames (`m13_f179_clod_ab`, `fellow152`):

| scene | GPU off | on | Δ | render thread Δ |
|---|---:|---:|---:|---:|
| Sponza | 6.311 ms | 6.113 | **−0.198 ms (−3.1 %)**, p < 10⁻³⁰ | +0.207 ms (+24 %) |
| Bistro exterior | 4.270 | 4.273 | +0.1 % (p 0.42) | +0.1 % |
| Bistro interior | 3.273 | 3.275 | +0.1 % (p 0.49) | +0.2 % |
| Foliage Lab | 3.052 | 3.054 | +0.1 % (p 0.56) | −0.1 % |

Only Sponza has surfaces large enough to reach coarser levels on its camera path (1.29 million coarser ranges drawn
over the run against 27 500 in Bistro exterior); Bistro interior has no clustered surface and Foliage Lab's are
MultiMeshes. The render thread pays for the walk of the trees in every pass (0.86 → 1.07 ms in Sponza, whose frame is
limited by the GPU at 6.1 ms).

Image (`m13_f179_clod_seq`, warm 60-frame sequences, on against off): Sponza 63.6 dB mean, 59.3 dB at its worst frame,
max |d| 72, 0.079 % of pixels more than 2 levels apart; Bistro exterior bit for bit (no coarser level on its path),
Bistro interior at its floor (67.1 dB). Stills (`m13_f179_clod_still`): Sponza 52.9-56.8 dB, 0.26-0.87 % of pixels
more than 2 levels apart, at the silhouettes of distant arches and columns and in the ivy (side by side the images are
indistinguishable; the differences are where LOD puts them).

## Decision

**KEEP as an option, off.** It changes the image as LOD does, so it cannot be on by default; where a project has a
large surface without chunks and no CPU to spare it saves what LOD saves there (Sponza 3 %, the lavapipe terrain 86 %
of its triangles). The walk's CPU cost could be cut (once per frame for all the camera passes instead of per pass);
not done while the bench has no scene where it matters.
