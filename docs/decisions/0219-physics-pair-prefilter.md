# 0219 — GodotPhysics3D broadphase: the existing pairs dropped on the worker threads

Status: **KEEP**, on by default (exact: the same hashes as stock's) · Branch `fellow/geometry` · Setting
`rendering/fellow/physics/pair_prefilter` · Code: `core/math/bvh.h` (`BVH_Manager::_check_for_collisions()`,
`_fellow_cull_changed_item()`), `modules/godot_physics_3d/godot_broad_phase_3d_bvh.cpp`, `fellow_settings.*` · Checks:
Physics Lab (`benchmarks/physics_bench.py`, `--physics_scale`), its hashes.

## Problem

Milestone 110 measured Physics Lab's pile at 10 000 bodies: GodotPhysics3D's broadphase 63 ms of FellowEngine's
144 ms a tick on the laptop. Decision 0087 runs the broadphase's culls on worker threads; what is left is serial: for
each moved object, its leavers (pairs whose boxes no longer meet) and `_collide()` for every hit of its cull — which
looks the pair up in the object's pair list and returns when it exists. In a pile almost every hit is a pair that
exists: on the cloud (2 threads, 5 000 bodies) the culls took 10.8 ms, the leavers 1.9 ms and the hits' lookups
8.6 ms of 21.4 (timed by hand, not kept).

## Change

With the parallel culls, each worker drops from its object's hits the pairs that exist and will still exist when the
serial loop reaches the object; the loop then calls `_collide()` only for the hits that may be new pairs. A pair
(A, B) existing before the loop can only be removed before A's turn by B's leavers, when B moved and comes earlier in
the list: kept then when B's expanded box still meets A's box (the leavers' own test, on the same boxes: nothing moves
during the loop). A's own leavers keep every hit (the cull's test is theirs: the leaves hold the expanded boxes,
`BVH_EXPAND_LEAF_AABBS`, and the swizzled test is the same comparison). A pair created during the loop is not in the
starting lists, so its hit stays and `_collide()` finds it as before. The pairs are created and removed in stock's
order; with `p_full_check` (masks changed) nothing is dropped. A table of each moved object's place in the list
(`fellow_changed_index`), reset after the loop.

## Verification

Physics Lab on the cloud (the release template, 2 threads): `pile` at 5 000 bodies, 600 ticks — the same 20 hashes with
the prefilter, without it and before the change; `stack`, `chars` and `areas` (300 ticks) the same hashes with and
without it. The laptop (job f431, the release template at 60e88f04b1, `benchmarks/physics_bench.py`): every hash of
`pile` at 2 000, 5 000 and 10 000 bodies and of the five cases equal to stock Godot 4.7.2's, with the prefilter and
without it.

## Results

The laptop (24 threads), `pile`, 600 ticks, two alternating runs each (the median), ms a tick (the physics: sync,
process, step):

| bodies | stock (the frame) | without the prefilter | with it | | the broadphase without → with |
|---:|---:|---:|---:|---:|---|
| 2 000 | 28.1 | 14.27 | 12.90 | −9.6 % | 2.73 → 1.25 ms |
| 5 000 | 121.0 | 41.38 | 35.88 | −13.3 % | 8.30 → 3.78 ms |
| 10 000 | 421.5 | 149.64 | 128.70 | −14.0 % | 69.5 → 50.2 ms |

The five cases (job f431, one run each, the default scale): `pile` 12.25 → 10.18 ms, `areas` 2.43 → 2.23 ms, `stack`
1.08 → 1.06 ms; `chars` and `rays` (few pairs) the same within the run's spread. Cloud (2 threads), pile at 5 000
bodies: the broadphase 22.2 → 16.5 ms a tick.

At 10 000 bodies the broadphase is still 50 ms: the loop's pairs made and freed (8 000 and 7 200 a tick in the first
four seconds, as the tall pile falls through itself; 1 300 at 5 000), each made with an allocation and two hash-map
insertions in GodotPhysics3D (the stats `physics_godot_bp_*_usec`, `physics_godot_pairs_made`/`_freed`, milestone
112).
