# 0096 — 0085's face test with the capsule or sphere itself and the faces' edge axes

Status: **KEEP**, on by default (milestone 17) · Branch `fellow/geometry` · Setting
`rendering/fellow/physics/distance_face_shape_axes` · Code: `modules/godot_physics_3d/godot_collision_solver_3d.*`
(`_FellowSeparationInfo`), `godot_space_3d.cpp` (the callers pass the shape without its motion).

## Problem

On the fixed Physics Lab terrain (the characters stand on it) GodotPhysics3D's `chars` spends most of
`test_body_motion()` in the motion phase: per test 22 faces culled, 14 given to GJK, 3.4 touching — and 8 of the 10.6
found apart were more than 1 mm away, 7 more than 1 cm (container counts). Decision 0085's test bounds the shape by its
box: a capsule standing on the ground fills its box's bottom only at the centre, so the faces beside its foot overlap
the box on every axis tested.

## Change

* Where shape A is a capsule or a sphere (`move_and_slide()`'s usual shape; `ShapeCast3D`'s spheres) its extent along
  an axis a is computed from the shape itself: with w = Bᵀa (B: its transform's basis, any scale or shear), the support
  is radius·|w| + (half the capsule's segment)·|w.y|, plus the motion's projection on its side; centred on the
  transform's origin.
* More axes per face: across each edge in the face's plane (a face beside the shape), across each edge and the
  capsule's segment, across each edge and the motion; per query, across the capsule's segment and the motion. With the
  face's normal these are the directions between the closest features of a swept capsule and a triangle, but for
  vertex against vertex.

The test stays a proof: a face separated by more than g (1 mm + 1e-5 of the coordinates, as 0085) along any axis is
farther than GJK's tolerance, so GJK would find it apart; faces are skipped, never added, and the answer — whether any
face touches — does not depend on which faces are given to GJK first.

## Verification

Physics Lab, GodotPhysics3D: the same per-tick hashes. `physics/verify` (every skipped face given to GJK): 2.3 million
skipped faces in `chars` and 34 million in `rays` over 300 ticks, none touching.

## Results

Container, physics per tick: `chars` 26.0 → 22.3 ms (−14 %), `rays` 14.9 → 14.4 ms.
