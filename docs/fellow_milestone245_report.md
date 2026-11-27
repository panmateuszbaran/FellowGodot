# FellowEngine — milestone 245 report (the people's own faces)

Status: **FINAL** (2026-11-27). Branch `fellow/geometry`. Decision 0387. Checked on lavapipe; the people's cost on the
RTX.

## 0. Result

The last of the four steps the owner's "not very photorealistic" started (243 skin and eyes, 244 hair): each person
now has a face of its own — MakeHuman's face modifiers chosen per person and a little asymmetry — instead of the
average face for its sex, age and ancestry. On the RTX the 63 people with the new skin, eyes and hair cost what they
did before them.

## 1. Changes

* `make_characters.py`: `face_shape` (the modifiers as shape keys before the assets are fitted), `MPFB_DIR`.
* `people.json`: `face_shape`, `face_asym` for the seven people.
* `fellow_person.gd`: `HAIR_CURLY`; the hair's highlights at specular 0.3, roughness 0.55.
* The seven people generated again.
* Docs: decision 0387 (and 0385's RTX measurement), the characters' README, the changelog,
  `docs/showcase/faces_m245.jpg`.

## 2. Checks

* Faces before and after (the image above); 36-44 targets a person, up to 8 mm.
* RTX laptop (milestone 244's build, the square by day): the people +0.41 / +0.31 ms GPU (before 243: +0.36 /
  +0.43 ms), +1.56 / +1.74 ms render thread (before: +1.7 ms).

## 3. Open

* What MakeHuman cannot give: scanned skin detail, film-grade hair; MetaHumans (usable in Godot since June 2025) on
  the owner's decision.
