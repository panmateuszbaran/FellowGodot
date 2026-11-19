# 0286 — Narrow holds

Status: **KEEP** (opt-in: `hold_min_width`; nothing changes at 0) · Branch `fellow/geometry` · Code:
`modules/fellow_animation/fellow_traversal_3d.{h,cpp}` (`_fit_hold()`, `Ledge::spread`, `_find_jump()`, `_start_hang()`,
`_step_hang()`, `_step_transfer()`) · Checks: `bench/climb_check.gd` (`narrow_holds`) on GodotPhysics3D and Jolt, the
regression scenes `climb`, `ledge_corners` and `traversal`.

## Problem

Decisions 0282 and 0285 climb from hold to hold, but a hold had to take both hands 0.48 m apart: a reach or a leap
found a ledge only where the edge went on 0.24 m either side of the point found. Assassin's Creed's walls are climbed on
narrow holds too — a beam's end, a jutting brick, a small corbel, a shutter's hinge — the hands together on them. A
narrow hold caught from the air was held anyway, the hands 0.48 m apart, in the air beyond its ends.

## Change

* **`hold_min_width`** (m, 0: off): a hold narrower than 0.48 m, down to this wide, is held with the hands closer
  together. `_fit_hold()`: where the hands do not fit 0.24 m either side, the hold's ends are looked for in 2 cm steps up
  to 0.3 m either way; the body hangs from its middle, each hand 5 cm in from an end (`Ledge::spread`, `spread` in the
  ledge's dictionary; 0.24 m on a wide ledge).
* A narrow hold is caught from the air, reached up or across and leapt to (up, down, back, along, diagonally) like any
  other; the reach up looks 0.16 m either side of the hands' middle as well. The body does not shimmy along it (the
  look ahead finds nothing), and a hang starts only where the hands fit.
* The hands go where `spread` puts them: hanging, on the way to another hold, and for a reach across, the leading
  hand's first grip at the near end.

## Checks

`climb_check.gd`, a new lane `narrow_holds` (`hold_min_width` 0.2 m, with `hang_reach_up` 1.25 m, `hang_reach_side`
0.8 m, `hang_brace_height` 0.65 m): a 5.5 m wall on its own ground, holds 0.25 m wide and 0.2 m out (a beam's end) at
3.4 m, at 4.4 m 0.1 m to the left, and at 4.4 m 0.5 m past that one's end:

| | |
|---|---|
| caught | the first at 3.4 m, at 1.20 s, the hands 0.16 m apart |
| up to the second | hand over hand, at 2.20 s (hung from its middle, 0.1 m to the left) |
| across to the third (a gap of 0.5 m) | hand over hand, at 3.00 s, the hands 0.14 m apart |
| up to the wall's top (1.1 m) | hand over hand, at 3.75 s, the hands 0.48 m apart again |
| over the top | a high mantle, 3.77-4.95 s; standing on it at 5.50 m |

No frame of the capsule in the world; the same on Jolt. The other 30 lanes: value for value as before (milestone 151's
results; `side_reach` as decision 0285 measured it), on both engines. The regression scenes `climb`, `ledge_corners` and
`traversal`: exact. A picture strip (lavapipe): both hands on the 0.25 m block; reaching up, one hand on each block;
across, the leading hand on the next block's near end while the other holds the last.

## Decision

KEEP.

## Rejected / not done

* **A hand on each of two narrow holds** (two bricks 0.6 m apart): both hands go on one hold; between two, the body
  reaches across from one to the other.
* **Shimmying along a hold narrower than 0.48 m**: the body hangs from its middle.
* **The showcase's walls**: their holds are wide (sills, corbels, beams); narrow ones (the putlogs' ends) would be the
  next step.
