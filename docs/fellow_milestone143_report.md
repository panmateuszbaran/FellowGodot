# FellowEngine — milestone 143 report (milestones 137-142 on the laptop's GPUs)

Status: **FINAL** (2026-11-18). Branch `fellow/geometry`. The RTX 5070 Ti Laptop GPU and the laptop's Intel GPU again
(jobs f487-f495), m142 (062ff9bc64) built there as a release template and an editor.

## 0. Result

On the RTX (1080p, release template, autotune off, interleaved runs):

| | measured | result |
|---|---|---|
| Bistro exterior's DDS textures streamed (decision 0264) | 3 runs each; m136b streamed for comparison | loaded in **1.5-1.7 s instead of 2.9-3.4 s**, video memory **1.70 GB instead of 3.08 GB** (textures 1.35 instead of 2.76 GB), the frame +2 % (4.28 → 4.36 ms), its 99th percentile 5.3 → 7.4 ms (the streamer's swaps: next); m136b streamed: as whole (DDS not streamed then) |
| Crowd Lab with `animation/update_lod` (decision 0265) | 256 characters in the idle process, 3 runs each | the frame **4.38 → 2.99 ms (−32 %)**, its 99th percentile 6.4 → 4.4 ms, the GPU the same |
| Frontier Lab's groups in two levels (decision 0262) | generated again by m142's editor; 3 runs of 90 s each | the same 469 draws and 5.55 M triangles, 4.06 and 4.09 ms: the flight stays inside the world, where the first level is what the far cells need |
| Milestone 33's device loss (decisions 0133, 0267) | Bistro exterior, ray-traced GI, the clouds' shadows, the map written and read in one frame | **no device lost** — with m142 twice and with a build from before milestone 142's fixes: the cause is gone on this driver; the double buffer stays |

On the Intel GPU (720p):

* **City Lab** (the one scene milestone 136 found slower than stock there, +7 %): **13 % faster than stock** once the
  GPU's clock has risen — stock 9.72 ms, FellowEngine's defaults 8.48 ms, autotune off 8.95 ms (the last three of five
  runs each; the GPU's own time 8.27, 7.07 and 7.51 ms). Every variant's first two runs were 30-50 % slower: the Intel
  GPU's clock rising, which the bench's heat soak had not done — it heated the RTX (it ran the first build without its
  engine arguments, `--gpu-index 1`). Fixed: the soak runs the first build as it will be measured (its engine
  arguments, toggles, arguments and setting overrides). Milestone 136's +7 % came from the same ramp (f487 again: its
  frame times fell 40 % over its four minutes for every variant).
* **The terrain holes' run-to-run differences** (milestone 136, open): narrowed, not found. The regression scene
  captured three times in each of 26 variants (`th_variants.py`): the differences only with FellowEngine's renderer
  on (off: within 1 level), always in the patches over the holes (the material that discards), mostly ±1 in single
  channels, up to 5 levels; the same before milestone 142's synchronization fixes; and the same with each of 23 of
  FellowEngine's settings and groups off alone (the terrain's region uploads, MultiMesh culling, the shadow cache, the
  pipelines' waits, autotune, the post effects, shadows, geometry, lights, the CPU's work, the parallel replay, the
  depth keys, the stable scene bits, the submission and present threads, the visibility barriers, new textures'
  clears, the fluids, the transfers, the lazy variants, the recorded pipelines, a shading skip) — all threads off
  together halved them. Next: two frames of one run (a difference inside a run or only between runs), and the hole
  patches drawn with the terrain's own material.

## 1. Checks

`benchmarks/run_benchmarks.py` sessions `m143_f488_bistro_dds`, `m143_f488_crowd_update_lod`,
`m143_f489_frontier_levels`, `m143_f490_intel_city`, `m143_f493_ddgi_*`, `m143_f495_ddgi_*`; Frontier Lab generated and
captured from its six cameras with each level count (f489); the terrain holes' variants (f490, f492, f494:
`.tools/scripts/th_variants.py` on the laptop).

## 2. Next

Bistro streamed: what its rare long frames hold (the hitch sampler on the RTX). The terrain holes on Intel: within a
run or between runs.
