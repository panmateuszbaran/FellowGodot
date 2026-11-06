# FellowEngine — milestone 54 report (an image regression suite)

Status: **checked on the cloud's CPU** (lavapipe) **and on the RTX** (2026-11-06). Branch `fellow/geometry`. Decision record
`docs/decisions/0155-image-regression-suite.md`.

## 0. Result

Asked what brings FellowEngine closer to AAA, the first answer was a standing check: a suite that renders its
features after every change and fails on any difference. `tools/fellow_regression/regression.py` renders twelve
scenes — lights, shadows, materials, water, rain, snow, lightning, fire, HLOD and MultiMesh geometry, sky and clouds,
ray tracing — on lavapipe and compares each picture with its golden, the release template's picture with the editor
build's, and FellowEngine off with on. Every picture is bit-exact between runs and between the two builds, and
FellowEngine's defaults leave stock's frame exactly as stock draws it.

Building it found four faults, all fixed: a gas volume's box drawn in the wrong place in the editor since milestone
50 (its floor is on the node; the gizmo and handles centred it), the FellowEngine menu's Fire, Smoke and Burning Box
(milestone 53) emitting nothing (their emitters below the box, with no warning to say so: there is one now), a checker
of noise in the clouds traced a quarter of the pixels per frame (each pixel's history lost since milestone 19), and
the rain's splashes changing between runs (stock Godot's sub-emitter particles: left out of the suite's scene). Two
more were the suite's own: stock's frame is not defined at widths like 480 and 1920 (decision 0067), and a FellowHLOD3D
builds other proxies with FellowEngine off.

## 1. Development

* `tools/fellow_regression/`: `regression.py` (`run`, `bless`; `--template`, `--only`, `--goldens`, `--tolerance`,
  per-scene tolerances; each group's project imported before a template runs it), `suite.json` (four groups, 512 ×
  288, 40 frames at 60 fps), `project/` (twelve scenes), `goldens/`, `README.md`.
* `tools/fellow_agent/fellow_agent.{py,gd}`: the global random sequence seeded (`--seed`, 1).
* `modules/fellow_editor/fellow_editor_plugin.cpp`: the gas volume's box drawn up from the node, its handles width,
  depth and height; the Fire, Smoke and Burning Box setups' emitters inside their boxes.
* `modules/fellow_fluid/fellow_gas_emitter_3d.{h,cpp}`, `fellow_gas_volume_3d.cpp`: a configuration warning for an
  emitter that does not reach into its volume's box, kept current as it moves or the shapes change.
* `servers/rendering/renderer_rd/shaders/effects/fellow_clouds.glsl`: the interleaved trace's history bounded by the
  traces of the 3 × 3 blocks around.
* The editor check: the gas volume's height handle dragged and undone, an emitter below its volume's floor warned.
* `AGENTS.md`: run the suite before committing a change to rendering or a node.

## 2. Verification

* The suite blessed and run with the release template after each fix (Linux editor and template builds, lavapipe
  under xvfb); the goldens looked at one by one.
* The clouds: still and panning, interleaved against the full trace, before and after.
* The editor check and fellow_agent's self-test, on lavapipe and on the RTX (the Windows editor rebuilt with
  milestones 52-54); the suite on the RTX with its own goldens.

## 3. Measurements

The final run (`run --template`, Linux editor and release template builds of milestone 54, lavapipe under xvfb; 554 s
for 32 captures), seconds per capture and the checks:

| group / scene | editor build | release template | against its golden | release against editor | against `base` |
|---|---:|---:|---|---|---|
| base / lights | 8.3 | 8.2 | identical | identical | — |
| base / shadows | 13.3 | 12.7 | identical | identical | — |
| base / materials | 20.6 | 20.0 | identical | identical | — |
| base / water | 23.4 | 23.8 | identical | identical | — |
| base / rain | 14.7 | 14.7 | identical | identical | — |
| base / snow | 14.4 | 14.5 | identical | identical | — |
| base / lightning | 5.3 | 5.2 | identical | identical | — |
| base / fire | 13.2 | 13.2 | identical | identical | — |
| base / geometry | 23.5 | 23.3 | identical | identical | — |
| base / instances | 23.5 | 23.3 | identical | identical | — |
| stock / lights | 7.5 | 7.6 | identical | identical | identical |
| stock / shadows | 11.2 | 11.2 | identical | identical | identical |
| stock / materials | 18.6 | 18.4 | identical | identical | identical |
| stock / instances | 21.6 | 21.6 | identical | identical | identical |
| post / sky | 17.8 | 18.1 | identical | identical | — |
| rt / raytracing | 33.2 | 33.5 | identical | identical | — |

No engine error in any capture. Each picture matched a golden rendered by another process earlier (`bless`), so the
pictures are the same between runs as well as between the two builds. The faults above were found by looking at the
goldens (the empty fire, the clouds' checker, the clipped flash) and by the checks (stock's last column, the splashes,
the HLOD town, the template's script cache).

| check | result |
|---|---|
| the clouds, interleaved against the full trace (frame 40, 512 × 288) | still camera 45.3 → **51.5 dB**; panning 60° a second 44.4 → **47.5 dB** (frame 41: 44.0 → 47.7) |
| the rain, two runs | with splashes 20 of 147 456 pixels more than 8 levels apart (up to 98); without, identical |
| editor check (lavapipe, X11 under xvfb) | **passed**: the gas volume's height handle 1 → 1.5 m and undone, its box drawn up from the node; the emitter below its volume's floor warned; the seven setups added, owned, **without a warning**, undone; every earlier check |
| fellow_agent self-test | **28 of 28** |

On the RTX 5070 Ti Laptop (job f343: the Windows editor built from 5fb45ba; 2026-11-06):

| check | result |
|---|---|
| fellow_agent self-test | **28 of 28** |
| editor check, window and headless | **EDITOR_CHECK_OK** both |
| the suite, blessed into the RTX's own goldens (`--goldens`), then run against them | **every picture identical**: the GPU's frames are the same between runs too (16 captures in 32 s) |
| FellowEngine off against on, on the RTX | identical but `stock/shadows`: **one pixel** one level apart (7 × 10⁻⁶ of the pixels; lavapipe: identical); cause not found |
| the RTX's pictures against lavapipe's goldens | lights, shadows, materials, lightning, rain within 2-8 levels (4-13 % of the pixels differ at all, none by more than 8); fire, geometry, snow up to 19-158 levels in 0.02-0.05 % of the pixels; sky 1.8 %, water 7.3 %, ray tracing 16.7 % of the pixels more than 8 levels off — lavapipe's goldens are lavapipe's, a GPU keeps its own |

## 4. Limits

* Goldens are lavapipe's (Mesa 25.2, LLVM 20): the RTX renders 2-8 levels apart in lit scenes and up to 17 % of the
  pixels apart in ray tracing; `--goldens` keeps a GPU's own (on the RTX: identical between runs). On the RTX one pixel
  of the shadow scene differs by one level between FellowEngine on and off (lavapipe: identical; cause not found).
* The rain's splashes are not covered (stock Godot's sub-emitter emission order is not deterministic); nor is every
  option: each group of features is rendered once, with its defaults.
* lavapipe is slow: the scenes are small (512 × 288, a few hundred thousand triangles); the suite says whether a frame
  changed, not what it costs.
* The editor once aborted (`std::system_error`, "Invalid argument") 35 s after start while the suite ran next to it in
  a container short of memory (7 GB, two CPUs), as a wind idle run had once in milestone 52; not reproduced alone.

## 5. Next

The vertical slice: one large level with every system, played for an hour on the RTX with frame times, hitches and
memory recorded.
