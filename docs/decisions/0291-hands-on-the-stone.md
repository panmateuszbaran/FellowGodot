# 0291 — The hands on the stone: grips, the body fitted to them; the climber's back no longer to the wall

Status: **KEEP** (opt-in: `FellowTraversalPose3D.hang_fit`; the hints' `hand_grips` change no pose by themselves) ·
Branch `fellow/geometry` · Code: `modules/fellow_animation/fellow_traversal_3d.{h,cpp}` (`PoseHints::hand_grip`,
`_step_hang()`, `_step_transfer()`), `modules/fellow_animation/fellow_traversal_pose_3d.{h,cpp}` (`_pose()`); the
showcase's hanging clips (`tools/blender/mannequin_anims.py`), its demo (`scripts/rooftops_demo.gd`) · Checks:
`bench/climb_check.gd` (`leap_style`: the gripping hands' distance to their holds), the other 36 lanes, the showcase's
demo probed bone by bone and filmed.

## Problem

The owner, of milestone 153's first video: "why does the character jump with its back to the wall? why are its arms
always stretched out, floating? it does not look realistic at all." Both were so, measured on the demo's climb up
CasaCrema (the skeleton's bones read after the modifiers, `skeleton_updated`):

* **Its back to the wall.** Running at the house, the demo steered the body at a point 1.2 m out from the front and
  then on into it. The sprint carried it past that point; the next wish pointed back at it, and the body, in the air,
  turned 160° to it (`rotation.y` 0 → 2.79 rad over 0.7 s): it jumped, and was caught, with its back to the wall, then
  turned round on the hold.
* **The hands short of the holds.** Hanging, every hand was 0.2-0.35 m from its hold: the shoulders 0.62-0.77 m from
  the edge, the mannequin's arm 0.54 m. The hanging clips leaned the torso back from the wall (the shoulders out), and
  the traversal hangs the capsule 2.05 m under the edge; the two-bone IK can only point a hand at a target out of reach.
  The arms stood straight up, the hands in the air before the stone.

## Change

* **`hand_grips`** in the traversal's pose hints (`get_pose_targets()`): 1 where a hand is on its hold — hanging, at a
  move's ends, on the near end of a reach across — 0 while it moves to the next.
* **`FellowTraversalPose3D.hang_fit`** (off by default): hanging or moving from hold to hold, the body is moved (the
  hips; up to 0.75 m, eased over 0.1 s) until each shoulder is within 97 % of its arm's reach of the hold its hand
  grips. A hand that takes a hold pulls the body up to it; a hand moving to the next reaches for it as far as the arm
  goes. A clip from a hang (decision 0290's `hang_clip`) starts where the fitted body was, the move eased out over
  0.2 s.
* The showcase: the hanging clips lean the torso in to the wall (18°, the head kept up at the hold); the leap's hands
  move with the body (a little ahead) instead of reaching the ledge long before it; the jump at a wall swings the arms
  back, forward, then up as the hold comes; `hang_fit` on; the demo steers straight at the wall once past its point.

## Checks

* **The demo, probed** (every 0.1 s, the bones after the modifiers): the body's facing constant through the run, the
  jump and the catch (before: turned 160° in the air). The gripping hands within 0-8 cm of their holds (before:
  20-35 cm), a hand that has just taken a higher hold 30-60 cm short for 0.1-0.2 s while the body is pulled up to it;
  the body moved 0.1-0.35 m.
* **`climb_check.gd`**: `leap_style` with `hang_fit` — the gripping hands within 1.6 cm of their holds from 0.3 s after
  they take them (216 samples), the events as before, never in the wall. The other 36 lanes: value for value (no
  `hang_fit`).

## Decision

KEEP (opt-in).

## Rejected / not done

* **Hanging the capsule higher** (`hang_reach` 1.9 m for the mannequin): the capsule's place decides the ledges' checks
  (room for the body, the next holds within reach); the body's place on the hands is the pose's.
* **Fingers on the stone** (a grip per hold's shape): next.
