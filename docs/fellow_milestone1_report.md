# FellowEngine — milestone 1 report

Date: 2026-09-30 · Base: Godot **4.7.2-stable** (`ed1daf0bf`) · Measured FellowEngine binary: `7f257b56ff`
(branch `fellow/gpu-fluid`) · Machine: Acer Predator PHN16S-71, Core Ultra 9 275HX, RTX 5070 Ti Laptop 12 GB
(driver 617.14), Windows 11 26200, Vulkan 1.4.351.

HTML report with sliders, diffs, graphs: `reports/2026-09-30_7f257b56ff/index.html` (copy in `reports/latest/`).
Renderer audit: `docs/fellow_renderer_audit.md`. Decision records: `docs/decisions/`.

## 1. Result

FellowEngine is Godot 4.7.2 plus a small set of isolated, runtime-toggleable changes. The first evidence-based
experiment targeted the temporal anti-aliasing stage (measured at 3-12 % of the frame, with concrete code defects):
**TAA resolve v2** and **TAA history ping-pong**. With both enabled, the TAA stage is 3-15 % cheaper than stock in all
8 scene/resolution configurations, the whole frame is unchanged within noise, and the image is closer to an
accumulated ground truth in every scene on fixed cameras (LPIPS −19 to −25 %) and in 7 of 8 motion sequences. One
regression was measured (more shimmer on alpha-tested foliage during a lateral strafe). A GPU shallow-water prototype
simulates a 512² grid in 0.21 ms. The largest measured costs — directional shadows in Sponza and opaque shading
everywhere — are not addressed yet.

### Stock vs FellowEngine (1920×1080, preset `high`, session `fellow-m1_perf`, interleaved ABBA; image quality = mean over fixed cameras vs ground truth)

| Scene | Renderer | GPU ms | CPU ms (render thread) | Frame ms | p95 ms | p99 ms | VRAM MB | SSIM ref | PSNR ref | LPIPS ref |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| PBR Lab | Stock | 2.557 | 0.224 | 2.978 | 3.632 | 3.855 | 304 | 0.9860 | 38.70 | 0.0289 |
| PBR Lab | FellowEngine | 2.636 | 0.224 | 3.045 | 3.695 | 3.886 | 313 | **0.9881** | **39.09** | **0.0219** |
| Sponza | Stock | 13.082 | 0.654 | 13.468 | 19.543 | 20.873 | 2533 | 0.9821 | 40.68 | 0.0281 |
| Sponza | FellowEngine | 12.718 | 0.673 | 13.103 | 18.634 | 19.478 | 2541 | **0.9840** | **41.06** | **0.0219** |
| Bistro exterior | Stock | 5.214 | 1.457 | 5.583 | 6.193 | 6.291 | 2109 | 0.9719 | 34.37 | 0.0321 |
| Bistro exterior | FellowEngine | 5.229 | 1.436 | 5.603 | 6.231 | 6.371 | 2118 | **0.9751** | **34.60** | **0.0252** |
| Bistro interior | Stock | 3.957 | 0.593 | 4.307 | 4.733 | 4.824 | 1272 | 0.9860 | 38.23 | 0.0214 |
| Bistro interior | FellowEngine | 3.948 | 0.589 | 4.307 | 4.732 | 4.830 | 1281 | **0.9875** | **38.48** | **0.0173** |

FellowEngine = `taa_resolve_v2` + `taa_history_pingpong`. **All frame-time differences in this table are within
run-to-run noise** (Welch's t-test, p > 0.05; the FellowEngine null test in the same session moves by up to 1.9 %).
2560×1440 and every variant (null test, ping-pong only) are in the HTML report and in
`benchmarks/results/fellow-m1_perf.csv`.

### Per-pass evidence (GPU timestamps, `fellow-m1_profile`)

| Scene | Res | TAA span stock | ping-pong only | v2 + ping-pong |
|---|---|---:|---:|---:|
| PBR Lab | 1080p / 1440p | 0.317 / 0.454 ms | 0.260 / 0.373 (−18 % / −18 %) | 0.304 / 0.438 (−4 % / −3 %) |
| Sponza | 1080p / 1440p | 0.272 / 0.537 ms | 0.235 / 0.419 (−14 % / −22 %) | 0.252 / 0.454 (−7 % / −15 %) |
| Bistro ext. | 1080p / 1440p | 0.260 / 0.481 ms | 0.215 / 0.380 (−17 % / −21 %) | 0.246 / 0.457 (−5 % / −5 %) |
| Bistro int. | 1080p / 1440p | 0.250 / 0.464 ms | 0.204 / 0.380 (−19 % / −18 %) | 0.241 / 0.432 (−4 % / −7 %) |

### Motion (60-frame sequences vs ground truth, `fellow-m1_temporal_analysis`)

v2 is closer to the ground truth in 7/8 sequences by PSNR (+0.3 to +1.8 dB), 8/8 by SSIM, 7/8 in motion regions and
6/8 in flicker. Worse in `foliage_strafe` (alpha-tested grass + chain-link fence, lateral): motion error +6 %, flicker
+5 %, PSNR −0.4 dB. Full table in `docs/decisions/0001-taa-resolve-v2.md`.

### Comparison modes

* **Iso-quality** (same settings, identical output): TAA history ping-pong — TAA stage 14-22 % faster
  (0.04-0.12 ms), no image change beyond the GPU's run-to-run noise.
* **Iso-performance** (no measurable frame-time difference): TAA v2 + ping-pong vs stock TAA — better image quality
  on every fixed camera and in 7/8 motion sequences.
* **Max quality**: not claimed. FellowEngine does not add new quality features beyond the TAA stage in milestone 1;
  the FELLOW quality presets are deliberately not created yet (the task requires benchmarking systems first).

## 2. The four questions

* **Does FellowEngine render better than stock Godot?** Yes, measurably but modestly, and only where it was changed
  (temporal anti-aliasing): closer to the ground truth in 4/4 scenes on fixed cameras and 7/8 motion sequences, worse
  in one (alpha-tested foliage strafe). All other rendering is identical to stock.
* **Does it run faster at equivalent quality?** Only the TAA stage (0.04-0.12 ms, 8/8 configurations, pass
  timestamps). At frame level the gain is ≤ ~1-4 % and not statistically resolvable with 3-4 runs. The dominant costs
  are untouched, so the honest answer for "the game runs faster" is **no, not yet**.
* **Does it produce better image quality at equivalent performance?** **Yes** for anti-aliasing (TAA v2 + ping-pong).
* **Can we prove those claims with reproducible measurements?** **Yes** for the claims above (method in §4, all
  commands in §5, raw data and hashes in `benchmarks/results/` and `benchmarks/baselines/godot-4.7.2/MANIFEST.json`).
  Limits: one machine, one GPU vendor, 3-4 runs per configuration, and a laptop whose absolute performance drifts
  between sessions (up to 6 % for stock itself) — therefore only within-session interleaved comparisons are used.

## 3. Identified bottlenecks (measured, stock, day state)

1. **Directional shadows** — Sponza 3.74 ms = 28 % of the frame at 1080p (up to 39 % on one camera); 78 % of all
   Sponza draw calls are shadow draws. CONFIRMED BOTTLENECK.
2. **Opaque shading** — 36-49 % of the frame in PBR Lab and both Bistro scenes. CONFIRMED (largest cost).
3. **Alpha-blended foliage** (Sponza cypress leaves, glTF `alphaMode: BLEND`) — transparent pass 16-19 %.
4. **SSAO (quality 3) + SSIL** — 9-20 % + 7-8 % of the frame.
5. **The TAA path** — resolve 0.26-0.54 ms plus motion-vector output in the opaque pass (+0.1-0.6 ms); total TAA path
   +0.24-1.24 ms (3-27 %) vs. no AA.
6. **Tooling**: headless `--import` leaves glTF textures uncompressed and without mipmaps (Sponza: 6.8 GB VRAM and
   2× GPU time before the fix).
Not bottlenecks here: render-thread CPU (≤ 1.6 ms at ~3000 draws), pipeline compilation during play (0 after
warm-up), static positional-shadow re-rendering (cached). Details and the full code audit: `docs/fellow_renderer_audit.md`.

## 4. Benchmark methodology

See `benchmarks/README.md` (full) and audit §A.1. In short: identical frame sequence on both engines
(`--fixed-fps 60`), VSync off, no cap, native 1920×1080 and 2560×1440, warm-up of every camera and one full
flythrough (0 pipeline compilations measured), ≥ 3 fresh processes per configuration interleaved ABBA, extra runs
when CV > 3 %, hitch rule (frames > max(100 ms, 5× median) excluded and replaced), 240 s heat soak and per-run GPU
telemetry, per-pass GPU timestamps in separate profile runs, lossless captures + linear EXR, ground truth accumulated
from 2×2 SSAA × 8× MSAA × 8-16 jitters, Cycles path-traced reference for the PBR lab (`benchmarks/reference/`),
PSNR / SSIM / LPIPS and temporal error / flicker metrics, Welch's t-test for decisions.

## 5. Commands

```bat
REM build (both engines use identical flags; see BUILDING_FELLOWENGINE.md)
benchmarks\scripts\build_windows.bat template_release
REM assets
.tools\venv\Scripts\python.exe benchmarks\scripts\fetch_sponza.py
.tools\venv\Scripts\python.exe benchmarks\scripts\fetch_bistro.py
.tools\venv\Scripts\python.exe benchmarks\scripts\import_assets.py
REM stock baseline (perf, profile, captures, temporal, ground truth)
.tools\venv\Scripts\python.exe benchmarks\run_suite.py baseline --build stock --tag godot-4.7.2
REM A/B (what fellow-m1 ran; .tools\job3.bat has the exact sequence)
set ABB=stock,fellow_off=fellow@enabled=false,fellow_pp=fellow@postfx/taa_history_pingpong=true,fellow_v2=fellow@postfx/taa_resolve_v2=true;postfx/taa_history_pingpong=true
.tools\venv\Scripts\python.exe benchmarks\run_benchmarks.py --builds "%ABB%" --scene pbr,sponza,bistro_exterior,bistro_interior --mode perf --res 1920x1080,2560x1440 --runs 3 --session fellow-m1_perf
.tools\venv\Scripts\python.exe benchmarks\run_benchmarks.py --builds "stock,fellow_pp=...,fellow_v2=..." --mode perf --profile --percam 300 --runs 1 --max-runs 1 --session fellow-m1_profile ...
.tools\venv\Scripts\python.exe benchmarks\run_benchmarks.py --builds stock,fellow_v2=... --mode temporal --res 1920x1080 --session fellow-m1_temporal ...
.tools\venv\Scripts\python.exe benchmarks\run_benchmarks.py --builds ... --mode capture --res 1920x1080 --extra=--freeze_exposure=1 --session fellow-m1_capture_gt ...
REM fluid sweep
.tools\venv\Scripts\python.exe benchmarks\run_benchmarks.py --builds "fl512=fellow#--fluid_res=512,fl512_hidden=fellow#--fluid_res=512 --water_visible=0,..." --scene fluid --mode perf --session fellow-m1_fluid_perf
REM analysis + report
.tools\venv\Scripts\python.exe benchmarks\tools\temporal.py --test <stock dir> --test2 <fellow dir> --ref <ground truth dir> --seq <name> --out <dir> --anim
.tools\venv\Scripts\python.exe benchmarks\tools\ab_stats.py benchmarks\results\fellow-m1_perf.json
.tools\venv\Scripts\python.exe benchmarks\tools\report.py --baseline godot-4.7.2 --ab fellow-m1 --fellow-label fellow_v2 --fluid fellow-m1_fluid --fluid-capture fellow-m1_fluid_capture --capture-gt fellow-m1_capture_gt --engine-commit 7f257b56ff --notes docs\fellow_milestone1_summary.md
```

## 6. Visual comparison locations

* `reports/2026-09-30_7f257b56ff/index.html` — draggable stock/FellowEngine sliders per fixed camera, diff heatmaps,
  ground truth, Cycles comparison, temporal plots and animated comparisons (`temporal/*.webp`), Fluid Lab captures.
* `reports/2026-09-30_7f257b56ff/screenshots/{stock,fellow,reference,diff}/` (downscaled copies; not in git).
* Lossless originals: `benchmarks/results/raw/fellow-m1_capture/`, `..._capture_gt/`, `..._temporal/`,
  `godot-4.7.2_reference*/` (ground truth), `godot-4.7.2_pbrref_export/cycles_*.exr` (Cycles).

## 7. Successful improvements

| Change | Evidence | Decision |
|---|---|---|
| TAA history ping-pong | TAA span −14 to −22 % in 8/8 configurations; output identical within GPU noise | KEEP (0002) |
| TAA resolve v2 | better vs ground truth: 4/4 scenes (fixed cameras), 7/8 sequences; cost neutral with ping-pong | KEEP (0001) |
| GPU shallow water (`FellowShallowWater3D`) | 512²: 0.21 ms sim, 1024²: 0.75-0.79 ms; mass conserving (NumPy reference); no readback | KEEP, opt-in (0003) |
| Benchmark suite, audit, report tooling | reproducible, all runs, raw data and hashes kept | — |

## 8. Failed / rejected experiments and mistakes (kept visible on purpose)

* **TAA v2, first version — failed.** Produced black blotches on flat transparent surfaces (PSNR 20 dB vs stock on a
  static camera). Root cause: inverted clip box from rounding + a clip function that divides by ≈ 0. Fixed before any
  measurement (`94f43415e7`); the iteration is recorded in decision 0001.
* **TAA v2 on alpha-tested foliage under lateral motion — measured regression** (+6 % motion error, +5 % flicker).
  Not fixed in milestone 1.
* **Frame-level speed-up — not demonstrated.** Ping-pong's pass-level saving does not reach statistical significance
  at frame level with 3-4 runs; we do not claim a frame-rate gain.
* **First stock baseline (night) — discarded as a baseline.** Thermally limited laptop state (1.0 GHz, 87 °C, 45 W);
  kept as `godot-4.7.2-night45W_*`.
* **First Sponza baseline — invalid.** Textures uncompressed and without mipmaps (headless import pitfall).
* **~480 ms periodic hitches** in the thermally limited night state — cause not identified (not reproducible in the
  day state with or without telemetry); guarded by the hitch rule now.
* **First reference settings** (16 jitters × 4 frames per sample, ~3 h) — replaced by 8 jitters; **first Cycles
  reference** rendered alpha-tested grass black (Cycles' 8 transparent bounces) and failed once on relative paths;
  **external-scene captures vs ground truth** were compared with different exposure (auto vs frozen) in the first
  analysis — all fixed and re-run; nothing from those attempts is in the reported numbers.

## 9. Verification status

| Check | Result |
|---|---|
| FellowEngine builds (template_release + editor, MSVC 2019, same flags as stock) | ✔ `.tools/bin/fellow/` (`7f257b56ff`), incremental rebuild `fe66bee7a9` ✔ |
| Editor opens a normal project (headless `--editor`) | ✔ stock and FellowEngine, no errors (`benchmarks/verify_project`) |
| Scene import (glTF/GLB with textures) | ✔ both; note: headless `--import` sometimes crashes **at exit** after finishing (0xC0000005 after `EditorNode::is_cmdline_mode` null singleton) — identical on stock 4.7.2 (1 of 3 fresh runs each), upstream issue |
| Exported/template run of a normal project | ✔ both render the imported models |
| Null test (all FellowEngine toggles off vs stock) | ✔ within noise in 8/8 configurations |
| C# (.NET) | **not verified by building**: the machine has .NET 6, Godot 4.7 needs the .NET 8 SDK; FellowEngine changes no file in `modules/mono` (checked with `git diff fellow/baseline..fellow/gpu-fluid -- modules/mono`: empty) |
| Shader variants | all TAA and fluid variants compile (`glslangValidator`); fluid kernels free of `OpVectorExtractDynamic` since `fe66bee7a9` |

## 10. Exact files changed (vs. `4.7.2-stable`)

Engine (`fellow/gpu-fluid`, 16 files, +1747 / −9):

```
modules/fellow_fluid/SCsub, config.py, register_types.h/.cpp        (new module)
modules/fellow_fluid/fellow_shallow_water_3d.h/.cpp                 (FellowShallowWater3D)
modules/fellow_fluid/fellow_swe.glsl                                (advect / height / velocity / surface kernels)
modules/fellow_fluid/doc_classes/FellowShallowWater3D.xml
modules/fellow_fluid/tests/swe_reference.py                         (NumPy reference of the kernels)
servers/rendering/fellow_settings.h/.cpp                            (rendering/fellow/* toggles)
servers/rendering/rendering_server.cpp                              (+3: register the toggles)
servers/rendering/rendering_server_default.cpp                      (+6: pass-timestamp toggle)
servers/rendering/renderer_rd/effects/taa.h/.cpp                    (variants, ping-pong path)
servers/rendering/renderer_rd/shaders/effects/taa_resolve.glsl      (resolve v2, velocity write)
```

Benchmarks and docs (`fellow/bench-update`, all new): `benchmarks/**` (project, runner, suite, tools, scripts, Cycles
reference, verification project, results aggregates, baseline), `docs/fellow_renderer_audit.md`,
`docs/fellow_fluid_architecture.md`, `docs/decisions/*`, `docs/fellow_milestone1_*.md`, `BUILDING_FELLOWENGINE.md`,
`reports/2026-09-30_7f257b56ff/` (without screenshots).

Branches: `fellow/baseline` (= `4.7.2-stable`), `fellow/benchmark-suite` (suite; fast-forwarded to the final suite
state), `fellow/postfx` (toggles + TAA), `fellow/gpu-fluid` (postfx + fluid + suite; the measured engine),
`fellow/gpu-scene` (created, empty: GPU-scene work starts after this milestone).

## 11. Architecture changes

See the table in `docs/fellow_renderer_audit.md` Part A and `BUILDING_FELLOWENGINE.md` ("Runtime toggles"):
a `Fellow::` toggle layer read with `GLOBAL_GET_CACHED`, TAA shader variants selected per dispatch with a ping-pong
state stored as render-buffer custom data, and a self-contained compute module for shallow water that submits its
work on the render thread and exposes its surface as a `Texture2DRD`. No changes to SceneTree, editor, importers,
physics (Jolt), C#, other renderers or the render graph; no stock default changed.

## 12. Remaining high-value opportunities

1. **Directional shadows** (largest measured pass in geometry-heavy content): static/dynamic caster split with cached
   far cascades, cascade scrolling, GPU culling of shadow casters. → **recommended next subsystem**, and the natural
   entry point for the GPU-scene work (Phase 4).
2. **SSAO/SSIL**: temporally accumulated half-resolution GTAO (9-20 % + 7-8 % of the frame today).
3. **TAA path**: foliage shimmer of v2 (reactive/alpha mask, contrast-adaptive γ); camera-only motion vectors for
   static geometry (opaque-pass cost of TAA up to 0.6 ms).
4. **Alpha-blended foliage**: offer alpha-to-coverage / dithered alpha for glTF BLEND foliage (Sponza transparent pass
   16-19 %).
5. **Fluid rendering**: the refraction screen copy costs more than the simulation; tile streaming; buoyancy readback.
6. **Tooling**: fix the headless import pitfall upstream-style (apply 3D texture detection in `--import`).

## 13. Next recommended engine subsystem

**Directional shadow rendering** — it is the largest measured single pass in the most geometry-heavy scene (28-39 % of
the frame in Sponza), it scales with exactly what an open-world third-person game has most of (vegetation, props,
distance), and improvements can be isolated behind a toggle and measured with the same pass timestamps. GPU-driven
culling for shadow casters is the first step of the GPU-scene architecture work.
