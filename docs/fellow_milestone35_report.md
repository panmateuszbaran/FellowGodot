# FellowEngine — milestone 35 report (rain shafts)

Status: **measured** — lavapipe and the RTX 5070 Ti Laptop (`m35_f307`, `m35_s18`, `m35_s19`, `m35_f308`,
2026-11-03). Branch `fellow/geometry`. Decision record `docs/decisions/0135-rain-shafts.md`.

## 0. Result

Under a broken layer of showers, grey curtains of rain hang below the clouds' dense parts in the distance and drift
with them — the way a storm is seen coming across a lake. Part of the clouds' own trace; without rain the trace's
result is the same bit for bit.

## 1. Development

* While a FellowRain3D rains or snows, its rate times `shafts` (1) goes to the renderer, with whether it snows.
* The clouds' trace marches the air below the cloud base (16 steps, up to 40 km) for a view ray from below it: where
  the local coverage above a point is 0.7 to 0.95, rain falls, up to 0.6 per km of extinction at the heaviest rain,
  streaked along the fall and drifting with the clouds; lit by the sky's light from above and a little of the sun
  (snow twice as bright). Not within 2 km of the camera, fading out from 25 to 40 km.
* The first version (from coverage 0.55, 1.2 per km, the trace's whole distance) greyed the whole horizon (lavapipe);
  sharper, thinner and nearer, the curtains separate.

## 2. Verification

* Without rain the previous image (lavapipe, against milestone 33's double-buffered build): Rain Lab at dusk under the
  clouds, no rain — 0 pixels differing.
* Lavapipe, the test scene at coverage 0.72 with the horizon in view: grey masses under the denser clouds with the
  shafts, the clear band of the horizon without.

## 3. Performance

Water Lab raining under a FellowWeather3D at coverage 0.75, 1080p, 2 runs each (`m35_f307_water`, fellow229): the
clouds' pass 0.329 → 0.379 ms (+0.05 ms); the frame 4.226 / 4.140 ms, within the runs' noise.

The defaults (`m35_f308_final`, 3 runs per scene, stock against fellow220 — milestone 30 — and fellow229): fellow229
faster than stock in every scene and pair of runs (Sponza −54.5 %, Bistro exterior −36.4 %, City −24.1 %, Bistro
interior −23.8 %, Lights −21.2 %, Foliage −17.9 %, Crowd −17.7 %, PBR −14.0 %, Stress −4.0 %); against fellow220 each
scene within its runs' spread (−0.3 % to +2.4 %): milestones 32-35's weather costs nothing without its nodes. The
README's table is from this check.

## 4. Showcase

Over the lake with the drops around the camera (`m35_s18_water_showers`): subtle — the drops, lit against the low
sun, take most of the frame. From a dry gap, the drops not drawn (`m35_s19_water_shafts_dry`, `--weather_drops=0`,
coverage 0.78; `water_rain_shafts.jpg`): the curtains hang over the far shore, which is hazy behind them, and drift
with the clouds in the loop (`m35_s19_water_shafts_loop`, `loop_water_rain_shafts.webp`). Milestone 37 makes the dry
gap the rain's own (`showers`).
