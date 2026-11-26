# 0384 — The hierarchy's walks of a pass in parallel

Status: **KEEP, on** (RTX laptop, 2026-11-27: the prepare step halved; it was off until measured there) · Branch `fellow/geometry` · Setting
`rendering/fellow/geometry/cluster_dag_parallel` · Code: `render_forward_clustered.{h,cpp}` (`_fellow_dag_prepare`,
`_fellow_dag_walk_job`), `fellow_cluster_dag_settings.h` (setting, counters `prepare_usec`, `parallel_walks`)

## Problem

The gap analysis after milestone 239 left the cluster hierarchy's CPU cost open: every pass walks the cut of each
surface instance with a hierarchy on the render thread, one after the other (0379: 2.3-2.7 ms a frame on this
container for 26-39 walks).

## Change

`_fellow_dag_prepare` (0381) now works in three steps: it lists the pass's elements with a hierarchy and their planes
in their space; walks their cuts — with the setting on, spread over the render thread and the CPU worker tasks
(decision 0016's loops: the render thread takes walks itself, the helpers take the rest if they start in time), each
walk writing its own surface's camera cache or its own vector (a surface appears once in a pass's list); and writes
their commands in the elements' order, so the command buffer is the same whichever thread walked what. Off, the walks
run one after the other as before. Two counters: `geometry_cluster_dag_prepare_usec` (the render thread's time in
`_fellow_dag_prepare`, walks included) and `geometry_cluster_dag_parallel_walks`.

## Checks (lavapipe, 2-core container, 0379's test scene, 24 frames a view)

* The images: the same with the walks in parallel and in series, in all three views; in series the same as milestone
  239's.
* The render thread's time in the prepare step, series → parallel: ground 2.06 → 1.96 ms, close 0.67 → 0.64 ms,
  high 0.30 → 0.45 ms. Nothing gained: with two cores the helpers rarely start before the render thread has taken
  most walks, Mesa's rasteriser threads share the cores, and the terrain's walk alone is most of a pass's time (a walk
  is not split).

## RTX laptop (2026-11-27; RTX 5070 Ti Laptop, 1920×1080, 120 frames a view, two runs each)

The same scene, the render thread's time in the prepare step, series → parallel: ground 1.49 / 1.45 → 0.71 / 0.59 ms
(−52 to −59 %), close 0.55 / 0.46 → 0.24 / 0.25 ms, high 0.24 / 0.20 → 0.16 / 0.15 ms; the viewport's render CPU time
1.66 / 1.59 → 0.91 / 0.73 ms on the ground view. The GPU's time the same (1.31 / 1.24 against 1.24 / 1.20 ms). The CPU
summed over the threads grows a little (the walks 1.45 → 1.80 ms: the helpers' caches cold).

## Decision

KEEP, **on** by default since the RTX measurement (it was off until then). Next if a single walk dominates: the walk's
levels split (each level's tree is walked independently of the others).
