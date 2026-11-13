# FellowEngine — milestone 104 report (the head moving with what the face says)

Status: **IN PROGRESS** (2026-11-13): the owner said "ok kontynuuj". Branch `fellow/geometry`. Decision record:
`docs/decisions/0212-head-motion.md`.

## 0. Result

* **`FellowHeadMotion3D`**: a skeleton modifier that nods the head at each stressed syllable a `FellowFaceAnimator`
  hears (6°, its lowest point 0.12 s later) and sways it (0.5° at rest, up to 2.5° while speaking), neck and head
  sharing the turn, over the character's animation.
* **Checked**: three lanes (six nods at the test voice's six sounds, the bones turned as the angles say; the sway; the
  bones untouched at every amount 0); 0.38 µs a head a frame.

## 1. Development

`modules/fellow_animation/fellow_head_motion_3d.{h,cpp}` (new), `fellow_face_animator.{h,cpp}` (the emphases' count
and strength, the speech activity), `register_types.cpp`, `config.py`, `icons/FellowHeadMotion3D.svg`, the class
reference; `benchmarks/project/bench/face_check.gd` (the lanes, `--heads=1`); the editor check's icon list.

## 2. Verification

* Decision 0212's table: the editor build and the release template the same; the earlier lanes the same.
* The editor check: 36 icons, no warning; the regression scenes `face` and `cape` exact.
* The laptop: next job.

## 3. Measurements

100 heads: 0.38 µs a head a frame (0.04 ms a frame; the cloud, the release template).

## 4. Next

The head turning with the eyes' target (needs the gaze measured from the head bone).
