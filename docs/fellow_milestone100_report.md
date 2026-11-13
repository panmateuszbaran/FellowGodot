# FellowEngine — milestone 100 report (faces with expressions)

Status: **FINAL** (2026-11-13): the owner said "ok kontynuuj". Branch `fellow/geometry`. Decision record:
`docs/decisions/0208-face-expressions.md`.

## 0. Result

* **Expressions** (`FellowFaceAnimator.set_expression()`): happy, sad, angry, surprised, afraid and disgusted built in
  (after FACS's action units), the game's own (`expressions`), faded in and out and layered on top of the lip sync,
  the blinks and the gaze — seventeen channels more (the brows, the mouth's corners, the cheeks, the eyes' squint and
  widening, the nose) on ARKit's shapes.
* **Brows that follow the voice** (`speech_brows`): raised on each stressed syllable (the level rising over its last
  half second's), the signal `emphasized` for the game's head nods and gestures.
* **Checked**: two lanes (expressions faded, crossfaded, a custom one, cleared; six raises on the six sounds of the
  test voice), the earlier lanes and the regression scene `face` unchanged.

## 1. Development

`modules/fellow_animation/fellow_face_animator.{h,cpp}`, `doc_classes/FellowFaceAnimator.xml`;
`benchmarks/project/bench/face_check.gd` (two lanes).

## 2. Verification

* Decision 0208's table: the editor build and the release template the same; under Movie Maker the mouth as before.
* The regression scene `face`: exact against its golden, the release template against the editor.
* The full regression suite (35 scenes) exact on milestone 99's build (this one touches only the face node).
* The laptop (job f401, a release template, MSVC): both lanes as on the cloud (the six raises at the same times);
  under Movie Maker the mouth as before.

## 3. Measurements

A face's frame 0.37-0.43 µs on the cloud (0.37-0.39 before); on the laptop 0.41-0.44 µs (0.28 at milestone 96: the
seventeen channels more written, the level read for the brows) — 100 faces 0.04 ms a frame.

## 4. Next

The head's motion with the voice (nods on the emphases, a sway), with the gaze's target beyond the eyes' range.
