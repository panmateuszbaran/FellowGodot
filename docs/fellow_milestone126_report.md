# FellowEngine — milestone 126 report (hair on the worker threads)

Status: **FINAL** (2026-11-16). Branch `fellow/geometry`. Without the GPU (the cloud's CPU and lavapipe).

## 0. Result

* Every `FellowHair3D` of a frame is stepped on the worker threads at once (decision 0250): the scene read on the main
  thread, the steps in parallel, the uploads on the main thread. 24 heads on the cloud's two cores: 5.8 ms of steps in
  3.5 ms of the main thread (1.9×); every head stepped every frame, the lengths exact.
* `FellowHair3D.get_global_stats()` / `reset_global_stats()`.

## 1. Checks

`bench/hair_crowd_check.gd` (both builds); `bench/hair_check.gd` and `bench/hair_lod_check.gd` with every figure as
before; the regression scene `hair` as its golden from both builds; the class reference; the editor check.

## 2. Next

The hair on the RTX (its GPU cost, with and without the level of detail) and on a desktop's 24 threads.
