# 0212 — The head moving with what the face says

Status: **KEEP** (`FellowHeadMotion3D`, a skeleton modifier: nothing moves unless a skeleton has one) · Branch
`fellow/geometry` · Code: `modules/fellow_animation/fellow_head_motion_3d.{h,cpp}`, `fellow_face_animator.{h,cpp}`
(three getters) · Checks: `benchmarks/project/bench/face_check.gd` (three lanes; `--heads=1` with `--many`).

## Problem

Decisions 0191-0208 move the face — the mouth from the voice, blinks, the eyes, expressions, the brows on stressed
syllables — while the head stays where the body animation put it. A speaking head that never moves reads as a mask:
people nod on stressed syllables and sway while they talk (the beat gestures procedural dialogue systems add: JALI's,
the head layers of Audio2Face and of conversation tools). Decision 0208 left the `emphasized` signal "for the game to
nod the head"; the readiness assessment listed "no head motion".

## Change

`FellowHeadMotion3D`, a `SkeletonModifier3D` (after the animation, before the skin; the skeleton restores the
animation's pose after drawing), following a `FellowFaceAnimator`:

* **Nods**: at each stressed syllable (the face's new `get_emphasis_count()`, read each frame) a pulse of pitch,
  `nod_degrees` (6°) × its strength at `nod_time` (0.12 s) after it — x e^(1−x), x the time over `nod_time`: down
  smoothly, back in about half a second; overlapping nods add. `nod()` for the game's own.
* **Sway**: pitch, yaw and roll each two sines at frequencies that never repeat together (about `sway_frequency`,
  0.3 Hz; phases from `seed`), `idle_sway_degrees` (0.5°) at rest plus `speech_sway_degrees` (2°) × the face's speech
  activity (its new `get_speech_activity()`: the voice's level between the floor and full, eased over 0.25 s).
* The rotation in the skeleton's space (the face along `forward`, +Z by default; up +Y), the neck taking `neck_share`
  (0.4) and the head the rest. Every amount 0: the bones untouched.

## Checks (cloud: the CPU)

`face_check.gd` headless, the editor build and the release template the same; three heads (a neck and a head bone)
following the face that reads the test voice from a timeline (six sounds after silences, decision 0208):

| lane | expected | result |
|---|---|---|
| nods only (no sway) | six nods, one at each stressed syllable, 6° deep, the lowest point 0.12 s (7.2 frames) after it; the bones turned as `get_head_angles()` says; still before the voice and back after it | six: 6.00°, then 6.25° (the last nod's tail adds 0.25°), each 7.0 frames after its syllable; bones against angles 0.0000°; 0° before, 0.000° after |
| sway | at rest within 0.5°, more while the voice speaks (up to 2.5°) | 0.499° at rest, 2.06° speaking |
| every amount 0 | the bones exactly the animation's (here the rest), 73 frames sampled | exact |

The earlier lanes (the analysis, the level, the blinks, the gaze, expressions, the brows) the same; the editor check
(36 icons); the regression scenes `face` and `cape` exact.

## Cost

100 heads: 0.38 µs a head a frame (0.04 ms a frame, the release template on the cloud), the modifier's own time.

## Decision

Keep: a head that nods where the voice stresses and sways while it talks.

## Rejected / not done

* **The head turning towards what the eyes look at** (eye-head coordination): the face's gaze is measured from the
  face mesh, which a skinned face does not move with the head bone — the eyes would turn twice. Left to the
  animation or a `LookAtModifier3D`.
* **Gestures** (the hands' beats on the same syllables): the `emphasized` signal is there for the game.
* **Prosody-driven head motion** (pitch contours, questions rising): the voice's level and its onsets only.
