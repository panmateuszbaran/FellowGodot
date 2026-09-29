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

Build times on the benchmark machine (Core Ultra 9 275HX, 24 threads): template_release ~14 min, editor ~18 min
from scratch; incremental FellowEngine rebuilds take 1-3 min.

## C# (.NET) builds

Not part of milestone 1 benchmarks. C#/mono builds require the .NET 8 SDK (the machine currently only has .NET 6);
FellowEngine does not modify `modules/mono`. See "Verification status" in `docs/fellow_milestone1_report.md`.

## Linux / cross-compilation

Unchanged from upstream Godot (`scons platform=linuxbsd ...`). Not used for published results.
