# FellowEngine

<p align="center">
  <img src="docs/showcase/bistro_night_rain_cafe.jpg" width="100%" alt="Bistro at night in the rain: FellowRain3D wetting the cobblestones, ray-traced reflections of the lamps">
</p>

**FellowEngine is a fork of [Godot Engine](https://github.com/godotengine/godot) 4.7.2-stable** that makes the
Forward+ renderer faster and adds real-time effects that stock Godot does not have: water with FFT waves, rain and wet
surfaces, wind with gusts, GPU smoke and fire, wood that burns to char and ash, automatic HLOD, ray tracing and a set
of post-processing effects.

* **Faster with the same image.** With the default settings FellowEngine renders the nine benchmark scenes
  11-55 % faster than stock Godot (table below). The changes that are on by default are *exact*: verified
  bit for bit against stock on lavapipe, and at the run-to-run floor on an RTX GPU.
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
</p>

These are real-time captures from the benchmark scenes on an RTX 5070 Ti Laptop GPU, with FellowEngine's visual
features, nodes and (in Bistro) ray tracing on. Captions, settings and more images are in
[docs/SHOWCASE.md](docs/SHOWCASE.md).

## Performance

Stock Godot 4.7.2 against FellowEngine with its defaults, frame time in ms. Measured on an Acer Predator PHN16S-71
(RTX 5070 Ti Laptop GPU, Core Ultra 9 275HX, Windows 11) at 1920 × 1080 with the benchmark's "high" preset. Three
runs per scene after a pass that fills the shader caches (`m26_f285_final`, build `d183ac7`). FellowEngine was faster
in every scene and every pair of runs.

| scene | stock | FellowEngine | change |
|---|---:|---:|---:|
| Sponza | 13.568 | 6.166 | **−54.6 %** |
| Bistro exterior | 5.999 | 4.149 | **−30.8 %** |
| City Lab (18 800 meshes) | 2.716 | 2.033 | **−25.1 %** |
| Bistro interior | 4.184 | 3.200 | **−23.5 %** |
| Lights Lab | 3.021 | 2.351 | **−22.2 %** |
| Crowd Lab (256 animated characters) | 5.463 | 4.271 | **−21.8 %** |
| Foliage Lab | 3.553 | 2.959 | **−16.7 %** |
| PBR Lab | 2.582 | 2.169 | **−16.0 %** |
| Stress Lab | 4.925 | 4.359 | **−11.5 %** |

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
| `FellowRain3D` | rain around the camera: GPU drops that stop and splash on the scene, roofs sheltering the ground, wet materials, puddles with ripples | 0.36 ms (Rain Lab) |
| `FellowWind3D` | wind with gusts rolling over the landscape: plants bend, sway, flutter and shine with it; shadows and motion vectors follow | 0.56 ms (Wind Lab); projects' own `TIME`-based sway costs 1.81 ms |
| `FellowGasVolume3D` | smoke and fire simulated on the GPU (sparse bricks, multigrid pressure solve), physics bodies as obstacles, blackbody flames lighting the scene | Fire Lab's three volumes (0.47 M cells): the simulation 1.58 ms per step, the drawing 0.31 ms |
| `FellowBurnable3D` | a mesh burns like wood in the gas's fire: voxels pyrolyse to char and glowing embers, the surface recedes, burns through and falls as a heap of ash and charcoal | drawing 0.35 ms (Burn Lab) |
| `FellowHLOD3D` | automatic HLOD: repeated far meshes instanced and culled on the GPU, the rest merged per cell with LODs, optional octahedral impostors | City Lab: render thread −23 to −42 % |
| `raytracing/*` | hardware ray tracing (`VK_KHR_ray_query`): ambient occlusion, sun shadows, dynamic diffuse GI with probes, reflections | — |
| `post/*` | motion blur, histogram and local exposure, contact shadows, sky atmosphere with aerial perspective, volumetric clouds, bloom with lens dirt and flare, light shafts, sharpening, white balance, lens effects | — |

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
contributors, released under the MIT license ([LICENSE.txt](LICENSE.txt), [COPYRIGHT.txt](COPYRIGHT.txt)). This fork is
not affiliated with or endorsed by the Godot Foundation. The benchmark assets are not in the repository, and
`benchmarks/scripts` fetches them. They keep their own licenses: Intel New Sponza (CC BY 4.0, Intel) and Amazon
Lumberyard Bistro (CC BY 4.0, Amazon Lumberyard / NVIDIA ORCA), both shown in the showcase images.
