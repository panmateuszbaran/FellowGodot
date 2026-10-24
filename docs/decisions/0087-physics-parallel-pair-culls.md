# 0087 — GodotPhysics3D broadphase: the pair update's culls on worker threads

Status: **KEEP**, on by default (milestone 17) · Branch `fellow/geometry` · Setting `rendering/fellow/physics/parallel_pair_culls` ·
Code: `core/math/bvh.h` (`BVH_Manager::_check_for_collisions()`, `fellow_parallel_culls`), `core/math/bvh_cull.inc`
(`cull_aabb_into()`), `modules/godot_physics_3d/godot_broad_phase_3d_bvh.cpp`, `fellow_settings.*`.

## Problem

Each step the 3D broadphase (`BVH_Manager`, `GodotBroadPhase3DBVH::update()`) walks the items that moved: for each, it
removes the pairs it no longer overlaps (`_find_leavers()`), culls the tree with its expanded box, and creates the pairs
it newly overlaps (`_collide()`, which calls the space's pair callback: a `GodotBodyPair3D` is created and added to both
bodies' constraint maps). All on the stepping thread. Physics Lab `pile` (2 000 moving bodies) on the laptop: 7.8-8.0 ms
of its 27.6-29.2 ms step (`m17_f248`, `m17_f249`, toggles off); the culls are most of it.

## Change

The culls only read the tree (the loop changes pairs, never nodes or boxes, and the cull test of the physics broadphase
looks at nothing else), so they are computed first, one per moved item, on worker threads into per-item lists
(`cull_aabb_into()`: `cull_aabb()`'s traversal, the same order, into the caller's buffer instead of the tree's
`_cull_hits`). The serial loop then removes and creates pairs exactly as stock, in the same order, reading those lists.
From 64 moved items; set by the 3D broadphase only.

## Verification

Physics Lab, both engines, five cases, 600 ticks, state hashed every 30 ticks: the same hashes (container and laptop,
`m17_f248`-`m17_f250`), also with 1, 2, 4, 8, 16 and 24 workers (`m17_f250`).

## Results

Laptop (`m17_f248`): `pile` broadphase 8.0 → 2.6 ms per tick, `areas` (1 000 bouncing spheres) 1.8 → 0.67 ms.
