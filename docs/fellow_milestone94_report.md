# FellowEngine — milestone 94 report (eyes that converge)

Status: **FINAL** (2026-11-12): the owner said "kontynuuj usprawnienia do poziomu AAA naszego silnika, pracuj
przynajmniej do 8 rano" (the GPU allowed). Branch `fellow/geometry`. Decision record:
`docs/decisions/0202-eye-vergence.md`.

## 0. Result

* **Vergence**: with `eye_spacing` the eyes of `FellowFaceAnimator`'s face converge on a near target (a hand, a face
  in a close-up) instead of staring past it.
* **Checked**: near and far targets read as expected, the earlier lanes unchanged, the release template the same, the
  face followed a played voice under Movie Maker as before, the regression scene `face` exact.

## 1. Development

`modules/fellow_animation/fellow_face_animator.{h,cpp}`, `doc_classes/FellowFaceAnimator.xml`;
`benchmarks/project/bench/face_check.gd`.

## 2. Verification

Decision 0202's table. The laptop: not run (not linked to the session).

## 3. Measurements

One more angle a frame a face; nothing measurable.

## 4. Next

Head and neck turning beyond the eyes' range; brows and emotions.
