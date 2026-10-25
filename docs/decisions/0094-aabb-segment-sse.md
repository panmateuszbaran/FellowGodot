# 0094 — AABB::intersects_segment() without outputs: the three axes at once

Status: **KEEP**, always on where SSE2 is available and `real_t` is float (milestone 17) · Branch `fellow/geometry` · No
setting (the same answers by construction) · Code: `core/math/fellow_segment_sse.h` (`_fellow_intersects_segment_sse()`),
`core/math/aabb.cpp`.

## Problem

Raycasts (`RayCast3D`, `intersect_ray()`) walk two trees with segment-against-box tests: the broadphase's BVH and a
trimesh's (`GodotConcavePolygonShape3D::_cull_segment()`). After decision 0085 these tests were 20-30 % of Physics
Lab's `rays` case with GodotPhysics3D. `AABB::intersects_segment()` runs a scalar loop over the axes with a division
per axis and early returns; the trees only ask for the answer (no intersection point, no normal).

## Change

When neither output is requested the answer is computed with packed SSE: the same subtractions and the same IEEE
divisions (packed division rounds as scalar division), each quotient used only where the scalar code would compute
it (the others masked to the 0 or 1 the scalar code starts from), NaN treated as the scalar comparisons treat it (it
never wins one). The scalar code's early returns only stop it sooner: it rejects when an axis rejects or when the
running interval empties, which the final interval shows too.

## Verification

60 million random segments and boxes against the scalar code, including zero, negative and infinite sizes, NaN and
infinite coordinates and coincident coordinates: the same answers. Physics Lab: the same per-tick hashes in every case.

## Results

Container, GodotPhysics3D `rays`, with the face-test changes of the same commit: 19.0 → 14.4 ms per tick. Decision 0099
calls it inline from the BVH.
