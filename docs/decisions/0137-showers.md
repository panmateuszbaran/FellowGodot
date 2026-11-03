# 0137 — Showers: the rain falls where the clouds let it

Status: **KEEP** (opt-in: `FellowRain3D.showers` 0 by default — the rain falls everywhere as it is set) · Branch
`fellow/geometry` · Node: `FellowRain3D.showers`, `get_rain_here()` · Code:
`servers/rendering/renderer_rd/shaders/effects/fellow_clouds.glsl` (`MODE_PROBE`), `effects/fellow_clouds.{h,cpp}`
(`process_probe()`), `effects/fellow_weather.{h,cpp}` (`State::showers`, `set_rain_here()`, `get_rain_here()`),
`forward_clustered/render_forward_clustered.cpp` (`_fellow_process_cloud_shadow()`),
`modules/fellow_fluid/fellow_rain_3d.{h,cpp}`, `fellow_weather_3d.cpp` (`_apply_ground()`),
`benchmarks/project/bench/main.gd` (`--weather_showers`).

## Problem

Under a broken layer of showers the rain shafts (0135) hang from the dense clouds in the distance — and it rains at
the camera all the same, wherever it stands: under a gap in the sun, the drops fall, the puddles ripple and the street
gets wet. A shower does not pass over: it rains or it does not, everywhere at once.

## Change

**The probe.** While a raining FellowRain3D asks (`showers` above 0) and the volumetric clouds are on, a compute pass
of one thread, after the clouds' shadow map (0133), evaluates the clouds' layer at the camera — the weather field the
clouds stand on, at this frame's time and wind, the layer the clouds pass draws (a FellowWeather3D's coverage or the
project's) — and writes the share of the rain the shafts let fall there (0135: none below 0.7 of the local coverage,
all of it from 0.95) into a 16-byte buffer, read back asynchronously a frame or two later
(`FellowWeather::set_rain_here()`). A reading older than half a second counts as unknown (no clouds).

**The rain node.** `FellowRain3D.showers` (0-1) mixes it in: the rain here is `rain × lerp(1, probed, showers)`,
eased over 2 s (at the first reading at once). The drops' amount, the ripples, the wetting and the snow's settling
take the rain here; the rain shafts and the rainbow take the rain set (under a gap the bow stands in the showers
around). A FellowWeather3D's ground model wets, covers and frosts the ground by the rain here. `get_rain_here()`
returns the share. Without the clouds, or before the first reading, the rain falls as it is set; at `showers` 0 the
share stays 1 (the rain the same bit for bit, the probe not run).

## Verification

* **Off is the previous image** (lavapipe, against milestone 36's build): Rain Lab raining without drops under the
  volumetric clouds, `showers` 0 (5 images) — 0 pixels differing.
* **Lavapipe, the test scene** in the controller's rain under the clouds, `showers` 1, the clouds' wind 0.2 km/s
  (each second for 30 s, the rain here eased): at coverage 0.82 a shower over the first four seconds (0.27, 0.55,
  0.73, 0.83), the camera dry from the seventh to the twentieth (down to 0.000), a second shower from the twenty-first
  (0.12 to 0.69); at coverage 0.75 and 0.3 km/s, mostly dry (one shower brushing past at 0.11). `showers` 0: 1
  throughout.

## Results

(RTX: to be filled.)

## Decision

**KEEP.**
