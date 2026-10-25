# 0097 — A concave shape's faces culled once per box within one motion query

Status: **KEEP**, on by default (milestone 17) · Branch `fellow/geometry` · Setting
`rendering/fellow/physics/distance_face_cull_cache` · Code: `modules/godot_physics_3d/godot_collision_solver_3d.*`
(`FellowFaceCullScope`, the cache in `fellow_test_separation()`), `godot_space_3d.cpp` (`test_body_motion()`,
`cast_motion()` open a scope).

## Problem

The motion phase of `test_body_motion()` and `cast_motion()` tests each body up to ten times (whole motion, at rest,
eight bisection steps), and every test culls the concave shape's tree with the same box (the whole motion's).

## Change

While a `FellowFaceCullScope` is open (one query; shapes cannot change during it), the faces one cull gives for one
(shape, box) — their vertices, normal and back-face flags, in the cull's order — are kept (4 entries) and given to the
face test again in that order for the same shape and box. The first cull collects all faces instead of stopping at
the first touching one; the faces are then processed, and stopped, exactly as the cull would.

## Verification

Physics Lab: the same per-tick hashes.

## Results

Container: `rays` 14.8 → 14.0 ms per tick (−5 %; its shape casts sweep 25 m over hundreds of faces); `chars` no
change (its boxes hold a few faces). Decision 0100 builds on the kept faces.
