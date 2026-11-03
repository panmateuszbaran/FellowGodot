# FellowEngine — milestone 40 report (slush)

Status: **measured** — lavapipe and the RTX 5070 Ti Laptop (`m40_f315`, `m40_s25`, `m40_s26`, 2026-11-03). Branch
`fellow/geometry`. Decision record `docs/decisions/0140-slush.md`.

## 0. Result

Melting snow soaks with its meltwater: greyer, glossier, without the fresh snow's glints, its thin parts first, while
the ground under it runs wet. The weather controller turns it on as the snow melts and off again below freezing.

## 1. Development

* `FellowRain3D.slush` (0-1): the snow cover's albedo towards a wet grey (85 %), its roughness towards 0.25, its glints
  out; fully where the cover is thin, at 70 % where it covers.
* A FellowWeather3D's ground model raises it while snow lies above 0 °C (fully at 3 °C or in the rain) in a twentieth
  of `freezing_time`, and lowers it below freezing and under fresh snowfall. `--weather_ground` takes it as a fifth value.
* The first grey (lighter, roughness 0.18) mirrored the sky white at a grazing angle and stayed too light (lavapipe).

## 2. Verification

* Off is the previous image (lavapipe, against milestone 39's build): Rain Lab without drops, the wind meadow under
  snow — 0 pixels differing.
* Lavapipe, the test scene: snow at 0.8 with slush 1, a wet sheen and greyer; at 0.5 at the feet, greyer than the
  fresh snow and without its glints.

## 3. Performance

Bistro exterior under snow at 3 °C (the ground held), fresh against soaked, 1080p, 2 runs each (`m40_f315_slush`,
fellow236): +0.02 ms. The first measurement and stills (`m40_f314`, `m40_s24`) ran on a dry street (no rain node in a
clear weather above freezing; FellowBench fixed).

## 4. Showcase

Bistro under snow at 3 °C (`m40_s25_bistro_slush`; `bistro_slush_street.jpg`): the soaked snow greyer with a wet
sheen — subtle under auto exposure. The snow melting (`m40_s26_bistro_melt_loop`; `loop_bistro_melt.webp`): the snow
stops, it turns warm, the cover greys and thins and the wet cobbles come through.
