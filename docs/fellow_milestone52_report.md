# FellowEngine — milestone 52 report (the editor: idle, warnings, handles, tooltips)

Status: **checked on the cloud's CPU** (2026-11-05; the GPU set aside by the owner). Branch `fellow/geometry`.
Decision record `docs/decisions/0153-editor-idle-warnings-handles.md`.

## 0. Result

The owner asked for editor optimizations and editor UX, without the GPU. Measured first: Godot's editor redraws only
when something changes, and with one FellowEngine node in a scene it redrew all the time — lightning between strikes,
rain rewriting positions that had not changed — or dispatched simulation steps 145 times a second into frames that
never came (shallow water: the main loop at 54 ms instead of 7). Now FellowEngine's nodes write to the renderer only
what changed and ask for a frame only when they move; an untouched scene costs nothing again, and a toggle on the 3D
toolbar (**Animate**) stills the nodes that do move. The same measurement found the rain's shelter bound into the pass
that renders it (debug builds — the editor — skipped those draws: no shelter under roofs there).

For UX: FellowEngine's nodes now say when they cannot work where they are (configuration warnings, 24 cases checked),
and scripts and agents can ask (`FellowEditorTools`, fellow_agent's `check`); every shaped node has handles, dragged in
the check with real mouse events and undone; and all 296 `rendering/fellow/*` project settings have tooltips.

## 1. Development

* Writes only on change and passes only with new inputs: `fellow_water_3d`, `fellow_rain_3d`, `fellow_lightning_3d`;
  simulations asking for frames (`Fellow::request_redraw()`): `fellow_shallow_water_3d`, `fellow_gas_volume_3d`; the
  still mode (`fellow_editor_still.h`, `Fellow::set_editor_animate()`), also in `fellow_wind_3d`.
* `modules/fellow_editor`: the toolbar toggle and the editor setting `editors/3d/fellow/animate_nodes`; handles for
  emitters, water, shallow water, cells, snow trackers, lightning; `FellowEditorTools`.
* Configuration warnings in eleven classes (`fellow_node_warnings.h` for the shared ones).
* `doc/tools/fellow_settings_docs.py` and the 296 settings in `ProjectSettings.xml`.
* Fixes: `render_forward_clustered.cpp` (the rain heightfield only in the camera's passes), `editor_node.cpp` (Update
  Continuously applied when changed in Editor Settings), `fellow_agent.gd` (low processor mode off).
* The editor check: `--perf` (idle or continuous, animating or still, frames drawn, a picture), the toolbar toggle,
  warnings, handles by mouse; idle test scenes in `project/idle/`.

## 2. Verification

* The idle editor per node, animating and still (Linux editor build, lavapipe, 8 s untouched).
* The editor check, fellow_agent's self-test, the class reference's checks.
* Games: the changes are exact there (the values not written again are the ones already written; a pass skipped is one
  with the inputs of the last), except that a shallow water with its simulation off now shows its initial surface.

## 3. Measurements

The idle editor, one node in a scene, 8 s untouched (Linux editor build, lavapipe; `--perf --perf-idle`): frames
drawn and the main loop's mean time (7.3 ms is the editor's idle sleep; on lavapipe a frame with water costs 0.5-0.8 s,
so a node redrawing every frame shows as few frames and long loops).

| node (defaults) | milestone 51 | 52, animating (default) | 52, Animate off |
|---|---|---|---|
| none (a box) | 1 · 7.3 ms | 1 · 7.4 ms | — |
| FellowWater3D | 11 · 778 ms | 10 · 806 ms (the waves move, as a `TIME` shader's) | **1 · 8.0 ms** |
| FellowLightning3D | 51 · 158 ms | **8 · 11.6 ms** (one strike's flash; nothing between strikes) | 1 · 7.3 ms |
| FellowRain3D | 33 · 244 ms, 50 errors a frame | 30 · 268 ms (drops falling, as particles), **no error** | **1 · 7.4 ms** |
| FellowShallowWater3D | 2 · 54 ms (148 loops, work piling up; timestamp errors) | 17 · 504 ms (each step drawn), no error | **1 · 7.6 ms** |
| FellowGasVolume3D, empty | 2 · 8.5 ms | 2 · 7.7 ms (nothing asked for) | 1 · 7.4 ms |
| FellowGasVolume3D with an emitter | — | 22 · 375 ms (the fire drawn as it burns) | 1 · 7.4 ms (prewarmed, still) |
| FellowWind3D | 1 · 7.3 ms | 1 · 7.3 ms (its clock runs, nothing redrawn) | 1 · 7.3 ms (clock held) |
| FellowSnowTracker3D, FellowWeather3D, FellowHLOD3D, FellowWorldStreamer3D | 1 · 7.3 ms | 1 · 7.3 ms | — |

| check | result |
|---|---|
| editor check (lavapipe, X11 under xvfb) | **EDITOR_CHECK_OK**: icons 17, the BVH import, 8 gizmos and their picture, the split tool (15 cells), the monitors; new: the **Animate** toggle in the 3D toolbar sets its setting off and on; **24 configuration-warning cases** each with the expected warning or none (a stock CollisionShape3D's warnings come through too); **handles dragged by mouse events**: the water's width 1 → 2.004 m, the shallow-water domain's depth 1 → 1.496 m, a cell's +X face 0.8 → 1.201 m (the -X face kept), the tracker's radius 0.2 → 0.5 m, each undone to the value before |
| fellow_agent self-test | **28 of 28** (26 before; new: the misplaced emitter's warning on node `Emitter`, the empty volume's on `Volume`, the check still passing) |
| class reference | `xmllint` valid; `make_rst.py --dry-run` no warning (5 before: three FellowEngine notes in stock classes, two in the new tooltips, fixed) |
| a game with the nodes (water, rain, lightning, wind, fire, shallow water; fellow_agent `capture`) | rain, a rainbow, fire, waves and the shallow water drawn, no error; not compared with milestone 51 (a second build of it was not made), the game-side changes being exact by construction (no value not written again differs from the last written; a pass skipped had the last one's inputs) |

## 4. Limits

* Measured on lavapipe (a frame of a scene with water costs 0.5-0.8 s there): the counts of frames drawn transfer to
  a GPU, the times do not.
* A FellowWater3D animating in the editor redraws it every frame, as a `TIME` shader does; the toggle stills it.
* The editor's grid, drawn once on lavapipe after a scene opens, shows stepped grey bands until more frames are drawn —
  with FellowEngine off as well (stock's editor on lavapipe).
* Configuration warnings about the renderer are given only with one (not headless).

## 5. Next

The laptop's Windows editor rebuilt with milestones 51-52 and the self-test and editor check run there, when the GPU
is back in use.
