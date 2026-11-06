# FellowEngine — milestone 55 report (a vertical slice played for an hour)

Status: **measured on the RTX** (2026-11-06, the night) and checked on lavapipe. Branch `fellow/geometry`. Decision
record `docs/decisions/0156-vertical-slice-soak.md`.

## 0. Result

Point 2 of the plan towards AAA: every system at once, for an hour. **Slice Lab** is a 1 km valley — a town of 711
buildings under HLOD with street lights, a square with a campfire and a crowd, a lake, a forest in the wind,
weather that turns through rain, storm and snow, a day that turns to night — and the bench's new **soak mode** plays
it in real time, recording every second's frame times, the hitches with what compiled in them, and memory.

On the RTX at 1080p with the atmosphere and clouds on, the first hour ran at 251 fps (4.0 ms; 99th percentile 6.6
ms) with nothing growing in Godot's accounts — video memory 394 MB at the start and at the end, objects +2 — but
with 61 hitches up to
180 ms, half of them compiling pipelines, a hitch 3 s into every start (4.8 s the very first time), and an error
every four minutes. All three came from the burning objects: relighting one freed invalid IDs and compiled its
shaders anew; its ash heap made its material only when the first ash appeared. Fixed, the slice starts without a
frame over 21 ms, warm or with Godot's caches off, and the hour again ran at 252 fps with 25 hitches (2 of them
compiling pipelines) and no error. The process's own memory rises while the session's pipelines compile (1.76 GB at
5 minutes, 1.99 GB at 15, 2.09 GB at 60), by less as fewer do.

## 1. Development

* `benchmarks/project/scenes/slice_lab.gd`: the valley (`--slice_without`, `--slice_weather`, `--slice_day`,
  `--slice_daytime`, `--slice_weather_seconds`, `--slice_fire_seconds`, `--slice_crowd`, `--slice_trees`,
  `--slice_grass`).
* `benchmarks/project/bench/main.gd`: `--mode=soak` (`--soak_minutes`, `--soak_warmup`, `--soak_fellow_stats`).
* `benchmarks/tools/soak/run_soak.py` (the process's memory sampled from outside), `soak_report.py` (CSV, chart,
  summary).
* Fixes in `modules/fellow_fluid/fellow_burnable_3d.cpp`: new texture objects after `reset_burning()` frees its voxels;
  its shaders from `FellowSharedShaders`; the ash heap made at configuration, hidden until there is ash.

## 2. Verification

* lavapipe: the five cameras; short soaks with parts left out, fast weather and fire cycles; the burnable's fixes
  (errors, pipelines compiled per relighting, burning voxels after each).
* RTX (jobs f344-f347): the five cameras at 1080p; soaks of 3 and 60 minutes; the first 25 seconds with each part left
  out, without the atmosphere and clouds, warm and with Godot's caches off; 20 minutes with the process's memory
  sampled; the hour again with the fixes.

## 3. Measurements

RTX 5070 Ti Laptop, 1080p, release template, atmosphere and clouds; the hour after a minute's warm-up:

| | the first hour | with the fixes |
|---|---|---|
| frame time mean (fps); 99th, 99.9th percentile | 3.99 ms (251); 6.57, 7.34 ms | 3.97 ms (252); 6.58, 7.27 ms |
| hitches; compiling pipelines | 61; 31 | **25; 2** |
| pipelines compiled after the warm-up | 858 | **386** (378 in the background) |
| invalid frees | 28 | **0** |
| video memory, start → end | 394 → 395 MB | 394 → 395 MB |
| objects, nodes, orphans | +2, 0, 0 | +2, 0, 0 |
| the process's memory (at 5, 15, 60 min) | — | 1 757, 1 992, 2 089 MB |

| the slice's first 25 s | before | after |
|---|---|---|
| warm caches | 313 ms at 3.2 s (the ash heap's pipelines), 216 ms at 2.1 s | largest frame 17.5, 20.2 ms |
| Godot's caches off | — | largest frame 13-16 ms, 2.8-3.8 s to the first frame |

Per weather: snow and blizzard 4.3-4.4 ms, the others 3.7-3.9 ms; the sun up 4.5-5.5 ms, at night 3.3-3.8 ms.
Decision 0156 has the details (the pipelines a minute, the memory's curve, the variants left out).

## 4. Limits

* One machine and one GPU; an hour, not a day.
* A release template counts no static memory: the process's memory is sampled from outside (`run_soak.py`).
* Hitches without a pipeline compiled (three an hour of 120-240 ms, with the GPU and the render thread idle: the
  main thread's) are not yet explained (`--soak_fellow_stats=1` records FellowEngine's counters with each); a snow's
  first fall compiles 6 surface pipelines (51 ms).
* The process's memory after the first twenty minutes: +97 MB in 45 minutes, within its swings; a longer soak would
  show whether it stops.
* The slice is built in code: no streamed cells (World Lab's) and no texture streaming (Streaming Lab's textures) in it
  yet; its terrain is a plain height field (no terrain system).

## 5. Next

The remaining hitches explained; the soak in the regression habit (a short soak per milestone); terrain (Terrain3D,
MIT, a GDExtension for Godot 4.4-4.6, is the candidate to evaluate against FellowEngine's weather and wind).
