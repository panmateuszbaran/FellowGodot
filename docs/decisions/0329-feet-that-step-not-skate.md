# 0329 — Feet that step, not skate: a step's arc kept over the grounding, a foot down but not held lifted as it moves

Status: **KEEP**, on by default — `FellowMotionMatcher3D.foot_skate_guard` (on; off: milestone 186's feet) · Branch
`fellow/geometry` · Code:
* `modules/fellow_animation/fellow_motion_matcher_3d.{h,cpp}`: `_solve_feet()` (the lift of a foot down but not held,
  the grounding under a step's arc, the feet's height over the ground worked out once for both), `get_stats()` adds
  `foot_skim_distance` and `foot_air_time`; `foot_skate_guard` (new);
* `benchmarks/project/bench/balance_check.gd` (the feet's skating, the motion matcher's skimming, five pushes while
  walking, `--skate_guard`), `bench/balance_video.gd` (`--program=staggers`, `--skate_guard`),
  `scenes/matching_lab.gd` (`--matching_skate_guard`, the feet's skimming).

Checks: `balance_check.gd` on Jolt Physics (headless), Matching Lab's crowd (`matching_eval.gd`), the staggers filmed on
lavapipe with the guard off and on. No GPU (the owner's wish).

## Problem

The owner had seen the character "slide unnaturally" (milestone 184); milestone 184 made feet that would slide step,
but a stagger's feet still skated. `balance_check.gd`, a new metric — how far a foot's body goes over the ground while
within 1 cm of its standing height (each such stretch's farthest from where it began, summed) — standing, pushed at the
chest: 20.8 cm at 20 N·s (no stagger at all: the motion matcher's own weight shift), 13.4 at 60, 11.7 at 80.

Traced: a foot let go of by its lock because its animation had gone `unlock_radius` away (a CMU clip's shuffle, a
transition's blend between two stances) stepped there on milestone 184's arc — and slid there over the ground, 14-26 cm
at 1-1.6 m/s. Its contact stays on through such a step (the database's feet in contact are the clip's; it is the blend
that moves them), and since milestone 185 the grounding lowers a foot in contact by its height over the ground: its
step's arc with it, back down within 0.04 s.

## Change

* **The arc kept**: `foot_grounding` lowers a foot by its height over the ground less its step's arc — the arc goes on
  top of where the foot stands.
* **A foot down but not held lifted as it moves**: a foot in contact that no lock holds (stepping, or carried by a
  blend) is lifted by its speed over the ground, as milestone 184 lifted a foot dragged low in the air — 0.12 s of it,
  at most half of `foot_step_lift` (4 cm), its step's arc counted — whether the character stands or not (milestone
  184's lift was only for a character standing still, and only in the air).
* `get_stats()` adds how far a foot in the air went under 1 cm over the ground (`foot_skim_distance`,
  `foot_air_time`).

## Measurements

`balance_check.gd` (Jolt, 60 Hz), `foot_skate_guard` off (milestone 186, to the digit) → on:

| pushed at the chest, standing | skating (cm) | slip (cm, all / one stance's most) |
|---|---:|---:|
| 20 N·s (no stagger) | 20.8 → **3.2** | 23.4 / 20.4 → 20.1 / 17.0 |
| 40 N·s | 4.8 → 4.8 | 4.8 / 3.4 → 4.8 / 3.4 |
| 60 N·s | 13.4 → **2.2** | 23.4 / 18.6 → **8.2 / 5.4** |
| 80 N·s | 11.7 → **5.7** | 25.0 / 13.9 → **8.9 / 3.8** |
| 100 N·s | 5.3 → 5.3 | 24.1 / 19.6 → 24.3 / 19.6 |
| 130 N·s | 4.0 → 4.9 | 8.6 / 2.2 → 9.7 / 2.2 |
| 160 N·s | 4.2 → 4.5 | 19.7 / 8.3 → 12.9 / 5.4 |
| 200 N·s (a step, then a fall) | 0.6 → 2.0 | 20.8 / 17.2 → 21.3 / 17.2 |

(The slip counts a foot within 3 cm of its standing height: a low step's ends count.)

* After getting up, the feet's slip as the motion matcher takes over: 24.8 / 24.8 / 28.6 / 7.1 → **15.7 / 16.4 /
  10.6 / 7.1** cm (falls from the front, behind, left, right).
* Five pushes while walking (70 N·s from either side, behind and the front, 110 from the right): skating 6.8 / 9.0 /
  30.7 / 9.0 / 4.3 → 7.9 / 9.3 / 31.1 / 10.5 / 6.1 cm — the motion matcher's path the same (its feet skim the same
  distances), the physical feet up to 1.8 cm more.
* The falls (200 N·s) as before: the hips down 1.02-1.23 s after the push, the joints' gap within 3 mm.

Matching Lab, 16 characters walking, jogging and running for 60 s (`matching_eval.gd`), off → on: a foot in contact
moving over the ground with it down (under 1 cm raised) 0.0312 → **0.0245 m/s (−21 %)**; all of a foot in contact's
motion 0.0784 → 0.0783 (more of it stepping: 0.0472 → 0.0537); locked 0.0211 → 0.0213; a foot in the air under 1 cm
0.354 → 0.350 m/s; the planted feet over the ground −0.8 / 0.0 / +1.6 → −0.7 / 0.0 / +1.7 cm (5th / 50th / 95th
percentile); popping (p99) 39.2 → 39.2 mm; transitions 2.39 a second.

## How it got there

* A lift by speed for feet in the air came first, its height from the data: the database's feet in the air at least
  as high as their 5th percentile at each speed (1.2-1.6 cm at 1 m/s, 2-2.5 cm from 1.5 to 3 m/s; worked out once per
  database). It did not touch the staggers (their feet are in contact as they slide), the crowd's feet in the air
  skimmed 22 % less (0.354 → 0.277 m/s) — but pushed while walking the physical feet skated more in each of five
  trials (6.8-30.7 → 12.5-57.7 cm): dropped.
* The skating summed every update's motion at first and counted Jolt's jitter (1-2 mm an update while a foot is
  held): each stretch's farthest from where it began now, as the slip does.
* One push while walking was a single chaotic sample — a change anywhere sends the motion matcher down another path —
  so there are five now.

## Decision

KEEP. A stagger's feet step where they skated (60 N·s: 13.4 → 2.2 cm over the ground), the feet after getting up slide
less, the crowd's feet in contact move over the ground 21 % less; walking and falls as before.

## Rejected / not done

* A clearance from the database for feet in the air (above).
* Steps planned to where the animation's foot will land (the database's next contact), not to where it is.
