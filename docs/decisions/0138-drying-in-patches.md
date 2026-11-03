# 0138 — Wetting and drying in patches

Status: **KEEP** (`FellowRain3D.drying_patches` 1; at a wetness of 0 or 1 nothing changes) · Branch `fellow/geometry` ·
Node: `FellowRain3D.drying_patches` · Code: `servers/rendering/renderer_rd/shaders/forward_clustered/scene_forward_clustered.glsl`
(`fellow_wet_surface()`), `scene_forward_clustered_inc.glsl` (`FellowWeatherData::frost.z`), `effects/fellow_weather.{h,cpp}`
(`State::drying_patches`), `modules/fellow_fluid/fellow_rain_3d.{h,cpp}`.

## Problem

The wetness (0124) is one number for the whole scene: as a street dries after the rain (`wetness_follows_rain`, or a
FellowWeather3D's ground model, 0132) every surface turns from wet to dry at once, evenly, and the puddles fade out like
a dissolve. In a time-lapse the street dims uniformly; a real one dries in patches — the high spots first, the dips and
the puddles last, shrinking to their deepest — and wets the other way round.

## Change

Between dry and wet (`0 < wetness < 1`) and with `drying_patches` (0-1) above zero, the surface's own water (the
darkening of porous materials, the film's gloss and its ripples) takes a wetness of its own: the puddles' noise (0124)
taken as the ground's lows — where the puddles stand — and highs, with a broad noise (cells of 20 m, turned: the
puddles' 70 m repeat is not seen from above) and a fine one at the edges (9 cm); where `1.4 × wetness − 0.2` exceeds
the height (± 0.08, smoothly) the surface is wet, elsewhere dry. Walls take the noise across their face. The puddles'
amount is scaled by `smoothstep(0.15, 1, wetness)` and they appear from 0.1 to 0.3 of the wetness instead of 0.4 to
0.9: as the ground dries they shrink to their deepest instead of fading. `drying_patches` mixes the two (0: the even
wetness as before). At a wetness of 0 or 1 the branch is skipped: the same image bit for bit.

## Verification

* **Off is the previous image** at full wetness (lavapipe, against milestone 37's build): Rain Lab raining without
  drops (5 images), the wind meadow under snow (3) — 0 pixels differing.
* **Lavapipe, the test scene** at a wetness of 0.3, 0.6 and 0.85: mostly dry with a wet patch in the distance; large
  wet patches and dry ground between them; nearly all wet with the puddles — against the even, uniformly damp ground of
  0.6 without the patches. The first pattern (two noises of 80 and 20 cm) mottled the ground without patches one would
  see drying; the puddles' noise gives patches of metres. From 400 m up the puddles' 70 m repeat showed as a grid; the
  broad noise breaks it.

## Results

(RTX: to be filled.)

## Decision

**KEEP.**
