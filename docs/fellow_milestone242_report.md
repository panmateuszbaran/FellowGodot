# FellowEngine — milestone 242 report (the hierarchy's walks in parallel)

Status: **FINAL** (2026-11-26). Branch `fellow/geometry`. Decision 0384. No GPU work: checked on lavapipe.

## 0. Result

The cluster hierarchy's walks of a pass can run on the worker threads as well as the render thread
(`geometry/cluster_dag_parallel`), with the same images. On this 2-core container it saves nothing (the render thread
takes most walks before helpers start, and one large surface's walk dominates), so it stays off until it is measured
on the RTX laptop's CPU.

## 1. Changes

* `render_forward_clustered.{h,cpp}`: `_fellow_dag_prepare` in three steps (list, walk — in parallel or not —, write
  the commands in order); `_fellow_dag_cut` takes an output vector; `_fellow_dag_walk_job`.
* `fellow_cluster_dag_settings.h`, `fellow_settings.cpp`: the setting (off) and the counters `prepare_usec`,
  `parallel_walks`.
* Docs: decision 0384, `FELLOWENGINE_OPTIONS.md`, `ProjectSettings.xml`, the changelog.

## 2. Checks

* 0379's test scene, three views: the same images in parallel and in series; in series the same as milestone 239's.
* Prepare step, series → parallel: 2.06 → 1.96, 0.67 → 0.64, 0.30 → 0.45 ms.

## 3. Open

* Measure on the RTX laptop (many cores); split a single large walk by its levels if it dominates.
