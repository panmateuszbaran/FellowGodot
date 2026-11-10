# FellowBench — Stock Godot vs FellowEngine benchmark suite

One Godot project (`benchmarks/project`) that runs unmodified on **stock Godot 4.7.2** and on **FellowEngine**,
a Python runner (`benchmarks/run_benchmarks.py`) and offline analysis tools (`benchmarks/tools`).

## Scenes

| Key | Scene | What it stresses |
|---|---|---|
| `pbr` | **PBR Validation Lab** (procedural, seeded) | roughness / metallic sweeps, normal maps, HDR emissive bars, transparency, alpha-scissor grass + fence, dark room, comb of thin cylinders, subpixel wires, checkerboard at grazing angles, moving box, rotating fan |
| `sponza` | **Intel New Sponza** (main + curtains + ivy + cypress trees, glTF) | 12.8 M triangles, 4K PBR textures, alpha foliage, cloth, bright atrium vs dark arcades |
| `bistro_exterior` | **Amazon Lumberyard Bistro v5.2** exterior (FBX) | 2.8 M triangles, 1296 instances, 132 materials, vegetation, long shadows, many props |
| `bistro_interior` | Bistro interior (FBX) | 1.0 M triangles, 2062 surfaces, shadowed omni lights, glass, dense props |
| `fluid` | **Fluid Lab** (FellowEngine only) | GPU shallow-water simulation 256² / 512² / 1024², surface reconstruction, water rendering |
| `slice` | **Slice Lab** (FellowEngine only, built in code) | a 1 km valley with every system at once: a town of 711 buildings under HLOD, street lights, a campfire with a burning log, a crowd, a lake, 9 000 trees and grass in the wind, turning weather, a day and night; the soak's scene (decision 0156) |
| `frontier` | **Frontier Lab** (FellowEngine only, generated once into `external/frontier_<size>/`) | an open world at AAA scale: 4 km of terrain at 1 m in 256 streamed regions with forests and meadows, 1 024 streamed cells of towns, villages and the wild (windows of a modular kit, street lights, props, rocks), proxies for what is not loaded, Slice Lab's systems in the first town; `--frontier_size=1024` for a small one (milestone 67) |

Every scene defines fixed cameras (`*_cam_NN`), a deterministic flythrough and short temporal sequences.
Sponza's fixed cameras are the six artist cameras shipped in Intel's glTF (`PhysCamera001-006`); Bistro's
flythrough is the camera animation embedded in the ORCA FBX (`Take 001`).

Asset acquisition (large files never enter git; see `.gitignore`):

```bat
.tools\venv\Scripts\python.exe benchmarks\scripts\fetch_sponza.py      REM ~7 GB download, CC BY 4.0 (Intel)
.tools\venv\Scripts\python.exe benchmarks\scripts\fetch_bistro.py      REM 0.9 GB, CC BY 4.0 (Amazon Lumberyard / NVIDIA ORCA)
.tools\venv\Scripts\python.exe benchmarks\scripts\import_assets.py     REM headless editor import (~3 min)
```

**Texture import pitfall (found during milestone 1):** a headless `--import` never runs the editor's "detect 3D"
reimport, so glTF textures stay *uncompressed without mipmaps*. The first Sponza baseline ran like that (6.8 GB
VRAM, GPU 23 ms at 1080p) and was discarded; `import_assets.py` now applies what the editor detection sets (VRAM
compressed, mipmaps, BC5 normal maps) by editing the `.import` files in place (2.5 GB VRAM afterwards).

Each fetched package writes `SOURCE.json` (URL, archive size, sha256, license, date). Conversion procedure:
* Sponza glTF is imported by Godot's glTF importer with default settings. The glTF contains 24 punctual lights with
  intensity 0 (export artefacts); the scene removes them (they would still cost clustered-light work).
* Bistro FBX is imported by Godot's FBX importer (ufbx). DDS textures are loaded natively. The BC5 normal maps use the
  DirectX convention and are converted *losslessly* in the compressed domain (`scripts/_bc5.py`, max error 1 LSB) into
  `TexturesGL/`. Materials are rebuilt at load time from the ORCA naming convention (BaseColor / Specular=ORM /
  Normal / Emissive) as `ORMMaterial3D`. The imported sun (Falcor energy 150) is disabled and replaced by the
  benchmark sun; interior omni lights are scaled x0.02, range-limited to 15 m and shadowed. All of this is recorded
  in every result file (`scene_desc.info`).

## Methodology

* **Determinism**: Godot runs with `--fixed-fps 60`, so every frame advances virtual time by exactly 1/60 s
  (animation, shader `TIME`, TAA jitter index, camera paths) independent of the achieved frame rate. Stock and
  FellowEngine therefore render the *same* frame sequence. Nothing in the project uses wall-clock time.
* **No sync / caps**: `--disable-vsync`, `application/run/max_fps=0`, `Engine.max_fps=0`, borderless window at the
  exact requested size (`--resolution WxH --position 0,0`); the result is marked invalid if the viewport size differs.
* **Warm-up**: every fixed camera is visited (20 frames each) and then whole flythrough passes are rendered until
  >= `--warmup` s (default 10 s) and at least one full pass; pipeline compilations during warm-up and during
  measurement are recorded separately (measurement compilations should be 0 = no shader stutter measured).
* **Measurement**: whole flythrough passes until >= `--measure` s (default 30 s).
* **Runs**: >= 3 fresh processes per configuration; builds are interleaved ABBA; if the coefficient of variation
  of the per-run mean frame time is > 3 % more runs are added (max 6) and the result is flagged `noisy`.
* **Metrics** (per frame): wall-clock frame time; GPU time and render-thread CPU time of the viewport
  (`viewport_get_measured_render_time_{gpu,cpu}`); frame-setup CPU time; process (script) time; draw calls /
  primitives / objects (`get_rendering_info`); shadow-pass draw calls. Summary: mean, std, CV, p50/p90/p95/p99/p99.9,
  average FPS, "1% low" (average FPS of the slowest 1 % of frames), stutters (> max(2 x median, median + 4 ms)).
  Memory: Godot-tracked VRAM / texture / buffer bytes (peak), process peak working set (psutil), load time.
  GPU clocks / temperature / power / throttle reasons are sampled every second with `nvidia-smi`.
  All child processes are started with `CREATE_NO_WINDOW`: when the runner is launched without a console, each
  `nvidia-smi` call would otherwise open a console window that steals focus from the benchmark window (measured as
  ~480 ms stalls every ~1.03 s; those runs were discarded).
* **Hitch rule**: a frame longer than max(100 ms, 5 × the run's median) is an external stall, not rendering cost. Runs
  with hitches are kept in the raw data but excluded from the aggregate, and replacement runs are added (up to
  `--max-runs`); if no clean run exists the aggregate says `hitches_not_excluded`. (Introduced after ~480 ms stalls
  every ~1.03 s appeared in the thermally limited night state; they could not be reproduced later with the same
  runner, telemetry on or off — see the milestone report.)
* **Thermal steady state** (laptop): a discarded heat-soak run (default 240 s) precedes every perf session; GPU clock,
  temperature and power are logged per run so drift is visible in the results.
* **Variants of one binary**: `--builds "stock,fellow_off=fellow@enabled=false,fellow_pp=fellow@postfx/taa_history_pingpong=true"`
  runs several FellowEngine toggle states interleaved with stock in one session (`label=dir@key=value;key=value`,
  optional per-variant scene arguments after `#`, e.g. `fl256=fellow#--fluid_res=256`).
* **Re-measuring part of a session**: `--merge` replaces the re-measured entries in `results/<session>.json` and keeps
  the old ones under `superseded` with `--supersede-note` (nothing is silently overwritten).
* **Per-pass GPU timings** (`--profile`): the engine's own `RENDER_TIMESTAMP` markers (enabled with `--gpu-profile`)
  are read every frame through `RenderingDevice.get_captured_timestamp_*`. Timestamps are ordering fences in the
  render graph, so profile runs are reported *separately* and never mixed with normal performance runs.
* **Ground truth** (`--mode reference`, preset `reference_ssaa`): 2×2 supersampling × 8× MSAA per sample, 8 Halton
  sub-pixel jitters per frame (fixed PBR cameras: 16), TAA off, averaged in linear light by `tools/accumulate.py`;
  6 settle frames after a pose change (SSIL/SSR reproject the previous frame), 1 between jitters.
* **Image metrics**: PSNR, SSIM and (if `torch` + `lpips` are installed in the venv) LPIPS-AlexNet on display-referred
  images; temporal metrics per frame vs. the ground truth (`tools/temporal.py`: error in motion regions, error in
  static regions, flicker relative to the ground truth's own frame-to-frame change).
* **Hardware record**: CPU, GPU, VRAM, RAM, OS, driver, display adapter, power state, engine commit, compiler,
  build flags, binary hashes -> `hardware.json` in every session.

Machine caveat: the benchmark laptop drives its internal 2560x1600 panel from the Intel iGPU (hybrid graphics);
frames rendered on the RTX 5070 Ti are copied to the iGPU for presentation. `gpu_ms` measures only RTX work;
`frame_ms` includes the cross-adapter present.

## Commands

```bat
REM performance (default: 1920x1080 and 2560x1440, 3+ runs, 10 s warm-up, 30 s measurement)
.tools\venv\Scripts\python.exe benchmarks\run_benchmarks.py --builds stock --scene all --mode perf
.tools\venv\Scripts\python.exe benchmarks\run_benchmarks.py --builds stock,fellow --scene pbr,sponza --mode perf
.tools\venv\Scripts\python.exe benchmarks\run_benchmarks.py --builds fellow --fellow "postfx/taa_v2=true" --scene pbr --mode perf
REM per-pass GPU timings (separate runs)
.tools\venv\Scripts\python.exe benchmarks\run_benchmarks.py --builds stock --scene all --mode perf --profile --percam 300 --runs 1
REM lossless captures of every fixed camera (+ linear EXR before tonemapping); note the --extra=<value> form
.tools\venv\Scripts\python.exe benchmarks\run_benchmarks.py --builds stock --scene all --mode capture --extra=--hdr=1
REM temporal sequences and accumulated ground truth
.tools\venv\Scripts\python.exe benchmarks\run_benchmarks.py --builds stock --scene pbr --mode temporal --res 1920x1080
.tools\venv\Scripts\python.exe benchmarks\run_benchmarks.py --builds stock --scene pbr --mode reference --res 1920x1080 --preset reference_ssaa --extra="--seqs=moving_object --jitter=8 --ref_settle_first=6 --ref_settle=1"
.tools\venv\Scripts\python.exe benchmarks\tools\accumulate.py <session>\stock\pbr --delete-inputs
REM offline Cycles reference for the PBR lab
godot ... -- --scene=pbr --mode=list --lighting=ref --export-ref=<dir>
blender --background --factory-startup --python-exit-code 1 --python benchmarks\reference\pbr_lab_cycles.py -- --scene <dir>\pbr_lab_scene.json --out <dir>
REM a soak: Slice Lab played in real time for an hour (frame times, hitches, memory per second; the process's memory
REM sampled from outside), then its CSV, chart and summary
.tools\venv\Scripts\python.exe benchmarks\tools\soak\run_soak.py --godot <template> --out <dir> --minutes 60
.tools\venv\Scripts\python.exe benchmarks\tools\soak\soak_report.py <dir>\slice_soak_<date>.json --out <dir>\report
REM whole plans (baseline = perf + profile + captures + temporal + references; ab = the same without references)
.tools\venv\Scripts\python.exe benchmarks\run_suite.py baseline --build stock --tag godot-4.7.2
REM report (HTML with sliders, graphs, tables) -> reports/<date>_<commit>/ and reports/latest/
.tools\venv\Scripts\python.exe benchmarks\tools\report.py --baseline godot-4.7.2 --ab fellow-m1 --fellow-label fellow_v2 --fluid fellow-m1_fluid
```

Performance overlay: `bench/overlay.gd` (F3 toggles; F4 exports the last 60 s of samples to JSON). It works on
stock Godot (public APIs only); per-pass timings need `--gpu-profile` on stock or
`rendering/fellow/profiling/pass_timestamps` on FellowEngine.

Results: `benchmarks/results/<session>.json|csv` (aggregates, committed) and `benchmarks/results/raw/<session>/`
(per-run JSON with every frame, logs, images; not committed). Baselines: `benchmarks/baselines/godot-4.7.2/`.
