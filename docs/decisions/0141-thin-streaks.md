# 0141 — Thin rain streaks

Status: **KEEP**, opt-in (`FellowRain3D.streak_width` 0: the drops as before) · Branch `fellow/geometry` · Node:
`FellowRain3D.streak_width` · Code: `modules/fellow_fluid/fellow_rain_3d.{h,cpp}` (the drops' shader),
`benchmarks/project/bench/main.gd` (`--weather_streaks`).

## Problem

A drop (0124) is a camera-facing streak of `drop_size` (2.4 cm wide, 55 cm long) wherever it is: 5-6 pixels wide at
4 m at 1080p, as wide in pixels as it gets nearer, a bar of the same width in metres at 20 m. At night it shows the
rain in the lamplight; by day against a low sun, lit forward-scattering, the rain around the camera is a curtain of
thick white bars (milestone 35's showers over the lake): heavier than rain is seen.

## Change

`streak_width` (m, 0 by default) is the drops' own width. A streak is then drawn at that width or a pixel at its
distance (`distance × 2 / (P[1][1] × viewport height)`), whichever is wider, and its opacity is scaled by the ratio of
the two — the light it carries the same, spread over the pixel it covers: near drops thin, far ones faint, as rain is
seen. 0.004 m is a large drop's streak. The splashes and the snowflakes are unchanged; at 0 the shader skips it.

## Verification

* **Off is the previous image** (lavapipe, against milestone 40's build): Rain Lab without drops (5 images) — 0 pixels
  differing (the drops are not deterministic from run to run; at 0 their code is skipped).
* **Lavapipe, the test scene** in the controller's rain at 640 × 360: against the bars of `drop_size`, thin faint
  streaks at 0.003 m (the first version's least width, a pixel and a half, left them almost gone at that resolution;
  a pixel).

## Results

**Cost** (RTX 5070 Ti Laptop, 1080p, Water Lab in a FellowWeather3D's rain, the drops as bars against thin streaks, 2
runs each, `m41_f315_streaks`, fellow236): the transparent pass 1.134 / 1.154 ms, the frame 3.628 / 3.663 ms (both
builds' runs noisy): not measurable.

**By day** (`m41_s25_water_streaks`, `m41_s25_bistro_streaks`: the controller's rain, the drops as bars against thin
streaks; `bistro_thin_streaks_ab.jpg`, `loop_water_thin_rain.webp` from `m41_s25_water_streaks_loop`): against the
low sun the bars are a curtain of white rods across the square and the lake; as thin streaks near drops are fine lines
and far ones faint, the view behind them clear. Milestone 42 makes it the default of the weather's own rain node.

## Decision

**KEEP.**
