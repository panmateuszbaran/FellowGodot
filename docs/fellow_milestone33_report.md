# FellowEngine — milestone 33 report (the clouds' shadows and the sky's light under them)

Status: **measured** — lavapipe and the RTX 5070 Ti Laptop (`m33_f298`, `m33_f302`-`f305`, `m33_s17`, 2026-11-03).
Branch `fellow/geometry`. Decision record `docs/decisions/0133-cloud-shadows.md`.

## 0. Result

The volumetric clouds now shade the scene: a shadow map of the sun through the cloud layer, 16 km around the camera,
darkens the sun on every surface under a cloud — patches that come and go on a cloudy day, sunny gaps under an
overcast, the whole street in shade under rain clouds. Where the clouds cover the sky, its light turns grey: the
ambient light and the sky's reflections lose the clear sky's blue, so a wet street under rain clouds no longer mirrors
a blue sky. A FellowWeather3D's coverage drives both; its even `sunlight` factor steps aside while the shadows are on.

## 1. Development

* The map: a compute pass of 32 steps through the layer per texel (512², 31 m texels), with the clouds' own density
  (the weather field, shapes and eroded edges), at the frame's time and wind; snapped to whole texels; the clear
  sky's share beyond its edge.
* The colour passes (a new specialization bit, `fellow_cloud_shadow`) multiply the sun's shadow by it, the point moved
  down along the sun to the ground first; and grey the sky's ambient light and, above the horizon, its reflections
  (the radiance map's blurriest level for the reflections' grey: no sun disc).
* Without the eroded edges the shadows were fatter than the clouds: the ground stayed dark where the sun showed past a
  cloud's edge (lavapipe, walking under the layer looking at the sun).
* **The device lost with DDGI** (RTX): reading the map written earlier in the same frame lost the device whenever
  ray-traced GI was on; found with a diagnostic build (`m33_f302`-`f304`), avoided by double-buffering the map (the
  colour passes read the previous frame's). The cause in the frame's graph is not found yet.

## 2. Verification

* Off is the previous image (lavapipe, against milestone 32's build): Rain Lab without drops, Water Lab, and Rain Lab
  at dusk under clouds with the shadows and the sky light off — 0 pixels differing.
* Lavapipe: an 8 km plain under a broken layer (shadow patches with soft edges), seen from 6 km up (the patches and the
  fade beyond the map), walking under it towards the sun (shadow where the sun is behind a cloud), rain under a deck
  (the blue tint gone from the wet ground with the sky light).

## 3. Performance

Bistro exterior under the clouds, 1080p, 2 runs each (`m33_f305_bistro`, fellow228): the map's pass 0.12 ms, the frame
+0.17 ms in the second runs (4.389 → 4.555 ms; the first run without the shadows, 4.906 ms, warming up).

## 4. Showcase

The weather time-lapse again (`m34_s17_bistro_weather_loop`, `loop_weather_lapse.webp`, in the README): the street
goes into the clouds' shade as they close in and back into the sun as they part, the sun behind the camera this time
(a rainbow follows the storm, milestone 34). Bistro is a few hundred metres across, so a still shows it under one
cloud or in one gap (`m33_s17_bistro_cloud_shadows`); a loop of passing clouds at a time-lapse's wind is left for later.

## 5. Left for later

* The cause of the device lost with DDGI and a map written and read in one frame.
* The sky's radiance map itself does not see the clouds (only its use in the colour passes is greyed): ray-traced
  reflections that miss see the clear sky.
