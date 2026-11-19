# 0289 — Over a parapet from a hang

Status: **KEEP** (in `hang_reach_up`'s mode, from a hang; nothing changes without it) · Branch `fellow/geometry` · Code:
`modules/fellow_animation/fellow_traversal_3d.{h,cpp}` (`_check_ledge()`, `Ledge::drop`, `_path()` / `_path_mantle()`,
`_start()`, `_update_hints()`) · Checks: `bench/climb_check.gd` (`parapet`) on GodotPhysics3D and Jolt, the regression
scenes `climb`, `ledge_corners` and `traversal`, the showcase's terraces.

## Problem

The showcase's CasaRosa and CasaBianca end in roof terraces behind a parapet. Climbed from hold to hold (decision 0288),
the player reached the parapet's top and hung there: a mantle needs room on the top at the top's height, and behind a
parapet 0.3 m thick the terrace is 0.9 m lower ("no room on top"). Assassin's Creed climbs over such a wall onto what is
behind it.

## Change

From a hang, climbing from hold to hold (the check from the hang, `p_min_height` given):

* where the top has no room, its far face is looked for within 0.6 m (a ray back from 0.65 m in, 6 cm under the top);
  beyond it, past the body's radius and 0.12 m, a floor 0.3-1.6 m under the top with room for the body standing there:
  the mantle goes there (`KIND_HIGH`, `Ledge::drop` the floor's depth under the top);
* over the parapet, room for the body bent going over (a capsule 1.3 m tall: CasaRosa's altana roofs its terrace 1.7 m
  over the parapet);
* the move: the mantle (its clip and pose) onto the top's middle over the first 74 % of a move made longer by as much,
  then on over the far side and down onto the floor with a 0.1 m hop;
* a push held through a leap up goes on climbing, as through a reach (the player free-running up a front holds the
  push: it had to be let go of after each leap to climb over the last hold).

## Checks

`climb_check.gd`, a new lane `parapet` (`hang_reach_up` 1.25 m, `hang_brace_height` 0.65 m): a house 3.2 m high with a
parapet 0.3 m thick and 0.9 m high along its front, a corbel at 3.0 m. A jump at the wall, then pushing up: caught at
0.82 s, up to the parapet's top hand over hand (1.82 s), over it 1.83-3.43 s, standing on the terrace at 3.20 m, 0.77 m
behind the front. No frame of the capsule in the world; the same on Jolt. The other 35 lanes: value for value; the
regression scenes: exact; the showcase's demo: the same path.

The showcase, the player sprinting at a front and pushing on: CasaBianca and CasaRosa from the street over their
parapets onto their terraces (11.2 m) in 6 s — impost band, rustication, sills, string course, hood, the parapet's top,
over it. Before: hanging from the parapet's top.

## Decision

KEEP.

## Rejected / not done

* **A clip of its own** (a leg swung over the parapet): the high mantle's clip, then a hop down.
* **Over from standing** (a running vault over a parapet onto a lower floor): the vault already goes over thin
  obstacles to a landing within `vault_max_height`.
