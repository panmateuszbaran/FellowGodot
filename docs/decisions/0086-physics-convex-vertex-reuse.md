# 0086 — GodotPhysics3D SAT tests: a convex polygon's vertices transformed once per test

Status: **KEEP**, on by default (milestone 17) · Branch `fellow/geometry` · Setting `rendering/fellow/physics/convex_vertex_cache` ·
Code: `modules/godot_physics_3d/godot_collision_solver_3d_sat.cpp`, `godot_shape_3d.h`, `fellow_settings.*`.

## Problem

Physics Lab's `pile` case (2 000 boxes, spheres, capsules, cylinders and 20-point convex hulls) spent 22 % of its
time in `GodotConvexPolygonShape3D::project_range()`. The SAT tests (`_collision_box_convex_polygon()` and the other
pairs with a convex polygon) project both shapes on every candidate axis — the faces' normals, the edges' cross
products, about 200 axes for a box against a 20-point hull — and `project_range()` transforms every vertex again for
each (a matrix product per vertex per axis); the edge loops transform the edges' vertices again for every edge pair.

## Change

A SAT test with a convex polygon whose `project_range()` scans every vertex (at most three times its extreme vertices,
i.e. at most 78) transforms them once on the stack (`FellowConvexVertices`); the separator projects those on its axes
with `project_range()`'s own loop, and the edge and vertex loops use them where stock calls `p_transform.xform()` on
the same vertex. The same values in the same expressions. Polygons above that size (`project_range()` walks
`get_support()` instead) are tested as in stock.

A per-thread cache keyed by the mesh and the transform's bits inside `project_range()` was tried first: its lookup
(two setting reads, the thread-local access, comparing the transforms) cost more than the transforms saved
(`pile` 13 % of the time in the lookup).

## Verification

Physics Lab, five cases, 600 ticks, state hashed every 30 ticks: **the same hashes** as the build without the change.

## Results

Container: `project_range()` and the SAT functions that call it fell from about 35 % to 21 % of `pile`'s samples; the
container's timing noise (± 10 % between runs of one binary) hides the total; with the faces' vertices reused as well
(from `fellow184`) container `pile` 67 → 61.5 ms per tick, `chars` −3 %. On the laptop the SAT tests of `pile` run on
24 workers (pair setup 3.6 → 2.5 ms per tick with every milestone 17 change, `m17_f252`).
