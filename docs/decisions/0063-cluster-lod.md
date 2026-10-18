# 0063 — Large surfaces: each cluster at its own mesh LOD

Status: **measuring** (`fellow152`; option, off: changes the image as LOD does) · Branch `fellow/geometry` · Setting
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

`m13_f179` (pending).
