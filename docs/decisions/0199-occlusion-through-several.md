# 0199 — Through several things: occlusion added up along a path

Status: **KEEP** (only paths through colliders with `fellow_occlusion` under 1 are traced further; a plain wall ends a
path as before) · Branch `fellow/geometry` · Code: `modules/fellow_world/fellow_acoustics_3d.cpp` · Checks:
`benchmarks/project/bench/acoustics_check.gd` (two more lanes; the others unchanged).

## Problem

Decision 0198's "Rejected": the first collider on a path decided. Two panes of glass muffled a sound as one did, and a
pane in front of a wall let the sound through the wall as if it were glass.

## Change

Each path from the listener to a source is traced through up to three colliders: each adds its `fellow_occlusion` (1
without it) to the path's block, the ray continuing from where it hit with that collider excluded, until the block is
full or nothing more is hit; the path counts the sum, at most 1. A plain wall (1) ends the path at once — no ray more
than before; a partial one costs a ray more (counted in the stats' rays). A collider is excluded whole: two walls of
one body count once.

## Checks (cloud: the CPU, lavapipe)

`bench/acoustics_check.gd`, GodotPhysics3D and Jolt alike, the release template the same:

| source (12 m away) | expected | occlusion |
|---|---|---|
| behind one pane (0.3), north-east (decision 0198) | 0.3 | 0.30 |
| behind two panes (at 8 and 10 m), south-west | 0.6 | 0.60 |
| behind a pane and a wall (at 9 and 10.5 m), 20° west of south | blocked | 1.00 |

Decision 0186's and 0198's lanes: the same values, and the listened mix (`--listen=1`) the same; the open field's and
the closed room's readings differ in the fourth decimal (the new walls are within the reverb's rays at the start, where
the readings begin easing from).

## Decision

Keep: what lies on a path adds up.

## Rejected / not done

* **More than three colliders on a path**, and walls of one body counted apart.

## Later

Milestone 92 (decision 0200): the highs blocked apart from the level; this record's lanes are unchanged.
