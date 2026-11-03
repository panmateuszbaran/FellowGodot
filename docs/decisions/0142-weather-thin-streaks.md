# 0142 — The weather's own rain in thin streaks

Status: **KEEP** · Branch `fellow/geometry` · Code: `modules/fellow_fluid/fellow_weather_3d.cpp` (`_get_rain()`),
`doc_classes/FellowWeather3D.xml`.

## Problem

Thin streaks (0141) are opt-in: a FellowRain3D keeps `drop_size`'s bars unless told otherwise. A FellowWeather3D that
creates its own rain node (`create_missing`, the scenes without one) left it with the bars — the heavy white rain of
milestone 35's lake by day.

## Change

A rain node the controller creates has `streak_width` 0.004 m. A scene's own rain node is left as it is.

## Verification

The drops are not deterministic from run to run (0124), and nothing else changes: by construction. The look is
milestone 41's comparisons (`m41_s25_water_streaks`, `m41_s25_bistro_streaks`: the controller's own node with
`--weather_streaks=0.004`, which is this default).

## Decision

**KEEP.**
