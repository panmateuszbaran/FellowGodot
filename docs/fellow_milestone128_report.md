# FellowEngine — milestone 128 report (one proxy for a group of far cells)

Status: **FINAL** (2026-11-16). Branch `fellow/geometry`. Without the GPU (the cloud's CPU and lavapipe).

## 0. Result

* `FellowWorldStreamer3D.bake_proxies()` groups the cells 4 × 4 (`group_cells`) and bakes one proxy for each group from
  its cells' content (decision 0252): as many triangles as its cells' proxies over the square of how much farther it is
  seen, coarser texels, a terrain's outer edges kept. It is drawn beyond the distance at which none of its cells can be
  loaded, as its cells' proxies' visibility parent (Godot's HLOD dependency: one test hides 16 proxies), and the
  streamer hides it while any of its cells is loaded. The readiness assessment's "a proxy a draw" is closed.
* World Lab (12 × 12 cells and 144 terrain regions, its corner view): 347 → **107 draws**, 519 k → 451 k triangles; the
  picture 0.43 → 0.65 % off (mean 0.0019 → 0.0022); 1.5 MB more of textures, 7 s more of bake.
* The editor's Bake Streaming Proxies dialog has the group size and reports the groups.

## 1. Checks

`bench/world_proxies_check.gd` (`--groups`; each lane reports the groups' proxies shown and those shown with a cell of
theirs loaded: none); `bench/proxy_texture_check.gd` (its cells' own proxies: as in milestone 127); the editor check
(3 groups for the split level's 15 cells, owned and saved; `stream_back.gd`: shown in a game while their cells are
unloaded, hidden once loaded); the image regression suite; the class reference.

## 2. Next

The proxies and their groups on the RTX in Frontier Lab's 4 km world (the draws' CPU cost); a second level of groups.
