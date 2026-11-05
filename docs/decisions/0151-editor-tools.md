# 0151 — Editor tools: gizmos and icons, a BVH importer, the cell splitter, previews, monitors, the class reference

Status: **KEEP** (an editor-only module; in games only the monitors and the fixes) · Branch `fellow/geometry` ·
Module `modules/fellow_editor` (`fellow_editor_plugin.{h,cpp}`, editor builds only) · Icons in
`modules/fellow_{fluid,world,animation,motion}/icons/` · `FellowWorldStreamer3D.split_into_cells()` and
`editor_preview` (`modules/fellow_world`) · Performance monitors (`modules/fellow_world/register_types.cpp`,
`modules/fellow_animation/register_types.cpp`) · The class reference (`modules/*/doc_classes/`) · Check:
`benchmarks/tools/editor_check/` (`run_editor_check.py`, an editor plugin in `project/`), and the web check
`benchmarks/tools/platforms/web/`.

## Problem

FellowEngine's 49 milestones are in the runtime. In Godot's editor its nodes were plain `Node3D`s: the generic icon,
nothing in the 3D view for a gas volume's box, a water body's extent, a shallow-water domain, a streaming cell's bounds
or the wind's direction; BVH motion capture could only be read by a script at run time
(`FellowMotionDatabase.import_bvh()`); world streaming (decision 0147) needed a level already cut into cell scenes by
hand, and the cells were empty boxes in the editor; nothing reported texture streaming, world streaming or motion
matching to the debugger. The assessment (`docs/fellow_aaa_readiness.md`) rated the editor and tools a gap. No check
had ever run an editor build: the bench runs export templates.

## Change

**Gizmos** (`FellowGizmoPlugin`, an `EditorNode3DGizmoPlugin`). What each node covers, drawn from its properties
(the plugin reads them by name, so it needs none of the other modules' headers): a gas volume's box with six handles
that resize it (`Gizmo3DHelper`, undoable), an emitter's sphere, box or cylinder, a water body's extent (nothing
when it reaches the horizon), a shallow-water domain, a streaming cell's bounds and, with its own load distance, the
box it loads from (orange), the wind's arrow along −Z as long as its speed, lightning's distance band and the line up
to its clouds, a snow tracker's capsule. Every FellowEngine 3D node also gets its icon as a billboard.

**Icons**: 17 SVGs in the modules' `icons/` folders (Godot's build picks them up): the nodes' in the 3D nodes' red,
the database's, weather's and the impostor baker's in the resources' grey.

**BVH import** (`FellowBVHImporter`, a `ResourceImporter`): a `.bvh` file in a project is imported as an `Animation`
(`.res`) through `FellowMotionDatabase.import_bvh()` (called through ClassDB, so the editor module does not depend on
the animation module), with two options — `scale` (the file's units in metres) and `skip_first_frame` (a T-pose
first frame) — and two presets: centimetres (0.01) and the CMU database's conversion (0.056444, the first frame
skipped). The animation can be dropped into an `AnimationPlayer` or given to `FellowMotionDatabase.build()`.

**Split into Streaming Cells** (Project > Tools): with a `FellowWorldStreamer3D` selected whose parent is the level,
a dialog asks for the cell size and a directory and calls `split_into_cells()`: the parent's 3D children are grouped
by their bounds' centre on the streamer's XZ grid, each group moved into a new scene saved in the directory
(`cell_X_Z.scn`) and replaced by a `FellowStreamingCell3D` under the streamer, centred on its cell, its bounds the
group's (grown by 1 cm). Cameras, directional lights, environments, streamers and cells stay (`exclude_classes`), and
so does a node wider than two cells (`max_span`) — the ground, a road. The new cells are owned by the edited scene,
which is marked unsaved; the file system is rescanned; a dialog reports the cells made. The method is a runtime one
(tools and tests can call it).

**Cells shown in the editor** (`FellowWorldStreamer3D.editor_preview`, on): in the editor each cell under a streamer
loads its scene as an internal child that the Scene dock does not list and the scene does not save; a click on it
selects the cell. After a split the level looks as it did. Off for a world too large to show whole. Nothing in a game.

**Performance monitors** (the debugger's Monitors tab, under *FellowEngine*; `Performance.get_custom_monitor()` in a
game): texture streaming's resident and wanted bytes and pending loads, the world's cells loaded and loading (every
streamer in the tree), motion matching's CPU time and updates a frame (averaged since the previous sample, summed over
the worker threads). Each is read when the debugger samples it, about once a second.

**The class reference.** `--doctool` regenerated FellowEngine's class XML in a copy and compared with the repository's
(job f340): 184 entries of FellowEngine's nodes were missing or empty — the members, methods, constants and signals of
`FellowBurnable3D` (51), `FellowGasVolume3D` (71), `FellowShallowWater3D` (28) and `FellowGasEmitter3D` (18), four
`time` members, the motion database's bone roles (10), a method each of `FellowMotionModel` and `RenderingServer` —
so the inspector showed no description for them; and `FellowLightning3D`, `FellowSnowTracker3D` and `FellowWeather3D`
were not in `fellow_fluid`'s `get_doc_classes()`. All are written now (from the code and
`docs/FELLOWENGINE_OPTIONS.md`); `make_rst.py` reports no error or warning in FellowEngine's classes. FellowEngine's
296 project settings stay documented in `docs/FELLOWENGINE_OPTIONS.md`, not in `ProjectSettings.xml`.

**The editor check** (`run_editor_check.py --godot <editor> --out <dir> [--headless]`): copies a small project and
opens it in the editor, where its plugin waits for the first import and checks the icons (every class's in the
editor's theme), the `.bvh` importer (a 5-joint, 11-frame file: its importer, tracks, the hips' first position, the
length), the gizmos (a scene with eight nodes: each has exactly one FellowEngine gizmo; a picture of the 3D view where
the editor renders), the split through its menu item and dialog (40 boxes in a 256 m level), the cells' content in
the editor, the saved level (no box in it, the cells), and the monitors; then `stream_back.gd` loads the saved level
in a game and streams it in. One JSON line, exit code 1 on a failure. `--plain N` opens the project without the
check and quits after N frames; `--editor-args` passes more (`--verbose`).

### Faults found and fixed

* **`FellowWeather3D.weather` did not exist in the editor** (job f338): its setter, `set_weather(weather, transition)`,
  takes two arguments, which debug builds' ClassDB refuses for a property (an error at start-up); release templates
  accept it, so no game showed it. In the editor the property was missing — not in the inspector, a scene's value not
  applied there and not written back when the scene was saved. A one-argument setter (`_set_weather_property`) now.
* **The pipeline cache's limit was 64 MiB, not 1024** (decision 0050; the editor's log, job f339): the
  RenderingDevice reads `rendering/fellow/frame/pipeline_cache_max_mb` before `RenderingServer::init()` defines it;
  `GLOBAL_GET` found nothing (a warning in every log) and the clamp made the limit 64 MiB. The laptop's runs have been
  discarding their 67-77 MiB pipeline caches at start — a few runs a session, since milestone 9 (the frame times are
  measured after warm-up and not affected; a cold start rebuilds its pipelines). The setting's default applies now
  until it exists.
* **The water was invisible with the Compatibility renderer** (the web check): the shader took the depth texture's
  value as the RenderingDevice's normalized device depth, while OpenGL's is a [0, 1] window depth ([−1, 1] in device
  space), so the water's thickness — its alpha — was 0 everywhere; and with no wave textures every cascade sampled an
  unbound array's white and lifted the surface 1 m a cascade. Now converted (`ndc_z()`, under `CURRENT_RENDERER`)
  and no cascade sampled without a RenderingDevice: a flat lake with its reflections traced. The RenderingDevice
  renderers' code is unchanged (the conversion is the identity there). The shallow-water mesh's refraction got the
  same conversion. Milestone 49's renderer matrix had reported no error there; the image had the lake bed without the
  lake.
* A streaming cell freed its content twice at exit (`split_check.gd`): the content is now freed on
  `NOTIFICATION_PREDELETE`, before the children, where the pointer is still valid.
* The platform scripts: the web image lacked `libatomic` for Binaryen's `wasm-opt`; `platforms.sh` copied the host's
  generated sources (`*.gen.*`) into a platform's tree with an older time stamp, and SCons, which does not regenerate a
  target that changed, linked the web template with Linux's `modules_enabled.gen.h` (undefined modules). Generated
  sources are the containers' own now.

## Verification

The editor on the laptop (MSVC, `target=editor`, Vulkan on the RTX, Windows; and `--headless`), jobs f338-f341. The
module compiled with GCC in the cloud as well. The web template in headless Chromium (Playwright's, WebGL 2 on
SwiftShader), its image against the Linux template's on desktop OpenGL (llvmpipe) with the same `.pck`. Water Lab on
the Compatibility renderer (llvmpipe) before and after.

## Results

| check | result |
|---|---|
| icons | 17 of 17 in the editor's theme |
| gizmos (gas volume, emitter, water, shallow water, cell, wind, lightning, snow tracker) | one FellowEngine gizmo each, drawn (`docs/showcase/editor_gizmos.jpg`) |
| `.bvh` import | `fellow.bvh` importer; 6 tracks (`Skeleton3D:Hips` position and rotations); the hips at (0, 0.9, 0) m; 0.333 s |
| split (40 boxes, 64 m cells) | 15 cells and 15 scenes in 85-99 ms; the sun and the streamer left in the level; every cell owned by the scene |
| the cells in the editor | 40 of 40 boxes shown; the saved level has 0 boxes and 15 cells |
| the saved level in a game | 15 cells loaded, 40 of 40 boxes back, the worst offset 1.1 µm; the monitor reports 15 cells loaded |
| Performance monitors | 7 of 7 registered |
| errors in the editor's log | none of the check's; one "Can't use get_node() with absolute paths from outside the active scene tree" when the editor quits after the check's scripted scene changes (not when it opens and closes the project: `--plain`), its source not found |
| `--doctool` against the repository | before: 184 entries of FellowEngine's nodes missing or empty, 3 classes outside the module's list; after: none (job f341) |
| web template (Emscripten 4.0.10, threads) | 10.3 MB; cross-origin isolated, threads on, both cells streamed in (worker threads) by frame 9; 129 frames in 74 s on SwiftShader; the image is desktop OpenGL's (mean difference 0.46 of 255, 0.6 % of the pixels over 8 levels: edges) |
| Android template (NDK r29, arm64) | `libgodot_android.so` 76 MB, built; not run (no emulator) |
| Water Lab, Compatibility (llvmpipe) | before: no water (luma 115.1 / 21.0 at 480 × 270); after: the lake drawn (103.6 / 25.4 at 1280 × 720; `docs/showcase/compatibility_water_before_after.jpg`) |

## Decision

**KEEP.** FellowEngine's nodes are visible and editable in the editor, motion capture imports like any other asset,
a level becomes a streamed world from a menu and still looks the same in the editor, and the debugger shows the
streaming and matching systems; the class reference covers every node. The first checks of an editor build found two
faults no game could show (the weather property, the pipeline cache's limit), and the first web run a renderer's
water that milestone 49's images had missed. Left: no inspector plugins (a preview button for the gas or the
weather), the weather and the simulations do nothing in the editor, HLOD and impostors are still baked at run time,
the project settings have no tooltips, no DCC round-trip or asset pipeline for a large team.
