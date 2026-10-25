# 0098 — A ray through a concave shape skips the tree's nodes beyond its closest hit

Status: **KEEP**, on by default (milestone 17) · Branch `fellow/geometry` · Setting `rendering/fellow/physics/ray_prune` ·
Code: `modules/godot_physics_3d/godot_shape_3d.*` (`GodotConcavePolygonShape3D::_cull_segment()`).

## Problem

`GodotConcavePolygonShape3D::intersect_segment()` visits every tree node the segment crosses, depth first, and keeps the
nearest face hit (a later face replaces the kept one only if strictly nearer).

## Change

Once a face is hit, a node whose box the segment enters farther along it than the nearest hit (by more than
1 mm + 1e-5 of the coordinates and length) is not visited. A face in that box is hit no nearer than the box's entry,
so it could not replace the kept face; the visiting order is unchanged (stock's depth-first order, only fewer nodes),
so ties between equally near faces are resolved as in stock.

## Verification

Physics Lab: the same per-tick hashes (hashed every 10 ticks).

## Results

Container `rays`, four runs each: 12.45 → 11.96 ms per tick (−4 %).
