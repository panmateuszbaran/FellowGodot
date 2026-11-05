# 0152 — An agent toolkit: scenes rendered, described, checked and measured from the command line

Status: **KEEP** (tools; one engine fix) · Branch `fellow/geometry` · `tools/fellow_agent/` (`fellow_agent.py`,
`fellow_agent.gd`, `selftest.py`, `test_project/`, `AGENTS.project.md`) · `AGENTS.md` and `CLAUDE.md` at the root ·
Fix: `servers/rendering/renderer_rd/cluster_builder_rd.{h,cpp}`.

## Problem

FellowEngine's owner works on it, and on games made with it, mostly with AI agents. An agent edits text — scenes
(`.tscn`), scripts, shaders, C++ — and can then only guess whether the result works: Godot's tools assume a person at
the editor, looking at the viewport and the Output panel. The bench (`benchmarks/`) measures FellowBench's labs, not
an arbitrary project; the editor check (decision 0151) checks the editor module. What an agent needs is to see a
scene, read its structure, find its errors with file and line, and measure it, from a command, with output it can
parse, and to know where things are and what the rules are when it works on the engine itself.

## Change

**`tools/fellow_agent/fellow_agent.py`** (Python 3, standard library; Pillow and NumPy only speed up `compare`):
every command prints JSON and exits 0 (fine), 1 (it ran and found problems) or 2 (it could not run: no binary, a
crash — reported as such, with the engine's log — or a timeout). It runs the engine with `fellow_agent.gd` as the main
loop (`-s`, outside the project: nothing is copied into it), which prints one line, `FELLOW_AGENT {json}`.

* **capture** — a scene (a `.tscn`/`.scn`, an imported `.glb`/`.gltf`, or a node script that builds one) rendered
  after it has built itself (10 frames) and settled (30 frames at a fixed 60 fps, so the same picture every run): from
  its camera, or from views framed on its geometry (`iso`, `front`, `back`, `left`, `right`, `top`: every corner of
  the bounds in the frame), a 2D scene from the screen. A sun and a sky are added where the scene has none, as the
  editor's viewport does (`--preview-lighting`; reported). A PNG per view and a contact sheet; per view the image's
  luma statistics (and `blank` for a flat image), the draw calls, objects, primitives, video memory, the CPU and GPU
  render times, FellowEngine's monitors; the scene's node and class counts, its bounds.
* **describe** — the scene's tree as data, not instantiated in the tree (no script runs): class, script, instanced
  scene, transform, bounds, and the stored properties that differ from the class's defaults (resources by class and
  path).
* **check** — every `.tscn`/`.scn` and `.gd` of the project outside `addons/` (or the ones given): scripts loaded
  (parse errors), scenes' dependencies checked, scenes loaded, instantiated, run 5 frames and freed; per file its
  errors.
* **run** — a scene run for some seconds (a framed camera where it has none): frame time mean and percentiles, the GPU
  time, the last frame.
* **compare** — two PNGs: mean and largest difference, the share of pixels over a threshold, PSNR, an amplified
  difference image.
* **import** (an editor build's `--import`), **api** (a class's reference from the repository's XML — members,
  methods, signals, constants — or `--search` by name, with close matches for a misspelt class), **init** (an
  `AGENTS.md` for a game project, from `AGENTS.project.md`).

**Errors**: a `Logger` (Godot 4.5's `OS.add_logger`) catches every error and warning while the script runs — type,
message, the C++ function, file and line, and for scripts the GDScript backtrace (`script_location`: the script's own
file and line, what an agent fixes). Lines it cannot catch (before the script starts, other threads') come from the
engine's log, counted. A project not imported yet, or a release template (which does not check scripts' errors: a call
on `null` crashes it), is said so in the report.

**`AGENTS.md`** at the repository's root (and `CLAUDE.md`, which imports it): where FellowEngine's code is, how to
build, how to check a change (the toolkit, lavapipe, the renderer matrix, the bench, the editor check, the class
reference's checks), the fork's rules, and what is out of scope.

**`selftest.py`**: every command on `test_project/` — a 3D scene with FellowEngine's nodes and no camera or light, a
scene with a camera, positional lights, a 2D interface, a spinning node, and three planted faults (a parse error, a
call on `null` at `_ready`, a missing dependency) — 26 checks of what each must report.

### Fault found and fixed: positional lights missing in debug builds (decision 0051)

The first run on a real scene (Sponza, the bench project's imported glTF, in the Windows editor build on the RTX, job
f342) reported every frame "Uniforms supplied for set (0) ... are not the same format as required by the pipeline
shader". The light clusters' store pass in FellowEngine's element-parallel variant (`lights/cluster_pack_elements`, on
by default since `fellow130`, milestone 10) clears the render buffer it reads, so its shader declares binding 1
writable; it was given the uniform set made for the stock variant, where binding 1 is read-only. Release templates do
not check the format — Vulkan's descriptors are the same either way — so every bench run drew correctly. Debug builds
check it and **refuse the dispatch**: in the editor's viewport and in debug exports the clusters stayed empty, and
omni lights, spot lights, decals and reflection probes were not drawn. The variant has its own uniform set now.
Checked on lavapipe with debug builds without and with the fix: the lights scene of the self-test had its four
positional lights missing and 43 errors before, and is lit with none after
(`docs/showcase/debug_build_lights_before_after.jpg`; 31 % of the pixels differ by more than 8 levels).

The same run showed `capture` framing Sponza from 12 km: a directional light's bounds (thousands of metres) were in
the box framed. Views are framed on geometry only now, and each view reports `scene_coverage` (the share of pixels
the scene changes, from a frame with the scene hidden; a warning below 1 %).

## Verification

The self-test on the cloud's CPU (Linux editor build; lavapipe and llvmpipe) and, once, on the RTX laptop (the
Windows editor, job f342), where Sponza was also captured, described and run.

## Results

| run | result |
|---|---|
| self-test, the cloud's CPU (Linux editor build, debug; lavapipe; 640 × 360) | **26 of 26 checks pass**: the planted parse error (`does_not_parse.gd:6`), the call on `null` (`script_location` `fails_at_ready.gd:7`) and the missing dependency (`res://scenes/gone.tscn`) found, the five good scenes and the good script clean; three framed views of a scene with no camera or light (coverage 13-39 %), lit by the preview sun and sky; the scene's own camera used; framed on the geometry (10 m), not on the sun; two captures identical (largest difference 0); a 2D scene captured; the positional lights drawn with no error; the tree described; 34 frames measured; the class reference; `AGENTS.md` written |
| self-test, the RTX (Windows editor, job f342; before the lights, framing and coverage checks were added) | 22 of 22 pass; captures identical; 8240 frames of the spinning scene in 2 s (0.23 ms a frame) |
| time per command, the cloud's CPU | `describe` 0.3 s, `check` (10 files) 0.6 s, `capture` one view 12 s (lavapipe), three views 36 s, a 2D scene 2 s, `run` 2 s plus start 12 s |
| Sponza on the RTX (job f342) | `describe`: 156 nodes (115 meshes, 24 lights, 6 cameras) in 1.3 s; `capture` and `run`: the light clusters' error above, and a camera 12 km away — both fixed |
| the lights scene in a debug build, without / with the fix (lavapipe) | 43 errors, four lights missing / no error, lit |

## Decision

**KEEP.** An agent can now see what it built (pictures, a coverage figure that says when the camera sees nothing),
read a scene as data, find errors with their script line, measure a scene and diff two renders, all from commands that
answer in JSON; and an agent working on the engine finds its way from `AGENTS.md`. Run once on a real scene, the
toolkit found that FellowEngine's editor had not drawn positional lights since milestone 10 — a fault every
measurement had missed, because they all ran release templates.
