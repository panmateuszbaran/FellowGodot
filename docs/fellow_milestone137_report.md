# FellowEngine — milestone 137 report (levels of proxy groups)

Status: **FINAL** (2026-11-17). Branch `fellow/geometry`. Lavapipe in the cloud (the laptop's GPUs busy).

## 0. Result

* **Levels of groups** (decision 0262): `bake_proxies()`'s `group_levels` (1: as before; at most 4) groups the groups'
  proxies again, `group_cells` × `group_cells`, level by level; each set gets one coarser proxy baked from the proxies
  below it (their meshes and textures; a terrain's outer edges kept), drawn from where all of them would be drawn, as
  their visibility parent, and hidden while any of them is (a cell of its loaded: `_update_group()` walks up the
  levels). Its objects keep at least what an error of `terrain_error` pixels needs at that distance (a count alone left
  a house five triangles); its texels take the surfaces facing them, edge texels a second ray or their neighbours, and
  it reads the textures below it a texel and a half inside their charts.
* World Lab's 16 × 16 cells and regions (1 km) seen from outside, with groups of 2 × 2 and three levels: **8 draws and
  31 205 triangles instead of 128 and 66 500**, the picture 0.29 % off the world all loaded (0.25 % with the groups
  alone). 8 × 8 cells with two levels: 2 draws instead of 8.
* **A fix for every proxy**: a simplified triangle's normal took its side from its welded corners' source normals — at
  a box's corner one of its other faces', either side for a wall. Proxies baked from many-quad content were mostly
  right; a level baked from proxies had many walls lit from behind (the buildings in white from outside: 23.6 % of
  their pixels off, 4.7 % now — as the groups'). The faces now take the source's winding. World Lab's streamed view
  with its proxies: 1.05 → 0.42 % of the picture off the world all loaded.
* The editor's dialog has the levels and reports them; Frontier Lab `--frontier_group_levels` for the RTX.

## 1. Checks

`bench/world_proxies_check.gd` (`--group_levels`; `--far=1`: lane F, the world from outside it, the coarser levels shown
and hidden against the world all loaded, and the buildings alone in white, compared on the world's pixels); the editor
check (two levels baked from the dialog over the split level's cells, owned and saved with it; in a game shown while the
cells are unloaded, hidden once they load); the image regression suite (exact).

## 2. Next

The proxies' textures load whole with the level: streamed with `streaming/textures` instead (milestone 138). Frontier
Lab with two levels on the RTX, and City Lab on the Intel GPU (job f487, queued), when the laptop's GPUs are free.
