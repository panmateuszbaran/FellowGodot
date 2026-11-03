# 0134 — Rainbows

Status: **KEEP** (with a FellowRain3D that rains: `rainbow` 1; nothing without rain) · Branch `fellow/geometry` · Node:
`FellowRain3D.rainbow` · Code: `servers/rendering/renderer_rd/effects/fellow_rainbow.{h,cpp}`,
`renderer_rd/shaders/effects/fellow_rainbow.glsl`, `effects/fellow_weather.h` (`State::rainbow`),
`forward_clustered/render_forward_clustered.{h,cpp}` (`_fellow_process_rainbow()`),
`modules/fellow_fluid/fellow_rain_3d.{h,cpp}`, `benchmarks/project/bench/main.gd` (`--weather_rainbow`).

## Problem

Milestones 25-33 rain in the sun: a shower under broken clouds, a storm clearing with the sun low behind the camera.
That is when a rainbow stands in the rain — and nothing draws one.

## Change

**When.** `FellowRain3D` keeps the rain it has seen lately: the rain's rate while it rains, fading over 90 s once it
stops (the rain moving away while it still falls in the distance); nothing once it snows. Its `rainbow` (1) times that
goes to the renderer (`FellowWeather::State::rainbow`). The pass runs while it is above zero and the sun (the clouds'
light: the first directional light that lights the sky) is above the horizon.

**The bows.** A table of the bows' colour by angle from the point opposite the sun (30-60°, 512 entries), built once:
for each wavelength (380-720 nm), water's index of refraction (n = 1.3240 + 3046 / λ²: 1.343 at 400 nm, 1.330 at 700
nm) gives Descartes' angles — the primary bow's 4r − 2i at the incidence of least deviation (cos² i = (n² − 1) / 3:
40.7° violet to 42.5° red), the secondary's π + 2i − 6r (cos² i = (n² − 1) / 8: 50.2° red to 53.4° violet). Geometric
optics brightens each bow towards its angle as 1 / √ of the distance from it — on the inner side for the primary (the
sky inside it brighter, nothing just outside it: Alexander's dark band), on the outer side for the secondary, 0.43 of
the primary's light — softened by the sun's disc and the drops' diffraction (0.35°, 0.5°) and fading over 4°. Summed
through the CIE matching functions (Wyman, Sloan and Shirley's fit) to linear sRGB and pulled into the gamut towards
the grey of the same luminance (a bow's colours are purer than sRGB's); the primary's brightest luminance 1.

**The pass.** After the clouds' composite, before the transparent pass, a compute pass adds to each pixel the table at
its angle from the point opposite the sun, times the sun's light (exposed as the scene's lights are) × 0.12 × the
rain's rainbow, times the rain in front of it — all of it for the sky, `1 − 2^(−distance / 150 m)` for a surface — and
the sun the rain gets: the clouds' shadow at the camera (0133), or a FellowWeather3D's `sunlight` without it (0132).
Under rain clouds the bow is gone; as they clear and the sun comes out, it appears.

## Verification

* **Off is the previous image** (lavapipe, against milestone 32's build): Water Lab (5 images), and Rain Lab at dusk
  with the clouds on but their shadows and sky light off and no drops (5) — 0 pixels differing.
* **Lavapipe, the test scene** (the sun 15° up behind the camera, clouds on): in the controller's rain no bow (the
  sun in the clouds' shadow); turned to clear, the primary bow — red outside, violet inside — the secondary above it,
  the sky brighter inside the primary and darker between them. At 0.03 × the sun's light the bow was barely seen
  against a clear sky; at 0.12 it reads as a rainbow does.

## Results

**Cost** (RTX 5070 Ti Laptop, 1080p, Bistro exterior raining under a FellowWeather3D, the sun 14° up behind the
cameras, 2 runs each, `m34_f305_rainbow`, fellow228): the pass 0.045 ms; the frame 4.824 / 4.818 ms with
`--weather_rainbow=0`, 4.893 / 4.884 ms with the rainbow (+0.07 ms).

**Bistro as the rain clears** (`m34_s17_bistro_rainbow_loop`: the controller's rain turning to clear over 2.5 s, the
sun 14° up behind the camera, the day look with ray tracing): the rain clouds part, the sun comes out on the wet
street, and the primary and secondary bows stand over it; in the weather time-lapse a bow follows the storm.

## Decision

**KEEP.**
