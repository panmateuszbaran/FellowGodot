# 0376 — Faces for the people: ARKit's shapes from MakeHuman, a mouth that talks without a voice

Status: **KEEP** · Branch `fellow/geometry` · Code: `modules/fellow_animation/fellow_face_animator.*` (`follower_paths`,
`talking`, `talk_rate`), `tools/fellow_characters/make_characters.py` (`face_units`, `apply_shape_keys`),
`showcase/scripts/fellow_person.gd` (`_face`), `showcase/scripts/rooftops_crowd.gd` (`_faces`, `_say`)

## Problem

After decisions 0374-0375 the rooftops' people and the player were MakeHuman people, their faces still: no blink, no
mouth moving in a talking group or while a speech bubble was shown. FellowFaceAnimator (decision 0191) moves ARKit's
blend shapes from a voice; the people had no blend shapes and no voices.

## Change

* **The shapes** (`make_characters.py`): MakeHuman's Faceunits 01 pack (CC0) is ARKit's 52 shapes as MakeHuman targets.
  They are loaded on the body as shape keys; the eyebrows and eyelashes get the eye, brow and cheek shapes, and the teeth
  and tongue (now added to every person) the jaw and mouth shapes — each fitted again to the body (MPFB's own fitting)
  with one shape at 1 and kept as that shape's key. The macros' mix is applied under them (their offsets kept), and they
  are exported as morph targets (without normals: 3.2-3.7 MB a person instead of 1.7-2.2).
* **FellowFaceAnimator** (engine): `follower_paths` — meshes that take the face mesh's blend shapes by name (the lashes
  close with the lids, the lower teeth open with the jaw); `talking` and `talk_rate` — while no voice plays, the mouth
  talks: phrases of 1-3.2 s of syllables (4.5 a second, each 0.7-1.3 of its length) and pauses of 0.25-1.1 s; a syllable
  an open, wide or round vowel at a random strength, the jaw in an arch over it, a third begin closed (p, b, m), a
  sixth end on the teeth (f, s); the brows raised at some phrases' starts. Deterministic from `seed`. Off by default:
  nothing changes for existing faces (the regression suite's `face`, `crowd` and `lights` scenes identical).
* **The showcase**: each person gets a FellowFaceAnimator ("Face": blinking, no gaze — the rig has no eye bones);
  the crowd's people talk while their clip is "talk" (the groups) and while their bubble is read (a twentieth of a
  second a character, 1-5 s); faces farther than 25 m from the camera are not processed.

## Checks (lavapipe)

* A face with `jawOpen` 0.7, a smile, a wink, the brows up (`person_look.gd --face=`): the lids and the lashes closed
  together, the teeth and tongue in the open mouth.
* A talking face over 1.5 s (`--talk=1`): the mouth open, round, wide, closed and on the teeth by turns, pauses
  between phrases.
* The crowd script with faces: no error. The regression suite's `face`, `crowd`, `lights`: identical.

## Decision

KEEP. Open: the eyes' gaze (the rig has no eye bones: MPFB's "default" rig has them, or the eye meshes as followers
of the eyeLook shapes), expressions from the crowd's moods (a smile when greeting, a frown when knocked).
