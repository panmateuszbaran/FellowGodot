# FellowEngine — milestone 51 report (tools for AI agents)

Status: **checked on the cloud's CPU and, once, on the RTX laptop** (2026-11-05). Branch `fellow/geometry`. Decision
record `docs/decisions/0152-agent-toolkit.md`.

## 0. Result

FellowEngine's owner works with AI agents rather than a team, so after the tools of milestone 50 came tools an agent
uses to check its own work. `tools/fellow_agent/fellow_agent.py` renders a scene to pictures (from its camera, or from
views framed on its geometry, lit if it has no light), dumps a scene's tree as data, checks every scene and script of
a project, measures a scene's frame times, compares two pictures, imports a project, prints a class's reference and
writes an `AGENTS.md` for a game project. Every command prints JSON and exits 0, 1 or 2; every error comes with its
file, line and, for scripts, the backtrace. `AGENTS.md` at the repository's root tells an agent working on the engine
where things are, how to build, how to check a change and what the fork's rules are.

The self-test (every command on a project with three planted faults) passes on the cloud's CPU, 26 of 26 checks, and
on the RTX. Its first run on a real scene, Sponza in the editor build on the RTX, found a fault older than the bench's
whole history of release-template runs: since milestone 10 **debug builds — the editor's viewport, debug exports —
drew no omni lights, spot lights, decals or reflection probes**. FellowEngine's element-parallel light-cluster pass
(decision 0051) was given the stock variant's uniform set; release builds do not check it and drew correctly, debug
builds refused the dispatch every frame. Fixed, and the self-test now has a scene with positional lights.

## 1. Development

* `tools/fellow_agent/`: `fellow_agent.py` (the commands; Python's standard library, Pillow and NumPy optional),
  `fellow_agent.gd` (the engine side, run with `-s` from outside the project: a `Logger` for the errors, one
  `FELLOW_AGENT {json}` line), `selftest.py` and `test_project/` (planted faults), `AGENTS.project.md` (for `init`),
  `README.md`.
* `AGENTS.md`, and `CLAUDE.md` importing it.
* The fix: the light clusters' element-parallel store pass has its own uniform set
  (`servers/rendering/renderer_rd/cluster_builder_rd.{h,cpp}`).
* Built in the cloud for the first time: a Linux editor (for `import` and debug checks there).

## 2. Verification

* The self-test on the cloud's CPU with the Linux editor build (lavapipe, llvmpipe) and on the RTX with the Windows
  editor (job f342, before the GPU was set aside).
* Sponza (the bench project's imported glTF) captured, described and run on the RTX.

## 3. Measurements

| run | result |
|---|---|
| self-test, the cloud's CPU (Linux editor build; lavapipe, 640 × 360) | 26 of 26 pass: the parse error at `does_not_parse.gd:6`, the call on `null` at `fails_at_ready.gd:7`, the missing `gone.tscn` found; the good scenes clean; framed views (coverage 13-39 %), the scene's camera, a 2D scene, the positional lights lit; two captures identical |
| self-test, the RTX (Windows editor, job f342) | 22 of 22 (the checks added after it not run: the GPU was set aside) |
| a command's time, the cloud's CPU | `describe` 0.3 s, `check` 0.6 s for 10 files, `capture` 12 s a view (36 s for three), `run` 2 s plus 12 s of start |
| the lights scene, debug builds without / with the fix | 43 errors and the four positional lights missing / none, lit ([picture](showcase/debug_build_lights_before_after.jpg)) |
| Sponza on the RTX | described (156 nodes) in 1.3 s; captured from 12 km before the framing fix (the sun's bounds) |

## 4. Limits

* `capture` and `run` need a renderer: a display, or `xvfb-run` on Linux (on the cloud's CPU, lavapipe: about 12 s a
  view at 640 × 360 for a small scene); `describe` and `check` run headless.
* A scene that builds itself only when a harness calls it (the bench's labs, `build()`) is not built by `capture`; the
  bench has its own capture mode.
* The coverage measure compares the view with and without the scene: temporal effects that keep the previous frame
  (TAA) can leave a little of it in the empty frame.
* Pictures are the same every run on one machine and build; across GPUs or drivers they differ (compare with a
  threshold).

## 5. Next

Whatever the agents working with the engine need next; candidates: a `watch` mode that re-captures on file changes,
golden-image tests over a project's scenes, an MCP server around these commands.
