# FellowEngine Showcase

A small valley that shows what FellowEngine adds to Godot, with a character to walk it. Everything in it is made by
code in this folder (no third-party asset): open it in a FellowEngine editor build and press F5.

## Opening it

* With the editor: start a FellowEngine editor build (`scons platform=windows target=editor winrt=no accesskit=no
  d3d12=no`, see `BUILDING_FELLOWENGINE.md`; on Linux `scons platform=linuxbsd target=editor`), choose **Import** in
  the project manager and pick `showcase/project.godot`, or run `godot --path showcase --editor`. The first opening
  imports the project and compiles its shaders (a minute or two).
* Without the editor: `godot --path showcase` plays it.

A stock Godot build cannot open it: the scene's terrain, water, weather, fire and pipeline warm-up are FellowEngine
nodes.

## Playing it

| keys | |
|---|---|
| WASD or the arrows, Shift, Space | walk, run, jump (a gamepad's left stick, left shoulder and A) |
| the mouse, the wheel | look around (the right stick), zoom |
| 1-7 | the weather: clear, cloudy, overcast, rain, storm, snow, blizzard (it changes over 20 s) |
| 0 | the weather changes by itself every two minutes |
| T, [ and ] | the clock stops or runs; an hour back or forward (a day lasts 12 minutes) |
| F | the campfire lit again (it is, by itself, 20 s after its log has burned out) |
| F1, F2, F11, F12 | the help, the HUD, full screen, a screenshot (in the project's user folder) |
| Esc | frees the mouse (a click captures it again) |

## What is in it

| | FellowEngine | where |
|---|---|---|
| the valley's ground | `FellowTerrain3D`: a 513 × 513 heightmap at 1 m with levels of detail and collision, six layers painted by rules (rock on the slopes, sand at the shore, the village's earth and paths, meadows, forest floor) | `Terrain` |
| grass, flowers, bushes, trees, rocks | `FellowTerrainFoliage`: scattered by the terrain over its layers around the camera; the plants sway in the wind (the `wind` group of `StandardMaterial3D`) | `Terrain.foliage` |
| the lake | `FellowWater3D`: FFT waves driven by the scene's wind, reflections, refraction, caustics, foam at the shore | `Lake` |
| the campfire | `FellowGasVolume3D` (GPU smoke and fire, its own light) with `FellowGasEmitter3D` kindling, and a log that burns to char and ash with `FellowBurnable3D` | `Village/Campfire` |
| the weather | `FellowWeather3D` drives `FellowWind3D`, `FellowRain3D` (rain, snow, wet and snowy ground, puddles, rain shafts, rainbows) and `FellowLightning3D`, the clouds, the fog and the sunlight | `Weather`, `Wind`, `Rain`, `Lightning` |
| the sky | FellowEngine's atmosphere (aerial perspective, the sun's colour through the air) drawn by `shaders/atmosphere_sky.gdshader`, volumetric clouds and their shadows, light shafts, contact shadows, bloom | project settings `rendering/fellow/post/*` |
| footprints | `FellowSnowTracker3D` under each foot: tracks in the snow | `Player/Model/Skeleton3D/Foot_*` |
| no hitch at the start | `FellowPipelineWarmer3D` compiles the pipelines of the scene's materials while it starts (the HUD says so) | `PipelineWarmer` |

The character is a humanoid of 52 bones made in code (`tools/humanoid_builder.gd`) with idle, walk, run and falling
clips blended by an `AnimationTree` from its speed; it wades and swims in the lake.

## Changing it

`main.tscn`, `player.tscn` and the files in `world/` and `character/` are written by `tools/build_showcase.gd`:

```
godot --headless --path showcase -s res://tools/build_showcase.gd
```

Edit the builder and run it again, or edit the scenes in the editor (running the builder again overwrites them). The
scripts in `scripts/` are the game: `showcase.gd` (the clock, the weather keys, the village's lights, the campfire),
`player.gd` (the character), `hud.gd`. With `-- --showcase_time=<0..1> --showcase_weather=<0..6> --showcase_stop
--showcase_at=<x>,<z>,<heading>[,<zoom>[,<pitch>]]` on the command line it starts at a time, weather and place.
