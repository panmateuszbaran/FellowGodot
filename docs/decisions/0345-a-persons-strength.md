# 0345 — Climbing within a person's strength: the run braked at the wall, leaps a person can make, a step up for the rest

Status: **KEEP** · Branch `fellow/geometry` · Code:
* `modules/fellow_animation/fellow_traversal_3d.{h,cpp}`: `leap_push_force` (a leap's push as a person's — a half sine
  to that many body weights, no dip on straight arms), `climb_pull_speed` (the body pulled up between holds no faster),
  `hang_step_up_height` with `TRANSFER_STEP_UP` (a hold beyond a leap stood up to from the one below: pulled up,
  pressed to straight arms, a foot on the top, stood up, reached), `get_aim_state()` (an aimed jump's catch for the
  controller); the pose hints' elbow and knee poles and the soles of feet standing on a top;
* `modules/fellow_animation/fellow_traversal_pose_3d.{h,cpp}`: `sway_vertical`, `fit_max_speed`; the hint poles;
* `showcase/scripts/rooftops_player.gd`: the run braked for the wall, the jump closer to it, the body steered in the air
  to where it will hang (`--wall_brake=0`: as before); `--trav_set=`, `--pose_set=`, `--hinge_max_turn=`;
  `showcase/rooftops/player.tscn`, `tools/build_rooftops.gd`: the new defaults; the joint limits' `hinge_max_turn` 175°;
* `showcase/tools/effort.py` (new): the forces the climbing asks of the body, seven gates; `climb_look.gd` records the
  head's, chest's, hips' and shins' gaps to the stone; `anim_quality.gd`: the climb 26 s, `--dump=1`.

## Problem

The owner, after milestone 202: "why does the character jump face first into the wall, and why is its push up so
effortless for it — assess the forces needed for the jump and make the animation realistic". Asked how a hold 2 m up
should be reached, the owner chose standing up onto the ledge below it (pull, press, a foot on the top, stand, reach)
over a leap no person could make.

## The measure (`tools/effort.py`)

From `climb_look.gd`'s frames (the demo at 60 fps), for a 75 kg body whose centre of mass is taken as its hips (their
heights low-passed at about 6 Hz, as motion capture's are before forces are read): the run's speed at the takeoff, the
body's deceleration against the wall, the face's, chest's and knees' least gap to the stone; for each move from hold to
hold the hips' rise and fastest climb, the support force (`1 + a/g` while held), the flight's deceleration against
gravity (a body in the air slows at 1 g; less floats), the take-off speed and the leap's rise, the largest change of
speed over three frames. Gates from human limits: a climber's explosive pull-up peaks at 1.4-1.7 body weights
(PeerJ 11:e15886), with the legs on the wall a strong climber's at about 2.5; a countermovement jump leaves the ground at
2.8-3.4 m/s (0.4-0.6 m of flight); a body stopped by its legs takes a few g, one stopped by stone tens.

## What was found (milestone 202's motion)

* **The face into the wall**: the run reached the wall at a sprint (7.0 m/s at the takeoff, 1.1 m from it) and the stone
  stopped it — 42.8 g in a frame; the face came to 7 cm of the stone, the knees into it (−1 cm).
* **The effortless push**: the hips were held by the pose's fit while the body rose (`body_sway`'s vertical drive and
  `hang_fit` pinned them), then let go — up to 10.2 body weights (17 unfiltered), snaps of 8.5 g; a leap up rose 2.2 m
  off a 4.3 m/s push and floated (0.66 g): under gravity 4.3 m/s carries a body 0.94 m, and 2.2 m needs 6.6 m/s — twice a
  standing jump, on arms; the reaches' hand took the hold first and yanked the body up to it (5.3 m/s, 10 body weights).

## Change

* **The run at the wall** (the showcase's player): braked to what the jump's legs can take (`sqrt(4² + 2·6·d)` m/s at
  `d` m from the takeoff; 4 m/s at it), the takeoff 0.95 m from the wall (not 1.1), and in the air the body steered to
  where it will hang by the catch (`get_aim_state()`), its legs taking the rest against the stone.
* **The leap** (`leap_push_force`, 2.3 in the showcase): the push a half sine of force from rest to 2.3 body weights
  over at most 0.65 m — `v = sqrt(4 d (F − 1) g / π)`, 3.25 m/s, about 1.2 m of rise with the flight —, no dip on
  straight arms (the body swings out from the wall); a hold higher than that is not leapt to.
* **The step up** (`hang_step_up_height`, 2.3 m): such a hold, where the ledge below has a top 0.12 m deep and a body
  fits standing on it, is stood up to — pulled up until the chest is at the edge (at `climb_pull_speed`), pressed up on
  the hands until the arms are straight (0.8 m/s at most) with a foot on the top, stood up on it (0.9 m/s, the other foot
  following once the body is over it), reached up on the toes as far as the hold needs, hung from it. Each phase eased
  from rest to rest; the hips out from the face by what the hold standing out from it needs, in over the top only once
  both feet are on it; the hands off the hold and up the wall's face, the elbows down and out from it; the knee's pole up
  and out over a foot on the top while the body is below it; the ball of a foot standing on the top kept on it.
* **The pull** (`climb_pull_speed`, 1 m/s): a reach up or across takes as long as the body needs to rise at that pace,
  the leading hand reaching the hold as the body gets there.
* **The pose** (`FellowTraversalPose3D`): `sway_vertical` 0 (the body sways along the wall and out from it as a pendulum,
  not up and down), `fit_max_speed` 1.2 m/s (the body fitted to the arms' reach no faster than arms move it, its speed
  changing by at most 8 m/s²).
* **Joint limits**: a hinge turned into its bend however far round (`hinge_max_turn` 175° in the showcase: a leg stepped
  onto a hold at the hips' height had its knee bent backwards past 120°).

## Measurements (headless; the demo from the house, `effort.py`)

| | milestone 202 | **milestone 203** |
|---|---|---|
| the run at the takeoff; the body stopped against the wall | 7.0 m/s; 42.8 g | **4.0 m/s; 2.5 g** |
| the face, chest, knees from the stone at the closest | 0.07, 0.27, −0.01 m | **0.29, 0.49, 0.04 m** |
| support force at its peak, any move (6 Hz) | 10.2 body weights | **2.3** |
| the body's fastest climb between holds | 5.8 m/s | **1.1 m/s** (the step up), 0.9 (a reach) |
| the largest change of speed over 3 frames | 8.5 g | **1.2 g** |
| a leap's flight against gravity; its rise | 0.66 g (floating); 2.4 m | **no leap in the demo** (each 2 m gap stood up to; a leap at 2.3 body weights rises ≤1.2 m) |
| the demo's first eight moves from hold to hold | 8.5 s (three of them leaps) | 19.6 s (three step ups) |
| `effort.py` gates | 0 / 7 | **6 / 6** (no flight to measure) |
| the same build, milestone 203's behaviour off by its flags | 0 / 7: 7.0 m/s, 42.8 g, 0.05 m, 10.3 body weights, 0.74 g, 8.5 g, 2.44 m | — |

`anim_quality.py` (the climb now 26 s, to the roof: 14 s had ended in the step ups): 35 / 45 gates, against milestone
202's 38 / 45. The step up's anatomy is not yet clean: from hold to hold the fastest joint 2,661 °/s (a thigh as the
foot goes up onto the top; gate 2,200), elbows and knees out of their plane 6.2 % of the frames (a knee bent backwards
during the high step), the wrists 7.0 % (pressing on the hold), the ankles 0.98 %, the hips past their range 11.5 % (the
high step's flexion and spread; not gated), limbs in the stone 8.5 % (the stepping leg's toes and shin against the
top's edge, the forearms in the press; milestone 202: 1.0 %). Hanging, the mantle and the gaits are as they were.
`climb_check.gd` (37 lanes) and `traversal_check.gd` (19) are identical to milestone 202's with the properties' defaults.

## How it got there

* The face first: braking alone still left the body to the stone at the catch → the takeoff closer and the body steered
  to its hang in the air: 2.5 g.
* The pinned hips: `sway_vertical` 0 and `fit_max_speed` (a speed limit alone started the fit at full speed in a frame →
  with an acceleration limit).
* The leaps: the measured peak equals `leap_push_force` and the capsule is ballistic (at 5.5 body weights for the check);
  at 2.3 the demo's 2 m gaps are beyond a leap → the step up.
* The step up: the standing body leaned back off the top (hips 0.30 m out) → in over the top with the torso tipped to
  the wall; knees into the stone standing bent → standing straight, the ankles 0.19 m off the face; a knee bent
  backwards → the hinges turned 175°; a hand's pop at its let-go and the head's at the look's switch → eased; the
  forearms through the wall (153 frames) → the elbows' poles down and out on the face (5-14).
* The press: pressed to 0.2 m over the top, the hips were held under the hold by the fit for 0.7 s and let go at 2.1 m/s
  (2.8 body weights) → pressed only to straight arms, the grip eased off as the body rises over the hands: 2.3, 1.1 m/s.

## Decision

KEEP: the three traversal properties, `get_aim_state()`, the pose's `sway_vertical` and `fit_max_speed` (each off by
default: nothing changes without them); the showcase climbs with them.

## Rejected / not done

* A leap at a person's force to the 2 m gaps (the owner's other option): at 2.3 body weights it rises 1.2 m; reaching
  2 m needs a dyno's run-up and a catch at full stretch — not a hold's move.
* The step up's remaining anatomy (above): a high step with the foot at the hips' height needs the thigh's turn and the
  knee's way planned with the hip's limits (a leg re-solve), the toes kept off the top's edge as the foot goes round it.
  Left to the next milestone, with gates.
* Running at a wall at a sprint and stopping by the legs: a person cannot take 7 m/s into a wall at 1 m; the run brakes.
