# 0102 — Fast mode: GodotPhysics3D's motion queries by the time of impact

Status: **KEEP as an option**, off by default — fast physics mode (see 0095), not stock's results (milestone 17) · Branch
`fellow/geometry` · Setting `rendering/fellow/physics/fast/godot_toi` (false) · Code:
`modules/godot_physics_3d/godot_collision_solver_3d.*` (`fellow_time_of_impact()`), `godot_space_3d.cpp`.

## Problem

GodotPhysics3D's `cast_motion()` and `test_body_motion()` find the first contact with a body by eight GJK tests on a
bisection of the motion (after a test of the whole motion and one at rest): its resolution is 1/256 of the motion (10 cm
on a 25 m shape cast). After the exact changes these eight tests are most of `chars`' motion phase.

## Change

The two first tests stay (whether the body is reached at all; whether the shape starts touching it: then it is stuck,
as in stock); the bisection is replaced by the time of impact computed by GJK ray casting (G. van den Bergen, 2004) in
double precision on the Minkowski difference of the shape and the body (for a concave body: each face 0085/0096's test
keeps, the earliest), with the shapes' own support functions as GJK uses them. The unsafe fraction is the time of
impact (1 if the cast finds none), the safe one a millimetre along the motion before it. The motion shape is left at the
unsafe fraction (stock leaves it at the bisection's last step, and the next body's first test uses it).

## Verification

Container, fixed terrain, against the exact build: shape casts' safe fractions within 0.006 of stock's, mean 0.002 (on
25 m: stock's bisection steps are 0.004); rays identical; characters within 6 mm RMS after 30 ticks (largest 8.5 cm),
diverging later as their collisions with each other change (0.7 m RMS after 300 ticks); the characters stand on the
terrain as in stock (the same median height above it, 1.5 cm).

## Results

Container, physics per tick: `chars` 17.7 → 11.7 ms (−34 %), `rays` 11.3 → 10.1 ms (−10 %). Laptop (`m17_f254`, frame
per tick): `chars` stock 30.0, exact fork 19.9, fast 12.7 ms; `rays` stock 78.6, exact fork 8.7, fast 8.0 ms. The
deviations from stock are the same as in the container (Linux, GCC) to the last digit printed.
