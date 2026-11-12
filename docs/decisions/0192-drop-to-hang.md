# 0192 — Over an edge into a hang: walking off a platform, hanging from its edge

Status: **KEEP** (on `FellowTraversal3D`: `drop_to_hang`, off by default; `try_drop()` called by the controller) ·
Branch `fellow/geometry` · Code: `modules/fellow_animation/fellow_traversal_3d.{h,cpp}` · Checks:
`benchmarks/project/bench/climb_check.gd` (twelve lanes: the ten of decisions 0183 and 0189 unchanged, two new),
`bench/traversal_check.gd` (decision 0180's course, unchanged), the regression scenes `climb`, `ledge_corners`,
`traversal`.

## Problem

Decision 0189's "Rejected" named drops to ledges below. In a climbing game the way down from a roof or a ledge is to
walk to its edge and hang from it — then shimmy along it, drop to the ground or to a ledge below, or climb back up —
instead of falling off it.

## Change

* **The edge found** (`_find_drop`): no floor a step ahead (the capsule's radius and 0.2 m, from 0.3 m over the feet to
  a metre under them; a step down less than that is walked); looking back from there 0.12 m under the floor's top, its
  side; the ledge there facing back into it (decision 0183's probe), at the feet's height, both hands on it (the edge
  found 0.24 m either way); room for the body hanging from it and standing just over it outside the wall.
* **The drop** (`TRANSFER_DROP`, 0.8 s, decision 0189's transfer): out over the edge (the first 40 %), then straight
  down to where hanging puts it, turning to face the wall; the hands reaching the edge in the first half, the feet
  finding the wall in the second. `hang_started` at once, `hang_transferred` when it hangs.
* **When**: `try_drop(direction)` (the controller's "drop" button), or with `drop_to_hang` walking over the edge — a
  `CharacterBody3D` on the floor, the intent towards it, no faster than `drop_max_speed` (2 m/s; faster, it goes over
  and falls). After a ladder (a ladder at the edge is climbed down).
* **The wish that walked it over**: it points away from the wall and would let go at once; held as it was it does
  nothing (decision 0189's wish held round a corner, here to nothing); changed, it moves the body as when hanging.

## Checks (cloud: the CPU, lavapipe)

`bench/climb_check.gd`, the editor build, a capsule 0.35 × 1.8 m with a test mannequin; GodotPhysics3D and Jolt alike,
no frame of the capsule in the world:

| lane | expected | result |
|---|---|---|
| on a 3.5 m platform walking at 1.5 m/s over its edge, then left, then down | hangs from the edge facing back, along it, lets go, lands in front of the wall | the drop at 0.63 s, hanging at 1.43 s, 0.9 m along the edge, let go at 4.02 s, on the ground 0.43 m from the wall |
| the same at 4.5 m/s | over the edge and down | no hang; on the ground 5.25 m beyond the edge |

The ten lanes of decisions 0183 and 0189 (`drop_to_hang` off): the same numbers as before; decision 0180's course: as
before. The release template: the same lanes. Lavapipe: the lanes filmed (on the platform, hanging from its front,
along it).

## Decision

Keep, off by default (a game turns it on for its controller, or calls `try_drop()`): walking off an edge into a hang,
with decision 0189's corners and jumps, gives a climber a way down.

## Rejected / not done

* **Dropping from a hang to a ledge below** (letting go and catching the next edge down): letting go falls.
* **Turning round on the spot at the edge** (a clip): the body turns as it goes over, procedurally.
* **Edges of moving platforms** while dropping: the target is where it was when the drop began.
