# 0085 — GodotPhysics3D motion queries: yes/no distance tests without the faces that cannot touch

Status: **KEEP**, on by default (milestone 17) · Branch `fellow/geometry` · Settings `rendering/fellow/physics/distance_face_cull`,
`rendering/fellow/physics/verify` (debug) · Code: `modules/godot_physics_3d/godot_collision_solver_3d.*`
(`fellow_test_separation()`), `modules/godot_physics_3d/godot_space_3d.cpp` (`cast_motion()`, `test_body_motion()`),
`fellow_settings.*`.

## Problem

Physics Lab's `rays` case (1 500 `RayCast3D` and 150 `ShapeCast3D` nodes on rigs over a trimesh terrain and 600 boxes)
cost GodotPhysics3D 84 ms per tick in the container (Jolt 6.8 ms). A profile: ~80 % in GJK, called from
`GodotPhysicsDirectSpaceState3D::cast_motion()` against the terrain. `cast_motion()` (and the motion phase of
`test_body_motion()`, which `move_and_slide()` calls) asks `GodotCollisionSolver3D::solve_distance()` up to ten times per
object: does the shape collide over the whole motion, does it at rest, then eight steps of a binary search over the
motion's fraction. Against a concave shape `solve_distance()` gives GJK every face whose bounding box overlaps the
whole cast's box (the hint), until one is found touching — for a 25 m diagonal sweep of a 0.5 m sphere a hundred
terrain faces, nearly all far from the swept sphere, ten times over. Only whether the shapes are apart is used,
except by `cast_motion()`'s `r_info` (the closest points of the last motion found apart).

## Change

`GodotCollisionSolver3D::fellow_test_separation()` returns what `solve_distance()` returns, for concave shapes with the
same faces culled in the same order, but before a face is given to GJK it is tested against a box that holds shape A
over its motion: the parallelepiped its transform makes of its local AABB, swept along the motion, projected on its
three axes, the motion's direction, two directions across the motion, and the face's normal, in double precision. A
face separated on one of them by more than g = 1 mm + 1e-5 × the largest coordinate involved is skipped.

GJK reports two shapes as touching only if the closest point of their Minkowski difference it has found is nearer
than 1e-4 (`GJK_MIN_DISTANCE`), if its simplex encloses the origin, or if it fails to converge within 128 iterations.
The first two need the true distance to be within 1e-4 plus GJK's single-precision rounding (relative to the
coordinates; the 1e-5 covers it with a factor of ten), so a skipped face is one GJK would have found apart, and the
answer — the first face found touching ends the search in both — is the same. The third case is not geometric:
`physics/verify` gives every skipped face to GJK anyway and counts those it reports touching.

The callers that discard the closest points use it (`test_body_motion()`'s three calls, `cast_motion()`'s ten).
`cast_motion()` with `r_info` computes the closest points afterwards with `solve_distance()` as stock did — at the
fraction found (or at rest when every fraction touched), only when they can be kept — and leaves the motion shape's
motion as the search left it (stock's next object starts its first test with it).

## Verification

Physics Lab, headless, `--fixed-fps 60`, 600 ticks, the scene's physics state (every body's transform; the rays'
collision points, the shape casts' counts and safe fractions) hashed every 30 ticks: **the same 20 hashes** as the
build without the change in all five cases with GodotPhysics3D. `physics/verify` on: 50.2 million faces skipped in
`rays` and 58 000 in `chars`, every one found apart by GJK (0 mismatches).

## Results

Container (2 cores), physics per tick: `rays` 84.0 → 20.4 ms (× 4.1); `chars` 28.1 → 27.3 ms (its cost is in the
recovery phase's SAT tests, not the motion phase). Laptop (`m17_f252`, all of milestone 17 against stock, the rest of
which does not touch queries): `rays` 79.3 → 18.8 ms per tick (× 4.2), `chars` 22.2 → 21.1 ms. The faces kept are given
to GJK in order of overlap (`physics/distance_face_order`, from `fellow184`): container `chars` −5 %. With
`physics/verify` on the final build: 66.4 million faces skipped in `rays`, 131 741 in `chars`, none found touching.
