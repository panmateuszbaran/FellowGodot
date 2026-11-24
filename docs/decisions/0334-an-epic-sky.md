# 0334 — The rooftops' sky made epic: towering cumulus, a sky lit by a white sun, its blue deepened

Status: **KEEP** (the showcase only; nothing in the engine changes) · Branch `fellow/geometry` · Code:
* `showcase/scripts/rooftops.gd` (the clouds' layer; `sky_sun_color`, `sky_saturation`, `sky_brightness` 4 → 3),
  `showcase/shaders/atmosphere_sky.gdshader` (`sky_saturation`), `showcase/rooftops/rooftops.tscn` (`fog_sky_affect` 0,
  as `build_rooftops.gd` has had it since milestone 168);
* the tools: `showcase/tools/sky_look.gd` (`--env`, `--skymat`), `showcase/tools/weathering_look.gd` (`--scene`,
  `--env`, the views `sky_n` and `sky_sun`).

## Problem

The owner, playing the rooftops (milestone 190): "I would still work on the sky to make it more epic — look at the sky
in the Witcher 3 or other AAA games". Ours was a grey-lavender golden-hour sky (the clear sky 50° up, 90° from the sun:
160, 161, 169 in sRGB — barely blue) over a layer of flat stratocumulus (milestone 168's, the owner's choice then).
A Witcher 3 sky is a deep blue over towering white cumulus with shaded bases.

## What made it grey (lavapipe, `sky_look.gd`, 960x540)

* **The sun reddened twice.** The sky and the clouds are lit by the scene's SkySun (milestone 168: a light only the sky
  sees, 10° up), and it took the Sun's own golden colour (1, 0.9, 0.78: linear 1, 0.79, 0.57). FellowEngine's
  atmosphere reddens a sun's light through the air itself, so the sky's blue in-scattering was scaled by 0.57 on top:
  the clear sky's blue over its red 1.15 (sRGB) instead of 1.32 with a white sun.
* **The depth fog over the sky.** `build_rooftops.gd` has set `fog_sky_affect` 0 since milestone 168 (the atmosphere
  draws its own haze), but the saved scene still had the default 1: the fog's beige over the sky.
* **Brightness.** A sky at milestone 168's four times the sun's energy sits where AgX desaturates: the same radiance
  saturated by 1.8 in the sky shader comes out nearly as pale. At three times, saturated by 1.6, it reads blue.
* Not the atmosphere's physics: a sun 10° up shines through five air masses, and a golden hour's sky is that pale
  (its blue over its red about 2 in linear light; ours 1.85). A deep blue at the golden hour is an artist's.

## Change

* `sky_sun_color` (white): the SkySun's own colour, the atmosphere reddening it; its energy keeps the sky's luminance
  (times the Sun's colour's luminance over the white's).
* `sky_saturation` (1.6): the sky seen — not its radiance map, so the scene's ambient light and reflections stay the
  atmosphere's — saturated about its luminance in the sky shader, before the sun's disk and the stars.
* `sky_brightness` 4 → 3 (the radiance map's scale with it: the scene's light unchanged).
* The clouds: towering cumulus — bases at 1.5 km, 2.2 km tall, coverage 0.45, density 2.6 (from stratocumulus 2.5 km up,
  0.6 km thick, 0.6, 2.5).
* `fog_sky_affect` 0 in the saved scene.

## Measurements (lavapipe)

* The clear sky over the square (`weathering_look.gd --lite=1 --views=sky_n`): 170, 176, 191 → **130, 160, 193**.
* `sky_look.gd`, 50° up, 90° from the sun: 160, 161, 169 → 143, 156, 181 (with the towering clouds in part of the
  sample); opposite the sun 184, 176, 174 → 157, 166, 184.
* The buildings' light unchanged (the radiance map's scale follows `sky_brightness`; the Sun as it was).
* The GPU's cost of 2.2 km of cloud instead of 0.6 is not measured (no GPU work for now): the march's steps are fixed
  (64 coarse; fine ones inside clouds), the fine ones more where a ray crosses more cloud.

## Decision

KEEP for the showcase. The owner judges it in play; every term is a property of the scene's root (rooftops.gd).

## Rejected / not done

* The whole frame more saturated and contrasted (`adjustment_saturation` 1.05, AgX contrast 1.1): the sky bluer
  still, but milestones 158-159 set the buildings' colours against the owner's reference photograph; kept.
* A higher sun for the sky (22-24°): a deep, bright midday blue, the golden hour lost.
* Less haze (`atmosphere_mie` 0.25) and more Rayleigh (1.3): a brighter sky, not a bluer one.
* Silver linings and darker cloud bases, crepuscular rays through the cumulus: the clouds' own lighting, another
  milestone.
