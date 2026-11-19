# 0285 — Reaching across from hold to hold

Status: **KEEP** (opt-in: `hang_reach_side`; nothing changes at 0) · Branch `fellow/geometry` · Code:
`modules/fellow_animation/fellow_traversal_3d.{h,cpp}` (`_find_jump()`'s along and diagonal branches, `_fit_reach()`,
`_step_hang()`, `_step_transfer()`) · Checks: `bench/climb_check.gd` (`side_reach`) on GodotPhysics3D and Jolt, the
regression scenes `climb`, `ledge_corners` and `traversal`, the showcase's demo.

## Problem

Decision 0282 made climbing go from hold to hold: up, a ledge within `hang_reach_up` is reached hand over hand. To the
side there was only the leap (`hang_jump()`, `TRANSFER_JUMP_ALONG`, 0.5 s, the hands letting go), however small the
gap: from one window's sill to the next, from corbel to corbel. Assassin's Creed reaches across such gaps: the hand on
that side out to the next hold, the body swinging under the hands, the other hand following. A ledge up and to the side
(a sill above and past the end of the one held) could only be leapt to as well (`TRANSFER_JUMP_DIAGONAL`), and only by
`hang_jump()`; holding the wish along at a ledge's end did nothing where the wall does not turn.

## Change

* **`hang_reach_side`** (m, 0: off): hanging, a gap up to this wide to a ledge to the side (within 0.35 m of the edge's
  height, both hands on it) is crossed hand over hand (`TRANSFER_REACH_SIDE`, 0.8 s): holding the wish along at the
  edge's end (looked for after the corner, again 0.2 s after a look in vain), or by `hang_jump()`.
* With `hang_reach_up` too, a ledge past the edge's end up to `hang_reach_up` higher (0.4 m at least) is reached the same
  way, pushing up and to the side. A ledge further, or wider apart, is still leapt to.
* The move: the hand on that side leads, out from the wall to the other ledge's near end (0-0.4 of the move), holding it
  there while the body swings across under the hands (0.2-0.95, 6 cm lower halfway); it moves on to its place on the
  ledge (0.55-0.8) and the other hand follows to the near end (0.62-0.9). The feet step across the wall, the leading
  side's first (0.3-0.62, then 0.58-0.92), each off the wall as it steps. Where a hold below the other ledge is in the
  way, the body hangs out from it as for a reach up (0.08 m steps to 0.32 m).
* `_fit_reach()`: the room for the body hanging from the ledge reached and halfway there.

## Checks

`climb_check.gd`, a new lane `side_reach` (`hang_reach_up` 1.25 m, `hang_reach_side` 0.8 m, `hang_brace_height` 0.65 m):
a 5.4 m wall on its own ground, corbels (0.8 m wide, 0.22 m out) at 3.4 m and 0.6 m to its left at the same height, one
at 4.3 m 0.4 m past that one's end; a jump at the wall, the wish left, then up and left, then up:

| | |
|---|---|
| caught | the first corbel at 3.4 m, at 0.98 s |
| across to the second (a gap of 0.6 m) | hand over hand (`reach_side`), at 2.67 s |
| up and across to the third (0.9 m higher) | hand over hand (`reach_side`), at 4.40 s |
| up to the wall's top (1.1 m) | hand over hand (`reach`), at 5.15 s |
| over the top | a high mantle, 5.17-6.35 s; standing on it at 5.40 m |

No frame of the capsule in the world; the same on Jolt. The other 29 lanes: value for value as milestone 151's results
(GodotPhysics3D and Jolt). The regression scenes `climb`, `ledge_corners` and `traversal`: exact. A picture strip
(lavapipe, `--only=side_reach --times=...`, new options of the script): the lead hand on the near end with the other
still on the first corbel, the legs apart across the wall; up and across, the trailing arm stretched down to the
corbel left.

The showcase's player has `hang_reach_side` 0.8 m (its clip: the reach of the leading hand's side); the demo, which
climbs the tower's column straight up, runs through as before.

## Decision

KEEP.

## Rejected / not done

* **Reaching down and to the side**: a ledge lower and past the end is still leapt down to (`TRANSFER_JUMP_DIAGONAL`).
* **A clip of its own** for the sideways reach: the reach up's clip of the leading hand, the pose putting the hands and
  feet on the stone.
* **Holds for one hand** (a brick, a beam's end): later work.
