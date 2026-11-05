# FellowEngine — milestone 50 report (tools)

Status: **checked in the editor on the RTX laptop and headless, the web template in a headless browser** (2026-11-05).
Branch `fellow/geometry`. Decision record `docs/decisions/0151-editor-tools.md`.

## 0. Result

The last milestone of the agreed order (memory and streaming, upscaling, animation, platforms, tools) gives
FellowEngine's runtime work an editor side. A new editor-only module, `fellow_editor`, draws FellowEngine's nodes in the
3D view — a gas volume's box with handles, emitters, water extents, shallow-water domains, streaming cells' bounds,
the wind's arrow, lightning's band, snow trackers — imports `.bvh` motion capture as Animation resources, and splits a
level into streamed cells from **Project > Tools > Split into Streaming Cells**; the cells then show their content in
the editor, so the level looks as it did. Every FellowEngine class has an icon and a complete class reference (184
entries were missing or empty), and the debugger's Monitors tab shows texture streaming, world streaming and motion
matching.

The first checks of an editor build — an editor plugin that exercises all of it and quits, run on the laptop with the
editor's window and headless — pass. They also found two faults no game run could show: `FellowWeather3D`'s
`weather` property missing from the editor (debug builds refuse a two-argument setter), and the pipeline cache's
limit read before its setting existed, so that since milestone 9 caches over 64 MiB were thrown away at start. With
the toolchains approved, Android's and the web's templates are built at last; the web one runs in headless Chromium
with threads, its cells streamed on worker threads and its image desktop OpenGL's — and that run found the water
invisible with the Compatibility renderer, which milestone 49's images had missed (OpenGL's depth read as Vulkan's).

## 1. Development

* **`modules/fellow_editor`** (editor builds only; `fellow_editor_plugin.{h,cpp}`): `FellowGizmoPlugin` (lines from
  the nodes' properties, so the module needs no other module's headers; the gas volume's handles through
  `Gizmo3DHelper`, undoable; icon billboards), `FellowBVHImporter` (`FellowMotionDatabase.import_bvh()` through
  ClassDB; options `scale` and `skip_first_frame`, presets for centimetres and the CMU conversion),
  `FellowEditorPlugin` (the menu item and its dialog: cell size, directory).
* **`FellowWorldStreamer3D.split_into_cells(root, cell_size, directory, options)`** (runtime, `fellow_world`): the
  root's 3D children grouped on the streamer's XZ grid, saved as `cell_X_Z.scn`, replaced by cells; cameras,
  directional lights, environments, streamers, cells and nodes wider than two cells stay. **`editor_preview`** (on):
  in the editor the cells' scenes as internal children, not listed, not saved, a click selecting the cell.
* **Icons**: 17 SVGs in the modules' `icons/`.
* **Performance monitors**: `FellowEngine/Texture streaming resident|wanted|pending`, `World cells loaded|loading`,
  `Motion matching time|updates`.
* **The class reference**: the members, methods, constants and signals of the gas volume, emitter, shallow water,
  burning objects, the `time` members, the bone roles, `RenderingServer.fellow_get_texture_streaming_info()`; three
  classes added to `fellow_fluid`'s documented list; `make_rst.py` clean for FellowEngine's classes.
* **Checks**: `benchmarks/tools/editor_check/` (`run_editor_check.py`: the editor plugin's checks, then the split level
  streamed back in a game; `--plain`, `--editor-args`); `benchmarks/tools/platforms/web/` (`web_check.sh`: the web
  template, a `.pck` made with `PCKPacker`, a server with the cross-origin isolation headers, Chromium headless).
* **Fixes**: the weather property's setter; the pipeline cache's limit; the water in the Compatibility renderer (the
  depth's conversion, no cascade sampled without wave textures) and the shallow water's refraction there; a
  streaming cell's double free at exit; the web image's `libatomic`; `platforms.sh` no longer copies the host's
  generated sources into a platform's tree.

## 2. Verification

* The editor (MSVC, `target=editor`) on the laptop: the check with the editor's window (Vulkan, Forward+, the RTX)
  and headless, jobs f338-f341; `--plain` (no check) and `--verbose` to look for the source of an error at exit;
  `--doctool` into a copy of the docs, compared with the repository's.
* The module compiled with GCC in the cloud (Linux, `target=editor`).
* The web template (Emscripten 4.0.10, threads) in Playwright's Chromium (WebGL 2 on SwiftShader), its image against
  the Linux template's on desktop OpenGL (llvmpipe) with the same `.pck`; the Android template (NDK r29, arm64) built.
* Water Lab on the Compatibility renderer (llvmpipe) before and after the water's fix.

## 3. Measurements

| check | result |
|---|---|
| icons | 17 of 17 in the editor's theme |
| gizmos | one FellowEngine gizmo on each of the eight nodes ([picture](showcase/editor_gizmos.jpg)) |
| `.bvh` import (5 joints, 11 frames) | imported by `fellow.bvh`: 6 tracks, the hips at 0.9 m, 0.333 s |
| split (40 boxes, a 256 m level, 64 m cells) | 15 cells and 15 scenes in 85-99 ms, every cell owned by the scene, the sun and the streamer left |
| the cells in the editor; the saved level | 40 of 40 boxes shown; 0 boxes and 15 cells in the file |
| the saved level in a game | 15 cells loaded, 40 of 40 boxes where they were (1.1 µm at worst), the monitor at 15 |
| monitors | 7 of 7 |
| `--doctool` | 184 entries missing or empty and 3 classes outside the module's list before; none after |
| the editor's log | no error of the check's; one get_node() error when the editor quits after the scripted changes (none with `--plain`), its source not found |
| web template in Chromium | 10.3 MB; isolated, threads on; the cells in by frame 9; 129 frames in 74 s (SwiftShader); the image desktop OpenGL's, mean difference 0.46 of 255 ([picture](showcase/web_check.jpg)) |
| Android template | `libgodot_android.so`, 76 MB; not run |
| Water Lab, Compatibility | the lake missing before, drawn after ([before / after](showcase/compatibility_water_before_after.jpg)) |

The editor builds on the laptop in 14 minutes from milestone 2's objects, then 1-2 minutes; the web and Android
templates in 37 minutes each on the cloud's two cores.

## 4. Limits

* No inspector plugins (a preview of a gas volume or the weather in the editor); the simulations and the weather do
  nothing in the editor; HLOD and impostors are still baked at run time.
* The cell splitter groups by bounds' centres on a grid: no content-aware partition, no proxies baked for the cells.
* FellowEngine's 296 project settings have no tooltips in the editor (they are documented in
  `docs/FELLOWENGINE_OPTIONS.md`, not in `ProjectSettings.xml`).
* The Android template is built, not run (no emulator on the cloud's CPU); macOS and iOS still need Apple's SDK.
* With the Compatibility renderer the water is flat (no waves without a RenderingDevice).
* The error at the editor's exit after the check's scene changes is not explained.

## 5. Next

The agreed order is done. What the assessment still lists: measurements on other GPUs when hardware is available, HLOD
baked offline, the transient VRAM of texture streaming's swaps. Not pursued, by the owner's decision (2026-11-05): a
content pipeline for large teams, and DLSS, XeSS and frame generation for now — FellowEngine is for its owner's work,
much of it with AI agents.
