# FellowEngine — milestone 32 report (a weather controller)

Status: **measured** — lavapipe and the RTX 5070 Ti Laptop (`m32_f297`, `m32_s13`, 2026-11-03). Branch
`fellow/geometry`. Decision record `docs/decisions/0132-weather-controller.md`.

## 0. Result

Milestones 22-31 made the weather's parts — wind, rain and wet surfaces, snow and tracks, lightning, volumetric
clouds — each a node or a setting of its own. `FellowWeather3D` is the weather from one node: a preset (clear,
cloudy, overcast, rain, storm, snow, blizzard) or a custom state, and `set_weather(weather, seconds)` to change it
smoothly. It drives the wind, the rain or snow, the lightning, the clouds' coverage and density, the sun on the
scene's surfaces, the environment's fog and sky light, and the ground: wet in the rain, drying after it, covered by
falling snow below freezing and melting above it.

## 1. Development

* **The state and its presets**: wind speed and gusts, what falls and how hard, strikes per minute, cloud coverage
  and density, sunlight through the clouds, the sky's light, fog, temperature (the table in decision 0132). A value
  set on its own makes the weather Custom; only a custom state is stored in the scene.
* **Transitions**: every value eased from where it is; rain turning to snow eases off over the first half and the
  snow sets in over the second, so no drop becomes a flake in mid-air.
* **The nodes**: the scene's own (paths, the active wind and rain, the first lightning), or created as internal
  children when the weather needs them (`create_missing`), or all at the start (`prepare_nodes`).
* **The clouds** through a render-side override (`FellowWeather::SkyState`), not the project settings — writing those
  at run time would bump their version every frame, and in the editor save them into `project.godot`.
* **The sun on surfaces only**: the directional light buffer's energy times `sunlight` (`light_storage.cpp`), so the
  sky, the atmosphere and the clouds — which read the light's own parameters — stay lit by the full sun. The first
  build dimmed nothing: it identified the sun by `VisualInstance3D::get_base()`, which lights do not set; `Light3D` now
  has `get_light_rid()`.
* **The sky's light**: the environment's background energy. `ambient_light_energy`, tried first, does not scale a sky's
  ambient light (Godot multiplies it into the ambient colour only).
* **Fog**: added to the environment's own, in `fog_color` scaled by the background energy (dark at night), the sky
  greying with it. What a project changes in the environment meanwhile becomes its new base; it gets its values back
  when the node leaves the tree.
* **The ground**: the controller sets the rain node's wetness and snow itself (its follow modes off) — wetting in the
  rain, drying slower below freezing, snow settling at 1 °C and below, melting above 0 °C (faster when warmer and in the
  rain) and wetting the ground as it melts. `ground_speed` speeds it up; `settle_ground` starts it settled.
* Nothing runs in the editor (it would write the weather into nodes and resources that are saved).
* FellowBench: `--weather=<name>`, `--weather_cycle=<w>@<frame>[@<s>];...` (temporal sequences),
  `--weather_times=<wet,dry,snow,melt>`, `--weather_ground_speed`, `--weather_fog_color`, `--weather_controls`.

## 2. Verification

* Off is the previous image (lavapipe, against milestone 31's build): Rain Lab without drops 5 images, Water Lab 5
  images, 0 pixels differing. A FellowWeather3D with no controls against none: 0 pixels.
* Lavapipe test scene: each preset settled (the overcast's faint shadows, the rain's wet ground, the snow cover, the
  fog); the clouds of each from the ground (a few cumulus, a broken layer, a grey deck with gaps, a dark even deck); a
  cycle clear → rain → storm → snow → clear in 20 s with the state and the ground logged as they turned.
* PBR Lab on lavapipe: `--weather_cycle` and `--weather_times` in a temporal sequence (the bench's flags).

## 3. Performance

Bistro exterior, 1080p, 2 runs each (`m32_f297_bistro`, fellow222): no weather 4.666 / 4.323 ms, clear 4.263 /
4.297 ms, rain 4.821 / 4.821 ms. The controller itself is not measurable; its weathers cost their parts (rain in
Bistro +0.5 ms).

## 4. Showcase

Bistro by day under each preset (`m32_s13_bistro_weather`; `bistro_weather_grid.jpg`, `bistro_weather_storm_street.jpg`)
and a time-lapse through them in 10 s (`m32_s13_bistro_weather_loop`; `loop_weather_lapse.webp`, in the README):
[SHOWCASE.md](SHOWCASE.md).

## 5. Left for later

* The clouds cast no shadows: the sun is dimmed evenly by `sunlight`, so a cloudy day has no moving shadows and an
  overcast's gaps no sunny patches (milestone 33).
* The sky's light keeps the clear sky's colour under an overcast (only its energy changes); the sky's radiance does not
  see the clouds.
