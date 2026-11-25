# 0346 — Arcade pace, a person's motion: the gaps leapt again, the body's arc gravity's, the legs under the body

Status: **KEEP** · Branch `fellow/geometry` · Code:
* `modules/fellow_animation/fellow_traversal_3d.{h,cpp}`: with `leap_push_force`, the push as long as that force needs
  for the dead point, the reach to a hold less the settle into the hang, the hips held off the body from the push to the
  dead point (`PoseHints::hips_hold_weight`); with `climb_pull_speed`, a foot stepping up or across the wall no higher
  than `FOOT_HIGH_STEP` over its hanging place, the trailing hand across over 0.6-0.95 of the move; a foot's way in and
  out from the wall eased (`FOOT_DEPTH_SPEED`); `leap_push_force`'s range to 8;
* `modules/fellow_animation/fellow_traversal_pose_3d.{h,cpp}`: the hips' height off the body held while the hint asks;
* `showcase/rooftops/player.tscn`, `tools/build_rooftops.gd`: `leap_push_force` 5.5, `climb_pull_speed` 2, no step up;
  `scripts/rooftops_player.gd`: the joint limits' neck to −66..56°;
* `showcase/tools/effort.py`: `--profile arcade` (the showcase's): the gates that make a motion read as real kept, a
  hero's strength allowed, a pace gate; `climb_look.gd` records the pose's fit.

## Problem

The owner, after milestone 203: "the joints bend unanatomically again; the climbing is now very slow — it is not about
100 % realism but arcade fun, with the animations relatively realistic". Milestone 203 had stood up to the house's 2 m
gaps at a person's strength (2.3 body weights, 1 m/s): the demo's first eight moves took 19.6 s (milestone 202: 8.5),
and the step up's high step bent the joints (`anim_quality.py` 38 → 35 / 45: a knee bent backwards, the hips past their
range in 11.5 % of the frames, the toes and shins at the top's edge in 8.5 %).

## What reads as real (`effort.py --profile arcade`)

A game's hero can be stronger than a person; what gives a motion away is its shape: a body that floats in the air (a
flight decelerating at less than 1 g), a snap (a change of speed beyond any push), a run into a wall stopped by the
stone, a face against it. The arcade profile keeps those gates (milestone 203's) and allows a leap's push to 6.5 body
weights (a dyno of about 2.5 m), at a game's pace: the demo's first eight moves from hold to hold within 10 s.

## Change

* **The showcase**: `leap_push_force` 5.5 (the gaps leapt: a push from rest over 0.65 m to 5.3-5.5 m/s, then a flight
  under gravity to the dead point at the hold), `climb_pull_speed` 2 m/s, no step up.
* **The leap's push long enough**: a push of `d` to a speed `v` peaks at `1 + π v² / (4 d g)`; the clip's shorter push
  had cut the speed and caught the body 9 cm short of its hang, lifted in a frame. Now `d` is at least
  `top / (2 (F − 1) / π + 1)` (up to 0.65 m), and a hold is leapt to only if its rise and the settle into the hang are
  within reach.
* **The hips held off the body** from the push to the dead point, let go over the settle: the clip's own crouch in the
  push and rise in the flight (0.2 m down, then up) had made the hips float at 0.77-0.81 g on a ballistic body.
* **The feet under the body**: pulled up, the trailing foot had stepped to its next place while the body was half way —
  0.4 m higher under the hips than hanging, the leg folded against the wall, the knee swung 0.2 m sideways in a frame
  (2,000 °/s, bent 52° sideways). A foot now steps at most `FOOT_HIGH_STEP` (0.25 m) over its hanging place under the
  body's place, and its way in and out from the wall (the stone under it stepping out) at most `FOOT_DEPTH_SPEED`
  (1.5 m/s).
* **Across**: the trailing hand over 0.6-0.95 of the move (0.68-0.96: its forearm spun at 2,270 °/s).
* **The neck**: the joint limits' extension 66° (the measure's 70°, eased in at 720 °/s: a look up to a hold 2.5 m over
  the hands had passed it by 1-3°).

## Measurements (headless; the demo from the house)

| | milestone 202 | milestone 203 | **milestone 204** |
|---|---|---|---|
| the demo's first eight moves from hold to hold | 8.5 s (three leaps) | 19.6 s (three step ups) | **8.7 s** (three leaps) |
| the run at the takeoff; stopped against the wall; the face from the stone | 7.0 m/s; 42.8 g; 0.07 m | 4.0; 2.5; 0.29 | 4.0; 2.5; 0.29 |
| a leap: the push's peak; take-off; flight against gravity; rise | 9.3 body weights; 4.3 m/s; 0.66 g; 2.4 m | — | **5.2; 5.3-5.5 m/s; 0.91-0.94 g; 2.5 m** |
| between holds: the largest change of speed over 3 frames | 8.5 g | 1.2 g | 4.1 g (the leap's push itself) |
| `effort.py --profile arcade` | 2 / 8 | 6 / 7 (the pace; no flight) | **8 / 8** |
| `anim_quality.py` gates | 38 / 45 | 35 / 45 | **38 / 45** (the seven left: the gaits' own, as before) |
| from hold to hold: the fastest joint | 2,026 °/s | 2,661 | **2,000** |
| climbing: elbows and knees out of their plane; wrists; ankles; neck | 0.4; 0; 0; 0 % | 6.2; 7.0; 0.98; 0 % | **0; 0; 0.1; 0 %** |
| from hold to hold: the hips past their range; limbs in the stone | —; 1.0 % | 11.5; 8.5 % | **0; 0.5 %** |

`climb_check.gd` (37 lanes) and `traversal_check.gd` (19) are identical to milestone 202's.

## How it got there

| step | change | arcade gates | `anim_quality` | what it showed |
|---|---|---|---|---|
| 1 | the showcase's settings: 5.5, 2 m/s, no step up | 7 / 8 | 36 / 45 | flights at 0.77-0.81 g; a thigh's pop 2,318 °/s; the neck 4.2 % |
| 2 | the push long enough; the hips held off the body | 8 / 8 | — | flights 0.91-0.94 g, no catch lifted |
| 3 | a foot's way in and out eased | — | 36 / 45 | the knee still swung out (52° sideways): the foot was above the body, not the stone |
| 4 | the feet under the body | — | 36 / 45 | hinges 0 %; a forearm's 2,268 °/s across; the neck |
| 5 | the trailing hand across slower; the neck 66° | 8 / 8 | **38 / 45** | the fastest joint 2,000 °/s |

## Decision

KEEP: the leap's push length, reach and the hips' hold (with `leap_push_force`), the feet under the body and the
trailing hand across (with `climb_pull_speed`) — each with its property, off by default; the foot's eased depth (a
hanging foot only, where the stone under it steps; the benches identical); the showcase at an arcade's pace.

## Rejected / not done

* **The step up in the showcase** (milestone 203's, the owner's earlier choice): 4.4 s a move, its high step not yet
  within the hips' range. It stays in the engine (`hang_step_up_height`) for ledges a leap cannot reach.
* **A person's leap to the gaps** (2.3 body weights, 1.2 m): an arcade hero's leap of 2.5 m with a person's shape is
  what the owner asked for; `effort.py --profile human` still measures the person.
* **The hips at the mantle and the roof's crawl** (8 %, 7.3 %: the clips' own flexion past 140°): unchanged, as before.
