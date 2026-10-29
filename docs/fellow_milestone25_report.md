# FellowEngine — milestone 25 report (rain and wet surfaces)

Status: **measured** (lavapipe; RTX jobs `m25_f271b`, `m25_f272`, `m25_f273` (experiments), `m25_f274`, 2026-10-29).
Branch `fellow/geometry`. Decision record `docs/decisions/0124-rain.md`; user-facing summary
`docs/FELLOWENGINE_OPTIONS.md` ("Rain and wet surfaces").

## 0. Result

The user asked (2026-10-29), after water (milestone 24), for rain and wet surfaces, and to optimize the new features
as they go. Stock Godot has neither: a project makes its drops with particles and swaps materials by hand, and
nothing keeps the ground under a roof dry.

`FellowRain3D` (module `fellow_fluid`):

* **drops** around the camera: stock GPU particles drawn by a shader of their own — lit streaks along their velocity
  (leaning with the wind) facing the camera, dark at night but where the street lamps shine through the rain;
  colliding with the scene seen from above, splashing where they land;
* **wet materials**: every opaque Forward+ material wetted after its own code where the rain reaches it — porous
  surfaces darker, all with a glossy film, **puddles** on flat ground with the rain's **ripples** (a tiling texture
  computed on the GPU every frame), in the depth pre-pass too, so screen-space reflections show the street in them;
* **shelter**: a heightfield of the scene from above following the camera keeps the ground under roofs and trees dry
  and the drops off it;
* a `FellowWater3D` shows the ripples too.

Cost on the RTX 5070 Ti Laptop at 1080p: 0.36 ms of GPU time (after three rounds of optimization from 0.49 ms), a
third of it the screen-space reflections the wet surfaces now get.

## 1. Development

* **Rain Lab** (`--scene=rain`): a town street (asphalt, sidewalks, plaster and brick buildings with flat roofs), a
  cobbled square with a fountain basin (a FellowWater3D), an arcade along the south row (its floor must stay dry), a
  bus shelter, a parked car (metal paint: not porous), benches, trees, street lamps; `--rain=on|dry|off`,
  `--rain_light=day|dusk`, `--rain_occlusion=moved|always`, `--rain_wetness`, `--rain_drops`.
* The renderer side is a weather state on the render thread (`RendererRD::FellowWeather`): a uniform buffer and two
  textures (the heightfield, the ripples) at bindings 49-51 of the scene's set 1, and a specialization bit
  (`fellow_wetness`) so nothing changes without a rain.
* First images: the streaks invisible (1 cm wide, 15 % opaque), then a drop passing the lens as a bar across the
  screen (now not drawn within 0.5 m and faded in to 4 m), the ripples too faint (1.5 mm amplitude), then too large
  (rings of 24 cm: now 0.25 m/s for 0.55 s). Under the arcade bright specks on the ceiling: drops are stopped a step
  below the roof and their splashes fell through it; the splashes collide with the heightfield too now.
* On the RTX the unshaded drops were bright at dusk and the splashes white dots on the ground; stock's fixed-Y
  billboard stood the streaks upright whatever the wind. The drops got a shader of their own: lit, brighter against a
  light behind (forward scattering), aligned with the velocity; the splashes fade over their life.
* The shelter: stock's follow-camera mode moves the heightfield by 1 m, not whole texels (80 m / 1024), so its edges
  moved at every render. FellowRain3D moves it itself, by 5 m: whole texels, a fifth of the renders.
* MSVC: `Math::pow(double, float)` is ambiguous (the first RTX build failed).

## 2. Verification

* Without a FellowRain3D: City Lab and Rain Lab `--rain=off` bit for bit the images of `5e26af0`; with the final
  build Rain Lab `--rain=off` against `5e26af0`: 10 pixels by one level in the basin (the water's march arithmetic,
  float rounding); Water Lab within float rounding; Fluid Lab within its own run-to-run noise.
* With rain, two runs of the same build differ in a few thousand pixels (the splashes: sub-emitters' order is not
  deterministic). Each optimization of the wetting: the same image to 1-2 levels (mip selection), beyond that only
  the splashes' noise.

## 3. Performance

lavapipe (480 x 270, GPU time, first version): dry costs nothing; raining 5-12 % of the GPU time, 8-19 % of the
frame (see 0124).

RTX 5070 Ti Laptop, 1080p, autotune off, 3 runs each, pass timestamps:

| Rain Lab flythrough | frame | GPU | raining − no rain |
|---|---:|---:|---:|
| no rain (f274) | 1.889 ms | 1.763 ms | — |
| FellowRain3D dry (f271) | 1.954 ms | 1.828 ms | 0.04 ms (spread) |
| raining, first version (f271, `852c550`) | 2.398 ms | 2.170 ms | 0.49 ms |
| lit drops, ripples in two dispatches, noise from a texture (f272, `c986efd`) | 2.294 ms | 2.091 ms | 0.42 ms |
| explicit mip levels, noise only where a puddle can be (f274, `d8f2909`) | 2.243 ms | 2.039 ms | 0.36 ms |
| raining at dusk, the street lamps on (f272) | 2.434 ms | 2.232 ms | — |

The experiment `m25_f273` (the wetting cut short at three points) found the wetting's cost in the pre-pass: not its
arithmetic but five texture lookups with explicit gradients (`textureGrad`), which run at a fraction of the rate.
With explicit mip levels the wetting went from 0.175 to 0.116 ms. The rest of the 0.36 ms: SSR on the wet surfaces
0.13 ms (their roughness is now below SSR's cutoff), the particles 0.04 ms, the ripples' compute 0.04 ms (0.058 in
nine dispatches), the drops' drawing 0.03 ms; the shelter rendered every frame 0.025 ms. Captures at 1080p:
`m25_f271_rain_capture`, `m25_f272_rain_capture`; the walk `m25_f272_rain_temporal`.

Water Lab (f271): the reflection march with the clip position linear along the ray made no measurable difference
(the water's pass 1.393 ms against f270's 1.389: the march is bound by its depth lookups); 16 reflection steps
instead of 32 save 0.36 ms (frame 2.974 ms against 3.310).

## 4. Left for later

* Transparent materials are not wetted (glass; the water's own surface gets the ripples only).
* Drips and running water on walls, wet footprints, wetness maps painted by the user, puddles that grow and dry.
* SSR's share: a wet surface far away could keep its roughness above SSR's cutoff (the film's reflections are
  blurred anyway).
* Mobile and Compatibility renderers: the drops only.
