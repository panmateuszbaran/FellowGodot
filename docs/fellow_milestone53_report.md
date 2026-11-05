# FellowEngine — milestone 53 report (the editor: scene opening, ready-made setups, grouped inspectors)

Status: **checked on the cloud's CPU** (2026-11-05; the GPU set aside by the owner). Branch `fellow/geometry`.
Decision record `docs/decisions/0154-editor-scene-open-presets.md`.

## 0. Result

More editor work without a GPU. The editor's start was measured first: FellowEngine adds nothing measurable to it
(15.1 s with it, 15.3 s without, windowed on lavapipe). Opening scenes was the cost: every FellowWater3D compiled its
own copy of the same shader and kernels and built the same grid and foam texture — a quarter of a second each, again
every time the scene was opened. Now FellowEngine's nodes make those once per session (`FellowSharedShaders`): a
scene with water opened again takes 4.9 ms instead of 492 ms, a second water body 0.1 ms instead of 270 ms.

For UX: a **FellowEngine** menu on the 3D editor's toolbar adds ready-made setups — weather, fire, smoke, a burning
box, an ocean, a lake, a flowing pool — wired so that none has a configuration warning (checked), undoable; and the
five nodes with the longest flat property lists have inspector groups.

## 1. Development

* `modules/fellow_fluid/fellow_shared_shaders.{h,cpp}`: shaders from code, compute kernels' SPIR-V, shared
  resources by key; used by water, shallow water, rain, lightning; released at the module's end.
* `modules/fellow_editor`: the FellowEngine menu (`make_preset`, `_add_preset`: undoable, selected, owned).
* Inspector groups in shallow water, lightning, world streamer, motion matcher, motion character.
* The editor check: `--perf-reopen` (a scene closed and opened again), open and first-frame times, every preset
  checked for warnings and undone.

## 2. Verification

The editor's start with FellowEngine on and off; scene opening per node, first and again; instances added back to
back; the editor check; fellow_agent's self-test. Sharing is exact: the same code makes the same shaders.

## 3. Measurements

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

## 4. Limits

* Times on lavapipe: the shading-language and glslang compiles saved are CPU work on any machine, but a GPU driver's
  pipeline compiles (not shared) cost differently from lavapipe's.
* The first instance of a node in a session still compiles (37-87 ms for the water, windowed); FellowShallowWater3D
  opened again still takes 164 ms (not traced further).

## 5. Next

The laptop's Windows editor rebuilt with milestones 51-53 and checked there, when the GPU is back in use.
