# 0290 — The body's weight: leaps that wind up and are caught, hips that swing, limbs that do not jump

Status: **KEEP** (opt-in: `FellowTraversal3D.hang_leap_windup`, `hang_clip`; `FellowTraversalPose3D.body_sway`,
`limb_smoothing`; nothing changes at their defaults) · Branch `fellow/geometry` · Code:
`modules/fellow_animation/fellow_traversal_3d.{h,cpp}` (`_start_transfer()`, `_step_transfer()`, `leap_curve()`,
`_start()`, `_mantle_clip()`, `PoseHints::body_offset`), `modules/fellow_animation/fellow_traversal_pose_3d.{h,cpp}`
(`_sway_step()`, `_pose()`); the showcase's clips (`tools/blender/mannequin_anims.py`), its tree and controller ·
Checks: `bench/climb_check.gd` (a new lane `leap_style`; the 36 others), the regression scenes `climb`, `ledge_corners`
and `traversal`, the showcase's demo filmed from beside the climber.

## Problem

The owner, of milestone 152's video: "the climbing animations are unrealistic, the transitions between them strange;
it should come closer to AAA". Frames of the climb up CasaCrema's front showed why:

* **A leap had no weight.** The body went from hold to hold on one smooth curve, from rest to rest, in 0.45 s: no
  wind-up before it, no speed when caught, nothing after. Its hands' IK fell to 15 % halfway (`1 − 0.85 sin(πu)`), so
  the arms dropped to the clip's in mid-air — hanging at the sides, or held out level — and came back to the ledge at
  the end. The feet's IK stayed at 0.3, half on the wall, half the clip's: the legs splayed.
* **The braced hang was a crouch.** `hang_brace_height` 0.65 m put the feet 0.25 m under the hips: the knees drawn up
  to the chest.
* **Up from a hang, the body stood up first.** The mantle from a hang played the high mantle's clip, made for a run
  at a wall: its first pose stands on the ground. Hanging became standing in a frame, and the standing body floated
  up the wall.
* **The jump at a wall** for its first hold flung the arms out to the sides (the jump's clip), not up at the hold.
* **Every change was a cut or a linear fade**: the IK targets jumped where the hints changed (a move's end, a hang's
  start), and the animation tree's cross-fades were linear.

## Change

`FellowTraversal3D`:

* **`hang_leap_windup`** (s, 0: off): a leap from a hold (up, along, down, diagonal, back, round a corner) first winds
  up this long — the capsule still, the body shown drawn in to the wall and up a little (a new pose hint,
  `body_offset`; the arms bend, the legs coil on the wall). Then it springs: its progress along the same path is
  `leap_curve()` — from rest, faster through the push off the wall (the first quarter), then on at a speed that ends at
  0.55 of the push's going up (gravity), 0.9 along, 1.35 going down — so it arrives with speed. The hands are thrown to
  the ledge ahead of the body on an arc out from the wall, the leading one first (going down, with the body; turning
  back, once it has turned), at full IK weight throughout. The feet stay on the wall through the push, leave it, and are
  braced again on the new wall over the last 28 %. The move is longer by the wind-up; `hang_transfer_started` reports
  the whole.
* **`hang_clip`**: the clip of a mantle from a hang (climbing up onto the ledge held), in place of the high or low
  mantle's; its root's motion scaled onto the ledge as theirs is.

`FellowTraversalPose3D`:

* **`body_sway`** (0-1, 0: off), with `sway_frequency` (1.6 Hz), `sway_damping` (0.35 of critical) and `sway_max`
  (0.15 m): the hips hang on a damped spring behind where the traversal shows the body (the skeleton's origin plus
  `body_offset`), driven by that point's acceleration — they keep `body_sway` of its speed when it changes, and swing
  back. Caught at the end of a leap, the hips go on up and fall back; started, they lag. The torso leans back against
  the swing (with `lean`); the hands and feet stay on their targets; and the hips go no lower than the arms reach
  (hanging, the body cannot swing below its arms: the part of the shift that takes the shoulders out of reach is given
  back).
* **`limb_smoothing`** (s, 0: off): the hands' and feet's targets and weights follow the traversal's with this time
  constant.

The showcase: the hang braced at 0.42 m (the feet under the hips) and its clips remade for it; the leaps wound up for
0.15 s with clips of their own (coiled, the legs driving, together in the air, swung in to the wall when caught),
scaled by the controller to each move's time (a time-scale node each); a clip for climbing up from a hang (pulled up,
the feet walking up the wall, the hands turning to press down, a knee onto the top, standing up); a jump at a wall with
the arms reaching up for the hold; the tree's cross-fades eased in and out; `body_sway` 1 (1.7 Hz, 0.32, 0.16 m),
`limb_smoothing` 0.05 s.

## Checks

`climb_check.gd`, a new lane `leap_style`: `hold_climb`'s wall and program with `hang_leap_windup` 0.15 s,
`body_sway` 1 and `hang_brace_height` 0.42 m — caught at 0.98 s, a reach (1.98 s), the leap (caught at 4.02 s against
3.87 s: its wind-up), a reach (4.77 s), over the top at 7.40 m; no frame of the capsule in the wall; the hips swung
15 cm by its catch (`sway_max`). The other 36 lanes: value for value (the 29 of milestone 151 against its results, the 7 of milestone
152 against decisions 0285-0289's); on Jolt too. The regression scenes `climb`, `ledge_corners` and `traversal`:
exact (the new settings off).

## Decision

KEEP (opt-in).

## Rejected / not done

* **A physical ragdoll** for the hanging body (bones on joints, driven to the pose): the spring on the hips gives the
  swing for a few operations a frame; a ragdoll's contacts with the wall would need the wall's shape at the bones.
* **Motion matching for the climb** (decision 0182's matcher over climbing clips): needs a library of captured
  climbing, not keyed clips.
