# 0153 — The editor: an idle editor that stays idle, configuration warnings, handles, settings' tooltips

Status: **KEEP** (editor behaviour and tools; three fixes) · Branch `fellow/geometry` · Code: the time-driven nodes of
`modules/fellow_fluid` (`fellow_water_3d`, `fellow_rain_3d`, `fellow_lightning_3d`, `fellow_wind_3d`,
`fellow_shallow_water_3d`, `fellow_gas_volume_3d`), `fellow_editor_still.h`, `fellow_node_warnings.h`;
`modules/fellow_editor` (`fellow_editor_plugin.*`, `fellow_editor_tools.*`); `servers/rendering/fellow_settings.*`
(`set_editor_animate()`, `request_redraw()`); fixes in `renderer_rd/forward_clustered/render_forward_clustered.cpp`
and `editor/editor_node.cpp` · Docs: `doc/classes/ProjectSettings.xml` (from `doc/tools/fellow_settings_docs.py`) ·
Checks: `benchmarks/tools/editor_check/` (`--perf`, warnings, handles), `tools/fellow_agent/` (`check`).

## Problem

Godot's editor redraws only when something changes (its low processor mode): a scene left alone costs nothing. Godot
counts a node that makes it redraw without a visible change as a bug, and deprecates *Update Continuously* (the menu
says so). Measured in the editor with one FellowEngine node in a scene (Linux editor build, lavapipe, 8 s untouched;
`run_editor_check.py --perf <scene> --perf-idle`), milestone 51's nodes broke that rule:

| node (defaults) | frames drawn in 8 s | main loop | what it did |
|---|---|---|---|
| none (a box) | 1 | 7.3 ms | — |
| FellowWater3D | 11 (every frame it could) | 778 ms | its clock written to its material every frame — animating, as a `TIME` shader does |
| FellowLightning3D | 51 (every frame) | 158 ms | the flash light's energy and colour and the bolt's material written every frame, **between strikes as well** |
| FellowRain3D | 33 (every frame) | 244 ms | the drops' and splashes' positions, rate and emitting written every frame; and 50 errors a frame (below) |
| FellowShallowWater3D | 2 | 54 ms | its steps dispatched every main-loop iteration (145 a second) without a frame to run them in: the work piled up, and its timestamps passed the renderer's 256 between two frames (an error each time) |
| FellowGasVolume3D (empty) | 2 | 8.5 ms | steps dispatched every physics tick, unseen |
| FellowWind3D, FellowSnowTracker3D, FellowWeather3D, FellowHLOD3D, FellowWorldStreamer3D | 1 | 7.3 ms | — |

Animating water is what Godot does with a `TIME` shader; lightning redrawing for nothing between strikes, rain
rewriting positions that did not change, and simulations dispatching into frames that never came are not. A
person working on a laptop also wants to stop the animation — Godot's own way, *Update Continuously* off, changes
nothing here (FellowEngine's nodes ask for frames themselves), and in 4.7 changing it in Editor Settings did not even
apply until a restart.

The same measurement showed two faults of debug builds (the editor): **the rain's shelter heightfield was bound into
the pass that renders it** ("Attempted to use the same texture in framebuffer attachment and a uniform", 50 a frame;
debug builds skip such draws: in the editor the scene under roofs was not dry), and shallow water's timestamp overflow.

Other gaps of FellowEngine in the editor: nodes that cannot work where they are said nothing (an emitter beside its
volume instead of in it, a streaming cell outside a streamer, a second rain node — only one works — a burning object
without a gas volume, a path to the wrong node, a node the project's renderer does not draw); only the gas volume had
handles; changing a property did not redraw most gizmos; none of the 296 `rendering/fellow/*` project settings had a
tooltip; and a script (or an agent) could not ask a node's configuration warnings at all
(`Node.get_configuration_warnings()` is not bound).

## Change

**Nothing written that did not change.** Every value FellowEngine's time-driven nodes give the RenderingServer each
frame is compared with the last one first: the water's clock, gust mapping and simulation transform; the rain's drops
and splashes (position, rate, emitting); the lightning's light and materials (written during a flash only). The water's
wave pass is not dispatched again with the same inputs (a clock standing still: the same waves — exact); the rain's
ripple pass runs while it rains and once when it stops (they flatten), not every frame of a wet scene; its tracks'
pass is skipped with nothing to stamp, refill or scroll. Games gain the same (fewer RenderingServer calls).

**Simulations ask for their frames.** A shallow-water step and a gas step (while something emits, and 20 s after, as the
smoke clears) call `Fellow::request_redraw()`: the editor draws what moved instead of piling up the dispatches. The
shallow water's timestamps are captured once per frame drawn; a reset shows its surface even with the simulation off.

**Animate or still: a toggle.** In the editor the nodes animate as in the game — waves move, rain falls, lightning
strikes, the gas and the shallow water simulate — like Godot's `TIME` shaders and particles. The 3D editor's toolbar has
a toggle (**Animate**, editor setting `editors/3d/fellow/animate_nodes`, on by default) that stills them: no clock, no
strikes, no falling drops (the wet or snowy scene shown still), no steps (the gas after its prewarm), and nothing sent
to the renderer that did not change — the editor then redraws only when something does. Games always animate
(`fellow_editor_still()` is false outside the editor).

**Configuration warnings** (the Scene dock's warning icon) for FellowEngine's nodes: gas emitter not a child of a
volume; a volume nothing fills; burnable without a mesh, with an empty mesh, without a gas volume in the scene; paths
leading to no node or to the wrong class (water, weather, burnable, streamer targets, motion matcher); a second rain or
wind (only the last to enter the tree works); snow tracker without a rain node or with its tracks off; streaming cell
outside a streamer, without a scene or with a missing one; streamer without cells (pointing at Project > Tools > Split
into Streaming Cells), or with neither the camera nor targets; motion matcher without a database or with an empty one;
motion character without a model; and where the project's renderer does not draw a node (gas on Mobile and
Compatibility, rain's surfaces off Forward+, shallow water and waves on Compatibility) or FellowEngine's fluids are off.
They update as the tree and the properties change. **`FellowEditorTools.get_configuration_warnings(node)`** (editor
builds) returns them — stock nodes' too — to scripts, and **fellow_agent's `check`** reports every node's warnings
(not failing the check).

**Handles**, undoable, for the emitters (box and cylinder through Godot's `Gizmo3DHelper`, the sphere's radius), the
water's extent, the shallow-water domain, a streaming cell's bounds (a handle per face, the opposite face kept), the
snow tracker's radius and length, lightning's nearest and farthest strike and cloud height; the 3D editor's snap
applies. The gizmos redraw when those properties change.

**Tooltips for every FellowEngine project setting**: 296 descriptions in `ProjectSettings.xml` (what it does,
exact or not, start-up, its decision record), written by `doc/tools/fellow_settings_docs.py` from an engine's
`--doctool` types and defaults; it fails when a setting has no description.

**Fixes.** The rain's heightfield is bound only in the camera's passes (the ones that read it). *Update Continuously*
changed in Editor Settings applies at once (stock 4.7 updated it only for the appearance group, where it no longer
is). `fellow_agent.gd` turns low processor mode off (a project with it on would never draw the frames a still capture
waits for).

## Verification

* The idle editor per node, animating and still (`--perf --perf-idle [--perf-still]`, 8 s each): frames drawn,
  main-loop time, the node's clock.
* The editor check (`run_editor_check.py`): every earlier check, plus the toolbar toggle and its setting, 24
  configuration-warning cases (each expected phrase, or none), and handles dragged with mouse events sent to the
  editor's window (water, shallow water, cell bounds, tracker radius), the property checked and undone.
* fellow_agent's self-test (a misplaced emitter must be reported with its node path).
* The class reference: `xmllint` against `doc/class.xsd`, `make_rst.py --dry-run` (no warning).

## Results

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

## Decision

**KEEP.** An untouched editor costs nothing again unless something in the scene moves, and then only what moves asks
for frames; one toggle stills the rest. Nodes that cannot work say why, in the editor and to agents; FellowEngine's
nodes can be shaped with handles; every setting explains itself where it is set. Rejected: stilling the nodes
whenever the editor is not updating continuously (the first version of this milestone) — Godot animates `TIME` shaders
and particles in the editor and deprecates *Update Continuously*, so FellowEngine's nodes would have been the only ones
needing a deprecated option to move.
