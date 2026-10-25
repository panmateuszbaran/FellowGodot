# 0100 — Faces proven out of reach of a sweep are not tested again for the sweeps within it

Status: **KEEP**, on by default (milestone 17) · Branch `fellow/geometry` · Setting
`rendering/fellow/physics/distance_face_sweep_reuse` · Code: `modules/godot_physics_3d/godot_collision_solver_3d.cpp`
(the records in 0097's kept faces).

## Problem

In the motion phase the ten tests of a body sweep the same shape from the same transform over the whole motion, at
rest, and over shorter parts of the motion; each test runs 0085/0096's face test on every kept face again — in `rays`
hundreds of faces ten times per shape cast.

## Change

For each kept face (0097), the gap that proved it out of reach and the sweep it was proved for (shape, transform,
motion vector) are recorded. A later test of the same shape and transform with motion m skips the face if
gap − δ > g, where δ is the distance from m to the segment [0, recorded motion]: the shape swept by m lies within the
recorded sweep grown by δ (the distance to a segment is convex and zero at 0), so the face is at least gap − δ away —
farther than GJK's tolerance, as in 0085. At rest δ is 0.

## Verification

Physics Lab: the same per-tick hashes (every 10 ticks). `physics/verify` counts the faces skipped this way too: 34
million in `rays`, 2.3 million in `chars` (with 0096's), none touching.

## Results

Container: `rays` 13.2 → 11.7 ms per tick (−12 %), `chars` 22.1 → 21.3 ms (−4 %).
