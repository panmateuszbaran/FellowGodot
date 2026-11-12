# 0202 — Eyes that converge on what is near

Status: **KEEP** (on `FellowFaceAnimator`: `eye_spacing`, 0 by default — the eyes parallel, as before) · Branch
`fellow/geometry` · Code: `modules/fellow_animation/fellow_face_animator.{h,cpp}` · Checks:
`benchmarks/project/bench/face_check.gd` (the gaze lane's near and far targets; the rest unchanged).

## Problem

Decision 0197's "Rejected": vergence. Both eyes took the same angles: looking at a hand or a face half a metre away
they stared past it in parallel — the first thing a close-up cutscene shows.

## Change

`eye_spacing` (m): each eye turns in by half the angle the target subtends between them (atan of half the spacing over
the target's distance from `eye_center`), eased with the gaze (`gaze_time`); the left eye's yaw is the gaze's minus
it, the right eye's plus it, each clamped to the eyes' range. `get_vergence()`. 0: parallel.

## Checks (cloud: the CPU)

`bench/face_check.gd`, the editor build and the release template the same; the test face's eyes 0.34 m apart:

| target | expected | result |
|---|---|---|
| 0.6 m ahead | both eyes in by atan(0.17 / 0.6) = 15.8° (0.53 of the 30° range) | vergence 15.82°; in 0.527 and 0.527, out 0 |
| 10 m ahead | under 1° | 0.97°; in 0.032 each |

Decision 0197's reads (ahead, left, up, right and down, the saccades) and decision 0191's lanes: as before
(`eye_spacing` 0 there); the regression scene `face` exact.

## Decision

Keep: a game gives its face its eye spacing and the eyes converge.

## Rejected / not done

* **Each eye's own direction to an off-axis target** (rather than the gaze plus a symmetric turn): the difference is
  under a degree beyond arm's length.
