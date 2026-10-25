# 0099 — The BVH's segment test inline

Status: **KEEP**, always on where 0094's vectorised test is (milestone 17) · Branch `fellow/geometry` · No setting (the
same answers by construction) · Code: `core/math/bvh_abb.h` (`BVH_ABB::intersects_segment()`),
`core/math/fellow_segment_sse.h`.

## Problem

After 0094 a quarter of GodotPhysics3D's `rays` case was still in segment-against-box tests, three quarters of them
from the broadphase's BVH (`GodotBroadPhase3DBVH::cull_segment()`): each node's test builds an AABB from the node's
stored bounds and calls the out-of-line `AABB::intersects_segment()`.

## Change

`BVH_ABB::intersects_segment()` (3D, float `real_t`, SSE2) calls 0094's test inline with the AABB `to()` would build:
position = min, size = −neg_max − min (the same scalar operations), so the same box and the same answer.

## Verification

Physics Lab: the same per-tick hashes (every 10 ticks), both engines.

## Results

Container, three runs each: GodotPhysics3D `rays` 13.48 → 12.56 ms per tick (−7 %).
