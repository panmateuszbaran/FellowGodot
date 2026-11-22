# FellowEngine — milestone 169 report (characters up and down steps)

Status: **FINAL** (2026-11-22). Branch `fellow/geometry`. The cloud's CPU only, headless (the owner: engine work on
collisions and animation, no GPU for now).

## 0. Result

Stock Godot's `CharacterBody3D` has no step height: a capsule climbs only what its rounded bottom touches under the
floor angle (8.8 cm for a 30 cm radius at 45°), so every stair, kerb, doorstep and stone above that stops a character,
and walking down stairs it falls off every step. Unreal (`MaxStepHeight`), Unity (`stepOffset`) and Jolt's virtual
character (`WalkStairs()`) have it built in; Godot's users write it in script.

`CharacterBody3D.floor_step_height` (decision 0311; 0 by default, stock's behaviour bit for bit):

* **up**: a move stopped by a riser or a step's edge steps up onto the step (cast up, along, down), kept if it lands
  on floor no higher than the step height above the floor it stood on; at once, `get_last_step()` for the camera;
* **down**: the floor snap reaches a step down as well as `floor_snap_length`;
* **edges**: a step's edge under the body counts as floor (a walkable top just past it, a floor no more than a step
  below on the body's side): it stands on the edge or rolls over it; off a higher ledge it still falls.

The character course (new): capsules walking (1.6 m/s) and running (5 m/s) over stairs, rubble, ledges, a steep slope
and a low ceiling, both physics engines (step height 0.35 m, radius 0.3 m):

| | stock (both engines) | `floor_step_height` 0.35 m |
|---|---|---|
| stairs 10, 15, 20, 25, 30 cm (6 up, 6 down) | stopped at the first riser | 7.65-7.68 s walking, 2.42-2.43 s running (flat: 7.50, 2.40); never off the floor |
| the same at 35° | slides along the first riser | 9.28-9.30 s / 2.95-2.97 s |
| stairs 40 cm (above the step) | stopped | stopped |
| a ceiling too low for the first step | stopped | stopped |
| ledges 20, 30 cm walked off | 9-14 ticks in the air | none |
| ledge 60 cm (above the step) | 19-20 ticks in the air | the same: it falls |
| slope 50° | stopped | stopped |
| rubble (40 stones of 4-25 cm) | stopped at the first stone | crossed: 7.67 s / 2.43-2.45 s |
| stopped on the stairs near an edge | — | stands: 0-5.5 mm while stopping, never in the air |

The same with a 45 cm radius. `move_and_slide()` costs as much on stairs as on flat ground (24-64 µs a body on the
cloud's CPU); off, every row of the course and Physics Lab's state hashes are the same as a stock build's.

## 1. How it got there

* The step up alone (cast up, along the motion left, down) landed the body's rounded bottom on the step's edge, where
  the contact is steeper than floor: stock then slid it back off. The edge rule makes that contact floor.
* The edge rule alone rolls the body over edges: smooth, but a 20 cm step took 20 ticks at half the walking speed
  (8.45 s for the 20 cm stairs). The step up is tried first now, also when the stopping contact is a step's edge.
* Measuring the step by the body's rise let a 30 cm radius climb 40 cm with a 35 cm step (the rounded bottom reaching
  over the edge); it is measured from the floor contact to the landing's.
* Jolt rounds boxes' edges by their 4 cm margin: its edge contacts are up to 4 cm below the top, and with a 2 cm
  tolerance its bodies stuck on the third step. 6 cm.
* A failed step is remembered (place, direction, length): a body pushing against a wall no longer pays three casts a
  tick (1.8-2.3 times stock's time before, 1.0-1.5 after). With only place and direction, a short remainder's failed
  try blocked the next tick's full one and a body stuck on the 30 cm stairs.
* With a 45 cm radius the body rolled off the 60 cm ledge on the floor and down the snap; the edge rule asks for a
  floor within a step below on the body's side.

## 2. Checks

* The character course on both engines, radius 0.3 and 0.45 m, stock and with steps (`benchmarks/tools/character_course`).
* Off (0): the course's rows and Physics Lab's characters and pile (300 ticks, both engines; `benchmarks/results/
  20261122_143423_physics`) identical to a stock build's.
* `clang-format`, the class reference (`xmllint`, `make_rst.py --dry-run`).

## 3. Next (collisions and animation, without the GPU)

* Characters pushing rigid bodies (stock's `CharacterBody3D` moves them by penetration only) and being pushed.
* Feet placed on the steps the body now climbs (motion matching's ground adaptation, decision 0157) and the camera
  easing the step (`get_last_step()`) in the showcase's controller.
* Animation: retargeting between skeletons, additive layers.
