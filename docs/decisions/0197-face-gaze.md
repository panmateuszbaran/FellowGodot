# 0197 — Eyes that look: gaze and saccades on a face's blend shapes

Status: **KEEP** (on `FellowFaceAnimator`: `gaze` on, through ARKit's `eyeLook*` shapes — a face without them is as
before) · Branch `fellow/geometry` · Code: `modules/fellow_animation/fellow_face_animator.{h,cpp}` · Checks:
`benchmarks/project/bench/face_check.gd` (a gaze lane; decision 0191's lanes unchanged), the regression scene `face`
(re-blessed: its faces now have pupils and look at a point).

## Problem

Decision 0191's faces spoke and blinked with eyes that stared ahead; its "Rejected" named gaze. A character in a
conversation looks at whom it speaks to, and living eyes are never still.

## Change

* **Eight gaze channels** (`CHANNEL_LOOK_OUT_LEFT` … `CHANNEL_LOOK_DOWN_RIGHT`, "left" the character's), on ARKit's
  `eyeLookOutLeft`, `eyeLookInLeft`, `eyeLookUpLeft`, `eyeLookDownLeft` and the same `Right` by default.
* **Where they look** (`gaze`, on): `look_target_path`'s direction from `eye_center` in the face mesh's space (the face
  looking along +Z, its left +X) gives a yaw and a pitch, clamped to `gaze_range_horizontal` (30°) and
  `gaze_range_vertical` (25°); at the range a channel is 1 (to the character's left: the left eye out, the right eye
  in). Eased with `gaze_time` (40 ms). `get_gaze_angles()`.
* **Saccades** (`saccades`, on): every 0.4-1.6 s a new small offset within `saccade_amplitude` (1.5°; vertically 0.6
  of it) round where the eyes look, from a random generator of their own (the blinks' intervals the same as before).

## Checks (cloud: the CPU, lavapipe)

`bench/face_check.gd`, the editor build and the release template the same; a test face with pupils on the eyeLook
shapes, `eye_center` between its eyes:

| target | expected | result |
|---|---|---|
| ahead | all gaze channels 0 | 0 |
| at the character's left (65° off) | the left eye fully out, the right fully in | 1.0 and 1.0 (clamped at 30°), the `eyeLookOutLeft` shape 1.0 |
| above (30° up) | both up fully | 1.0 (clamped at 25°) |
| right and below (−14.5°, −14.7°) | the left eye in, the right out, both down, in proportion | 0.48 in and out, 0.59 down |
| nothing, saccades on (10 s) | small movements, within 1.5° | 5 visible jumps (over 0.3° a frame), at most 1.08° off |

Decision 0191's lanes (the analysis, the level, the blinks, the face following a played voice) unchanged. A face's
frame with the gaze 0.37 µs (100 faces 0.037 ms; 0.24 µs before). The regression scene `face`: its faces now carry
pupils and look up and to the right of the camera; re-blessed, the same picture from the release template. The first
pupils sank behind the eye's white as they moved towards the face's middle (their depth from where they started); they
stand 2 cm in front of it.

## Decision

Keep: eyes that look at a target and never quite stand still, from the same node.

## Rejected / not done

* **Head and neck turning** towards the target beyond the eyes' range (a skeleton's bones), **vergence** (the eyes
  converging on a near target) and **eye bones**: blend shapes only, both eyes the same angles.

## Later

Milestone 94 (decision 0202): vergence (`eye_spacing`, 0 by default); this record's reads are unchanged.
