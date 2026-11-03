# 0140 — Slush: melting snow soaks with its water

Status: **KEEP** (`FellowRain3D.slush` 0; a FellowWeather3D sets it while the snow melts) · Branch `fellow/geometry` ·
Node: `FellowRain3D.slush` · Code: `servers/rendering/renderer_rd/shaders/forward_clustered/scene_forward_clustered.glsl`
(`fellow_snow_cover()`), `scene_forward_clustered_inc.glsl` (`FellowWeatherData::frost.w`), `effects/fellow_weather.{h,cpp}`
(`State::slush`), `modules/fellow_fluid/fellow_rain_3d.{h,cpp}`, `fellow_weather_3d.cpp` (`_apply_ground()`).

## Problem

The snow cover (0128) melts above freezing by thinning out (0132's ground model): it stays the fresh, white, glinting
snow of a frosty morning until the last patch goes. Melting snow is not that: soaked with its meltwater it turns grey,
glossy and translucent, the thin parts first, while the ground under it runs wet.

## Change

`FellowRain3D.slush` (0-1) soaks the snow cover: its albedo goes towards a wet grey (0.45, 0.48, 0.52) by 85 % of it,
its roughness towards 0.25 (a wet sheen), its glints out; where the cover is thin (below half) fully, where it covers
at 70 %. A FellowWeather3D's ground model raises it while there is snow above 0 °C — fully at 3 °C and above or in the
rain (`t / 3 + 2 × rain`), in a twentieth of the rain node's `freezing_time` — and lowers it below freezing (the slush
setting again, a fifth as fast) and under fresh snowfall. The meltwater already wets the ground (0132). At 0 the image
is the previous one (the glints multiplied by exactly 1).

## Verification

* **Off is the previous image** (lavapipe, against milestone 39's build): Rain Lab raining without drops (5 images),
  the wind meadow under snow (3) — 0 pixels differing.
* **Lavapipe, the test scene**, snow at 0.8 over a wet ground: with `slush` 1 a wet sheen across the snow at a grazing
  angle and the snow greyer; at the feet, at 0.5, greyer than the fresh snow and without its glints. The first grey
  (0.5-0.57 by 75 %, roughness 0.18) mirrored the sky white at a grazing angle and stayed too light.

## Results

(RTX: to be filled.)

## Decision

**KEEP.**
