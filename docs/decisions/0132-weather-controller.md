# 0132 — A weather controller

Status: **KEEP** (a node: nothing changes without one) · Branch `fellow/geometry` · Node: `FellowWeather3D`
(`weather`, `transition_time`, `set_weather()`; the state: `wind_speed`, `gustiness`, `precipitation`, `intensity`,
`lightning_rate`, `cloud_coverage`, `cloud_density`, `sunlight`, `ambient_light`, `fog_density`, `temperature`;
`controls`, `settle_ground`, `ground_speed`, `create_missing`, `prepare_nodes`, `fog_color`, the node paths) · Code:
`modules/fellow_fluid/fellow_weather_3d.{h,cpp}`, `servers/rendering/renderer_rd/effects/fellow_weather.h`
(`SkyState`), `renderer_rd/storage_rd/light_storage.cpp` (the sun), `render_forward_clustered.cpp` (the clouds),
`scene/3d/light_3d.h` (`get_light_rid()`), `benchmarks/project/bench/main.gd`.

## Problem

Milestones 22-31 made the weather's parts: wind with gusts (0125, 0127), rain and wet surfaces (0124), snow and its
tracks (0128, 0130), lightning (0129, 0131), volumetric clouds (0109). Each is a node or a project setting of its own.
A game does not want "set the rain node's rate, the wind's speed and gusts, the clouds' coverage and density, dim the
sun, add fog, wet the ground over a minute and dry it over ten": it wants "a storm is coming", and later "it clears
up". The weather is all of them changing together, over time; and the ground lags behind the sky (it stays wet after
the rain, the snow lies until it melts).

## Change

**A state, presets, transitions.** `FellowWeather3D` holds the weather as a state: the wind's speed and gustiness,
what falls (nothing, rain or snow) and how hard, lightning strikes per minute, the clouds' coverage and density, the
share of the sunlight that gets through, the sky's light, fog, the temperature. Presets fill it in:

| weather | wind (m/s, gusts) | falls | lightning | clouds (coverage, density) | sunlight | sky light | fog (1/m) | °C |
|---|---|---|---|---|---:|---:|---:|---:|
| Clear | 2, 0.3 | — | — | 0.15, 1 | 1 | 1 | 0 | 18 |
| Cloudy | 4, 0.4 | — | — | 0.5, 1 | 0.75 | 1 | 0 | 15 |
| Overcast | 5, 0.45 | — | — | 0.9, 1.4 | 0.2 | 0.8 | 0.0004 | 12 |
| Rain | 6, 0.5 | rain 0.6 | — | 0.97, 1.8 | 0.1 | 0.7 | 0.0012 | 10 |
| Storm | 12, 0.7 | rain 1 | 5/min | 1, 2.6 | 0.05 | 0.55 | 0.0025 | 14 |
| Snow | 3, 0.35 | snow 0.6 | — | 0.95, 1.5 | 0.12 | 0.85 | 0.0015 | −3 |
| Blizzard | 14, 0.65 | snow 1 | — | 1, 2.2 | 0.06 | 0.7 | 0.01 | −10 |

Setting a value of the state yourself makes the weather Custom (a preset's values are not stored in the scene, a
custom state's are). `set_weather(weather, seconds)` turns to it: every value eased (smoothstep) from where it is
now; rain turning to snow (or back) eases off to nothing over the first half and the other sets in over the second,
so the drops are not turned into flakes in mid-air. Before the node is ready the weather is taken at once.

**What it drives** (`controls`, all by default):

* the scene's `FellowWind3D` (speed, gusts), `FellowRain3D` (rain or snow, rate) and `FellowLightning3D` (strikes):
  the node paths, or the active wind and rain, or the current scene's first lightning node. The missing ones are
  created as internal children when the weather needs them (`create_missing`), or all at the start
  (`prepare_nodes`: nothing created and compiled when the first rain comes). A rain node it created gets more drops in
  a smaller box for snow (60 000 in 30 m, 0128's showcase) and back for rain, switched at the transition's midpoint
  when nothing falls.
* the **clouds**: the coverage and density in place of the project settings', handed to the renderer
  (`FellowWeather::SkyState`, read by the clouds' pass). The clouds must be on (`post/clouds/enabled`).
* the **sun**, on the scene's surfaces only: `LightStorage::update_light_buffers()` multiplies the sun's energy in the
  directional light buffer (and its volumetric fog energy) by `sunlight`. The light's own energy is unchanged, so the
  sky, the atmosphere and the clouds — which read the light's parameters — keep the full sun: an overcast's clouds are
  lit from above, and its ground lies in their shade. The sun is the scene's first directional light that lights it
  (not a lightning flash), or `sun_path`. (`VisualInstance3D::get_base()` is not set for lights: `Light3D` now has
  `get_light_rid()`.)
* the **environment**: exponential fog added to its own (`fog_density`; the sky greys as the visibility drops:
  `fog_sky_affect` 1 − e^(−1500 · fog)), in `fog_color` as bright as the sky (the background energy, so dark at night);
  the sky's light (`ambient_light`): the background energy multiplier — the sky drawn, its ambient light and its
  reflections (the environment's `ambient_light_energy` does not scale a sky's ambient light). The environment's own
  values are its base: what a project changes meanwhile becomes the new base, and they come back when the node
  leaves the tree.
* the **ground** (the rain node's wetness and snow, its follow modes turned off): wet in the rain over
  `wetting_time`, dry after it over `drying_time` (four times slower below freezing); falling snow settles at 1 °C and
  below over `snowing_time`; above 0 °C the snow melts — over `melting_time` at 5 °C, faster when warmer, three times
  as fast in the rain — and its water wets the ground. `ground_speed` speeds it all up (a game's compressed day).
  `settle_ground`: at the start, the ground as the weather leaves it (wet in the rain, covered while it snows).

The node does nothing in the editor (it would write the weather into the scene's nodes and resources, which are
saved).

FellowBench: `--weather=<clear|cloudy|overcast|rain|storm|snow|blizzard>` in any scene; `--weather_cycle=<w>@<frame>[@<s>];...`
turns the weather at frames of temporal sequences; `--weather_times=<wet,dry,snow,melt>` (time-lapses),
`--weather_ground_speed`, `--weather_fog_color`, `--weather_controls`.

## Verification

* **Off is the previous image** (lavapipe, against milestone 31's build): Rain Lab without drops (wet, puddles) 5
  images and Water Lab 5 images with 0 pixels differing. A FellowWeather3D with `controls` 0 against none: 0 pixels.
* **The presets** (lavapipe, a test scene: ground with relief, a shelter, slopes, a sphere, a metal box): clear to
  blizzard, each settled — the shadows fading under the overcast, the ground wet in the rain, covered in the snow, the
  horizon greying in the fog. The first build dimmed nothing: it took the sun's RID from `get_base()`, which lights do
  not set. The volumetric clouds (a view of the sky): a few cumulus, a broken layer, a grey deck with gaps, a dark
  even deck for the storm.
* **A cycle** (lavapipe, clear → rain → storm → snow → clear in 20 s, `ground_speed` 30): the state and the ground
  logged every 50-100 frames — the rain easing off to nothing at the storm-to-snow midpoint and the snow setting in
  after it, the temperature through freezing, the wet ground drying slowly under the falling snow and faster after it,
  the fog off again in the clear.

* **Off is the previous image** for Water Lab too (lavapipe, against milestone 31's build): 5 images with 0 pixels
  differing.

## Results

**Cost** (RTX 5070 Ti Laptop, 1080p, Bistro exterior, 2 runs each, `m32_f297_bistro`, fellow222):

| | run 1 | run 2 |
|---|---:|---:|
| no weather | 4.666 ms | 4.323 ms |
| `FellowWeather3D`, clear | 4.263 ms | 4.297 ms |
| `FellowWeather3D`, rain | 4.821 ms | 4.821 ms |

The controller itself is not measurable: clear weather (a FellowWind3D it creates, the sun at full) is within the runs'
spread of no weather (the first run without, right after the heat soak, was the slowest of all). Its weathers cost what
their parts cost: rain in Bistro +0.5 ms (the drops, the wet surfaces and their SSR, the fog).

**Bistro by day** (`m32_s13_bistro_weather`, the day look with ray tracing, the sun at 32°, auto exposure): the same
street in each preset — the hard shadows gone under the overcast, the wet street and puddles in the rain, a strike
in view in the storm, the street under snow, the blizzard's fog. A time-lapse through them
(`m32_s13_bistro_weather_loop`, 600 frames: clear, cloudy, overcast, rain, storm, snow, clear, a second or so each,
the ground's times shortened with `--weather_times=1.5,5,1.5,2`): the clouds close in, the street darkens and wets,
two strikes, the rain turns to snow (nothing falling at the midpoint), the snow settles and melts in the returning sun,
the street drying.

## Decision

**KEEP.**
