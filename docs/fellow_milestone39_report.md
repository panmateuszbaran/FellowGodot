# FellowEngine — milestone 39 report (wind-driven rain)

Status: **measured** — lavapipe and the RTX 5070 Ti Laptop (`m39_f313`, `m39_s23`, 2026-11-03). Branch
`fellow/geometry`. Decision record `docs/decisions/0139-wind-driven-rain.md`.

## 0. Result

In a wind the rain's shelter follows the drops' slant: the facades facing the wind get wet under their eaves and
awnings — as wet as the street — the rain reaches in under an awning on the windward side, and the lee of a building
stays drier. Below a breeze of about 1.8 m/s the rain is taken as vertical and nothing changes; not for snow.

## 1. Development

* The rain node sends the drops' drift per metre of fall (the wind they were given over their speed) and how slanted
  the shelter is: from a drift of 0.2 to 0.6 (`wind_driven` 1).
* The colour passes look the heightfield up a second time, back along the drift (up to 1 m, as far as the path rises
  3 m), and mix the slanted shelter with the straight one; a wall facing the wind is as wet as the ground.

## 2. Verification

* Off is the previous image (lavapipe, against milestone 38's build): Rain Lab in its light wind (drift 0.15) without
  drops, the wind meadow under snow — 0 pixels differing.
* Lavapipe, the test scene in the controller's rain (6 m/s): under the shelter's roof the wet ground reaching in on
  the windward side, the dry patch moved to the lee and past the roof's edge, the posts' windward faces wet.

## 3. Performance

Bistro exterior in the controller's storm (12 m/s), 1080p, 2 runs each (`m39_f313_wind_driven`, fellow234): the depth
pre-pass 0.696 → 0.742 ms, the opaque pass 1.550 → 1.585 ms, the frame 4.908 → 4.968 ms (+0.06 ms).

## 4. Showcase

Bistro in the rain and the storm, straight against slanted, five views (`m39_s23_bistro_wind_driven`;
`bistro_wind_driven_ab.jpg`): the facades facing the wind darker and wet under their awnings and balconies, the
street's wet edges moved along the kerbs.

## 5. Also in this stretch

* FellowBench's `--builds` splits at commas, so the weather's lists in a build's own arguments (`--weather_ground`,
  `--weather_times`, `--weather_fog_color`) now also take ':'; job f309's measurement and stills that used them did not
  run and were redone in f313.
* A clear weather above freezing creates no rain node, so a ground given to it was dropped: milestone 38's half-dry
  and milestone 40's snow at 3 °C first ran on a dry street (f313, f314). The bench prepares the nodes when a ground is
  given and records it; both were redone in f315.
