# FellowEngine — milestone 137 report (levels of proxy groups)

Status: **IN PROGRESS** (2026-11-17). Branch `fellow/geometry`. Lavapipe in the cloud (the laptop's GPUs busy).

## 0. Result

In progress: `group_levels` (decision 0262) and the proxies' faces' winding; measurements being finished.

## 1. Checks

`bench/world_proxies_check.gd` (`--group_levels`; `--far=1`: lane F, the world from outside it, the coarser levels shown
and hidden against the world all loaded); the editor check (two levels baked from the dialog, owned and saved with the
level; in a game shown while the cells are unloaded, hidden once they load); Frontier Lab's `--frontier_group_levels`
(for the RTX); the image regression suite.

## 2. Next

Frontier Lab with two levels on the RTX (its 64 groups of cells and 16 of regions: 4 and 1 coarser proxies), when the
laptop's GPU is free; City Lab on the Intel GPU (job f487, queued).
