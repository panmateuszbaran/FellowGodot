# 0276 — Free climbing: FellowTraversal3D on any wall's face

Status: **KEEP** (opt-in per node) · Branch `fellow/geometry` · Code: `modules/fellow_animation/fellow_traversal_3d.{h,cpp}`
(`free_climb`, `climb_mask`, `climb_speed`, `climb_max_overhang`, `try_climb()`, `is_climbing()`, `KIND_CLIMB`,
signals `climb_started`, `climb_finished`; `_climb_top()`; the mantle's path over a leaning face and a sloping top) ·
Checks: `bench/climb_check.gd` (eight new lanes) on GodotPhysics3D and Jolt, `bench/traversal_check.gd`, the regression
scenes `traversal` and `climb`.

## Problem

`FellowTraversal3D` (decisions 0180, 0183, 0189 and on) mantles, vaults, hangs from ledges and climbs ladders. The
readiness assessment listed what it cannot do: no free climbing on any surface — a cliff, a building's wall, a tower,
climbed by hand and foot wherever the player goes, as open-world games do. A wall too high to mantle was a dead end
(`KIND_TOO_HIGH`).

## Change

With `free_climb` on, a body pushing at a face of a wall on a layer of `climb_mask` with nothing to mantle onto (the
automatic mantle runs first), or caught against it in the air, climbs it (`try_climb()` by hand):

* **On the face**: a point on it at the chest's height (1.2 m over the feet) and its normal; the upright capsule 8 cm
  off the face (its nearer end, on a face that leans), turned to face it. Each frame the controller's wish moves the
  point over the face at `climb_speed` (1 m/s): the intent towards the face or `set_climb_input()` up, away down,
  left or right along it. A ray from in front of the new point back at the face finds it again — a curved or leaning
  face followed, its normal the new one; a face in the way along the move is gone onto (an inside corner); past an edge
  a ray from beyond it finds the face round it (an outside corner); the capsule must be free where it would go.
* **The wish held turns with the face**: an intent held since the face's normal was some way turns with it since then
  (round a tower, round a corner, the stick held left keeps going left along the wall).
* **What it will not climb**: a face less steep than `max_ledge_slope` (ground to walk on: it steps off the wall onto
  it), one leaning over the body more than `climb_max_overhang` (20°), one outside `climb_mask`.
* **The top**: where the hands' height (`hang_reach` over the feet) finds no climbable face going up, a ray from over
  the hands just behind the face finds the top; the edge where the face's plane meets the top's; room on it, over the
  edge and in front of it as the ledge check asks. Room: a mantle from the climb; none on the top: hanging from the
  edge (milestone 75's hang). The ledge check itself took a slab's face (leaning back from the body) for a sloping top.
* **The floor**: climbing down onto ground, standing; `release()` lets go.
* **The pose**: the hands over the head and the feet on the face, a step (0.3 m) apart, changing over as it climbs,
  the torso towards a face that leans back; the FellowTraversalPose3D's two-bone IK as for hangs and ladders.
* **The mantle's path, for every mantle**: over a top that slopes, the capsule's lower end kept on its plane (it went
  into a top sloping away from the edge); from a face leaning back, the capsule kept off the face as it rises (a slab's
  face is nearer lower down). Nothing changes over a flat top from a vertical wall.
* The traversal's body-shape query keeps its size between calls (setting a shape's data again rebuilds it): a climbing
  step 33 → 18 µs.

## Result

`climb_check.gd`'s new lanes (the editor build, GodotPhysics3D and Jolt the same):

| Lane | Result |
|---|---|
| a 4.5 m wall with room on top | climbed (0.72-3.43 s), mantled over (high), on the top at 4.50 m |
| a 6 m wall: up, left 2 s, down | up to 1.53 m, 2.00 m to the left, down onto the floor, standing |
| a round tower (radius 2 m): up, then left 2 s | turned 57.3° round it, its axis 2.43 m from the tower's (2 + 0.35 + 0.08) |
| a 2 m block: up, then left past its corner | turned 90.0°, 0.43 m off the side face, on along it |
| a slab leaning back 20° | climbed, mantled over its edge (4.69 m) onto its sloping top |
| free_climb off; outside climb_mask; an overhang of 30° | not climbed, standing at the foot |

No frame of any lane with the capsule in the world; the hands and feet on their targets where they reach (0 cm). The
fifteen earlier lanes of `climb_check.gd` and all of `traversal_check.gd`'s the same as the build before, the
regression scenes `traversal` and `climb` exact. 32 bodies climbing: 18-19 µs a body a physics frame on either engine
(4-5 rays and a capsule's overlap).

## Decision

KEEP, opt-in (`free_climb` off: nothing changes).

## Rejected / not done

* **The ledge check for the top** (as ladders do): it took a slab's face for a sloping top ("slope"), and its edge
  for a top sloping away was the column's height, not the corner's — the capsule over the edge touched it ("no room
  over the edge").
* **A capsule leaning with the face**: the body's collision stays upright (a CharacterBody3D's capsule); the pose
  leans the torso.
* **Stamina, a climbing jump (a lunge up the face), climbing on moving bodies, a clip set for climbing**: a game's
  stamina ends the climb with `release()`; the others are later work.
