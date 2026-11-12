# FellowEngine — milestone 83 report (faces that speak and blink)

Status: **FINAL** (2026-11-12): the owner said "kontynuuj usprawnienia do poziomu AAA naszego silnika, pracuj
przynajmniej do 8 rano" (the GPU allowed). Branch `fellow/geometry`. Decision record:
`docs/decisions/0191-face-animation.md`.

## 0. Result

* **`FellowFaceAnimator`**: a character's face moves its mouth with its voice and blinks — the voice its player plays
  analysed as it plays (the level and five bands every 10 ms, shared by every face playing it) into the jaw open, the
  mouth wide, round or closed and the lower lip under the teeth, read 40 ms ahead of what is heard, eased and set on
  the face's blend shapes (ARKit's names by default); or driven by a timeline; blinks at random intervals.
* **Checked**: synthesised vowels (/a/ the jaw, /i/ wide, /u/ round, "s" the teeth, silence closed; at 120 and 210 Hz),
  the level, a minute's blinks, the face following a played voice under Movie Maker (the mouth with the sound, the
  blend shapes its channels), the release template the same, a regression scene (`face`), the editor check (its icon).

## 1. Development

* `modules/fellow_animation/fellow_face_animator.{h,cpp}`, `register_types.cpp`, `config.py`,
  `doc_classes/FellowFaceAnimator.xml`, `icons/FellowFaceAnimator.svg`.
* `benchmarks/project/bench/face_check.gd` (a test face built in code: a head, eyes and a mouth on ARKit's blend
  shapes); `tools/fellow_regression`: the scene `face`; the editor check's icon list.

## 2. Verification

* Decision 0191's table, on the editor build and the release template (the same).
* Lavapipe: the faces filmed; the regression scenes `face`, `climb` and `traversal` exact, the release template's the
  same.
* The editor check passes (34 icons).
* The laptop: not run (not linked to the session).

## 3. Measurements

A face's frame 0.24 µs (100 faces 0.024 ms a frame); a voice's analysis 14 ms a second of audio, spread as it plays
(about 0.23 ms a frame, once for all the faces playing it); analysing a whole 4 s voice at once had cost 49 ms.

## 4. Next

Phonemes aligned offline for authored dialogue (visemes per language), brows, gaze and head motion, emotions; a jaw
bone for faces without blend shapes; the analysis on a worker thread.
