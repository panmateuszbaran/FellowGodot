# 0156 — A vertical slice played for an hour: Slice Lab, the bench's soak mode; what the hour found

Status: **KEEP** (a lab, a bench mode and tools; a fix) · Branch `fellow/geometry` · Code:
`benchmarks/project/scenes/slice_lab.gd`, `benchmarks/project/bench/main.gd` (`--mode=soak`),
`benchmarks/tools/soak/run_soak.py`, `soak_report.py`; fix: `modules/fellow_fluid/fellow_burnable_3d.cpp`.

## Problem

Every measurement so far ran one system in its own lab for 30-60 s after a warm-up. A game runs all of them at once
for hours: the weather turns, a fire is lit again, the sun goes down, and what matters is what a player sees — a
frame that takes 50 ms when rain sets in, memory that grows by the hour, an error that appears only after minutes.
None of the 54 milestones measured that (the AAA assessment's point 6: "AAA-sized test levels, hour-long play
sessions, memory and hitch tracking").

## Change

**Slice Lab** (`--scene=slice`, built in code, deterministic): a 1 km valley — a 131 k-triangle terrain in 64 chunks,
a town of 711 buildings (6 materials) under a FellowHLOD3D, 99 street lights that come on at dusk, a square with a
campfire (a FellowGasVolume3D with a kindling emitter and a burning log, FellowBurnable3D, lit again every four
minutes) and 48 of Crowd Lab's skinned characters, a lake (FellowWater3D), 9 000 trees and meadows of grass in a
FellowWind3D, a FellowWeather3D driving rain, wind and lightning, a sun crossing the sky (a day of `--slice_day`
seconds), FellowEngine's atmosphere and volumetric clouds when the run turns them on. A 120 s flythrough over the
valley, down a street, through the square, along the lake, up the hills. `--slice_without=town,forest,water,weather,
fire,crowd` leaves parts out; `--slice_weather_seconds`, `--slice_fire_seconds` set the soak's cycles.

**`--mode=soak`** (`bench/main.gd`): the scene played in real time for `--soak_minutes`, its flythrough looping and
its own schedule running (the slice: the weather turning every two minutes — cloudy, rain, storm, overcast, snow,
blizzard, clear — over 40 s, the log lit again every four). Per second: frames, the frame time between frames as a
player sees it (mean, 99th percentile, max), the GPU's and the render thread's time, video, texture and buffer memory,
static memory (debug builds), object, node, orphan node and resource counts, pipelines compiled by kind; hitches
(a frame longer than twice the median of the 120 before and 10 ms over it) with the pipelines compiled in that frame.
The summary after `--soak_warmup`: frame time percentiles, hitches a minute, growth per hour of every memory and count
(a least squares line), the frame time per weather.

**`run_soak.py`** samples the engine process's resident memory and the GPU's memory in use from outside (a release
template counts no static memory); **`soak_report.py`** writes the per-second CSV, a chart (frame times with the
weather as bands and the hitches marked; memory) and the summary.

## What the hour found

1. **A burning object lit again freed invalid IDs** ("Attempted to free invalid ID", twice every four minutes on the
   RTX: each time the campfire's log was lit again). `FellowBurnable3D.reset_burning()` freed its voxels — and with
   them the views of the RenderingServer textures made from them — then pointed the same `Texture3DRD` and
   `Texture2DRD` at the new voxels; RenderingServer's `texture_replace` frees the old views without checking them.
   The burnable now takes new texture objects after freeing its voxels (lavapipe, the log lit every 10 s: 10 errors in
   a minute before, none after; RTX, 20 minutes: none).
2. **Lighting it again compiled pipelines.** The hour compiled 858 pipelines after its warm-up, about 30 every four
   minutes — the relightings: `reset_burning()` made the burning materials anew, each with a new `Shader` from the
   same code. Half the hour's hitches fell in seconds that compiled pipelines; in a 20-minute soak with the log lit
   every two minutes, 20 of 25 hitches were those (6 surface and 2 specialization pipelines, 56-64 ms). The burning
   and ash-heap shaders now come from `FellowSharedShaders` (decision 0154): on lavapipe a log lit again every 10 s
   compiled 9-10 pipelines each time before, none after.
3. **A hitch 3 s into every start** (45 ms with warm caches, 313 ms colder, 4.8 s the very first time on the RTX):
   6 surface and 2 specialization pipelines, gone when the slice was built without its fire — the campfire's log's
   first ash, which made the ash heap's mesh and material then. The heap is now made with the burning display when
   the burnable is configured, hidden until there is ash. On the RTX the slice's first 25 seconds then have no frame
   over 20.2 ms, with warm caches or with Godot's shader and pipeline caches off (the largest frame 13-20 ms; the
   compilation is now in the loading: 2.8-3.8 s to the first frame with the caches off, 0.9 s with them, 4.1 s the
   first run of a new build).
4. **Nothing grew in Godot's accounts**: video memory 394 MB at the start, 395 MB at the end (453 MB at most), objects
   3 083 → 3 085, nodes and orphan nodes unchanged. A release template counts no static memory, hence `run_soak.py`:
   the process's own memory rises as pipelines compile, by less as fewer do (results).
5. **What costs what**: snow and blizzard 4.3-4.4 ms a frame, the other weathers 3.7-4.0 ms; with the sun up
   4.5-5.5 ms, at night (the sun and its four shadow cascades off) 3.3-3.8 ms.

## Verification

* lavapipe: the slice captured from its five cameras, a 3-minute soak at 320 × 180 (the code path), short soaks with
  parts left out and fast cycles (`--slice_weather_seconds=15`: every weather in 105 s; `--slice_fire_seconds=10`).
* RTX 5070 Ti Laptop, 1080p, release template, atmosphere and clouds on: the five cameras; a 3-minute and a
  60-minute soak (job f344); the first 25 seconds with each part left out, and 20 minutes with the log lit every two
  minutes (job f345); with the fixes, the first 25 seconds warm and with Godot's caches off (f346) and the hour again
  with the process's memory sampled (f347).

## Results

RTX 5070 Ti Laptop, 1920 × 1080, release template, atmosphere and volumetric clouds on, the weather turning every
two minutes, a day of ten, the log lit again every four; the hour after a minute's warm-up:

| | the first hour (job f344) | the hour with the fixes (job f347) |
|---|---|---|
| frames | 888 268 | 891 418 |
| frame time mean (fps) | 3.99 ms (251) | 3.97 ms (252) |
| 50th / 90th / 99th / 99.9th percentile | 3.79 / 5.51 / 6.57 / 7.34 ms | 3.79 / 5.48 / 6.58 / 7.27 ms |
| largest frame | 180 ms | 236 ms |
| hitches (a minute) | 61 (1.03) | **25 (0.42)** |
| of them compiling pipelines | 31 (of all 62, the warm-up's included) | **2** |
| pipelines compiled after the warm-up | 858 | **386** (378 specialization, compiled in the background; 8 surface) |
| "Attempted to free invalid ID" | 28 | **0** |
| video memory (Godot's): start, end, largest | 394, 395, 453 MB | 394, 395, 453 MB |
| objects, nodes, orphan nodes: start → end | 3 083 → 3 085, 1 354 → 1 354, 0 → 0 | 3 083 → 3 085, 1 354 → 1 354, 0 → 0 |
| the process's resident memory (`run_soak.py`) | — | 1 757 MB at 5 min, 1 992 at 15, 2 005 at 30, 2 089 at 60 |
| the GPU's memory in use (nvidia-smi, the whole GPU) | — | 3 161 MB at 5 min, 3 167 at 25, 3 374 at 35, 3 536 at 50 and 60 |

The frame time by weather (the hour with the fixes): cloudy 3.82, rain 3.94, storm 3.73, overcast 3.79, snow 4.33,
blizzard 4.38, clear 3.93 ms. The pipelines compiled a minute fall from 12-79 in the first five minutes to 0-6 after
the first twenty (but 17, 8 and 13 in minutes 46, 48 and 58): the scene's variety of lights, weathers and times of
day compiles its specializations once. The process's resident memory rises while they compile — +235 MB from minute
5 to 15 (139 compiled), +97 MB in the last 45 minutes, within its ±100 MB swings — and the GPU's memory in use (the
whole GPU's) in three steps (minutes 30, 35, 50); Godot's own counters stay flat. No leak found in what Godot and
FellowEngine allocate; the process's growth slows as the compilations do (a longer soak would show whether it
stops).

The first 25 seconds of the slice (job f345 before the heap fix, f346 after; warm caches):

| | before | after |
|---|---|---|
| the whole slice | 313 ms at 3.2 s (6 surface + 2 specialization pipelines), 216 ms at 2.1 s | largest frame 17.5 and 20.2 ms (two runs) |
| without the fire | 204 ms at 1.9 s (no pipeline) | — |
| with Godot's shader and pipeline caches off (whole slice; without fire, town, weather) | — | no hitch; largest frame 13-16 ms; 2.8-3.8 s to the first frame |
| without every part | largest frame 4.3 ms | — |

## Decision

**KEEP.** Slice Lab and the soak are the first measurements of FellowEngine as a game runs it: every system at once,
for an hour. The first hour on the RTX ran at 251 fps with nothing growing in Godot's or FellowEngine's accounts, and
found what no lab had — a burning object that freed invalid IDs and compiled its shaders anew each time it was lit,
and made its ash heap's material mid-fire (a 4.8 s freeze the first time). Fixed: the hour's hitches 61 → 25, those
compiling pipelines 31 → 2, the start without a hitch. Not done: three hitches an hour of 120-240 ms that compile
nothing and leave the GPU and render thread idle (the main thread's; `--soak_fellow_stats=1` records FellowEngine's
counters with them), a snow's first fall compiling 6 surface pipelines (51 ms), pre-warming the specializations a
session compiles over its first twenty minutes, days rather than hours, other GPUs.
