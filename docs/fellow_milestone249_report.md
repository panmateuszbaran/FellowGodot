# FellowEngine — milestone 249 report (the people's skin: crevices and micro-relief)

Status: **FINAL** (2026-11-28). Branch `fellow/geometry`. Decision 0391. Checked on lavapipe (`person_look.gd`); its
GPU cost joins the queued RTX measurements.

## 0. Result

MakeHuman's skin was one painted colour texture on a smooth surface. Each person now carries its own baked ambient
occlusion (the crevices: eye sockets, nostrils, the mouth's corners, ears, fingers) and the skin gets a micro-relief —
pores and the fine furrows that cross it — on a second UV map of one density, masked by region. Close up the forehead
is no longer one plastic highlight; at a few metres the relief's mipmaps flatten, the occlusion stays.

## 1. Changes

* `tools/fellow_characters/make_characters.py`: `skin_ao` (Cycles' AO bake in the body's UV layout, 1024², 6 cm rays,
  the hair's and lashes' cards hidden, empty texels filled from the nearest baked one; `<name>_ao.png`), `detail_uv`
  (the second UV map: the first's islands scaled to 20 mm of skin a unit), `--ao=0`.
* `tools/fellow_characters/make_skin_maps.py`: `pores_normal` (`skin_pores_normal.png`, a 512² tiling normal map),
  `skin_relief_mask.png`.
* `showcase/people/`: the seven `.glb` files again (with the second UV map), their `_ao.png`, the two shared maps, their
  imports.
* `showcase/scripts/fellow_person.gd`: `skin_relief`, `_skin_relief` (the occlusion as `ao_texture`, 35 % on the direct
  light; the relief as the detail normal on UV2 with its mask). `showcase/tools/person_look.gd`: `--relief=0`, `--dist`.
* Docs: decision 0391, the changelog, `docs/showcase/skin_relief_m249.jpg`.

## 2. Checks (lavapipe)

* The seven faces with and without (`skin_relief_m249.jpg`): crevices shaded, the skin less glossy.
* An extreme close-up (0.22 m, 1080p): pores and furrows on the forehead where the skin was smooth.
* No engine change.
