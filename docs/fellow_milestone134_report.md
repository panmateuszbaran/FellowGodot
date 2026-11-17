# FellowEngine — milestone 134 report (streamed cells and meshes cross over)

Status: **FINAL** (2026-11-17). Branch `fellow/geometry`. Lavapipe in the cloud; the laptop's RTX 5070 Ti Laptop GPU
(jobs f473, f474).

## 0. Result

* `FellowWorldStreamer3D.crossfade` (seconds, 0 by default; decision 0258): a streamed cell's content and its proxy
  cross over when the cell loads — the content entering with none of its pixels, its share growing while the proxy
  keeps the others, then the proxy hidden — and when it unloads, the content kept as a ghost fading out while the proxy
  fades in, then freed. Dithered as decisions 0256-0257's fades, through a new renderer call
  (`fellow_instance_set_dither_alpha()`); terrain regions take part (`FellowTerrain3D.fellow_set_dither_alpha()`: their
  patches and foliage).
* With it, a streamed mesh's swap between its stand-in and its full mesh crosses over too (decision 0259), the outgoing
  mesh drawn by an instance of its own meanwhile.
* No hole halfway through any of them (the check: a cell of boxes on a terrain, a streamed ball, both directions; the
  same on lavapipe and the RTX); settled, the pictures exactly the proxy's and the stand-in's again; the ghost freed.
* Frontier Lab streamed with its cells and regions crossing over in 0.5 s (RTX, 1080p, 3 runs interleaved, twice):
  4.14 and 4.26 ms against 4.22 and 4.21 ms switching — within the runs' spread; a dozen more draws.

## 1. Checks

`bench/dithered_fade_check.gd` sections 6 (a cell) and 7 (a streamed mesh); Frontier Lab `--frontier_crossfade=<s>`;
the image regression suite (exact); the editor check (a level streamed back, as before).

## 2. Next

The shadows still switch in every hand-over (a fading object casts its whole shadow); shadows dithered in the shadow
maps, by the main camera's fades, would fade them too.
