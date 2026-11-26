# FellowEngine — milestone 239 report (the hierarchy's cuts as indirect draws; instanced runs cut)

Status: **FINAL** (2026-11-26). Branch `fellow/geometry`. Decision 0381. No GPU work: checked on lavapipe.

## 0. Result

Two fixes to milestone 237's cluster hierarchy. Its cuts were drawn as thousands of small draw calls; now each
surface instance's cut is one indirect draw (the render thread 27-29 % shorter, the same image). And Godot's
instanced runs — many copies of the same mesh with the same material, exactly what the hierarchy is for — were drawn
whole; now each copy gets its own cut. On the test scene (a 2-million-triangle terrain, twelve 640 000-triangle rocks)
the hierarchy draws 81-86 % fewer triangles than stock Godot submits, at 36.5-44.2 dB against full detail, without
cracks. Milestone 237's record carries a correction: its counts missed the instanced rocks.

## 1. Changes

* `render_forward_clustered.{h,cpp}`: `_fellow_dag_cut` (the walk, shared), `_fellow_dag_prepare` (the commands
  written before each pass), the draw by indirect commands, instanced runs drawn instance by instance.
* `drivers/vulkan/rendering_device_driver_vulkan.cpp`: indexed indirect draws one by one without `multiDrawIndirect`.
* `geometry/cluster_dag_indirect` (on) and counters `geometry_cluster_dag_indirect_draws`, `_run_instances`.

## 2. Measurements (lavapipe, 8 frames averaged, every pass)

| view | stock's primitives | hierarchy | render thread: ranges → indirect |
|---|---:|---:|---|
| ground | 31.6 M | 5.88 M | 3.34 → 2.37 ms |
| close | 29.0 M | 6.18 M | 3.24 → 2.40 ms |
| high | 9.78 M | 1.36 M | 0.96 → 0.84 ms |

The full-detail scene records in about 0.5 ms (a hundred draws): the hierarchy's walks (about 2 ms a frame here) are
its CPU price for 81-86 % fewer triangles. The RTX decides which side wins.

## 3. Open

* The walks: one cut without planes shared by a frame's shadow passes, or the walk on the GPU.
* RTX measurements when the owner allows the GPU.
