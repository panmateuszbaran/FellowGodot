# 0387 — The people's own faces

Status: **KEEP** · Branch `fellow/geometry` · Showcase: `tools/fellow_characters/make_characters.py` (`face_shape`),
`people.json` (`face_shape`, `face_asym`), the seven `.glb`; `fellow_person.gd` (the hair's highlights kept off curly
hair)

## Problem

The fourth step of the people's realism (0385 skin and eyes, 0386 hair): their faces were MakeHuman's averages for
their sex, age and ancestry — every face the same face scaled, perfectly symmetric. That sameness, more than any one
feature, makes a crowd read as mannequins.

## Change

* **Face modifiers** (`make_characters.py`, `face_shape`): each person's `face_shape` in `people.json` names MakeHuman's
  face modifiers (MPFB's `data/targets/target.json` labels — the nose's hump, point, width; the lips' volume; the
  cheekbones; the chin's width and prominence; the eyes' bags; the ears; the head's shape) with a value; they are loaded
  as the body's shape keys right after the macros, before the eyebrows, eyes, hair and clothes are fitted (so these
  follow), and applied with the macros at the export. Giulia an upturned nose and full lips, Marco a square jaw and a
  hump, Rosa a long nose and thin lips with bags under the eyes, Aldo a hooked nose and large ears, Amara wide
  nostrils, full lips and high cheekbones, Kenji cheekbones and a broad chin, Luca a Greek nose and a cleft chin (6-11
  modifiers each, 0.1-0.6).
* **Asymmetry** (`face_asym`, 0.25-0.4): every one of the face's asymmetry modifiers (brows, cheeks, ears, eyes, jaw,
  mouth, nose, temples, top) at a random value within it, seeded by the person's name (the same face every build).
* The hair's highlights (0386) left off curly hair (`HAIR_CURLY`: the afro's top went grey) and toned down (specular
  0.3, roughness 0.55: Kenji's black hair showed grey patches).

## Checks (lavapipe, `person_look.gd --view=face --light=key`; `docs/showcase/faces_m245.jpg`)

* 36-44 targets a person; the body's vertices moved by up to 8 mm (Marco: 1 765 of 9 416 by more than 1 mm). The faces,
  their own: noses, lips, jaws and ears differ person to person; nothing is mirrored exactly.
* The face units (ARKit's 52 shapes, 0376) are deltas and still fit; blinking and talking unchanged.

## Decision

KEEP. With 0385 and 0386 the people's faces read less as mannequins; their skins' and hairs' painted textures stay
MakeHuman's (the gap analysis: mid-poly bodies, simple skins — far from scanned humans).
