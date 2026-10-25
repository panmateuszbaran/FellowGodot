# 0101 — A capsule's or sphere's SAT against a concave shape without the faces out of reach

Status: **REJECT** (removed; milestone 17) · Branch `fellow/geometry` (not committed).

## Problem

`test_body_motion()`'s recovery and rest phases (`solve_static()`, separating-axis tests with margins) were about a
quarter of GodotPhysics3D's `chars` after decisions 0096-0100.

## Change tried

`solve_concave()` for a capsule or a sphere ran 0096's face test (the shape at rest, g grown by both margins) before
each face's SAT and skipped the faces it proved out of reach. Exact: the capsule- and sphere-against-face SATs test the
directions between all pairs of closest features (face normal, edge × segment, the perpendiculars from the segment's
line and its end spheres to the vertices and edges), so a face farther than the margins and g is one they separate.

## Results

Container: the same hashes; `physics/verify` (the SAT on every skipped face): 1.3 million skipped faces over 300 ticks,
none colliding. But no gain (`chars` 21.7 → 22.2 ms per tick): the faces skipped were ones the SAT separates on its
first axes, as cheaply as the test. Removed.
