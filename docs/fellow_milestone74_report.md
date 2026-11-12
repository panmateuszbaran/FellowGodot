# FellowEngine — milestone 74 report (destruction)

Status: **IN PROGRESS** (2026-11-12): the owner said "kontynuuj usprawnienia do poziomu AAA naszego silnika, pracuj
przynajmniej do 8 rano" (the GPU allowed). Branch `fellow/geometry`. Decision record: `docs/decisions/0182-destruction.md`.

## 0. Result

* **`FellowDestructible3D`**: crates, pillars, walls and panes of glass that break — when struck hard enough (a rigid
  body's own blow, or a rigid body striking a static one), damaged to nothing, or told to — into rigid pieces along a
  Voronoi diagram computed on a worker thread: more, smaller pieces near the blow; a thin object cut across.
* **Closed pieces**: each cell's share of the surface clipped out, its faces capped with the mesh's own cross-sections
  (holes and islands included, intersected with the cell's face by Clipper2): every piece closed, the pieces' volumes
  the source's exactly — a wall with a window, a ring and a 4 000-triangle sphere too.
* **Physics**: masses from the pieces' volumes, convex hulls (a piece far from convex gets its parts' hulls), the
  body's velocity, a burst from the blow and its impulse shared; prefractured patterns shared by objects alike (they
  break at once); a budget of pieces spawned a frame and of debris kept, the oldest shrinking away.
* **Checked**: thirteen lanes with GodotPhysics3D and Jolt; a regression scene; the editor ("Add Destructible Crate").

## 1. Development

* `modules/fellow_world/fellow_fracture.{h,cpp}`: the source's triangles, the sites, the cells (the surface clipped,
  the cross-sections, the caps), the pieces' volumes, centroids, components and convex parts.
* `modules/fellow_world/fellow_destructible_3d.{h,cpp}`: the node (blows, damage, the task, prefracture and its cache,
  the pieces' meshes and shapes, spawning within the budget, the debris), `register_types.cpp` (the class, the
  monitor "FellowEngine/Debris pieces"), `config.py`, `doc_classes/FellowDestructible3D.xml`, an icon.
* `modules/fellow_editor/fellow_editor_plugin.{h,cpp}`: "Add Destructible Crate (breaks when hit)".
* `benchmarks/project/bench/destruction_check.gd` (the lanes, pictures, `--many=N [--prefracture=1]`);
  `tools/fellow_regression`: the scene `destruction`; the editor check: the icon, the setup, the monitor.

## 2. Verification

* The lanes on the editor build (cloud) with GodotPhysics3D and Jolt: all as expected (decision 0182's table).
* Lavapipe: the lanes filmed; the regression scene `destruction` exact on a second run; the whole suite with the
  release template.
* The editor check: the icon, "Add Destructible Crate" without a warning and undone, the monitor.
* The laptop (f393, release template): to be run.

## 3. Measurements (cloud, editor build)

| | fracture (worker) | spawn (main thread) |
|---|---|---|
| a crate, 12 pieces | 1.5-2.5 ms | 0.5-0.9 ms |
| a pane of glass, 30 pieces | 9 ms | 1.2 ms |
| a wall with a window, 20 pieces | 10 ms | 1.6 ms |
| a ring, 14 pieces (convex parts) | 48 ms | 6 ms |
| a 4 000-triangle sphere, 32 pieces | 51 ms | 10 ms |

64 crates at once: prefractured, 768 pieces spread over 7 frames by the budget; on the workers, 2.1 ms of fracture each.

## 4. Next

Support graphs (a wall's pieces holding until what holds them goes), cracks that hold, damage textures; the laptop's
measurements.
