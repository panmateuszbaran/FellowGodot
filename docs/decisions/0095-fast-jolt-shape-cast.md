# 0095 — Fast mode: Jolt's motion queries by one shape cast per body

Status: **KEEP as an option**, off by default — fast physics mode, not stock's results (milestone 17) · Branch
`fellow/geometry` · Setting `rendering/fellow/physics/fast/jolt_shape_cast` (false) · Code:
`modules/jolt_physics/spaces/jolt_physics_direct_space_state_3d.cpp` (`_cast_motion_impl()`), `fellow_settings.*`.

## The fast mode

Every other physics change of milestone 17 is exact: the simulation is stock's, tick by tick. The fast mode, approved
by the user as an opt-in, is a set of separate settings under `rendering/fellow/physics/fast/` whose results are not
stock's bit for bit: deterministic (the same inputs give the same results, run to run and with any number of threads)
and physically equivalent within a stated tolerance, measured against stock with `benchmarks/tools/physics_deviation.py`
on Physics Lab states dumped by the bench (`--physics_dump`).

## Problem

Jolt's motion queries in the Godot integration (`ShapeCast3D`'s `cast_motion()`, `move_and_slide()`'s
`body_test_motion()` cast) find the first contact with each body by bisection: a collision test with the shape swept
over the whole motion, then up to 16 more with it swept over part of it (millimetre precision; 4 for a short motion).
On the fixed Physics Lab terrain (container): `rays` 17.2 ms per tick, `chars` 9.2 ms, most of it in these tests.

## Change

One Jolt shape cast per body (`TransformedShape::CastShape()`, GJK ray casting to the time of impact t) instead: the
unsafe fraction is t, the safe one t less a millimetre along the motion (the bisection's precision) and the separation
margin. The cast uses the shrunken shapes and their convex radius (`mUseShrunkenShapeAndConvexRadius`), so a box's
rounded edges are where the collision tests see them (with the plain shapes the casts hit boxes' sharp edges 1-3 cm
early); back faces of triangles are ignored as in the tests. The motion queries' shape filters expect the first shape
to be the integration's motion shape (a wrapper is shown to them). Internal edges are not removed by the enhanced
method (the cast has no equivalent). A body the shape starts in contact with (t = 0) is bisected as in stock: the cast
judges a triangle's back face by the motion, the collision tests by where the shape is.

## Verification

Container, fixed Physics Lab terrain, 300-600 ticks, against the exact build (`physics_deviation.py`):

* `rays`: ray hit points identical (the rays do not use it); shape casts' safe fractions within 0.0001 (2.5 mm on their
  25 m sweeps), mean 2·10⁻⁵; 0-3 of 150 collision counts differ (grazing contacts).
* `chars`: after 30 ticks positions within 1.9 mm RMS (largest 2.7 cm); the 250 characters bump into each other and
  diverge later (RMS 0.26 m after 300 ticks), as any change to a contact does. The characters stand on the terrain as
  in stock (height above it: the same median 1.5 cm, none fallen through but the 7 that walk off its edge in both).

The bisection was also checked against an exact reference: with plain shapes a sphere's centre at the cast's t is
0.50000 m from the contact point (its radius); the bisection's first touching fraction was 1-3 cm deeper against boxes
(their convex radius), which is why the cast uses the shrunken shapes.

## Results

Container, physics per tick: `rays` 17.2 → 3.1 ms (× 5.5), `chars` 9.2 → 6.1 ms (−34 %). Laptop: see milestone 17's
report (continuation).
