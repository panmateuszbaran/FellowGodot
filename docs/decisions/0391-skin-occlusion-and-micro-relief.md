# 0391 — The people's skin: its crevices' occlusion baked, its micro-relief

Status: **KEEP** · Branch `fellow/geometry` · Showcase: `showcase/scripts/fellow_person.gd` (`skin_relief`,
`_skin_relief`, `SKIN_AO_LIGHT`), `showcase/people/<name>_ao.png`, `skin_pores_normal.png`, `skin_relief_mask.png`, the
seven `.glb` files (a second UV map) · Tools: `tools/fellow_characters/make_characters.py` (`skin_ao`, `detail_uv`),
`make_skin_maps.py` (`pores_normal`, the relief's mask), `showcase/tools/person_look.gd` (`--relief=0`, `--dist`)

## Problem

After decisions 0385-0387 the people's skin has its regions' roughness, an oily lobe, light through the ears, wet eyes
and its own faces, but it is still MakeHuman's one painted colour texture on a smooth surface: no normal map at all.
Close up it reads as plastic — one unbroken highlight over the forehead and nose — and its crevices (the eye sockets
round the eyeballs, the nostrils, the mouth's corners, the ears' folds, between the fingers) are as lit as the cheeks:
the screen's ambient occlusion is too coarse at a person's scale and the shadow maps do not resolve them.

## Change

* **The crevices' occlusion, baked per person** (`make_characters.py`, `skin_ao`): after each body is shaped (its face
  modifiers, decision 0387) and dressed, Cycles bakes its ambient occlusion into its UV layout (1024², 64 samples, rays
  6 cm long: crevices, not the body's large forms), the eyes, teeth, tongue and clothes occluding, the hair's,
  eyebrows' and eyelashes' cards (their alpha cut) not; texels no face covers take their nearest baked texel's value
  (the mipmaps do not darken the islands' edges). `<name>_ao.png` beside the `.glb` (120-155 KB). In the engine it is
  the skin's `ao_texture`: the ambient light and its reflections in the crevices darkened, and 35 % of the direct light
  (`SKIN_AO_LIGHT`: the crevices' own shadow, which the shadow maps miss).
* **The micro-relief** (`make_skin_maps.py`, `pores_normal`): a tiling normal map of skin's surface, 20 mm of skin per
  512² repeat — pores (pits 0.1-0.25 mm across on a jittered 0.9 mm grid) and the fine furrows that cross it in two
  families 0.6-0.8 mm apart, each wandering on its own; every pattern periodic in the tile. It is BaseMaterial3D's
  detail normal on the body's **second UV map** (`detail_uv`): the first's islands each scaled about the origin to one
  density, so the pores are the same size on the nose as on the back (MakeHuman's layout gives the face more texels)
  and the tangent frame stays the first's. `skin_relief_mask.png` (the detail mask, from MPFB's region masks as in
  0385) shows it most on the T-zone, less on the body, little on the lips and lids, not on the nails.

## Checks (lavapipe, `person_look.gd`, a low key light from the side)

* The seven faces at 0.62 m (`docs/showcase/skin_relief_m249.jpg`): the eye sockets and nostrils deeper, the mouth's
  corners and the ears' folds shaded; the skin a little less glossy where the relief's mipmaps break its highlight.
* At 0.22 m on 1080p (a close-up of a cut-scene's kind): pores and furrows over the forehead where the skin was one
  smooth sheet; at a few metres the relief's mipmaps are flat (as they should: a pixel covers millimetres).
* `--relief=0` gives the skin as before (the same `.glb` meshes; without the occlusion and the detail).
* Cost: one texture more for the occlusion, three for the detail (its albedo, white; its normal; its mask) on the skin's
  pixels; on the RTX with the queued measurements.

## Decision

KEEP, on in the showcase (`FellowPerson.skin_relief`). Open: the relief's effect on the roughness where its mipmaps flatten
it (Toksvig: a far face should be a little rougher, not as glossy as a smooth one), wrinkles from the people's ages
(MakeHuman's old skins have them painted, not in the surface), and the GPU cost on the RTX.

## Rejected

* **The micro-relief on the first UV map** (a 4096² map in the body's layout): at MakeHuman's layout a texel is 0.3 mm
  on the face and over a millimetre on the body — pores cannot be drawn at one size, and the map would be 64 times
  larger than a tile.
* **Triplanar projection** of a pore texture: the body is skinned, its object-space positions move with every pose, so
  the pores would swim over the skin.
* **The hair's cards as occluders**: the AO bake does not see their alpha, so their quads would occlude as solid shells
  over the scalp, forehead and neck; left out (the hair's shadow comes from the shadow maps).
