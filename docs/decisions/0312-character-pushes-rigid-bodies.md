# 0312 — Characters push rigid bodies and weigh on them

Status: **KEEP**, two properties (0 by default: stock's behaviour bit for bit) · Branch `fellow/geometry` · Properties
`CharacterBody3D.rigid_push_force` (N), `CharacterBody3D.rigid_standing_mass` (kg) · Code:
`scene/3d/physics/character_body_3d.{h,cpp}` (`_fellow_push_rigid_bodies()`, `_fellow_is_rigid()`) · Measured with
`benchmarks/tools/character_course/push_course.gd` (new; `run_course.py --course push`).

Checks: the cloud's CPU, headless, GodotPhysics3D and Jolt; no GPU.

## Problem

A `CharacterBody3D` is kinematic: `move_and_slide()` stops it at a crate or a ball as at a wall, and the rigid body
never feels it. On GodotPhysics3D a character walking into a 5 kg crate or running into a ball stands there for ever;
on Jolt the ball is shot away only because the body stepped half over it. Standing on a seesaw or a plank, the
character weighs nothing: the seesaw stays level under it. Godot's manual has a script loop applying impulses to the
slide collisions; Unreal's character movement pushes physics objects (`PushForceFactor`) and presses down on what it
stands on (`StandingDownwardForceScale`).

The push course (new): a capsule (1.8 m, radius 0.3, step height 0.35 m) walking at 1.6 m/s into 0.8 m crates of 5,
20, 80 and 300 kg (friction 0.6), running at 5 m/s into a 0.45 kg ball, walking along a 4 m, 30 kg seesaw on a hinge
(±15°) from one end to the other, and along a loose 15 kg plank laid across two 25 kg crates; 6 s.

## Change

* `rigid_push_force` above 0: after the move, each rigid body a slide was stopped by (a contact steeper than floor, the
  velocity asked for going into it) gets the impulse that brings its contact point to that velocity along the contact's
  normal: along the ground (a contact on the body's rounded bottom would push a light body down into the floor), at the
  body's centre of mass's height (a crate slides rather than tips over: where a capsule touches it says
  little of where a person would push), with its mass and inertia at that point (`1 / (1/m + (r × n) · I⁻¹ (r × n))`),
  at most the force for the tick; the strongest contact of each body. A step up never lands on a rigid body and its
  edges are not floor (decision 0311): it is pushed instead.
* `rigid_standing_mass` above 0: on the floor of a rigid body, that body gets the impulse of this mass under the body's
  gravity at the floor's contact each move. The floor's own body: the "platform" stock records may be a wall's (pushing
  a ball beside the floor, the first version weighed the ball down with 80 kg and it flew off at 28 m/s).

## Measurements

The push course, push force 500 N, standing mass 80 kg (the object's travel and largest speed; the body's speed while
pushing; the seesaw's tilt either way; `move_and_slide()`'s mean time):

| lane | GodotPhysics3D stock | with pushes | Jolt stock | with pushes |
|---|---|---|---|---|
| crate 5 kg | the body stopped at it | 7.53 m, the body 1.48 m/s | stopped | 7.80 m, 1.50 m/s |
| crate 20 kg | stopped | 7.59 m, 1.47 m/s | stopped | 7.70 m, 1.50 m/s |
| crate 80 kg (friction 470 N) | stopped | 2.40 m, 0.50 m/s | stopped | 2.25 m, 0.47 m/s |
| crate 300 kg (friction 1 760 N) | stopped | stopped | stopped | stopped |
| ball 0.45 kg, run into | stepped over it, the ball still | kicked ahead at 4.89 m/s | shot to 8.9 m/s as the body stepped over it | kicked ahead at 4.99 m/s |
| seesaw, walked over | level (±0.0°) | tips −11.7° then +11.7° | ±0.2° | −13.0°, +13.6° |
| plank on two crates | still | 4.7° at most, stays on them | still | 0.1° |

* No crate tipped (tilt 0.2° at most) or left the ground (upward speed 0); the body pushes at the crate's pace.
* 20-50 µs more per `move_and_slide()` while pushing (59-122 µs; stock's body stopped at the crate 38-83).
* Off (0, the default): the character course (decision 0311) and Physics Lab's characters and pile (state hashes, 300
  ticks, both engines) the same as a stock build's.

## Decision

KEEP as properties, 0 by default.

## Rejected / not done

* **The first version** (the push at the contact point along its normal, the weight on stock's platform body): on
  GodotPhysics3D the 5 kg crate tumbled end over end (170°, 3.5 m/s upwards), the 20 kg one tipped 6° and dragged
  the body to 0.15 m/s, and the ball flew back off at 28 m/s. The three changes above (along the ground, at the centre
  of mass's height, the floor's own body) together; the ball's was the weight alone (traced), the crates' not taken
  apart.
* **Being pushed** (a rolling boulder or a swinging door moving the character): the kinematic body does not see the
  rigid bodies' contacts; not done.
