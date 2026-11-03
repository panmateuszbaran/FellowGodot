# FellowEngine — milestones 41-42 report (thin rain streaks)

Status: **measured** — lavapipe and the RTX 5070 Ti Laptop (`m41_f315`, `m41_s25`, 2026-11-03). Branch
`fellow/geometry`. Decision records `docs/decisions/0141-thin-streaks.md`, `0142-weather-thin-streaks.md`.

## 0. Result

By day against the sun the rain around the camera was a curtain of white rods — every drop drawn 2.4 cm wide at every
distance. Drawn at its own width (4 mm) or a pixel, its opacity scaled to match, the rain reads as rain: fine lines
near the camera, faint ones far away, the view behind them clear. Opt-in on a FellowRain3D (`streak_width`); the
default of the rain node a FellowWeather3D creates.

## 1. Development

* `FellowRain3D.streak_width` (m, 0: off): the drops' shader widens a streak to a pixel at its distance where it is
  thinner, and scales its opacity by the ratio (the light it carries the same). The splashes and flakes unchanged.
* A pixel and a half as the least width left the streaks almost gone at 640 × 360 (lavapipe); a pixel.
* Milestone 42: a FellowWeather3D's own rain node gets 0.004 m.

## 2. Verification

* Off is the previous image (lavapipe, against milestone 40's build): Rain Lab without drops — 0 pixels differing (the
  drops themselves are not deterministic from run to run; at 0 their new code is skipped).

## 3. Performance

Water Lab in the controller's rain, bars against thin streaks, 1080p, 2 runs each (`m41_f315_streaks`, fellow236): not
measurable (the transparent pass 1.134 / 1.154 ms, both builds' runs noisy).

The final check of milestones 36-42 (`m41_f317_final`, stock against fellow229 — milestone 35 — and fellow236, the
nine scenes, 3 runs after a pass that fills the shader caches): fellow236 faster than stock in every scene and pair of
runs (Sponza −54.4 %, Bistro exterior −37.4 %, City −29.1 %, Bistro interior −23.6 %, Lights −23.1 %, Crowd −19.3 %,
Foliage −18.9 %, PBR −15.0 %) but Stress Lab, within its spread (−1.8 %, 2 of 3 pairs: its frame is bound by the render
thread; its GPU time 2.4 ms in both FellowEngine builds against stock's 3.2 ms); against fellow229 each scene within its
runs' spread. The README's table is from this check.

## 4. Showcase

Bistro's square against the sun, bars against thin streaks (`m41_s25_bistro_streaks`; `bistro_thin_streaks_ab.jpg`),
the lake (`m41_s25_water_streaks`), and the lake in the rain in thin streaks (`m41_s25_water_streaks_loop`;
`loop_water_thin_rain.webp`).
