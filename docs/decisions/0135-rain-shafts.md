# 0135 — Rain shafts under the clouds

Status: **KEEP** (with a FellowRain3D that rains and the volumetric clouds on: `shafts` 1; the trace unchanged without
rain) · Branch `fellow/geometry` · Node: `FellowRain3D.shafts` · Code:
`servers/rendering/renderer_rd/shaders/effects/fellow_clouds.glsl` (the trace), `effects/fellow_clouds.{h,cpp}`
(`Params::precipitation`), `effects/fellow_weather.h` (`State::shafts`), `forward_clustered/render_forward_clustered.cpp`,
`modules/fellow_fluid/fellow_rain_3d.{h,cpp}`, `benchmarks/project/bench/main.gd` (`--weather_shafts`,
`--weather_coverage`).

## Problem

It rains around the camera (0124) and the clouds above are dark (0109, 0133), but the distance shows nothing of the
rain: under a broken layer of showers, the grey curtains hanging from each cloud to the ground — the way a storm is seen
coming across a lake or a plain — are missing; the sky below the clouds is as clear as on a dry day.

## Change

While a FellowRain3D rains or snows, its rate (times `shafts`) goes to the renderer (`FellowWeather::State::shafts`,
and whether it snows). The clouds' trace, for a view ray from below the cloud base, first marches the air between the
camera and the base (16 steps, up to 40 km): where the clouds above a point are dense — their local coverage, the
weather field the clouds stand on, from 0.7 to 0.95 — rain falls there, its extinction up to 0.6 per km at the heaviest
rain, streaked along the fall (the shape noise stretched twelve times vertically) and drifting with the clouds. It
scatters the light under the clouds (the sky's light from above at a quarter, a little of the sun; twice as bright for
snow) and dims what is behind it. Not within 2 km of the camera (the rain around it is its own) and fading out between
25 and 40 km. Its light and transmittance go before the clouds' in the trace's result, so the clouds' composite (0109)
puts it over the sky's pixels. Without rain the trace's result is the same bit for bit (0 + 1 × the clouds').

## Verification

* **Without rain, the previous image** (lavapipe, against milestone 33's double-buffered build): Rain Lab at dusk under
  the clouds, no rain (5 images) — 0 pixels differing.
* **Lavapipe, the test scene** in the controller's rain at coverage 0.72, the horizon in view: with the shafts the sky
  below the clouds darkens into grey masses under the denser clouds; without them it is the clear band of the
  horizon. The first version (from coverage 0.55, 1.2 per km, the whole trace's distance) greyed the whole horizon;
  sharper, thinner and nearer, the curtains separate.

## Results

**Cost** (RTX 5070 Ti Laptop, 1080p, Water Lab raining under a FellowWeather3D at coverage 0.75, the clouds on, 2 runs
each, `m35_f307_water`, fellow229): the clouds' pass 0.329 ms without the shafts, 0.379 ms with them (+0.05 ms); the
frame 4.226 / 4.140 ms (the difference within the runs' noise).

**Over the lake** (`m35_s18_water_showers`: the day look, the sun 14° up, coverage 0.75, four views): with the shafts
the band below the clouds on the far shore turns grey under the denser clouds; without them it is clear. They are
subtle there — the drops around the camera, lit against the low sun, take most of the frame — and the loop over the lake
(`m35_s18_water_showers_loop`) the same. From a dry gap, the drops not drawn (`m35_s19_water_shafts_dry`,
`--weather_drops=0`, coverage 0.78, job f309 with fellow230): the curtains hang over the far shore, hazy behind them, and
drift with the clouds in the loop (`m35_s19_water_shafts_loop`; `water_rain_shafts.jpg`, `loop_water_rain_shafts.webp`).

## Decision

**KEEP.**
