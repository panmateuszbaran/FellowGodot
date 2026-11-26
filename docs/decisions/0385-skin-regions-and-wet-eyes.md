# 0385 — The people's skin by region, their eyes wet

Status: **KEEP** · Branch `fellow/geometry` · Showcase: `tools/fellow_characters/make_skin_maps.py` (new),
`make_characters.py` (the high-poly eyes, their texture's alpha kept), `showcase/scripts/fellow_person.gd`
(`skin_maps`, `_skin_regions`, `_wet_eyes`), `showcase/people/skin_*.png`, `eye_occlusion.png`, the seven `.glb`

## Problem

The owner, looking at the people (milestone 240): "these characters really look so-so, not very photorealistic". Up
close the faces read as wax: MakeHuman's skins are one colour texture each, so the whole body had one roughness —
lips, nose, eyelids and back alike — and no second specular lobe; the eyes were the asset pack's low-poly ones (96
vertices), opaque, without a wet surface or the shadow of the lids, so they looked painted on.

## Change

* **The skin by region** (`make_skin_maps.py`, from MPFB's region masks, CC0, which are in the base mesh's default UV
  layout that every MakeHuman skin shares — one set of maps for every person):
  * `skin_roughness.png`: the body 0.62, the face 0.52, the T-zone (a band over the brows, the nose's ridge) and the
    eyelids 0.42, the lips 0.34, the nails 0.25, with a little noise;
  * `skin_oil.png`: the skin's oily film as a second, sharper lobe (BaseMaterial3D's clearcoat at 0.2, roughness 0.5;
    R its strength — the T-zone and the lips most —, G the factor of its roughness);
  * `skin_transmittance.png`: where light shows through from behind (the ears, a little the nose's wings), BaseMaterial3D's
    transmittance in a reddish orange.
* **The eyes**: the pack's high-poly eyes (1 064 vertices; their cornea is a transparent island of the texture, cut
  by its alpha — the generator now keeps the eyes' texture as a full-colour PNG with its alpha, the dark brown iris
  too); wet (a clearcoat of 0.04 over a sclera of roughness 0.3); `eye_occlusion.png` (made from the eyes' mesh and
  texture: each eyeball's disc found by its pupil, "up" by the vertices' height) darkens the sclera towards the lids,
  the upper lid more, for the direct light too (`ao_light_affect` 0.85).
* `person_look.gd --skin=0` (without the maps), `--light=key` (a low sun, a light behind, a dim ambient: the lighting
  the faces were judged under).

## Checks (lavapipe, `person_look.gd --view=face --light=key`)

* `docs/showcase/skin_eyes_m243.jpg`: Marco before and after, and the seven faces. The forehead, the nose and the lips
  catch the light (the cheeks less, the body barely); the eyes have a sharp highlight, their corners shaded. A first
  try at 0.35 for the oil read as sweat: 0.2.
* The cost: three 1 024² textures shared by everyone; the body's material gains a clearcoat lobe and transmittance;
  the eyes 2 040 triangles a person instead of 172.
* Regression suite (the people are the showcase's): no engine change.

## Decision

KEEP. Not yet the faces' gap closed: the hair (painted cards, "a helmet") is the largest left — milestone 244's strands
— then the faces' shapes (MakeHuman's averages).
