# FellowEngine — milestone 89 report (eyes that look)

Status: **FINAL** (2026-11-12): the owner said "kontynuuj usprawnienia do poziomu AAA naszego silnika, pracuj
przynajmniej do 8 rano" (the GPU allowed). Branch `fellow/geometry`. Decision record:
`docs/decisions/0197-face-gaze.md`.

## 0. Result

* **Gaze**: `FellowFaceAnimator`'s eyes look at a target (ARKit's eyeLook shapes), within their range, and move in
  small saccades — milestone 83's speaking, blinking faces now look at whom they speak to.
* **Checked**: a target ahead, left, above, right and below read as expected, saccades within 1.5°, decision 0191's
  lanes unchanged, the editor build and the release template the same; the regression scene `face` re-blessed (the
  test faces have pupils now and look at a point), the same from the release template.

## 1. Development

`modules/fellow_animation/fellow_face_animator.{h,cpp}`, `doc_classes/FellowFaceAnimator.xml`;
`benchmarks/project/bench/face_check.gd` (a gaze lane, pupils on the test face);
`tools/fellow_regression/project/scripts/face.gd`, `goldens/base/face.png`.

## 2. Verification

Decision 0197's table; the laptop (job f396, milestone 97): as expected, exactly the cloud's results.

## 3. Measurements

A face's frame 0.37 µs with the gaze (100 faces 0.037 ms a frame).

## 4. Next

Head and neck turning beyond the eyes' range, vergence, brows and emotions.
