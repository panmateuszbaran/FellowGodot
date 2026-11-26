# FellowEngine — milestone 243 report (the people's skin by region, their eyes wet)

Status: **FINAL** (2026-11-27). Branch `fellow/geometry`. Decision 0385. Checked on lavapipe (the showcase; no engine
change).

## 0. Result

The owner: the people "look so-so, not very photorealistic". The first two of four steps (skin, eyes; then hair and
the faces' shapes): the skin's light now differs by region — glossy lips and T-zone, duller cheeks and body, an oily
second lobe, light through the ears — and the eyes are the pack's high-poly ones, wet, shaded towards the lids.
`docs/showcase/skin_eyes_m243.jpg`.

## 1. Changes

* `tools/fellow_characters/make_skin_maps.py` (new): `skin_roughness.png`, `skin_oil.png`, `skin_transmittance.png`
  from MPFB's region masks (CC0), `eye_occlusion.png` from the high-poly eyes.
* `make_characters.py`: the high-poly eyes by default, their texture's alpha kept (full-colour PNG); groundwork for
  milestone 244 — the hair traced into strand guides (`hair_guides.py`, `<name>_hair.json`; `--hair_dump`).
* `showcase/scripts/fellow_person.gd`: `skin_maps` (the maps, the clearcoat, the transmittance), `_wet_eyes`; the
  strands' code (`strand_hair`, off until milestone 244).
* `showcase/tools/person_look.gd`: `--skin=0`, `--light=key`, `--strands=0`.
* The seven people generated again.
* Docs: decision 0385, the characters' README, the changelog.

## 2. Checks

* Marco before and after, the seven faces (the image above).
* No engine change: the regression suite does not see the showcase's people.

## 3. Open

* Milestone 244: the hair as strands (the largest gap left in the faces).
* The faces' shapes (MakeHuman's averages); the skin's pores at close range (a detail normal: 0382 found one invisible
  at play distances).
* On the RTX: the cost of 63 people with the new materials.
