# 0358 — Night falls on the square

Status: **KEEP** · Branch `fellow/geometry` · Code: `showcase/scripts/rooftops_night.gd` (new: N), `rooftops.gd`
(`set_sun_own_color()`: the Sun's own colour, its share of the air's reddening, the energy the SkySun follows),
`rooftops_weather.gd` (the eye under the moon, the keys' line), `shaders/atmosphere_sky.gdshader` (the stars where the
sky is dark as it is seen); `rooftops/rooftops.tscn`, `tools/build_rooftops.gd` (the Night node);
`tools/climb_look.gd` (`--night=1`, `--night_at`, `--night_set`, the night's line).

## Problem

Milestone 188's open point: "no rooms at night in the showcase (it is a golden hour)". The rooftops square had one
time of day; its rooms behind the windows (decision 0330's `FellowInteriorMaterial3D`, with lamps it never lit), its
lamps, its brazier, its osteria and its sky's stars (the sky shader's, where the sky is dark) were never seen at night.

## Change

**N** (and N again for the day; `rooftops_night.gd`, a node of the scene), over 20 s (the sunset 60 % of it):

* **The sun sets**: down its own azimuth from its 24° to 10° under the horizon, slowly through the horizon (along
  k x³ + 2x: half the sunset within 3° of it, where the sky's colours are); its light reddening towards the horizon and
  gone under it (its energy faded from 6° to −1.5°); the sky's own sun (rooftops.gd's SkySun) keeps the day's
  energy, so that FellowEngine's atmosphere draws the sunset and the twilight from the sun's elevation and darkens the
  sky itself as it goes under.
* **The moon rises** where the sky's light had gone: the Sun's light turned to the moon's — a pale blue (0.62, 0.72,
  1.0), 0.15 of energy (the sun's 4), from 28° up, 80° round from where the sun set (over the square's west side, seen
  from its corner), a smaller disk (1.2°: sharper shadows) — and the sky's sun up with it (its elevation raised from
  10° to the moon's, the disk drawn over the roofs); the sky and its clouds lit by a tenth of the moon's light, the sky
  drawn at an eighth of its intensity: dark blue, its stars out; the moon's disk a tenth of the sun's.
* **The windows light up**: the rooms behind them (`glass.tres`) get their lamps — 55 % of them (none by day) — and the
  moon's light through the glass (0.05 of the day's).
* **The eye adapts**: 2.5 stops more under the moon (`rooftops_weather.gd`): the night darker than the evening, the
  square's lamps, the windows, the brazier and the osteria bright in it. The weather keys go on working at night (snow,
  a storm's lightning under the moon).
* The sky shader's stars come out where the sky is dark as it is seen (its luminance times its `intensity`) and are not
  dimmed with it — by day (intensity 1) the sky is drawn as before.

## Measurements

The RTX 5070 Ti laptop, 1280x720, the square from its corner; night falling from 0.5 s (f610): the mean sRGB of the
sky over the palazzo and of the cloud at the frame's left.

| s | 0.3 | 4.3 | 5.7 | 7.0 | 8.3 | 9.7 | 11.0 | 13.7 | 16.3 | 19.0 | 21.7 |
|---|---|---|---|---|---|---|---|---|---|---|---|
| the sky | 134, 164, 196 | 123, 143, 171 | 99, 100, 119 | 83, 78, 95 | 75, 66, 83 | 37, 30, 46 | 2, 2, 4 | 5, 7, 11 | 31, 43, 58 | 34, 46, 62 | 29, 39, 53 |
| the cloud | 199, 191, 187 | 195, 180, 169 | 160, 124, 103 | 127, 88, 79 | 102, 65, 65 | 61, 32, 36 | 57, 21, 14 | 10, 10, 10 | 32, 31, 32 | 53, 53, 54 | 54, 53, 54 |

The sky through its sunset (a pink-violet dusk at 7-8 s) and its twilight; the clouds, a kilometre and more up, lit
red after the sky has gone dark (their light through the atmosphere from their height); dark for a moment; then the
moonlit sky's blue and the moon's light on the clouds. With the sun going down at an even pace (14 s, the first try,
f608) the sky went from the golden hour to black in under 3 s of the 14.

At night (f608, f609): the frame's mean luminance under the sky 3.6 stops under the fair evening's (the eye 2.5 stops
up); the ground in the moonlight (23, 19, 20); the windows lit, the lamps' pools, the brazier, the osteria. The demo's
path unchanged (the node does nothing until N).

## Decision

KEEP: N for the night on the rooftops square.

## Rejected / not done

* The moon as a light of its own beside the sun: the sky's shader draws one light's disk and FellowEngine's atmosphere
  tables are for one light (the sky's first); the Sun and the SkySun become the moon's once the sky is dark.
* The night's sky as the atmosphere's for the moon at the moon's real light (1/400,000 of the sun's): with no eye that
  adapts by 18 stops it is black; a day-for-night — the moonlit sky's own blue, dimmed, its stars out — as films do.
