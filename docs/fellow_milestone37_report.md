# FellowEngine — milestone 37 report (showers)

Status: **measured** — lavapipe and the RTX 5070 Ti Laptop (`m37_f310`, `m37_s20`, 2026-11-03). Branch
`fellow/geometry`. Decision record `docs/decisions/0137-showers.md`.

## 0. Result

Under broken rain clouds it now rains at the camera only where the clouds let it: showers pass over with the clouds'
wind — the drops start, the puddles ripple and the street wets under a dense cloud, and stop in the gaps between them,
while the rain shafts (milestone 35) hang from the showers around. Opt-in (`FellowRain3D.showers`, 0 by default).

## 1. Development

* A compute pass of one thread, after the clouds' shadow map, evaluates the clouds' layer at the camera (the weather
  field at this frame's time and wind, the coverage the clouds pass draws) and writes the shafts' share of the rain
  there (local coverage 0.7 to 0.95) into a 16-byte buffer, read back asynchronously; a reading older than half a
  second counts as no clouds.
* The rain node mixes it in by `showers`, eased over 2 s (at the first reading at once): the drops, ripples, wetting
  and settling snow take the rain here; the shafts and the rainbow keep the rain set (in a gap the bow stands in the
  showers around). A FellowWeather3D's ground model wets, covers and frosts by the rain here. `get_rain_here()`.
* The probe runs only while a raining node asks with the clouds on: at `showers` 0 nothing changes.

## 2. Verification

* Off is the previous image (lavapipe, against milestone 36's build): Rain Lab raining without drops under the
  clouds — 0 pixels differing; `showers` 0 keeps the rain here at 1.
* Lavapipe, the test scene in the controller's rain under the clouds, the clouds' wind 0.2 km/s, 30 s: at coverage 0.82
  a shower over the first four seconds (the rain here up to 0.83), dry from the seventh to the twentieth, a second
  shower from the twenty-first.

## 3. Performance

Water Lab raining under the clouds at coverage 0.82, 1080p, 2 runs each (`m37_f310_showers`, fellow231): the probe
0.010 ms; the frame 4.149 → 4.120 ms.

## 4. Showcase

A shower passing over Bistro (`m37_s20_bistro_showers_loop`; `bistro_showers_street.jpg`, `loop_bistro_showers.webp`):
the street dry under a gap, the shower arriving with its curtain hanging from the cloud over the roofs, the drops and
the wet cobbles, the sky brightening again as it moves on — 30 s of weather in 7.5 s. Over the lake
(`m37_s20_water_showers_loop`) the camera stood under rain most of the time.
