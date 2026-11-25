# 0340 — Hands that hold the edge, leaps timed as captured hops, state changes inertialized

Status: **KEEP** · Branch `fellow/geometry` · Code:
* `modules/fellow_animation/fellow_traversal_pose_3d.{h,cpp}`: `ledge_grip` (a hand on a ledge held as a climber's:
  the knuckles over the edge, the palm down the face, the fingers and the thumb on the top; over a moulding, by a
  step's edge, the wrist straightened towards the shoulder as far as the arm must reach; its place along the whole way
  between holds, its shape turned to and let go of; the hands' targets eased over at most 15 ms, the hands in twice as
  fast as the rest of the pose, the elbows' bend turned at most 720°/s); `hand_flight_weight` homing in over the second
  half of a hand's way;
* `modules/fellow_animation/fellow_traversal_3d.{h,cpp}`: `set_leap_timing()` / `clear_leap_timing()` /
  `has_leap_timing()` (`LEAP_UP`, `LEAP_ALONG`: a leap from hold to hold as a clip's — its hips' way and lift, its hands'
  let-go and catch; up, the clip's rise scaled onto the leap's more in the middle of the way than at its ends; a longer
  leap slower by the square root of the ways' ratio, 0.8-1.6 times); the pose hints' `hand_reach`, `hold`,
  `hold_normal`, `hold_lip` (the stone just under a hold's edge, four rays and one down, again each 5 cm a hand moves
  along it); `HOLD_IN`, `HOLD_UP`;
* `showcase/scripts/rooftops_player.gd`: the states switch at once, `FellowInertializer3D` (before the IK's modifiers)
  blending over 0.3 s from the pose the character had (a variant's swap 0.2 s); the leaps timed from the library's hop
  data; `ledge_grip` 1; the tools' `--ledge_grip=`, `--no_inertia=1`, `--no_leap_timing=1`, `--max_joint_speed=`,
  `--body_sway=`, `--hang_fit=`;
* `showcase/tools/blender/mixamo_retarget.py`: the hops' `leap` data (`clips.json`: the hips' way along the leap's axis
  and their lift, sampled evenly; each hand's let-go — 10 cm from its start — and catch — under 0.6 m/s after its
  fastest; the leading hand);
* the tools: `showcase/tools/climb_look.gd` (each hand by its hold: the elbow, the wrist, the middle finger's joints and
  tip, the thumb's, out from the edge and over the top, how deep in the stone each bone goes — from the wall's side and
  from above; the wrists' turn; `--cam=hand|handside`, `--warm=`), `showcase/tools/anim_quality.gd` (each joint's
  95th percentile).

## Problem

The owner, after milestone 197: "these animations are not very accurate — the character does not grip the edge with
its hand but somewhere in the air, the hands sometimes go through the edge, the jump up is not anatomical: it does not
look as if the character puts energy into the leap, it just suddenly flies up, the blending between animations is
sometimes sudden and violent."

## What was found (headless, `climb_look.gd`: the hang program — 4 s still on CasaCrema's first hold, along it each way —
and the demo's climb up CasaCrema's front, with milestone 197's settings)

* **The hands in the air and through the edge**: the traversal puts a hand's target (its wrist) 6 cm in over the top
  and 2 cm over it; the hand's turn was the clip's. Hanging, the fingers stood 13.7 cm over the top (median; 3.7 cm
  climbing) — in the air — and the forearm, from the wrist on the top down to the elbow out in front of the wall, went
  through the edge's corner in 98.7 % of the hands' frames; some part of a hand (the forearm, the palm, a finger, the
  thumb) was more than 1 cm in the stone in **99.7 %** of them hanging, **72.9 %** of the holds' frames climbing and
  **29.6 %** between holds. A hand between holds was put on its hold by the IK only after the move had caught it (its
  weight eased from 0.3 over 0.1 s) and its target eased over 0.05 s — some 3 cm short, a twentieth of a second late.
  A hold under which the stone stands out (a moulding 8 cm proud just under CasaCrema's band, its top level with the
  traversal's edge) put the palm inside it.
* **The leap up** was the traversal's own: a wind-up, then the body along a curve in 0.6 s — up 2.3 m to the next hold,
  the body at 6.7 m/s stopped at the top at 23 g (−231 m/s²); Mixamo's hop played over it, three times its speed.
* **The blends**: the player's states cross-faded their animations over 0.2 s (two poses mixed); the approach's
  fastest joint 2,717°/s (the gate 2,200).

## Change

* **A climber's grip** (`ledge_grip`): the middle finger's knuckle 1 cm out from the edge and 2 cm over the top, the
  palm down the face from it to the wrist (70° from level, out of the stone), the hand turned to it after the arm's IK
  (its knuckles' row along the edge, the little finger on its outer side: the palm to the wall whatever the rig's
  axes); the fingers' two bones aimed
  over the edge onto the top (their middle joints 3.7 cm in and 1.6 cm over it, the tips 8 cm in and 1 cm over), the
  thumb's tip beside the index finger's — as far as the knuckles are over the top; under it (the arm not yet up to the
  hold) the fingers open up the face. Where the stone just under the edge stands out (the traversal probes it), the palm
  flattens until it clears it; a step just under the edge (at least 6 cm out, its top no more than 4 cm under) is held
  by its own edge. Where the arm cannot reach, the wrist straightens towards the shoulder with the knuckles kept on the
  edge (eased over 0.08 s: a body swinging across under a hand had twisted the wrist at 2,000°/s).
* **On the hold as it is caught**: the hand's place by its knuckles all the way between holds (blended from the
  hold's target to the grip's, it had cut through the corner), its shape turned to over the last 65 % of its way (0.12
  s), let go of over 0.24 s with the wrist's bend held against the forearm; the IK homing in over the second half of the
  way (`hand_flight_weight`); the hands' targets eased over 15 ms (not 50: stepped at the physics' rate they jumped
  where frames are drawn faster); the hands in twice as fast as the rest of the pose (a hold caught from the air), and
  full again when the pose comes in afresh; the elbows' bend turned at most 720°/s (an elbow had turned over in a
  frame where the pole's and the clip's bends crossed — with the joint speed limit after it, the hand left its hold by
  15 cm for two frames).
* **Leaps timed as Mixamo's hops** (`set_leap_timing`): the body where the clip's hips were along the move and lifted
  as they were (the push, over the top, dropping into the catch), the hands letting go and catching when the clip's did
  (the leading one first), the feet off the wall as the first lets go and on it again as the last catches; up, the
  clip's rise scaled onto the leap's more in the middle of the way than at its ends (the push's dip and the rise past
  the end the clip's); a longer leap slower by the square root of the ways' ratio, up to 1.6 times (a flight under
  gravity takes longer).
* **Inertialized state changes** (the showcase): each state switches at once and `FellowInertializer3D` blends from
  the pose the character had, carried on by its own velocity, over 0.3 s.

## Measurements (headless; `climb_look.gd`, `anim_quality.gd`)

| | milestone 197 | milestone 198 |
|---|---|---|
| hanging still and along a ledge: a hand more than 1 cm in the stone | 99.7 % of the hands' frames | **0.4 %** |
| climbing CasaCrema: a hand on its hold in the stone | 72.9 % | **1.8 %** |
| climbing CasaCrema: a hand between holds in the stone | 29.6 % | **10.6 %** |
| the fingertips over the top (median; hanging, climbing) | 13.7 cm, 3.7 cm (in the air) | **0.9 cm** (on it) |
| a hand on its hold: its wrist off its grip's place, 95th percentile (hanging) | — | 2.9 cm |
| the leap up (2.3 m): its time, the body's fastest, its sharpest stop | 0.62 s, 6.7 m/s, −231 m/s² (23 g) | **1.93 s, 7.3 m/s, −62 m/s² (6 g)** |
| the wrists' turn against the forearms, 95th percentile | 897°/s | 668°/s |
| `anim_quality.py`: gates | 30 of 38 | **31 of 38** (the approach's pop 2,717 → 1,659°/s) |
| from hold to hold: the fastest joint's 95th percentile; the hips' acceleration's | 1,355°/s; 447 m/s² | 1,299°/s; 311 m/s² |
| hanging: the fastest joint's 95th percentile | 722°/s | 855°/s (the catch from the air: 1,239 at most) |
| limbs through the walls (anim_quality: elbows, knees, ankles, toes), from hold to hold | 1.2 % | 0.4 % |

* The scramble up the tiles' toes: the same eight frames, 3.9 → 8.4 % of a shorter segment (the leaps take longer, the
  run ends sooner on the roof).
* `climb_check.gd`, `traversal_check.gd`: every lane as before (`ledge_grip` 0 and `hand_flight_weight` 1 by default;
  no leap timing unless set); the traversal's step 5.5 → 6.6 µs (debug build: the probes under the holds), the pose's
  3.6 → 3.9 µs.
* `tools/fellow_regression`: the base group's 28 scenes identical to their goldens (the climb, the traversal and the
  ledge corners among them).
* On the RTX (jobs f557-f559): the climb, the hands close up, the hang and the course filmed (with milestone 197's settings too, for comparison).

## How it got there

* The grip's wrist only, the hand's turn the clip's: the fingers still in the air. The hand turned, the fingers along
  the top 10-14° under its plane: the tips 0.2 cm into it (6-8° and later aimed at points).
* With the grip's place blended in with its shape (from 60 % of the way): the hand's way between holds cut through the
  corner (the hold's target is on the top) — 15.8 % of the moves' hand frames in the stone; the place all the way:
  10.6 %.
* The fingers curled over by the knuckles' height over the hand's own target, which between holds is on its way: they
  curled while the knuckles were still under the top; over the hold's own edge (`hold`) and open up the face under it.
* The hand turned by its palm's axis: its knuckles' row sloped back from the index finger's to the little finger's (1.9
  cm), the little finger's under the top and left standing up (the RTX's film); turned by the knuckles' row, level
  along the edge, every finger curled by the middle finger's knuckle.
* A depth test from six sides read a point under a cornice as in the open and one in the stone as well (rays from
  inside a wall meet nothing): from the wall's side and from above.
* The leap's slowing at most 1.25: the hips at 8 m/s; 1.6 and the rise eased at its ends: the body's stop at the top
  −68 → −62 m/s², its start out of the push gentler (the hips' own stop, −220 m/s², is the clip's legs' and the pose's,
  not the body's path).
* The grip's shape turned to over 0.06 s and let go of as fast: the hands at the 2,000°/s limit at every catch and
  release (hanging's fastest joint's 95th percentile 722 → 1,446°/s); 0.12 s in, 0.24 s out with the wrist's bend held,
  the hands in twice (not three times) as fast, the elbows' bend limited: 855°/s.

## Decision

KEEP: `ledge_grip` and leap timing opt-in (the defaults leave every other scene's climbing as it was); in the
showcase, the grip, the timed leaps and the inertialized states.

## Rejected / not done

* A hand reaching a hold the arm cannot reach yet (hand over hand up, the body still low): it waits open under the edge
  and closes as the body rises — a delay of the hand's catch rather than the body's rise.
* The hips' own stop at a leap's top (the clip's legs and the pose, −220 m/s²), the across move's wrist (1,778°/s at
  most as the body swings under the hand), the remaining 10.6 % of the hands between holds (most under a hold the arm
  cannot reach yet).
