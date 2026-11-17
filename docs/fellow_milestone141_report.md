# FellowEngine — milestone 141 report (streamed meshes in three levels)

Status: **FINAL** (2026-11-17). Branch `fellow/geometry`. Lavapipe in the cloud (the laptop's GPUs busy).

## 0. Result

* **A middle level for streamed meshes** (decision 0266, opt-in): `bake_proxies()`'s `mesh_middle_ratio` saves a middle
  level of each streamed mesh beside its full mesh (one per full mesh's file); `FellowWorldStreamer3D.mesh_middle_distance`
  shows it between `mesh_distance` and that distance, loaded and swapped as the full mesh is (crossing over with
  `crossfade`), the stand-in beyond. Without the options: two levels, as before.
* World Lab with curved props (252 streamed meshes, 1.32 M triangles), seen over the roofs at 960 × 540: the full meshes
  within 20 m and the middle level within 70 m hold **25.1 MB of buffers instead of 33.3 MB** (full within 70 m; the
  flight's mean −18 %) for 91 more of 518 400 pixels more than 16 levels off the world all loaded — fewer than the
  stand-ins alone from 35 m add (165), with less memory than them too.
* On World Lab's flat buildings alone the stand-ins already lose nothing at 35 m: the middle level gains nothing there.

## 1. Checks

`bench/world_proxies_check.gd` (`--mesh_middle_ratio`, `--mesh_middle_distance`; `--rocks`: curved streamed props;
`--ground=1`: a view over the roofs, all loaded and streamed; `--taa=0`); the image regression suite (exact).

## 2. Next

On the RTX when the laptop's GPU is free: Frontier Lab's levels of groups and middle mesh levels, Bistro streamed,
Crowd Lab with `update_lod`; City Lab on the Intel GPU (job f487, queued).
