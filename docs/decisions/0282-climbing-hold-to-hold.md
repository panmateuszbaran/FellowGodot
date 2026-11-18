# 0282 — Climbing from hold to hold

Status: **KEEP** (opt-in: `hang_reach_up`, `hang_brace_height`; nothing changes with both at 0) · Branch
`fellow/geometry` · Code: `modules/fellow_animation/fellow_traversal_3d.{h,cpp}` (`_find_jump()`'s up branch,
`_step_hang()`, `_step_transfer()`, `_hang_foot()`, `Ledge::out`, `_check()`'s lowest top, `_path()`) · Checks:
`bench/climb_check.gd` (`hold_climb`) on GodotPhysics3D and Jolt, the regression scenes `climb`, `ledge_corners` and
`traversal`, the showcase's bell tower.

## Problem

The owner, of the rooftops showcase's video: the character climbs "like Spider-Man" instead of looking for holds. It
was free climbing (decision 0276): any face of the buildings, at a steady speed, the hands put anywhere on the
plaster. Assassin's Creed's climbing goes from hold to hold — a sill, a hood, a string course, a beam — the body hanging
from one, braced on the wall, and reaching up to the next or leaping to one further up. FellowTraversal3D could already
hang from a ledge, shimmy and jump between ledges (milestones 75-95, 149), but:

* every ledge above was jumped to (`hang_jump()`, 0.45 s, the hands letting go), however close — a sill a metre up too;
* hanging, the feet were put 0.35 m up at the edge's face: in the air under a ledge that stands out (a sill, a
  corbel), not on the wall;
* a ledge was hung from only where the capsule hung free 8 cm off its edge: under the bell tower's floor, past the
  string course 0.2 m below it, no jump up was possible; nor from the course to a corbel above it that stands out less;
* pushing up from a hang to climb onto the ledge looked at the first face in front of the body: the course under the
  ledge the hands held ("no room on top"), never the floor above it;
* a controller learnt of a jump between ledges only at its end (`hang_transferred`): too late to play its clip.

## Change

* **`hang_reach_up`** (m, 0: off): a ledge up to this far over the edge (0.4 m at least, both hands on it, room for
  the body) is climbed to hand over hand (`TRANSFER_REACH_UP`, 0.75 s): pushing up or at the wall where nothing can be
  mantled (looked for again 0.2 s after a look in vain), or by `hang_jump()` up. The hand that followed last time
  leads, out from the wall on the way up (0-0.45 of the move); the body is pulled up after it (0.2-0.95) while the other
  hand follows (0.42-0.85); the feet step up the wall in turn, the one opposite the leading hand first, each off the
  wall as it steps. A ledge further up (to `hang_jump_height`) is still leapt to. A push held through a reach goes on
  climbing, over the top when the ledge reached is the last.
* **The body stands out from a ledge** (`Ledge::out`, in `hang_reach_up`'s mode): where it does not fit at its 8 cm,
  it hangs 0.08 m further out at a time, up to 0.32 m, and keeps that out along the ledge (shimmying) and over its top
  (the mantle's path does not go in until it is over the top: past the hold below).
* **Climbing onto what the hands hold**: pushing up from a hang in that mode, the check looks for tops from 0.25 m
  under the edge up, not at a ledge under it that stands out more.
* **`hang_brace_height`** (m, 0: off): the feet braced on the wall this high over the hanging body's feet, on the face
  found in front of the body at that height (a ray, each foot), the ball of the foot on it.
* **`hang_transfer_started(ledge, how, duration, lead_hand)`**: emitted when a move to another ledge starts.

## Checks

`climb_check.gd`, a new lane `hold_climb`: a 7.4 m wall with corbels (0.8 m wide, 0.22 m out) at 2.6, 3.7, 4.8 and
6.4 m; `hang_reach_up` 1.25 m, `hang_jump_height` 2.3 m, `hang_brace_height` 0.65 m; walking at the wall, a jump,
pushing at it, two calls of `hang_jump()`:

| | |
|---|---|
| caught | the corbel at 3.7 m, at 0.98 s |
| up to 4.8 m | hand over hand (`reach`), at 1.98 s |
| up to 6.4 m (1.6 m: out of reach) | a leap (`up`), at 3.87 s |
| up to the wall's top (7.4 m) | hand over hand, at 4.72 s, the body 0.16 m out past the corbel at 6.4 m |
| over the top | a high mantle, 4.73-5.92 s; standing on it at 7.40 m |

No frame of the capsule in the world (before the mantle kept the body out, 14 frames went through the corbel). Same on
Jolt. The other 28 lanes: the milestone 150 script with this build gives its results value for value (GodotPhysics3D);
the new script's ground is longer (to x 120 for the new lane), and two lanes (`climb_course`, `ladder_up`) finish
0.02 s sooner on it. The regression scenes `climb`, `ledge_corners` and `traversal`: exact.

The showcase's bell tower, its holds — two corbels in the rustication, four on each 6 m level (1.1 m apart), the string
courses, beams over putlogs higher up — climbed from a jump at its foot to its belfry's floor at 26.9 m: 18 reaches and
3 leaps (to each course, 1.6 m) in 15 s, then a high mantle into the arch; the body out by 0.16 m from the floor's edge
past the course 0.2 m under it.

## Decision

KEEP.

## Rejected / not done

* **Holds as nodes** (a hold placed by hand, AC's "grab points"): the collision's ledges are the holds — a sill or a
  corbel in the model is one — so a level needs no second set of data. A hold must still be 0.48 m wide (both hands)
  and 0.13 m deep.
* **A hand on each of two narrow holds** (a brick, a beam's end): later work, with holds for one hand.
* **Reaching sideways hand over hand**: along a ledge the body shimmies; to another ledge it leaps.
