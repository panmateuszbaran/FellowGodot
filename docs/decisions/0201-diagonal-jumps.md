# 0201 — Diagonal jumps between ledges

Status: **KEEP** (on `FellowTraversal3D`: `hang_jump()` with the wish both along and up or down) · Branch
`fellow/geometry` · Code: `modules/fellow_animation/fellow_traversal_3d.{h,cpp}` · Checks:
`benchmarks/project/bench/climb_check.gd` (fourteen lanes: the thirteen earlier unchanged, one new),
`bench/traversal_check.gd`.

## Problem

Decision 0194's "Rejected": diagonal jumps. Ledges on a façade are seldom in a row or a column: the next one is often
across a gap and higher or lower.

## Change

`hang_jump()` with the wish along the edge and up or down at once (each part over 0.35): past this edge's end, in 0.1 m
steps out to `hang_jump_distance` and at each 0.1 m steps up to `hang_jump_height` (or down to `hang_jump_depth`) from
0.4 m, the nearest edge found (decision 0183's probe at that height), 0.4 m higher or lower at least; hung from 0.3 m
into it (its edge found there and 0.25 m further at the same height), room for the body there and at the curve's
control point (half way, 0.35 m up). The body flies there in 0.55 s (`TRANSFER_JUMP_DIAGONAL`), its hands letting go
and catching the new edge. A wish mostly along, up or down takes decision 0189's and 0194's jumps as before.

## Checks (cloud: the CPU, lavapipe)

`bench/climb_check.gd`, GodotPhysics3D and Jolt alike, the editor build and the release template the same:

| lane | expected | result |
|---|---|---|
| a 2.5 m ledge and, 0.8 m to its left, one 3.4 m high: hung from the right one, to its end, `hang_jump()` with the wish left and up | across and up, hanging from the left ledge | the jump at 3.22 s, hanging from 3.4 m at 3.77 s, 0.39 m into the ledge; no frame of the capsule in the world |

The thirteen earlier lanes and decision 0180's course: as before.

## Decision

Keep: with decisions 0189, 0192 and 0194 a hanging climber goes to the next ledge in any of the wall's directions.

## Rejected / not done

* **Jumps to ledges on other walls** (round a corner in the air) and **wall jumps** (pushing off a wall to a ledge
  behind).

## Later

Milestone 95 (decision 0203): jumps back to the wall behind; this record's lane is unchanged.
