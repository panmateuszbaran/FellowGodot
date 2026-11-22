# 0311 — Characters up and down steps: CharacterBody3D.floor_step_height

Status: **KEEP**, a property (0 by default: stock's behaviour bit for bit) · Branch `fellow/geometry` · Property
`CharacterBody3D.floor_step_height` (m), method `get_last_step()` · Code: `scene/3d/physics/character_body_3d.{h,cpp}`
(`_fellow_step_up()`, `_fellow_step_ahead()`, `_fellow_edge_is_floor()`, `_fellow_snap_length()`), the editor's
mantling character preset (0.35 m) · Measured with `benchmarks/tools/character_course/` (new).

Checks: the cloud's CPU, headless, GodotPhysics3D and Jolt; no GPU.

## Problem

Stock `CharacterBody3D.move_and_slide()` has no step height. A capsule climbs an edge only as far as its rounded bottom
touches it under the floor angle: 8.8 cm for a 30 cm radius at 45°. Every stair, kerb and doorstep above that stops the
body dead, and walking off one it falls for 10-14 ticks (a 20-30 cm drop with the default 10 cm floor snap). Games
hand-write the fix in script (Unreal's character movement has `MaxStepHeight`, Unity's controller `stepOffset`, Jolt's
virtual character `WalkStairs()`); Godot users ask for it in every forum.

The character course (new, `run_course.py`): capsules (1.8 m, radius 0.3 or 0.45) walking (1.6 m/s) and running
(5 m/s) for 10 s along lanes of six steps up and six down (10-40 cm, 30 cm treads), the same stairs at 35°, under a
ceiling that leaves no room for the first step, ledges of 20, 30 and 60 cm to walk off, a 50° slope, rubble (40 boxes
4-25 cm high), and stairs the body stops on near an edge and stands. Stock, both engines: stopped at the first riser of
every stair, at the first stone of the rubble, 9-14 ticks in the air off the 20 and 30 cm ledges.

## Change

`floor_step_height` above 0 (the body on the floor, not moving up):

* **Up.** A move stopped by a riser, or by a step's edge under the body's rounded bottom (a contact steeper than floor,
  facing the way it goes, touching no higher above the floor than a step), tries a step: the body cast up by the step
  height (less under a ceiling), along the motion left, and down. Kept if it lands on floor higher than it started and
  no higher than the step height above the floor it stood on; the body moves there at once, on the floor, and the
  slide goes on with what the move along did not make. `get_last_step()` gives the height, for a camera or model to
  ease it out. A failed try is not repeated from the same place the same way and no further (a body pushing against a
  wall: no three casts every tick).
* **Down.** The floor snap reaches the step height as well as `floor_snap_length` while the body was on the floor.
* **Edges.** A steep contact under the body is floor when it is the edge of a step: a ray just past it finds a walkable
  top within 6 cm of it (Jolt rounds box edges by their 4 cm margin, its contacts below the top) and one on the body's
  side a floor within a step below. The contact's own normal stays the floor's, so that sliding along it rolls the body
  over the edge; a body stopping on a step's edge stands there instead of sliding back off it; walking off a ledge
  higher than a step it falls as before.

## Measurements

The character course, the step height 0.35 m, radius 0.3 m (time to the lane's end at 10 m; ticks off the floor;
the largest rise in a tick; `move_and_slide()`'s mean time). Flat ground: 7.50 s walking, 2.40 s running.

| lane | GodotPhysics3D stock | with steps | Jolt stock | with steps |
|---|---|---|---|---|
| stairs 10 cm, walk / run | stopped at the riser | 7.67 s / 2.43 s, 0 off, ≤ 0.06 m | stopped | 7.68 s / 2.43 s, 0 off, ≤ 0.05 m |
| stairs 15 cm | stopped | 7.65 / 2.43 s, ≤ 0.08 m | stopped | 7.68 / 2.43 s, ≤ 0.09 m |
| stairs 20 cm | stopped | 7.67 / 2.43 s, ≤ 0.11 m | stopped | 7.67 / 2.43 s, ≤ 0.11 m |
| stairs 25 cm | stopped | 7.65 / 2.42 s, ≤ 0.16 m | stopped | 7.67 / 2.43 s, ≤ 0.14 m |
| stairs 30 cm | stopped | 7.67 / 2.43 s, ≤ 0.19 m | stopped | 7.67 / 2.43 s, ≤ 0.17 m |
| stairs 40 cm (above the step) | stopped | stopped | stopped | stopped |
| stairs 15 cm at 35° | stopped (slides along) | 9.28 / 2.95 s | stopped | 9.30 / 2.97 s |
| 15 cm under a 1.9 m ceiling | stopped | stopped | stopped | stopped |
| ledge 20 cm, walk / run off | 10 / 11 ticks in the air | 0 / 0 | 9 / 10 | 0 / 0 |
| ledge 30 cm | 13 / 14 | 0 / 0 | 13 / 13 | 0 / 0 |
| ledge 60 cm (above the step) | 20 / 20 | 20 / 20 (falls) | 19 / 20 | 19 / 20 |
| slope 50° | stopped | stopped | stopped | stopped |
| rubble | stopped at 0.5 m | 7.67 / 2.43 s | stopped at 0.5 m / 3.4 m, 40 ticks in the air | 7.67 / 2.45 s |

* No tick off the floor on any stair, up or down (stock: every step down a fall). The walk over six steps up and six
  down takes 2 % longer than over flat ground.
* The rises are taken in parts: the step lands the body's rounded bottom on the step's edge and it rolls up over it
  (the largest rise in a tick a half to two thirds of the riser).
* Stopping on the stairs near an edge (the third step's, 15 and 25 cm, walking and running): the body stands, no tick
  in the air, drifting 0-5.5 mm while it stops.
* Radius 0.45 m: the same (stairs 10-30 cm 7.68-7.78 s walking, 40 cm stopped, the 60 cm ledge fallen off, the 50°
  slope stopped). The 60 cm ledge took a second ray: with only the top's, a 0.45 m bottom rolled off the edge on the
  floor and down the snap, never falling.
* `move_and_slide()`'s time on stairs and rubble with steps 24-64 µs, as stock's on flat ground (14-73 µs; stock's
  stopped at a riser 30-70 µs). Pushing against the 40 cm riser or the slope 1.0-1.5 times stock's (the failed try
  remembered: before that, 1.8-2.3 times); wedged under the ceiling 1.5-2.5 times.
* Off (0, the default): the course's every row (positions, contacts, ticks) and Physics Lab's characters and pile
  (state hashes, 300 ticks, both engines) the same as a stock build's.

## Decision

KEEP as a property, 0 by default (stock's behaviour; Godot's own API unchanged). The editor's mantling character preset
sets 0.35 m.

## Rejected / not done

* **Rolling over edges only** (the edge rule without the step up): the body climbs, smoothly, but a 20 cm step takes 20
  ticks and its horizontal speed halves on it (the slide along the edge mostly upwards): 20 cm stairs at a walk took
  8.45 s instead of 7.67 s (GodotPhysics3D).
* **The step's height as the body's rise**: a body's rounded bottom reaching over the edge climbed 40 cm with a 35 cm
  step; the step is now measured from the floor contact to the landing's.
* **The edge rule's top within 2 cm**: Jolt's rounded box edges put its contacts up to 4 cm below the top, and its
  bodies stuck on the third step.
* **Smoothing the rise in the body** (moving it up over several ticks): the physics body would be inside the step
  meanwhile; the camera or model is the place (`get_last_step()`).
