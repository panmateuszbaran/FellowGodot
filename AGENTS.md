# FellowEngine — notes for AI agents working on the engine

FellowEngine is a fork of Godot 4.7.2-stable (branch `fellow/geometry`): a faster Forward+ renderer and new systems —
water, rain, snow, wind, lightning, weather, GPU smoke and fire, burning objects, HLOD, texture and world streaming,
FSR 3.1, motion matching, neural motion, editor tools. `README.md` lists them, `FELLOWENGINE_CHANGELOG.md` says what
each milestone added, `docs/decisions/` has one record per change (the problem, the change, the measurements, the
decision, what was rejected) and `docs/FELLOWENGINE_OPTIONS.md` every setting and node.

## Where things are

| what | where |
|---|---|
| FellowEngine's nodes | `modules/fellow_fluid` (water, rain, snow, wind, lightning, weather, gas, burning), `modules/fellow_world` (HLOD, streaming), `modules/fellow_animation` (motion matching), `modules/fellow_motion` (neural motion), `modules/fellow_editor` (editor only) |
| renderer work | `servers/rendering/renderer_rd/` (files and blocks marked `fellow` / `FellowEngine`), effects in `renderer_rd/effects/fellow_*`, settings in `servers/rendering/fellow_settings.{h,cpp}` |
| Vulkan driver work | `drivers/vulkan/` (`FELLOW_VULKAN_PROFILE` in `fellow_vulkan_profile.*`) |
| the class reference | `doc/classes/*.xml`, `modules/*/doc_classes/*.xml` |
| benchmarks | `benchmarks/` (FellowBench project, `run_benchmarks.py`, analysis tools) |
| checks without a GPU | `benchmarks/tools/lavapipe/`, `benchmarks/tools/platforms/` (renderers, simulated GPUs, Windows/arm64/Android/web builds) |
| editor check | `benchmarks/tools/editor_check/` |
| agent toolkit | `tools/fellow_agent/` (below) |

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
* **Performance**: `benchmarks/run_benchmarks.py` (A/B with alternating order, heat soak, statistics); timings only
  mean something on a real GPU, not on lavapipe.
* **Editor features**: `benchmarks/tools/editor_check/run_editor_check.py`.
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
