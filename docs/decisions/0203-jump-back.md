# 0203 — A jump back to the wall behind

Status: **KEEP** (on `FellowTraversal3D`: `hang_jump()` with the wish away from the wall; without a ledge behind, the
wish away lets go as before) · Branch `fellow/geometry` · Code: `modules/fellow_animation/fellow_traversal_3d.{h,cpp}`
· Checks: `benchmarks/project/bench/climb_check.gd` (fifteen lanes: the fourteen earlier unchanged, one new),
`bench/traversal_check.gd`.

## Problem

Decision 0201's "Rejected": wall jumps. In a narrow alley or a shaft a climber hanging on one wall leaps back to a
ledge on the wall behind.

## Change

`hang_jump()` with the wish away from the wall (more than 0.5, little along or up): a ray from the body's back at the
hands' height, out to `hang_jump_distance` behind it, finds a face turned towards this wall; the ledge there (decision
0183's probe, facing back) at this edge's height within 0.3 m, both hands on it, room for the body hanging from it
and half way (the curve 0.35 m up). The body flies there in 0.6 s (`TRANSFER_JUMP_BACK`), turning round, its hands
letting go and catching the new edge. None: the wish away lets go, as before.

## Checks (cloud: the CPU)

`bench/climb_check.gd`, GodotPhysics3D and Jolt alike, the editor build and the release template the same:

| lane | expected | result |
|---|---|---|
| two 2.5 m ledges facing each other 2.4 m apart: hung from one, `hang_jump()` with the wish away | caught on the other, turned round | the jump at 2.5 s, hanging from the other at 3.1 s, 0.43 m off its face, turned 180°; no frame of the capsule in the world |

The fourteen earlier lanes and decision 0180's course: as before.

## Decision

Keep: a climber crosses between facing walls.

## Rejected / not done

* **Jumps round a corner in the air** (to a ledge on a wall at an angle) and **kicking off a bare wall**.
