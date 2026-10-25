# 0103 — Vector3::is_finite() inline; a capsule's range from one basis product

Status: **KEEP**, always on (milestone 17) · Branch `fellow/geometry` · No setting (the same values) · Code:
`core/math/vector3.h`, `core/math/vector3.cpp`, `modules/godot_physics_3d/godot_shape_3d.cpp`
(`GodotCapsuleShape3D::project_range()`).

## Problem

A profile of GodotPhysics3D's `chars` showed 3 % in `Vector3::is_finite()`: `Vector3::normalize()` (inline) checks its
argument with it, a call into `vector3.cpp` for every normalisation; and 8-10 % in `GodotCapsuleShape3D::project_range()`
(every SAT axis), which transforms n and −n by the full transform.

## Change

`is_finite()` defined inline in the header (the same expression). `project_range()` computes B·n once: B(−n) = −(B·n)
exactly (each product and sum only changes sign), and origin + (−B·n) is the same sum as xform(−n).

## Verification

Physics Lab: the same per-tick hashes in all ten engine × case pairs (container).

## Results

Container, two runs each: GodotPhysics3D `chars` 20.7 → 18.0 ms per tick (−13 %); the other cases within noise.
