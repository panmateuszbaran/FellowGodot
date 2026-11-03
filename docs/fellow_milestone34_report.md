# FellowEngine — milestone 34 report (rainbows)

Status: **measured** — lavapipe and the RTX 5070 Ti Laptop (`m34_f305`, `m34_s17`, 2026-11-03). Branch
`fellow/geometry`. Decision record `docs/decisions/0134-rainbows.md`.

## 0. Result

When the sun shines on falling rain, a rainbow stands opposite it: the primary bow, red outside and violet inside,
the fainter secondary above it with its colours reversed, the sky brighter inside the primary and darker between
them. From each wavelength's refraction in water, not painted on. It appears as rain clouds clear and the sun comes
out behind the camera, and fades as the rain moves away.

## 1. Development

* `FellowRain3D` keeps the rain it has seen lately (fading over 90 s once it stops, none once it snows); `rainbow` (1)
  scales it.
* A table of the bows' colour by angle (Descartes' angles per wavelength from water's dispersion, the 1 / √ brightening
  beside each bow softened by the sun's disc and the drops' diffraction, the CIE matching functions, pulled into the
  sRGB gamut), built once.
* A compute pass after the clouds adds it by each pixel's angle from the point opposite the sun, as bright as the
  sun's light × 0.12, times the rain in front (all of it for the sky, by distance for a surface) and the sun the rain
  gets (the clouds' shadow at the camera, or a FellowWeather3D's sunlight).
* At 0.03 × the sun's light the bow was barely seen against a clear sky (lavapipe); 0.12.

## 2. Verification

* Off is the previous image (lavapipe, against milestone 32's build): Water Lab, and Rain Lab at dusk with clouds,
  their shadows and sky light off and no drops — 0 pixels differing.
* Lavapipe, the test scene with the sun 15° up behind the camera: no bow in the controller's rain (the sun in the
  clouds' shadow), the two bows once it turns clear.

## 3. Performance

Bistro exterior raining under a FellowWeather3D with the sun behind the cameras, 1080p, 2 runs each
(`m34_f305_rainbow`, fellow228): the pass 0.045 ms, the frame +0.07 ms (4.82 → 4.89 ms).

## 4. Showcase

Bistro as the rain clears (`m34_s17_bistro_rainbow_loop`; `loop_bistro_rainbow.webp`, `bistro_rainbow_street.jpg`):
the clouds part, the sun comes out on the wet street, the two bows stand over it. In the weather time-lapse a bow
follows the storm, and a strike lights the street by day (`bistro_weather_storm_day.jpg`).
