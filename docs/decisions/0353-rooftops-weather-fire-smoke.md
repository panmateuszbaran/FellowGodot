# 0353 — The square's weather on the keys, a brazier burning, the chimneys smoking

Status: **KEEP** · Branch `fellow/geometry` · Code: `modules/fellow_fluid/fellow_burnable_3d.cpp` (the gas volume found
round the object itself), `modules/fellow_fluid/fellow_weather_3d.{h,cpp}` (a depth fog drawn in),
`servers/rendering/renderer_rd/forward_clustered/render_forward_clustered.cpp` (`_fellow_rt_lighting`: the weather's
sunlight); `showcase/scripts/rooftops_weather.gd` (new: the keys, the depth fog, the sky's sun, the
lamps, the rain and snow seen), `showcase/scripts/rooftops_fire.gd` (new: a brazier or a chimney's smoke);
`showcase/rooftops/rooftops.tscn`, `tools/build_rooftops.gd` (`_weather`: Rain, Lightning, Weather, WeatherKeys,
Brazier, ChimneySmoke); `tools/climb_look.gd` (`--program=weather`, `--view`, `--weather_seq`,
`--weather_transition`, `--drop`, `--draw_all`, `--perf`, `--setting`, `--env_set`).

## Problem

The owner: "can we also show fire, smoke and the weather in this showcase?" The rooftops square had one weather — the
golden hour's towering cumulus — and nothing burning. FellowEngine's weather (`FellowWeather3D` driving the wind, the
rain or snow, lightning, the clouds, the sun's share, the ground wet or snowy) and its gas (`FellowGasVolume3D`: fire
and smoke on the GPU) were shown only in the main showcase's valley.

## Change

* **The weather on the keys** (`rooftops_weather.gd`): 1 the scene's own fair weather (its cumulus: a custom weather
  with the scene's coverage 0.45, density 2.6, wind 3.5 m/s), 2 cloudy, 3 overcast, 4 rain, 5 a storm with lightning,
  6 snow, 7 a blizzard — over 10 s — and 0 the weather changing by itself (fair, cloudy, rain, storm, rain, overcast,
  fair, snow, blizzard, snow, 25 s each); F1 the keys. A `FellowRain3D` (wet in 40 s, dry in 4 min, white in 2 min of
  snow) and a `FellowLightning3D` in the scene for it. A weather chosen at once (`--weather=`) leaves the ground as it
  would have: wet in the rain, white in the snow.
* **The engine** (`FellowWeather3D`): it thickened an exponential fog only; the square's is a depth fog from 150 m to
  3 km. An environment's own depth fog is now drawn in to the visibility the weather's fog leaves (3 / its density: its
  end no further, its begin a twentieth of that at most), its colour turning to the weather's fog colour and the sky
  veiled as the weather's fog thickens; restored when the node leaves. And FellowEngine's ray-traced lighting inputs
  (the smoke's sun, DDGI, the screen gather, the RT reflections) take the weather's sunlight on the sun, as the
  surfaces do — a chimney's smoke under an overcast sky had the whole sun.
* **What the weather does not reach in this scene, done for it**:
  * the sky's own sun (rooftops.gd's SkySun, which lights the clouds and, through the sky, the square — the weather
    dims the Sun on the ground only): dimmed 0.85 of the weather's dimming and greyed (a storm's light had stayed
    golden);
  * the square's lamps: up to 2.4 times their light under the darkest sky.
* **Seen** (found on the RTX at 1280x720): the rain's streaks 1 cm wide (the main showcase's 4 mm were not seen at
  720p), the snow's flakes 3 cm, 60,000 of them in a 30 m box when it snows (24,000 in 40 m: a few specks).
* **A brazier** on the square (`rooftops_fire.gd`, kind "brazier", at (−7.2, 0, 4.2)): an iron bowl on three legs, a bed
  of embers, three logs (`FellowBurnable3D`, their gas volume named; burning slowly — a third of the default's
  pyrolysis rate — and one burnt half to ash replaced), a collider; a gas volume (3 by 5.5 by 3 m, 56 cells along its
  height, prewarmed, a fire light with shadows) fed by kindling in the bowl. The rain or snow on it damps it: its fuel
  down to 30 %, its smoke up to five times.
* **The engine** (`FellowBurnable3D`): left to find its gas volume (no `gas_volume_path`), a burnable took the volume
  holding its voxel box's middle — but the box reaches down to the ground under the object for its ash heap, so the
  brazier's logs looked 0.36 m under themselves, below the gas's floor, found no gas and stayed cold. It now looks from
  the object's own middle (its mesh's).
* **Smoke from three chimneys** (CasaCrema, PalazzoOcra, CasaOcraBassa; kind "chimney"): Fire Lab's chimney — warm
  smoke pushed up out of the flue under the cap, lasting, carried off — in a box 6 by 8 by 6 m (48 cells along its
  height) moved 1.5 m downwind. A wood fire's smoke, pale blue-grey (its colour 0.82: it scatters most of the light it
  takes; density 1.6, self-shadow 0.6, the flue's smoke 3.0): Fire Lab's grey soot (0.5, density 3.2) was a black
  plume against the sky where the bell tower's shadow falls on CasaCrema's (the sun 24° up behind the tower: the scene's
  shadow on the smoke is right; without it the plume was lit through the tower).
* **The wind**: each volume takes the scene's wind (its gusts too) times a share, at most 2.5 m/s — the square's
  houses shelter the brazier (0.2: a blizzard's 14 m/s had laid its flames flat along the bowl), the chimneys' plumes
  are bent and carried off (0.6). Each sleeps (frozen, not drawn) beyond 70 m of the camera.

## Measurements

| the brazier (RTX, fair, from 1 s) | logs burning | the fire's power |
|---|---|---|
| its gas volume left to be found (before) | 0 voxels, 293 K | 0.3-0.5, flames flat along the bowl |
| named / found round the log (after) | 571 → 1,388 voxels by 8 s, 2,450 K | 0.7 → 7.4 |

| the depth fog (headless) | fair | rain | storm | blizzard |
|---|---|---|---|---|
| begin - end | 150 - 3,000 m | 125 - 2,500 m | 60 - 1,200 m | 15 - 300 m |
| colour | the haze (0.85, 0.72, 0.58) | grey (0.44, 0.44, 0.45) | dark grey (0.30, 0.31, 0.34) | grey-blue (0.36, 0.39, 0.42) |

| CasaCrema's plume in the tower's shade (RTX, fair, its core's mean sRGB) | the scene's shadow on it | without |
|---|---|---|
| Fire Lab's soot (0.5, density 3.2) | 74, 71, 75 (black against the sky's 183, 201, 216) | 145, 128, 113 |
| its ambient light 4 times (a check) | 111, 113, 121 | — |
| a wood fire's (0.82, density 1.6, self-shadow 0.6) | see the films (f602) | 195, 178, 161 |

| the frame (RTX laptop, 1280x720, the square from its corner, fair) | GPU | render CPU |
|---|---|---|
| everything | 7.10 ms | 2.55 ms |
| without the brazier and the chimneys' gas | 6.23 ms | 1.64 ms |

* The demo's path unchanged: identical from the house, and the whole demo (60 s) identical with and without the new
  nodes; it passes no nearer than 10.1 m from the brazier.
* The image regression suite's fire, rain and lightning scenes identical (no FellowWeather3D in them).

## Decision

KEEP: the weather on the keys, the brazier, the chimneys' smoke in the rooftops showcase; the burnable's lookup.

## Rejected / not done

* The gas volumes on the scene's whole wind (`wind_from_scene`): the brazier's fire blown out of its box before it
  burnt; the plumes torn off at the box's side.
* The rooftops' depth fog swapped for an exponential one (which `FellowWeather3D` thickens itself): the golden hour's
  look — a haze from 150 m, none nearer — is the depth fog's; the weather's visibility is mapped onto it instead.
