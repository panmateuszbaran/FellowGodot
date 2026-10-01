# Building FellowEngine

FellowEngine is a fork of **Godot 4.7.2-stable** (`ed1daf0bf001b61586d9930840f2f1394092c079`, tag `4.7.2-stable`).
The newest stable 4.x release was verified on 2026-09-29 with `git ls-remote --tags` (no 4.7.3 / 4.8 stable exists).

The build system is Godot's own SCons build; FellowEngine adds no build steps. Everything below was
verified against `SConstruct` and `platform/windows/detect.py` of this tree and executed on the benchmark machine.

## Toolchain used for all published results

| Item | Version |
|---|---|
| OS | Windows 11 Home 10.0.26200 |
| Compiler | MSVC 14.29.30133 (Visual Studio 2019 Build Tools 16.11.60) |
| Windows SDK | 10.0.19041.0 |
| Python | 3.11 (venv in `.tools/venv`, portable, not committed) |
| SCons | 4.10.1 (`pip install scons==4.10.1` into the venv) |

Notes verified in `platform/windows/detect.py`:
* VS 2019 works; `winrt=no` is required with SDK 10.0.19041 (WinRT/OneCore TTS needs SDK >= 10.0.22621).
* `production=yes` sets `use_static_cpp=yes`, `debug_symbols=no`, `lto=auto`; for MSVC `lto=auto` resolves to
  **no LTO** (Godot's own choice, "doesn't help" comment in detect.py). MinGW builds would use LTO.
* `d3d12=no`: the D3D12 driver needs the Mesa/NIR libraries (`misc/scripts/install_d3d12_sdk_windows.py`).
  The default rendering driver on Windows in 4.7.2 is Vulkan (`main/main.cpp`), which is what we benchmark.
* `accesskit=no`: screen-reader SDK not installed (auto-disabled otherwise; made explicit for determinism).
* `disable_path_overrides=no`: export templates in 4.7 reject `--path` by default. The benchmark harness runs
  the benchmark project folder directly with the release template, so it re-enables that CLI switch. It only
  affects command-line parsing (`main/main.cpp`, `core/config/project_settings.cpp`), not rendering.

## Setup (once)

```bat
cd FellowGodot
"C:\Users\<you>\AppData\Local\Programs\Python\Python311\python.exe" -m venv .tools\venv
.tools\venv\Scripts\python.exe -m pip install scons==4.10.1 psutil numpy pillow scikit-image matplotlib OpenEXR
```

## Build commands

All builds go through `benchmarks\scripts\build_windows.bat`, which pins the common flags so STOCK and
FELLOW binaries are always built identically:

```bat
benchmarks\scripts\build_windows.bat template_release      REM benchmark binary (optimized, no editor, no debug)
benchmarks\scripts\build_windows.bat editor                REM editor (asset import, verification)
```

which runs

```
scons platform=windows target=<target> production=yes disable_path_overrides=no winrt=no accesskit=no d3d12=no
```

Outputs land in `bin\` (`godot.windows.template_release.x86_64.exe`, `godot.windows.editor.x86_64.exe` +
`.console.exe` wrappers). The benchmark runner looks for binaries in `.tools\bin\<build>\`:

* `.tools\bin\stock\`  built from the untouched `4.7.2-stable` tag (branch `fellow/baseline`)
* `.tools\bin\fellow\` built from the FellowEngine branch under test

### Building FellowEngine next to the stock build (git worktree)

The benchmark project (with the imported Sponza/Bistro assets and the `.godot` import cache) lives in the main
checkout, so FellowEngine is built in a separate worktree and only the binaries are copied:

```bat
git worktree add .tools\wt-fellow fellow/gpu-fluid
cd .tools\wt-fellow
..\venv\Scripts\python.exe -m SCons platform=windows target=template_release production=yes disable_path_overrides=no winrt=no accesskit=no d3d12=no
..\venv\Scripts\python.exe -m SCons platform=windows target=editor production=yes disable_path_overrides=no winrt=no accesskit=no d3d12=no
copy /y bin\godot.windows.*.exe ..\bin\fellow\
```

(`.tools\fellow_build_job.bat` is the exact script used for the milestone-1 binaries; flags are identical to
`build_windows.bat`, so stock and FellowEngine differ only in source.) FellowEngine binaries report their commit in
`Engine.get_version_info()` (`hash`), which the benchmark stores in every result file.

### Runtime toggles (FellowEngine only)

Defaults (bold = differs from stock behaviour) are the FellowEngine configuration: every experiment whose decision
record says KEEP is on; `rendering/fellow/enabled=false` turns every FellowEngine path off and renders exactly like
stock Godot 4.7.2 (the benchmark's null test).

All FellowEngine code paths are behind project settings under `rendering/fellow/*`
(`servers/rendering/fellow_settings.cpp`), read with `GLOBAL_GET_CACHED` every frame, so they can be changed at
runtime and per benchmark variant (`run_benchmarks.py --builds "label=fellow@key=value;key=value"`):

| Setting | Default | Effect |
|---|---|---|
| `rendering/fellow/enabled` | true | master switch; `false` = every FellowEngine path off (null test) |
| `rendering/fellow/postfx/enabled` | true | master switch for post-processing experiments |
| `rendering/fellow/postfx/taa_resolve_v2` | **true** | TAA resolve v2 (decision record `docs/decisions/0001-taa-resolve-v2.md`) |
| `rendering/fellow/postfx/taa_history_pingpong` | **true** | TAA history ping-pong instead of copies (`docs/decisions/0002-taa-history-pingpong.md`) |
| `rendering/fellow/fluid/enabled` | true | `FellowShallowWater3D` simulation (module `modules/fellow_fluid`) |
| `rendering/fellow/postfx/taa_v2_gamma` | 1.25 | resolve v2 variance-clip width (σ multiplier) |
| `rendering/fellow/postfx/taa_v2_feedback_min` / `_max` | 0.88 / 0.9375 | resolve v2 history weight range |
| `rendering/fellow/postfx/taa_v2_antiflicker` | 0.0 | resolve v2: keep the history weight at its maximum where the history is trusted |
| `rendering/fellow/postfx/taa_v2_velocity_dilation` | true | resolve v2: reproject with the closest pixel's velocity in 3×3 (false: the pixel's own, like stock) |
| `rendering/fellow/postfx/taa_v2_motion_soften` | **0.35** | resolve v2: under motion, pull the history towards the 3×3 mean in high-contrast neighbourhoods (0 = milestone-1 v2) |
| `rendering/fellow/postfx/taa_v2_soften_contrast` | **0.45** | resolve v2: relative contrast (σ/μ) above which the soften applies |
| `rendering/fellow/postfx/ssao_shared_tile` | false | SSAO gather: each workgroup's depths and normals loaded once into shared memory (image-identical; decision 0011: no measurable effect) |
| `rendering/fellow/shadows/enabled` | true | master switch for shadow experiments |
| `rendering/fellow/shadows/directional_cache` | **true** | directional shadow static cache (`docs/decisions/0004-directional-shadow-cache.md`) |
| `rendering/fellow/shadows/directional_cache_guard_texels` | 256 | guard band per side (cache = cascade + 2 × guard texels) |
| `rendering/fellow/shadows/directional_cache_static_frames` | 60 | frames without change before a caster is cached |
| `rendering/fellow/shadows/directional_cache_texel_lod` | 1.0 | cache LODs from the cache's texel size: coarsest LOD with error below this many shadow-map texels (0 = camera-distance LOD) |
| `rendering/fellow/shadows/directional_cache_lod_travel` | 0.0 | with `texel_lod` 0: camera travel (m) a cache stays valid for; cache LODs chosen for a camera this much closer |
| `rendering/fellow/shadows/directional_cache_max_rebuilds_per_frame` | 1 | cache re-renders per frame (others render stock that frame) |
| `rendering/fellow/shadows/directional_cache_min_cost_per_texel` | 0.125 | cost gate: cache a cascade only if its static casters cost at least this many primitive equivalents per cascade texel (0 = always cache) |
| `rendering/fellow/shadows/directional_cache_debug_mode` | 0 | verification: 1 = never use the cache, 2 = rebuild every frame |
| `rendering/fellow/shadows/skip_backfacing_directional` | **true** | no directional shadow lookup where N·L makes the light's contribution zero (image-identical) |
| `rendering/fellow/shadows/skip_backfacing_positional` | **true** | omni/spot: skip the light where N·L makes its contribution zero (image-identical) |
| `rendering/fellow/shadows/skip_unblended_cascade` | **true** | PCF path: second cascade only where its blend weight is non-zero (image-identical) |
| `rendering/fellow/shadows/pcss_blend_band` | false | PCSS path: blend cascades over the last 10 % of a split instead of the whole split (look change) |
| `rendering/fellow/shadows/pcss_directional_penumbra` | false | directional PCSS: parallel-light penumbra formula (look change; `docs/decisions/0005-directional-shadow-sampling.md`) |
| `rendering/fellow/shadows/directional_max_mip` | **true** | directional PCSS/PCF early-outs (fully lit / fully shadowed) from a min/max window pyramid of the atlas (exact; decision 0005, v7) |
| `rendering/fellow/shadows/positional_max_mip` | **true** | the same for omni/spot PCF from a pyramid per positional atlas (exact; decision 0005) |
| `rendering/fellow/shadows/debug_disable_caster_culling` | false | diagnostics: disable the camera-dependent tighter shadow caster culling at runtime |
| `rendering/fellow/geometry/enabled` | true | master switch for geometry experiments |
| `rendering/fellow/geometry/mesh_clusters` | **true** | mesh clusters: frustum culling of k-d clusters of large static surfaces (`docs/decisions/0007-mesh-clusters.md`); read when meshes are created and every frame |
| `rendering/fellow/geometry/cluster_triangles` | 4096 | maximum triangles per cluster |
| `rendering/fellow/geometry/cluster_blended_surfaces` | **true** | surfaces drawn with blending keep their triangle order and are culled with order-preserving clusters (off: drawn whole) |
| `rendering/fellow/geometry/cluster_min_surface_triangles` | 32768 | surfaces with fewer triangles are not clustered |
| `rendering/fellow/lights/enabled` | true | master switch for light-clustering experiments |
| `rendering/fellow/lights/cluster_depth_slices` | **true** | light/decal/probe clusters with logarithmic depth slices and conservative per-element depth ranges (`docs/decisions/0008-light-cluster-depth.md`); read every frame |
| `rendering/fellow/lights/zero_attenuation_skip` | **true** | omni/spot lights with exactly zero attenuation at the pixel return before the attenuation's pow(), shadow and BRDF (exact; decision 0008) |
| `rendering/fellow/shading/blended_alpha_zero_skip` | false | mix-blended fragments whose 2×2 quad has alpha exactly 0 skip lighting, shadows and fog (exact; decision 0010, in test) |
| `rendering/fellow/frame/early_submit` | false | submit the frame's rendering before the swap chain acquisition (decision 0009: rejected, no effect) |
| `rendering/fellow/frame/separate_present_queue` | false | start-up setting (project.godot / override.cfg): present from a second queue (decision 0009: rejected, slower) |
| `rendering/fellow/profiling/pass_timestamps` | false | per-pass GPU timestamps without `--gpu-profile` (performance overlay) |

Build times on the benchmark machine (Core Ultra 9 275HX, 24 threads): template_release ~14 min, editor ~18 min
from scratch; incremental FellowEngine rebuilds take 1-3 min.

## C# (.NET) builds

Not part of milestone 1 benchmarks. C#/mono builds require the .NET 8 SDK (the machine currently only has .NET 6);
FellowEngine does not modify `modules/mono`. See "Verification status" in `docs/fellow_milestone1_report.md`.

## Linux / cross-compilation

Unchanged from upstream Godot (`scons platform=linuxbsd ...`). Not used for published performance results.
During development a Linux `template_release` build was used for functional checks on a CPU Vulkan driver
(Mesa lavapipe under Xvfb): deterministic image comparisons of the TAA variants and the Fluid Lab, and
`glslangValidator` compiles of every shader variant. No performance number in the reports comes from it.
