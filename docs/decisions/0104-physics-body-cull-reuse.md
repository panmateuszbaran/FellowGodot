# 0104 — test_body_motion()'s broadphase culls from one collected set

Status: **REJECT** (removed; milestone 17) · Branch `fellow/geometry` (not committed).

## Problem

A profile of GodotPhysics3D's `chars` after decisions 0096-0103 put 7.8 % in the broadphase BVH's culls.
`test_body_motion()` culls the broadphase about five times per call (each recovery iteration with the body's box moved
by the last push, the motion's box, the rest's box), nothing moving in between.

## Change tried

At the start of `test_body_motion()` one traversal with a box holding the body over its motion (and a quarter of its
size more) recorded each item passing the leaf test with its stored (expanded) leaf box, in traversal order; a cull
whose box lay within it took the recorded items passing the leaf test with its box, in that order — cull_aabb()'s hits
exactly (the traversal order depends only on the tree; an item in a node fully within the box, added untested by
cull_aabb(), passes the test). Other boxes were culled as in stock.

## Results

Container: the same hashes; `physics/verify` compared every filtered cull with a real one: 0.9 million culls, all
identical. But `chars` 20.3 → 20.0 ms per tick (−1.5 %, within noise), `rays` unchanged: the culls of a few-object
neighbourhood are cheap; the profile's BVH share is mostly the step's own broadphase update. Removed.
