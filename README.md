# FellowEngine

<p align="center">
  <img src="docs/showcase/bistro_night_rain_cafe.jpg" width="100%" alt="Bistro at night in the rain: FellowRain3D wetting the cobblestones, ray-traced reflections of the lamps">
</p>

**FellowEngine is a fork of [Godot Engine](https://github.com/godotengine/godot) 4.7.2-stable** that makes the
Forward+ renderer faster and adds real-time effects that stock Godot does not have: water with FFT waves, rain and wet
surfaces, snow, lightning, wind with gusts and a weather controller that changes them all over time, GPU smoke and
fire, wood that burns to char and ash, automatic HLOD, ray tracing and a set of post-processing effects.

* **Faster with the same image.** With the default settings FellowEngine renders eight of the nine benchmark scenes
  16-54 % faster than stock Godot, the ninth 3-12 % (table below). The changes that are on by default are *exact*:
  verified bit for bit against stock on lavapipe, and at the run-to-run floor on an RTX GPU.
* **New effects, off until used.** The visual features and the new nodes do nothing until a project turns them on
  or adds the node. With them off, the frame is stock's.
* **Everything behind a setting.** All behaviour sits under *Project Settings → Rendering → Fellow*
  (`rendering/fellow/*`): see [docs/FELLOWENGINE_OPTIONS.md](docs/FELLOWENGINE_OPTIONS.md). Every change has a
  decision record with its measurements in [docs/decisions/](docs/decisions/).

The work is organised in milestones, from 2026-09-30 on. See the [changelog](FELLOWENGINE_CHANGELOG.md), the milestone reports
in [docs/](docs/) and the [showcase](docs/SHOWCASE.md).

## Showcase

<p align="center">
  <img src="docs/showcase/bistro_gold_tree.jpg" width="49%" alt="Bistro at the golden hour: the atmosphere sky, light shafts through the tree, ray-traced GI">
  <img src="docs/showcase/water_pier_sunset.jpg" width="49%" alt="FellowWater3D at sunset: FFT wind waves, the glitter path, volumetric clouds">
  <img src="docs/showcase/loop_bistro_rain_street.webp" width="49%" alt="Moving down a wet Bistro street at night in the rain">
  <img src="docs/showcase/bistro_gold_cafe.jpg" width="49%" alt="Bistro's cafe in warm low sunlight">
  <img src="docs/showcase/wind_forest_backlit.jpg" width="49%" alt="FellowWind3D: a forest backlit by a low sun, light shafts between the trunks">
  <img src="docs/showcase/loop_meadow.webp" width="49%" alt="Gusts rolling over the meadow">
  <img src="docs/showcase/burn_night_logs.jpg" width="49%" alt="FellowBurnable3D: logs charring and burning with their own flames at night">
  <img src="docs/showcase/loop_fire.webp" width="49%" alt="FellowGasVolume3D: a campfire simulated on the GPU">
  <img src="docs/showcase/bistro_snow_night_cafe.jpg" width="49%" alt="FellowRain3D's snow: Bistro's cafe at night under snow, flakes in the lamplight">
  <img src="docs/showcase/bistro_snow_gold_tree.jpg" width="49%" alt="Snow in Bistro's square against a low sun: bare cobbles under the tree, light shafts">
  <img src="docs/showcase/storm_square.jpg" width="49%" alt="FellowLightning3D: a bolt over Rain Lab's square, the flash on the rain and the wet ground">
  <img src="docs/showcase/loop_storm_clouds.webp" width="49%" alt="Walking down a street in a thunderstorm, the cloud deck lit by each strike">
  <img src="docs/showcase/bistro_weather_storm_day.jpg" width="49%" alt="FellowWeather3D's thunderstorm by day in Bistro: a bolt over the roofs, the wet street lit by the flash">
  <img src="docs/showcase/loop_weather_lapse.webp" width="49%" alt="A time-lapse of the weather in Bistro: clouds and their shadows, rain, a thunderstorm, a rainbow, snow and the sun again">
  <img src="docs/showcase/bistro_rainbow_street.jpg" width="49%" alt="A double rainbow over Bistro's wet street after the rain">
  <img src="docs/showcase/loop_bistro_rainbow.webp" width="49%" alt="The rain clouds part over Bistro and a double rainbow appears">
  <img src="docs/showcase/water_rain_shafts.jpg" width="49%" alt="Curtains of rain hanging from the clouds over the far shore of a lake">
  <img src="docs/showcase/loop_water_rain_shafts.webp" width="49%" alt="Showers drifting with the clouds over the lake">
  <img src="docs/showcase/bistro_showers_street.jpg" width="49%" alt="A shower over Bistro's street, its curtain of rain hanging from the cloud">
  <img src="docs/showcase/loop_bistro_showers.webp" width="49%" alt="Showers passing over Bistro with the clouds">
</p>

These are real-time captures from the benchmark scenes on an RTX 5070 Ti Laptop GPU, with FellowEngine's visual
features, nodes and (in Bistro) ray tracing on. Captions, settings and more images are in
[docs/SHOWCASE.md](docs/SHOWCASE.md).

## Performance

Stock Godot 4.7.2 against FellowEngine with its defaults, frame time in ms. Measured on an Acer Predator PHN16S-71
(RTX 5070 Ti Laptop GPU, Core Ultra 9 275HX, Windows 11) at 1920 × 1080 with the benchmark's "high" preset. Three
runs per scene after a pass that fills the shader caches (`m35_f308_final`, build `1cb7529`, milestone 35).
FellowEngine was faster in every scene and every pair of runs. The weather of milestones 32-35 costs nothing without its
nodes: against milestone 30's build in the same session each scene was within its runs' spread (−0.3 % to +2.4 %).

| scene | stock | FellowEngine | change |
|---|---:|---:|---:|
| Sponza | 13.757 | 6.266 | **−54.5 %** |
| Bistro exterior | 6.693 | 4.258 | **−36.4 %** |
| City Lab (18 800 meshes) | 3.012 | 2.286 | **−24.1 %** |
| Bistro interior | 4.313 | 3.286 | **−23.8 %** |
| Lights Lab | 3.007 | 2.370 | **−21.2 %** |
| Foliage Lab | 3.685 | 3.024 | **−17.9 %** |
| Crowd Lab (256 animated characters) | 6.207 | 5.106 | **−17.7 %** |
| PBR Lab | 2.600 | 2.235 | **−14.0 %** |
| Stress Lab | 5.020 | 4.820 | **−4.0 %** |

Where the time goes:

* **Render thread**: parallel culling and command recording, persistent instance data, batched shadow lists.
* **GPU**: the directional shadow cache, frustum culling inside large surfaces, MultiMesh instances culled one by
  one on the GPU, and colour passes compiled without the code the frame cannot run. Also faster light clustering,
  glow, tone mapping and fog passes, async compute and parallel command buffers.
* **Autotune**: measures the scene-dependent options per scene and keeps the faster setting.

Loading is faster as well: shaders are created in parallel and only when first needed, and a scene loads to its first
frame 12-62 % faster. With the defaults and autotune off, two runs at a fixed frame rate render the same frames bit
for bit, where stock's runs differ.

Opt-in options that change the image a little, within measured limits, gain more: variable-rate shading takes Lights Lab
to −21 %. They are listed in the [options](docs/FELLOWENGINE_OPTIONS.md).

## Features

| node or setting | what it is | cost (RTX 5070 Ti Laptop, 1080p) |
|---|---|---|
| `FellowWater3D` | lakes and seas: JONSWAP wind waves simulated by FFT in cascades, foam, screen-space reflections traced over a Hi-Z depth pyramid, refraction, absorption, shore foam, caustics, the sun's glitter | 1.6 ms of Water Lab's frame; reflections 0.61 ms |
| `FellowShallowWater3D` | a mass-conserving shallow-water simulation on the GPU (rivers, obstacles, moving bodies), drawn alone or blended into a `FellowWater3D` | 512²: 0.21 ms, 1024²: 0.75-0.79 ms per step |
| `FellowRain3D` | rain around the camera: GPU drops that stop and splash on the scene, roofs sheltering the ground, wet materials, puddles with ripples, a rainbow when the sun shines on it, curtains of rain in the distance under the clouds, ice and hoarfrost below freezing; or snow: flakes drifting in the wind and a snow cover on what faces up, bare under roofs and trees, glinting in the sun, with footprints and wheel ruts (`FellowSnowTracker3D`) | 0.36 ms (Rain Lab); snowing 0.37 ms; the rainbow 0.05 ms; the rain shafts 0.05 ms |
| `FellowWind3D` | wind with gusts rolling over the landscape: plants bend, sway, flutter and shine with it; shadows and motion vectors follow; water, smoke and fire, and particles (`ParticleProcessMaterial`'s Wind group) take the same wind | 0.56 ms (Wind Lab); projects' own `TIME`-based sway costs 1.81 ms |
| `FellowLightning3D` | lightning strikes around the camera: a few return strokes flashing over the scene with shadows, a branching bolt, a lit cloud, a signal for the thunder | +0.04 ms on average with a strike every 2 s (Rain Lab) |
| `FellowWeather3D` | the weather from one node: clear, cloudy, overcast, rain, a thunderstorm, snow, a blizzard or a state of its own, changing smoothly; it drives the wind, rain, snow and lightning, the clouds, the sun on the scene, the fog and the ground (wet, drying, under snow, melting, frozen and frosted below 0 °C) | the parts it turns on (Bistro: clear weather not measurable, rain +0.5 ms) |
| `FellowGasVolume3D` | smoke and fire simulated on the GPU (sparse bricks, multigrid pressure solve), physics bodies as obstacles, blackbody flames lighting the scene | Fire Lab's three volumes (0.47 M cells): the simulation 1.58 ms per step, the drawing 0.31 ms |
| `FellowBurnable3D` | a mesh burns like wood in the gas's fire: voxels pyrolyse to char and glowing embers, the surface recedes, burns through and falls as a heap of ash and charcoal | drawing 0.35 ms (Burn Lab) |
| `FellowHLOD3D` | automatic HLOD: repeated far meshes instanced and culled on the GPU, the rest merged per cell with LODs, optional octahedral impostors | City Lab: render thread −23 to −42 % |
| `raytracing/*` | hardware ray tracing (`VK_KHR_ray_query`): ambient occlusion, sun shadows, dynamic diffuse GI with probes, reflections | — |
| `post/*` | motion blur, histogram and local exposure, contact shadows, sky atmosphere with aerial perspective, volumetric clouds with their shadows on the scene, bloom with lens dirt and flare, light shafts, sharpening, white balance, lens effects | the clouds' shadows 0.17 ms (Bistro) |

Forward+ only: the Mobile and Compatibility renderers are unchanged.

## Building

FellowEngine builds like Godot 4.7 ([compiling docs](https://docs.godotengine.org/en/stable/engine_details/development/compiling/index.html)):

```
scons platform=windows target=editor           # the editor
scons platform=windows target=template_release # an export template
```

The work happens on the `fellow/geometry` branch. The other `fellow/*` branches are earlier stages it grew from.

## Benchmarks

`benchmarks/` holds FellowBench: a Godot project with the labs used for every measurement (Sponza, Bistro, City, Lights,
PBR, Foliage, Stress, Crowd, Physics, Fire, Burn, Fluid, Water, Rain, Wind...). It also has the runner
`run_benchmarks.py`, which runs builds A/B in alternating order with heat soak, telemetry, per-camera statistics and pass
timestamps, plus analysis tools. See [benchmarks/README.md](benchmarks/README.md).

## Documentation

* [FELLOWENGINE_CHANGELOG.md](FELLOWENGINE_CHANGELOG.md): what each milestone added and measured (Godot's own changelog is [CHANGELOG.md](CHANGELOG.md)).
* [docs/FELLOWENGINE_OPTIONS.md](docs/FELLOWENGINE_OPTIONS.md): every setting and node, with its cost.
* [docs/decisions/](docs/decisions/): one record per change, covering the problem, the change, the measurements and
  the decision, including what was tried and rejected.
* `docs/fellow_milestoneN_report.md`: the milestone reports.

## Credits and license

FellowEngine is built on [Godot Engine](https://godotengine.org) by Juan Linietsky, Ariel Manzur and the Godot
contributors, released under the MIT license ([LICENSE.txt](LICENSE.txt), [COPYRIGHT.txt](COPYRIGHT.txt)).
FellowEngine's own changes are released under the same MIT license. This fork is not affiliated with or endorsed by the
Godot Foundation. The benchmark assets are not in the repository, and `benchmarks/scripts` fetches them. They keep their
own licenses: Intel New Sponza (CC BY 4.0, Intel) and Amazon Lumberyard Bistro (CC BY 4.0, Amazon Lumberyard / NVIDIA
ORCA), both shown in the showcase images.
