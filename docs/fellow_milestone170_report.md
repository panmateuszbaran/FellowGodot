# FellowEngine — milestone 170 report (characters push rigid bodies and weigh on them)

Status: **FINAL** (2026-11-22). Branch `fellow/geometry`. The cloud's CPU only, headless (no GPU, by the owner's
decision).

## 0. Result

A `CharacterBody3D` is kinematic: stock's `move_and_slide()` stops it at a crate or a ball as at a wall, and the rigid
body never feels it; standing on a seesaw, it weighs nothing. Two properties (decision 0312; 0 by default, stock's
behaviour bit for bit):

* `rigid_push_force` (N): each rigid body a slide was stopped by gets the impulse that brings it to the velocity the
  character asked for — along the ground, at its centre of mass's height, its mass and inertia at that point taken
  into account — at most the force for the tick. A step up (decision 0311) then never lands on a rigid body.
* `rigid_standing_mass` (kg): the rigid body stood on gets the character's weight at the floor's contact.

The push course (new, `benchmarks/tools/character_course/push_course.gd`, `run_course.py --course push`): 500 N,
80 kg, both physics engines.

| | stock | with pushes |
|---|---|---|
| crates of 5 and 20 kg, walked into | the character stops at them | pushed 7.5-7.8 m in 6 s at walking pace (1.47-1.50 m/s), none tipped |
| crate of 80 kg (470 N of friction) | stops | pushed at 0.47-0.50 m/s |
| crate of 300 kg (1 760 N) | stops | stops |
| a ball, run into | stepped over and left (GodotPhysics3D), shot to 8.9 m/s (Jolt) | kicked ahead at 4.9-5.0 m/s |
| a seesaw, walked over | level | tips −12 to −13°, then +12 to +14° |

## 1. How it got there

The first version pushed at the contact point along its normal and weighed down stock's "platform" body: on
GodotPhysics3D a 5 kg crate tumbled end over end and a ball flew back off at 28 m/s. Traced: the ball was beside the
floor, and stock records a rigid wall as the platform too; the weight went to the ball. The weight now goes to the
floor's own body, the push is along the ground and at the centre of mass's height.

## 2. Checks

* The push course on both engines, stock and with pushes.
* Off (0): the character course (decision 0311) and Physics Lab's characters and pile (300 ticks, both engines;
  `benchmarks/results/20261122_144833_physics`) identical to a stock build's.
* `clang-format`, the class reference (`xmllint`, `make_rst.py --dry-run`).

## 3. Next (collisions and animation, without the GPU)

* Being pushed (rigid bodies moving the character).
* Feet placed on the steps the character now climbs; the camera easing the step (`get_last_step()`).
* Animation: retargeting between skeletons, additive layers.
