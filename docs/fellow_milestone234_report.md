# FellowEngine — milestone 234 report (faces for the people)

Status: **FINAL** (2026-11-26). Branch `fellow/geometry`. Decision 0376. No GPU work: checked on lavapipe.

## 0. Result

The rooftops' people have faces that live: they blink, their lashes close with their lids, and their mouths talk — in
the talking groups and whenever a speech bubble shows over them (the player's conversations, their shouts when run
into). The faces are ARKit's 52 shapes from MakeHuman's CC0 face units, given also to the eyebrows, eyelashes, teeth and
tongue; the engine's FellowFaceAnimator gained meshes that follow the face's shapes and a mouth that talks without a
voice.

## 1. Changes

* Engine: `FellowFaceAnimator.follower_paths`, `talking`, `talk_rate` (off by default: existing faces unchanged).
* `tools/fellow_characters/make_characters.py`: the face units on the body and its followers; teeth and tongue.
* Showcase: `FellowPerson` adds the face; the crowd drives `talking` (`_faces`, `_say`).

## 2. Open

* The eyes' gaze (no eye bones in MPFB's game rig); moods on the faces (smiles, frowns).
* The repository now holds the people twice (milestones 232-233 without faces, 234 with): 24 MB of `.glb` in this
  version.
