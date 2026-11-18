# 0281 — Free climbing past what stands out of the wall

Status: **KEEP** (in `free_climb`; nothing changes on a face with nothing standing out of it) · Branch
`fellow/geometry` · Code: `modules/fellow_animation/fellow_traversal_3d.{h,cpp}` (`_climb_clearance()`,
`_step_climb()`: the face in the way, the capsule's distance from the face) · Checks: `bench/climb_check.gd`
(`climb_course`, `climb_plinth`) on GodotPhysics3D and Jolt, the regression scenes `climb` and `ledge_corners`, the
showcase's bell tower.

## Problem

Decision 0276's free climbing moves a point on the face at the chest's height and stands the upright capsule 8 cm off
the face there; a move is taken only where the capsule is free. A real façade is not one plane: a string course
standing out 0.2 m, a cornice, a rusticated plinth the wall steps back from. Climbing towards a course, the ray along
the move hit its underside — a face in the way, not climbable (it faces down) — and the climb stopped under it; past a
plinth's top, the face behind it was found but the capsule, 8 cm off it, stood in the plinth — not free, no move. The
showcase's bell tower (a plinth 0.12 m deep to 4 m, courses 0.2 m deep every 6 m) was climbed to 2.8 m and no
higher; `climb_check.gd`'s new lanes stopped at 0.9 m (a course) and 0.78 m (a plinth).

## Change

* **An underside is not in the way**: the face found along the move is gone onto (an inside corner) or walked onto (a
  floor) as before; one that is neither (a course's or a cornice's underside) is passed by — the face is found again
  from in front at the new point as for any move, the course's front face when the point is at its height.
* **The capsule stands out as far as it must**: where it is not free at its gap, it is tried 0.08 m farther out at a
  time, up to 0.48 m (`_climb_clearance()`); the move is taken at the first place it is free. It stands out for what
  is 0.3 m ahead along the move too (its top reaches a course before the chest does), out at 3 m/s, back in at
  0.8 m/s once nothing stands out — and the feet's distance from the face is eased in the same way when the point steps
  onto a course's front and off it again (not round a corner or a curve, where the face's normal turns: there the feet
  follow it as before).

## Checks

`climb_check.gd` (the editor build), two new lanes on a row of their own:

| | before | after |
|---|---|---|
| `climb_course`: a 6 m wall, a course 0.2 m out (0.26 m tall) at 2.6 m | stuck under it at 0.9 m | up, mantled onto the top at 6.0 m |
| `climb_plinth`: a 5 m wall over a plinth 2 m tall, 0.15 m out | stuck at its top, 0.78 m | up, mantled onto the top at 5.0 m |

No frame of either capsule in the world. The other 26 lanes the same value for value (their JSON compared with the
build before); on Jolt all 28 pass. The regression scenes `climb` and `ledge_corners` exact.

The showcase's bell tower (its collision 40 convex pieces): up its front from the ground, past the plinth and four
courses (the capsule out 0.08 m and 0.16 m by them, back in between), into the belfry's arch at 26.9 m by a high
mantle — 16.8 s at 1.5 m/s. A climbing body's step there: 46 µs a physics frame (the capsule queries against the
tower's pieces); the bench's traversal steps over all lanes 2.8 µs → 3.5 µs on average (the two new lanes climbing and
mantling where they stood).

## Decision

KEEP.

## Rejected / not done

* **A capsule that bends** (the legs out over a course, the chest in): the pose already puts the feet on the face
  (`FellowTraversalPose3D`); the capsule only has to be free.
* **Climbing round an eave** (an overhang of a metre): out of reach of a 0.48 m step; the showcase's roofs are reached
  from terraces and the tower's belfry, or by a hang from the cornice under the eave (later work).
