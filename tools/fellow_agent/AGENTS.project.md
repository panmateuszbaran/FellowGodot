# Working on this project (for AI agents)

This is a game project for **FellowEngine**, a fork of Godot 4.7 with its own nodes (water, rain, snow, wind,
lightning, weather, smoke and fire, burning objects, HLOD, world streaming, motion matching). Scenes (`.tscn`) and
scripts (`.gd`) are text: edit them directly, then check the result yourself with the toolkit below — every command
prints JSON and exits 0 (fine), 1 (ran, found problems) or 2 (could not run).

```
T={{FELLOW_AGENT}}
python $T import   --project .                                    # after adding or changing assets (an editor build)
python $T check    --project .                                    # every scene and script: load, instantiate, run 5 frames
python $T capture  --project . --scene res://main.tscn --out /tmp/shot          # the scene's camera, or a framed view
python $T capture  --project . --scene res://level.tscn --views iso,top,front --out /tmp/views   # + sheet.png
python $T describe --project . --scene res://level.tscn           # the node tree as data
python $T run      --project . --scene res://main.tscn --seconds 10              # frame times, errors, the last frame
python $T compare  before.png after.png --out diff.png            # did a change alter the picture?
python $T api FellowWater3D                                       # a class's reference (members, methods, signals)
python $T api --search wetness                                    # find classes and members by name
```

The engine binary comes from `--godot` or `$FELLOW_GODOT`; use an **editor build** for `import`, `check` and `run`
(a release export template does not check scripts' errors: a call on null crashes it).

## How to check a change

1. `check`: no `errors` (each has `file`, `line`, `message` and, for scripts, `script_location` and
   `script_backtrace`). Warnings are listed and do not fail.
2. `capture` the scenes you changed and **look at the PNGs** (or `sheet.png`): `stats.blank` true means a flat image
   (nothing rendered, a missing light or camera); `preview_lighting_added` says the toolkit lit a scene that has no
   light of its own (a game would show it dark).
3. For a visual change that should not alter other scenes, `capture` before and after and `compare` (captures run at a
   fixed frame rate and are the same every run on the same machine and build).
4. For performance, `run` and read `frame_ms` (p50, p95, p99) and `gpu_ms_mean`.

## Conventions

* GDScript with static types (`var x: int`, `func f() -> void`); Godot's style (tabs, snake_case, PascalCase classes).
* Prefer editing `.tscn` text for structure and properties; keep `uid://` references as they are.
* FellowEngine's project settings are under `rendering/fellow/` (documented in the engine's
  `docs/FELLOWENGINE_OPTIONS.md`); its nodes do nothing in the editor that needs a running game (the weather, the
  simulations).
