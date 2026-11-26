# 0356 — The overcast weather's light: the indirect light and the air take the weather's sun, the sky's light turns overcast

Status: **KEEP** · Branch `fellow/geometry` · Code: `servers/rendering/renderer_rd/environment/gi.cpp` (SDFGI's and
VoxelGI's sun), `servers/rendering/renderer_rd/effects/fellow_weather.h` (`indirect_light_factor()`),
`tools/fellow_regression` (`base/weather_gi`, new);
`servers/rendering/renderer_rd/forward_clustered/render_forward_clustered.{h,cpp}` (`_fellow_process_cloud_shadow`:
`fellow_weather_overcast`; `_fellow_update_atmosphere`: the aerial perspective's sun); `showcase/shaders/atmosphere_sky.gdshader`
(the overcast sky's light), `showcase/project.godot` (the global), `showcase/scripts/rooftops_weather.gd` (the sky's
light not dimmed with the SkySun; the eye adapting).

## Problem

Milestone 211's weather on the rooftops square: under snow, rain or a storm the square stayed lit as on a fair
evening — golden, the bell tower's shadow sharp across the houses' fronts (the RTX's frames, f601-f603). The weather
dimmed the sun on the surfaces (decision 0132: 12 % of it in the snow, 5 % in a storm), but:

* **SDFGI** (the scene's indirect light, on in the rooftops and the main showcase) took the scene's directional lights
  at their whole energy (`GI::SDFGI::pre_process_gi()`, Godot's): the sunlit stone it bounces was the fair day's, and
  near a surface SDFGI's light carries the surface's own lit pattern — the facade under the snow lit 62 % as much as
  on the fair day. VoxelGI the same.
* **The sky's light on the scene** (its radiance map: the ambient light, the reflections, SDFGI's and DDGI's sky, the
  smoke's) kept the clear golden-hour sky's colour and shape under a full cloud deck: decision 0133 greys it only on
  the surfaces that take the radiance map themselves (not through SDFGI), only with the clouds' shadow map on.
* **The air** (the atmosphere's aerial perspective, decision 0108) kept the whole sun's in-scattering.
* The rooftops' own sky sun (rooftops.gd's SkySun, which the weather keys dimmed by 85 % of the weather's dimming for
  the clouds' sake) took the sky's light on the square down with it (to a quarter under the snow).

## Change

* **The indirect light takes the weather's sunlight** (`FellowWeather::indirect_light_factor(light)`: the weather's
  `sun_factor` for its sun, 1 for any other light or without a weather): SDFGI's and VoxelGI's directional lights, and
  the atmosphere's aerial perspective. With the clouds' shadow map on (decision 0133) the surfaces take the map and
  these the weather's mean share. And SDFGI's and VoxelGI's sun is coloured by the atmosphere (decision 0108's tint,
  which the surfaces' sun had and theirs had not).
* **How overcast the sky is, for the sky's shader** (`fellow_weather_overcast`, a float shader global the project
  declares, as the atmosphere's tables): the clouds' coverage — a FellowWeather3D's, or the project's — from half the
  sky (broken clouds: the clear sky's light between them) to 95 %, times `clouds/sky_light`; set as it changes (by a
  hundredth), the sky's radiance map redrawn with it.
* **The showcase's atmosphere sky** (`atmosphere_sky.gdshader`, the rooftops' and the main showcase's) turns its light
  on the scene (the radiance map only: the sky seen is behind the clouds) to an overcast sky's as it is told:
  grey; three times as bright overhead as at the horizon (the CIE's standard overcast sky, 1 + 2 sin(elevation)); the
  ground under the horizon lit by it (albedo 0.3); as much light on the ground as the clear sky's own (its sky-view
  table summed over the upper hemisphere, 32 directions) plus a third of the sun's through the air (what a cloud deck
  passes on of the light over it, scattered).
* **The rooftops** (`rooftops_weather.gd`): the sky's light on the square no longer dimmed with the SkySun (the sky
  shader's `radiance_scale` makes up for its dimming; the weather's own ambient light dims it); and the eye adapts to
  the weather — the exposure raised by 0.4 of the stops the frame darkens by under each weather (measured on the
  RTX with the eye not adapting: 0.3 cloudy, 2.4 overcast, 3.5 rain, 3.9 storm, 1.9 snow, 2.1 a blizzard), eased as the
  weather turns; the fair weather's exposure as it was. And less sun under a closed deck than FellowWeather3D's presets
  give (5 % overcast, 3 % rain, 1.5 % a storm, 5 % snow, 2 % a blizzard: theirs, 20-5 %, left the bell tower's shadow
  sharp across the houses once the eye had adapted).

## Measurements

The RTX 5070 Ti laptop, 1280x720, the square from its corner (climb_look `--view=square`, the frame at 4 s): the mean
sRGB of CasaCrema's sunlit front, of its front in the bell tower's shade and of the square's ground; and the frame's
mean luminance under the sky, in stops below the fair evening's.

| snow (key 6) | the lit front | in the tower's shade | the ground |
|---|---|---|---|
| the fair evening (all of them alike) | 168, 139, 108 | 66, 48, 36 | 132, 107, 86 |
| milestone 211 (f601) | 133, 98, 66 | 58, 38, 23 | 135, 108, 87 (golden) |
| the sky's light kept and overcast (f603) | 135, 100, 69 | 62, 43, 27 | 142, 119, 101 |
| SDFGI the weather's sun (f604) | 98, 74, 50 | 41, 31, 21 | 110, 98, 89 |
| less sun, the eye adapting (f609) | 102, 79, 57 | 57, 47, 36 | 139, 135, 132 (white) |

The front's sunlit part over its shaded part (the frame's linear luminance, after the tone curve): the fair evening
8.2, the snow of milestone 211 6.0 — SDFGI's bounce carried the fair day's sun — with the overcast sky light 5.2, with
SDFGI under the weather 5.1 (the weather's 12 % sun still sharp), with less sun and the eye adapting 2.9.

| stops below the fair evening | cloudy | overcast | rain | storm | snow | blizzard | night |
|---|---|---|---|---|---|---|---|
| the eye not adapting (f607) | 0.3 | 2.4 | 3.5 | 3.9 | 1.9 | 2.1 | — |
| adapting 0.6 (f608) | 0.1 | 0.1 | −0.1 | −0.2 | 0.1 | 0.1 | 3.6 |
| **adapting 0.4 (f609)** | 0.2 | 0.9 | 1.1 | 1.2 | 0.7 | 0.8 | — |

* **The main showcase** (its valley at 10:30, no SDFGI: the overcast sky's light and the air; job f615, the last frame of
  5 s, the frame's mean sRGB and its lower half's — the near ground): clear weather identical (121.5, 126.7, 95.0
  both); overcast 62, 63, 49 → 80, 82, 62 (the ground 36, 37, 19 → 64, 66, 39); a storm 36, 37, 34 → 50, 52, 43 (the
  ground 15, 15, 8 → 37, 38, 23: it had been black); snow 64, 65, 61 → 93, 95, 87 (the ground 43, 44, 37 → 86, 89, 75):
  the clear sky's light, greyed, had lit the ground from the horizon; the overcast sky's comes from above.
* `clouds/sky_light` 0 (the overcast light off) under the snow: the ground (140, 138, 147) bluer than with it (151, 143,
  137): the clear golden-hour sky's blue in the shade.
* The regression suite (lavapipe): 39 of its 40 scenes identical; ledge_corners differed already (the climber's legs at
  the corner since milestones 198-208, blessed again); none of its scenes had SDFGI under a weather — a new one,
  `base/weather_gi` (two walls and a red block in SDFGI's light, a FellowWeather3D overcast on the sun only): with
  `indirect_light_factor()` returning 1 (the bug put back, a check) 10.4 % of its pixels differ from its golden by more
  than 8 levels (at most 30: the walls lit by the fair day's bounce). The demo's path unchanged (0.1 mm over 60 s). The overcast light costs nothing while the coverage holds (the radiance map
  is redrawn only as it changes by a hundredth).

## Decision

KEEP: the weather's sun in SDFGI, VoxelGI and the aerial perspective; the atmosphere's tint on SDFGI's and VoxelGI's
sun; `fellow_weather_overcast` and the showcase sky's overcast light; the rooftops' sky light and eye adaptation.

## Rejected / not done

* The overcast light made in the engine on the radiance map itself (a pass after the sky is drawn into it): every sky
  shader would take it, but it needs the clear sky's light on the ground and the sun's in the radiance map's units
  inside Godot's sky passes (incremental, a layer a frame); the sky's shader has both at hand.
* FellowEngine's histogram eye adaptation (decision 0105) for the weather: it would also change the fair weather's
  views the owner chose (brightening every shaded street); the weather keys know how dark each weather is.
