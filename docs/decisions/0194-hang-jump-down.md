# 0194 — Down from a hang to a ledge below

Status: **KEEP** (on `FellowTraversal3D`: `hang_jump()` with the wish down; `hang_jump_depth`) · Branch
`fellow/geometry` · Code: `modules/fellow_animation/fellow_traversal_3d.{h,cpp}` · Checks:
`benchmarks/project/bench/climb_check.gd` (thirteen lanes: the twelve of decisions 0183, 0189 and 0192 unchanged, one
new), `bench/traversal_check.gd`, the regression scenes `climb`, `ledge_corners`, `traversal`.

## Problem

Decision 0192's "Rejected": dropping from a hang to a ledge below. Down a façade a climber goes from ledge to ledge:
a hang, a drop, caught on the next one.

## Change

* `hang_jump()` with the wish down (its height under −0.5, as letting go): a ledge under the edge, within
  `hang_jump_depth` (2.6 m), found as decision 0189's jump up looks above (the probe at each 0.1 m under the edge, the
  new edge 0.4 m lower at least, both hands on it); room for the body hanging from it, at the curve's control point and
  half way along it. The body goes out to the lower ledge's face (and 0.15 m beyond) first and then down — a ledge
  that stands out from the wall under the body is passed — in 0.4 s (`TRANSFER_JUMP_DOWN`), its hands letting go and
  catching the lower edge. None found: the wish down lets go, as before.

## Checks (cloud: the CPU, lavapipe)

`bench/climb_check.gd`, GodotPhysics3D and Jolt alike, the editor build and the release template the same:

| lane | expected | result |
|---|---|---|
| on a 6 m platform with a ledge at 3.6 m standing 0.3 m out of its face: over the edge into a hang (decision 0192), then `hang_jump()` with the wish down | caught on the lower ledge, hanging from it | the drop into the hang at 0.63-1.43 s; down at 2.52 s, hanging from 3.6 m at 2.92 s (the feet 0.43 m off the lower ledge's face); no frame of the capsule in the world |

The first path (decision 0189's curve for a jump up: its control half way, 0.15 m out) went through the lower ledge's
top: it stands 0.3 m out under the body, and the capsule came down past its top before moving out (two frames in it);
the body now moves out first. The other twelve lanes: the same numbers as before. Lavapipe: the lane filmed.

## Decision

Keep: with decisions 0189 and 0192 a climber goes along, round, across, up and down ledges.

## Rejected / not done

* **Diagonal jumps** (along and down at once) and **jumps to ledges on other walls** (across a corner).
