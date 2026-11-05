# 0154 — The editor: scenes with FellowEngine's nodes open without recompiling; ready-made setups; grouped inspectors

Status: **KEEP** (exact; editor tools) · Branch `fellow/geometry` · Code: `modules/fellow_fluid/fellow_shared_shaders.{h,cpp}`
and its use in `fellow_water_3d`, `fellow_shallow_water_3d`, `fellow_rain_3d`, `fellow_lightning_3d`
(`register_types.cpp` releases it); `modules/fellow_editor/fellow_editor_plugin.*` (the FellowEngine menu,
`make_preset`); property groups in `fellow_shallow_water_3d`, `fellow_lightning_3d`, `fellow_world_streamer_3d`,
`fellow_motion_matcher_3d`, `fellow_motion_character_3d` · Checks: `benchmarks/tools/editor_check/` (`--perf-reopen`,
the presets).

## Problem

Measured on the cloud's CPU (Linux editor build; lavapipe under xvfb, and headless): FellowEngine adds nothing
measurable to the editor's start (an empty project, after a first start that fills the caches: 15.1 s with FellowEngine
on, 15.3 s with `rendering/fellow/enabled` off, windowed; 4.8 s / 4.9 s headless; four runs each, alternating). Opening
a scene is another matter. The main thread's time to open a scene with one node (`EditorInterface.open_scene_from_path`),
against 8 ms for a box: FellowWater3D 492 ms, FellowRain3D 581 ms, FellowShallowWater3D 531 ms, FellowLightning3D
261 ms. And every instance paid again: adding a FellowWater3D to the tree cost 255-285 ms for the 2nd, 3rd and 4th
one (headless, without a renderer, 27-38 ms), a FellowShallowWater3D 68-73 ms. Each instance made its own `Shader`
from the same code (the shading language compiled, then to SPIR-V), its own compute kernels from the same GLSL
(glslang), the same grid mesh and the same foam texture — in the editor, every time a scene was opened again.

The inspector: the FellowShallowWater3D's 16 properties, the FellowMotionMatcher3D's and FellowMotionCharacter3D's
17, the FellowLightning3D's 11 and the FellowWorldStreamer3D's 10 were a flat list. And setting up a fire, a weather
system or a pool by hand meant knowing the rules the configuration warnings now state (decision 0153).

## Change

**`FellowSharedShaders`**: the shaders a node makes from code (`shader(code)`), the SPIR-V of compute kernels
(`file(glsl, name)`) and the resources that do not depend on the instance (`resource(key)`/`store()`: the water's polar
grid for its segment count and radii, its foam texture, the shallow water's grid for its size) are made once per
session and shared. Used by FellowWater3D (both shader variants, the wave kernels, grid, foam), FellowShallowWater3D
(surface shader, kernels, grid), FellowRain3D (drop and flake shaders, ripple and snow-track kernels) and
FellowLightning3D (bolt and glow shaders). Nothing shared is changed after it is made: a node wanting other code
asks for that code. Released at the module's end, while the RenderingServer lives. The RenderingDevice's shader and
pipeline objects stay per instance (their lifetimes are the nodes'). Exact: the same code, the same objects.

**A FellowEngine menu on the 3D editor's toolbar** adds a ready-made setup to the selected 3D node (or the scene's
root), undoable, selected, every node owned by the scene: *Weather* (a FellowWeather3D, and the rain, wind and
lightning the scene lacks — one of each works at a time), *Fire* and *Smoke* (a gas volume and its emitter), *Burning
Box* (a box with a FellowBurnable3D, a gas volume around it, a flame under it), *Ocean* (water to the horizon), *Lake*
(20 × 20 m), *Flowing Pool* (8 × 8 m of shallow water flowing in from one side, drawn by a FellowWater3D). Made by class
name (`FellowEditorPlugin::make_preset`): the editor module needs none of the others' headers.

**Inspector groups**: Domain, Simulation, Inflow (shallow water); Strikes, Look (lightning); Range, Budget
(streamer); Search, Blending, Feet (motion matcher); Motion, Inference (motion character). Property names are
unchanged: scenes load as before. The shallow water's gravity shows its unit.

## Verification

* Opening, and opening again in the same session, a scene per node (`run_editor_check.py --perf <scene> --perf-idle
  --perf-reopen`), and adding four instances of each node to the tree back to back (a script; windowed and headless).
* The editor check: every setup of the menu added to a scene, selected, owned by it, **without a configuration
  warning in any of its nodes**, and undone; and every earlier check.
* fellow_agent's self-test (its captures draw the water and the other nodes).

## Results

Start of the editor, an empty project (after a first start fills the caches; four runs each, alternating):

| | FellowEngine on | `rendering/fellow/enabled` off |
|---|---|---|
| windowed (lavapipe, xvfb) | 15.1 s (14.9-15.3) | 15.3 s (15.3-15.4) |
| headless | 4.8 s (4.7-4.9) | 4.9 s (4.8-5.1) |

Opening a scene with one node in the editor, the main thread's time in `open_scene_from_path` (windowed; a new copy of
the project each run, so the first open compiles):

| scene | milestone 52, first open | 53, first open | 53, closed and opened again |
|---|---|---|---|
| a box | 8.1 ms | 4.1 ms | 10.5 ms |
| FellowWater3D | 492 ms | 551 ms | **4.9 ms** |
| FellowRain3D | 581 ms | 639 ms | **23 ms** |
| FellowShallowWater3D | 531 ms | 369 ms | **164 ms** |
| FellowLightning3D | 261 ms | 306 ms | **6.0 ms** |
| the editor check's gizmo scene (eight nodes) | 191 ms | 120 ms | **14 ms** |

Four instances added to the tree back to back (a script, windowed; the 1st instance compiles once per session):

| node | 2nd-4th instance, before | 2nd-4th instance, after | added again after the first was freed |
|---|---|---|---|
| FellowWater3D | 255-286 ms | **0.1-0.2 ms** | 9-11 ms |
| FellowShallowWater3D | 67-73 ms | **0.0-0.1 ms** | 9-17 ms |
| FellowRain3D | 5-6 ms | 0.1-0.2 ms | — |
| FellowLightning3D | 2-3 ms | 0.0-0.1 ms | — |

| check | result |
|---|---|
| the FellowEngine menu (editor check) | 7 setups added: Weather (5 nodes), Fire, Smoke (2 each), Burning Box (5), Ocean, Lake (1 each), Flowing Pool (2); **no configuration warning** in any node; all owned by the scene; each undone |
| editor check | EDITOR_CHECK_OK (every earlier check too) |
| fellow_agent self-test | 28 of 28 |

## Decision

**KEEP.** A scene with FellowEngine's nodes compiles their shaders once per session: the second water body, or the
scene opened again, costs a tenth of a millisecond instead of a quarter of a second. Setups that used to take
knowledge of the nodes' rules are one menu item away, and come out warning-free. Not done: sharing the RenderingDevice
shader and pipeline objects across instances (their lifetimes would no longer be the nodes'; the drivers' pipeline
caches already make them cheap on a GPU).
