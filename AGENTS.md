# FellowEngine — notes for AI agents working on the engine

FellowEngine is a fork of Godot 4.7.2-stable (branch `fellow/geometry`): a faster Forward+ renderer and new systems —
water, rain, snow, wind, lightning, weather, GPU smoke and fire, burning objects, HLOD, texture and world streaming,
terrain and foliage, FSR 3.1, motion matching, neural motion, editor tools. `README.md` lists them,
`FELLOWENGINE_CHANGELOG.md` says what each milestone added, `docs/decisions/` has one record per change (the problem,
the change, the measurements, the decision, what was rejected) and `docs/FELLOWENGINE_OPTIONS.md` every setting and
node.

## Where things are

| what | where |
|---|---|
| FellowEngine's nodes | `modules/fellow_fluid` (water, rain, snow, wind, lightning, weather, gas, burning), `modules/fellow_world` (HLOD, streaming, terrain and its foliage, pipeline warm-up), `modules/fellow_animation` (motion matching), `modules/fellow_motion` (neural motion), `modules/fellow_diagnostics` (crash reports), `modules/fellow_editor` (editor only) |
| renderer work | `servers/rendering/renderer_rd/` (files and blocks marked `fellow` / `FellowEngine`), effects in `renderer_rd/effects/fellow_*`, settings in `servers/rendering/fellow_settings.{h,cpp}` |
| Vulkan driver work | `drivers/vulkan/` (`FELLOW_VULKAN_PROFILE` in `fellow_vulkan_profile.*`) |
| the class reference | `doc/classes/*.xml`, `modules/*/doc_classes/*.xml` |
| a playable showcase | `showcase/` (a Godot project; its scenes are written by `showcase/tools/build_showcase.gd`, README.md) |
| benchmarks | `benchmarks/` (FellowBench project, `run_benchmarks.py`, analysis tools) |
| checks without a GPU | `benchmarks/tools/lavapipe/`, `benchmarks/tools/platforms/` (renderers, simulated GPUs, Windows/arm64/Android/web builds) |
| editor check | `benchmarks/tools/editor_check/` |
| agent toolkit | `tools/fellow_agent/` (below) |
| image regression suite | `tools/fellow_regression/` (below) |

## Building

```
scons platform=linuxbsd target=editor                                        # the editor (debug checks on)
scons platform=linuxbsd target=template_release disable_path_overrides=no    # a template that runs --path projects
scons platform=windows target=editor winrt=no accesskit=no d3d12=no          # Windows (MSVC), see BUILDING_FELLOWENGINE.md
```

Other platforms build in containers: `benchmarks/tools/platforms/platforms.sh build windows|linux-arm64|android|web`.

## Checking your work

* **A scene, a node, a visual change**: `tools/fellow_agent/fellow_agent.py` (Python 3, no packages needed):
  `capture` renders a scene to PNGs (from its camera or framed views) with statistics and the errors with file and
  line; `check` loads and runs every scene and script of a project; `describe` dumps a scene's tree; `run` measures
  frame times; `compare` diffs two PNGs; `api` prints a class's reference. Use an editor build (`$FELLOW_GODOT`):
  release templates do not check scripts' errors. Look at the PNGs you capture.
* **Rendering changes**: FellowEngine's rule is that the frame is stock Godot's unless a setting or node says
  otherwise. Compare captures before and after (`compare`; on lavapipe, Mesa's CPU Vulkan driver, renders are
  deterministic: `benchmarks/tools/lavapipe/`), and the three renderers with
  `benchmarks/tools/platforms/renderer_matrix.py`.
* **Before committing a change to rendering or a node**: `tools/fellow_regression/regression.py run` (under
  `xvfb-run -a` on Linux; `--template` with a release template too): FellowEngine's features rendered on lavapipe
  against their goldens, the release template against the editor build, FellowEngine off against on. Every picture is
  exact; a difference you meant gets new goldens (`bless`, look at them, commit them with the reason).
* **Far from the origin** (milestone 103): `tools/fellow_regression/regression.py far --godot <build> [--offset ...]`
  renders each scene near the origin and far out with one build and compares them; a double-precision build
  (`scons ... precision=double`) should keep them within a fraction of a per cent. A node or effect that computes world
  positions on the GPU works relative to the camera (`fellow_camera_relative.h`); world positions passed to physics
  servers are `real_t`.
* **Performance**: `benchmarks/run_benchmarks.py` (A/B with alternating order, heat soak, statistics); timings only
  mean something on a real GPU, not on lavapipe.
* **Long play** (a node that runs for minutes: its errors, hitches, memory): Slice Lab's soak,
  `benchmarks/tools/soak/run_soak.py --godot <template> --out <dir> --minutes 60` and `soak_report.py` (on a GPU; on
  lavapipe a few minutes at 160x90 with `-- --slice_weather_seconds=15 --slice_fire_seconds=10` still shows errors and
  growth). A node that makes materials, meshes or kernels after it has started (a fire lit again, its first ash)
  compiles pipelines mid-game: make them when the node is set up, and shaders from code through
  `FellowSharedShaders`.
* **Editor features**: `benchmarks/tools/editor_check/run_editor_check.py` (`--perf <scene> --perf-idle` counts the
  frames an untouched editor draws: a node that writes to the RenderingServer every frame without a visible change, or
  dispatches GPU work without asking for a frame (`Fellow::request_redraw()`), is a bug).
* **A new node**: give it configuration warnings (`get_configuration_warnings()`, shared phrasing in
  `modules/fellow_fluid/fellow_node_warnings.h`) for what stops it working, and handles in `modules/fellow_editor` if it
  has a shape; `fellow_agent.py check` reports the warnings. Shaders it makes from code, compute kernels and meshes or
  textures that do not depend on the instance come from `FellowSharedShaders` (`fellow_shared_shaders.h`): made once
  per session, not once per instance; and a ready-made setup of it belongs in the 3D toolbar's FellowEngine menu
  (`FellowEditorPlugin::make_preset`), without a configuration warning.
* **A new project setting**: add its description to `doc/tools/fellow_settings_docs.py` and run it (it fails on a
  setting without one).
* **Bound API**: every new bound member, method, signal or constant gets a description in its class XML; check with
  `xmllint --noout --schema doc/class.xsd <file>` and `python doc/tools/make_rst.py --dry-run doc/classes modules platform`
  (no errors; no warnings in FellowEngine's classes). A class of a module is listed in its `config.py`'s
  `get_doc_classes()`.

## Rules of this fork

* A change that alters behaviour sits behind a project setting under `rendering/fellow/` or in a node that does
  nothing unless a scene contains it; settings are documented in `docs/FELLOWENGINE_OPTIONS.md`.
* A measured change gets a decision record in `docs/decisions/` (next number, the format of the others) and a line in
  `docs/decisions/README.md`; a milestone gets `docs/fellow_milestoneN_report.md` and a changelog entry.
* Godot's code style (`.clang-format`, tabs, the license header on new files); keep FellowEngine's edits to stock
  files small and marked.
* Never commit generated files (`*.gen.*`, `bin/`), benchmark assets (`benchmarks/project/external/`,
  `benchmarks/project/override.cfg`) or third-party weights (NVIDIA's MotionBricks `.fmb`, its mannequin); stage
  explicit paths.
* Out of scope by the owner's decision: content pipelines for large teams, and DLSS, XeSS or frame generation (FSR
  3.1's hand-written upscaler stays).
