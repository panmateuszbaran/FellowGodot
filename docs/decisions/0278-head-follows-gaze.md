# 0278 — The head turned towards what the face looks at

Status: **KEEP** (opt-in: `FellowHeadMotion3D.follow_gaze`, off) · Branch `fellow/geometry` · Code:
`modules/fellow_animation/fellow_head_motion_3d.{h,cpp}` (`follow_gaze`, `gaze_dead_zone`, `gaze_follow`,
`max_head_yaw`, `max_head_pitch`, `gaze_follow_time`, `get_gaze_turn()`), `fellow_face_animator.{h,cpp}`
(`set_head_turn()`, `get_head_turn()`) · Checks: `benchmarks/project/bench/face_check.gd` (`head_gaze`), the
regression scene `face`.

## Problem

`FellowFaceAnimator`'s gaze (decision 0197) turns the eyes towards a look target, up to their range (30° to the side,
25° up and down), and stops there: a character looking at someone beside it stares from the corners of its eyes with
its head straight ahead. `FellowHeadMotion3D` (decision 0212) nods and sways the head but does not turn it towards what
the eyes look at. People turn the head for anything much off centre — the eyes lead, the head follows and takes most of
a large turn. The readiness assessment listed it: "the head nods on stressed syllables and sways while it speaks, not
turning with the eyes".

## Change

With `follow_gaze`, each frame the head motion takes its face's look target (when the face's gaze is on):

* **The angles to it** from the head bone (the animation's pose), in the skeleton's space against `forward`: the yaw to
  the left and the pitch down.
* **The head's share**: past `gaze_dead_zone` (10°) — within it the eyes turn alone — `gaze_follow` (0.7) of the rest,
  at most `max_head_yaw` (70°) and `max_head_pitch` (35°); eased over `gaze_follow_time` (0.3 s); added to the nods and
  the sway, shared between the neck and the head as they are.
* **The eyes the rest**: the face is told the head's turn (`set_head_turn()`) and takes it off its eyes' angles to the
  target before their range — for a face mesh skinned to the skeleton, which does not move with the head's bone.

Off by default: a scene's heads as before.

## Checks (cloud)

`face_check.gd`'s `head_gaze`: two heads (the head bone 1.62 m up), their faces' look target 3 m from it, read two
seconds after it moves; one with `follow_gaze`, one without:

| the target | the head (its bone's facing) | the eyes | without follow_gaze |
|---|---|---|---|
| 60° to the left | 34.89° (34.89°) | 25.13° | the head 0°, the eyes 30° (their range) |
| 5° to the left | 0.04° (easing back from 35) | 4.95° | 0°, 5.00° |
| 30° up | 13.98° up | 16.02° up | 0°, 25° (their range) |
| 150° round to the left | 69.91° (its 70° at most) | 30° (their range) | 0°, 30° |

Every earlier lane of `face_check.gd` the same; the regression scene `face` exact.

## Decision

KEEP, opt-in: a character's head turns towards what it looks at, its eyes leading.

## Rejected / not done

* **The eyes first, then the head** (a delay before the head moves, as people's saccade-then-turn): the ease starts the
  head at once, slower than the eyes (0.3 s against the gaze's 0.04 s); a delay is a later refinement.
* **The torso turning past the head's limit**: the body's animation's work (a look-at layer in the AnimationTree).
