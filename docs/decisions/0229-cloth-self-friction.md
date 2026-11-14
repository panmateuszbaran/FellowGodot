# 0229 — Friction between a cloth's layers

Status: **KEEP** (`FellowCloth3D.self_collision_friction`, 0 by default) · Branch `fellow/geometry` · Code:
`modules/fellow_fluid/fellow_cloth_3d.{h,cpp}` (`_substep()`) · Checks: `benchmarks/project/bench/cloth_check.gd` (lanes
`self_slope`, `self_slope_friction`).

## Problem

Decisions 0221 and 0228 keep a cloth's layers apart but let them slide on each other freely: folds slip off folds, and
a heap on a slope slides down even when the cloth's friction with the slope holds its lowest layer — the upper layers
slide over it and pull it along by the fabric.

## Change

`self_collision_friction` (0-1, 0): in each substep, after a particle pair (decision 0221) or a particle and a triangle
(decision 0228) are pushed apart, that share of their sliding along each other in the substep (their displacements'
difference, less its part along the contact's normal) is taken away, by the same weights as the push (the particles'
inverse masses, and the triangle's corners' weights at the projection). 1: they do not slide where they touch.

## Checks (cloud: the editor build and the release template the same, timings aside)

The sheet of decision 0221 let go over a 25° slope (its friction with colliders 1; still air; self-collision against the
triangles at 2.8 cm). It falls in a heap on the slope. Over the last half second (3.5-4 s):

| | the heap's motion downhill | its parts against each other | its top | its footprint |
|---|---:|---:|---:|---:|
| `self_slope` (no friction between layers) | 34.4 cm/s | 63.4 cm/s | 1.32 m | 3.69 m² |
| `self_slope_friction` (1) | −0.4 cm/s | 0.8 cm/s | 1.63 m | 1.24 m² |

Without it the heap spreads down the slope; with it, it stays where it fell (lavapipe's pictures). In a scratch scene
over 8 s, friction 1 held such a heap on the slope; 0, 0.3 and 0.6 slid down within 3-6 s (and with decision 0221's
particles alone, 0 and 0.6) — contacts come and go between substeps, so friction below 1 lets a heavy heap creep. No cost of its own (64 flags: 60.4 against 60.7 ms in
the editor build).

## Decision

Keep, 0 by default (decisions 0221 and 0228 unchanged): folds that hold on each other where a scene needs it.

## Rejected / not done

* **Static and kinetic friction apart** (Coulomb's cone: sliding only past a share of the push): the push between layers
  is a position correction in a substep, not a force; a share of the sliding taken away per substep is its stable form.
