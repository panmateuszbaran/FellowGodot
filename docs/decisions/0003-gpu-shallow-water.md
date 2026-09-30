# 0003 — GPU shallow-water prototype (`FellowShallowWater3D`)

Status: **KEEP** (opt-in node; prototype quality, see limitations) · Branch `fellow/gpu-fluid` (`1d3f204b54`, fixes `212178afed`) · Module `modules/fellow_fluid`
· Toggle `rendering/fellow/fluid/enabled` · Binary: FellowEngine `7f257b56ff`

## Problem

Stock Godot has no fluid simulation. The FellowEngine target (rivers, floods, harbours, splashes around characters
and boats) needs water that reacts to gameplay without a CPU simulation and without per-frame readbacks, and that
scales from a small pond to a river section. Architecture and tier hierarchy: `docs/fellow_fluid_architecture.md`.

## Evidence (before the change)

Not a performance fix of an existing system: a new feature. The gate here is (a) cost per grid size on the benchmark
GPU, (b) numerical robustness (mass conservation, stability), (c) visual plausibility, (d) no cost when unused.

## Stock implementation

None (the Fluid Lab reports `unsupported` on stock; that is recorded in the benchmark results, not hidden).

## Proposed implementation

2D shallow-water equations on a staggered grid, 3 compute kernels per substep (advect, height with outflow
limiter, velocity with wet/dry, colliders, inflow/outflow, foam) + 1 surface kernel per frame; one-way coupling from
gameplay (up to 16 sphere colliders and 16 impulses per frame through a 768-byte buffer); a displaced grid mesh with
refraction, Beer-Lambert absorption, Fresnel and foam. No GPU → CPU readback. Details and resource sizes in the
architecture document.

Numerical verification (`modules/fellow_fluid/tests/swe_reference.py`, NumPy port of the kernels): mass ratio
1.00000 after 1200 steps of a dam break with obstacles (without the outflow limiter: +17 %).

### Iteration log

* First version gained mass at walls (flux clamping + negative-depth clipping) → wall fluxes zero + outflow limiter.
* Spikes of the water surface at piers (surface lifted to obstacle height; bilinear terrain resampling produced
  intermediate bed heights) → solid cells sink below the neighbouring water level, nearest resampling, Fluid Lab
  terrain generated at the simulation resolution.
* Impulse burst after time jumps in the benchmark (fixed-camera phases) → drop schedule catch-up bounded to 1 s.
* RID leak warnings at exit (compute shaders not freed when the node never became ready) → freed in the destructor.

## Expected benefit

Interactive water at a fixed, predictable GPU cost: target ≤ 1 ms for a 512² tile (64 m at 12.5 cm cells) at
120 Hz simulation on the benchmark GPU.

## Actual performance

Session `fellow-m1_fluid_perf` (FellowEngine `7f257b56ff`, day state, 4-6 interleaved runs per configuration;
`benchmarks/tools/fluid_table.py`). Simulation: 2 substeps per frame (120 Hz), 64 m × 64 m domain, river inflow,
5 piers, a moving sphere collider and a drop every 0.5 s. Simulation and surface times are RenderingDevice
timestamps inside the module (mean over ~10 800 frames per run); "water rendering" = viewport GPU time with the
water drawn − hidden (the simulation runs in both).

| Grid (cell size) | Res | Simulation GPU | Surface GPU | Viewport GPU drawn / hidden | Water rendering | Frame ms | Sim VRAM |
|---|---|---:|---:|---:|---:|---:|---:|
| 256² (25 cm) | 1920x1080 | 0.071 ms | 0.005 ms | 2.53 / 1.67 ms | 0.86 ms | 2.96 | 3.8 MB |
| 256² | 2560x1440 | 0.074 ms | 0.006 ms | 4.14 / 3.05 ms | 1.09 ms | 4.62 | 3.8 MB |
| 512² (12.5 cm) | 1920x1080 | 0.206 ms | 0.014 ms | 2.78 / 1.73 ms | 1.05 ms | 3.35 | 15.0 MB |
| 512² | 2560x1440 | 0.216 ms | 0.015 ms | 4.41 / 3.08 ms | 1.34 ms | 5.05 | 15.0 MB |
| 1024² (6.25 cm) | 1920x1080 | 0.744 ms | 0.048 ms | 3.01 / 1.91 ms | 1.10 ms | 4.17 | 60.0 MB |
| 1024² | 2560x1440 | 0.790 ms | 0.049 ms | 4.58 / 3.01 ms | 1.58 ms | 5.84 | 60.0 MB |

Stock Godot: the scene reports `unsupported` (no GPU fluid module) — recorded in the session, not hidden.

Findings:
* The simulation scales as expected with the cell count (×4 cells → ×2.9-3.6 time; 256² is dispatch-bound) and is far
  below the 1 ms target at 512² (0.21 ms). Per-frame variation is ~2 % (CV of the per-frame sim time).
* **Drawing the water costs more than simulating it** at 256²/512²: 0.9-1.6 ms, mostly the screen-texture copy with
  mip chain that the refraction needs plus the displaced grid mesh (render grid = min(simulation grid, 512²)).
  This is the first optimisation target for the fluid path (reuse the SSR/SSIL last-frame copy, or refract from a
  half-resolution copy; LOD the render grid).
* Frame time minus viewport GPU time grows with the grid (0.4 → 1.2 ms at 1080p), consistent with the simulation
  being submitted outside the viewport's timed region.

## Actual visual quality

Captures of the Fluid Lab's fixed cameras are in the report (`fellow-m1_fluid_capture`). The water reacts plausibly
(wakes and foam behind piers and the moving sphere, drops spreading rings, shoreline fading). Known prototype
limitations: stair-stepped boundaries around obstacles at coarse grids (256² = 25 cm cells), no sub-grid detail waves
(the surface is only as detailed as the grid), screen-space refraction only (nothing outside the screen is refracted),
one-way coupling (no buoyancy yet). Mass conservation is verified by the NumPy reference (`tests/swe_reference.py`),
not in the engine at runtime (no readback by design).

Post-measurement fix: the advection kernel indexed a vector with a runtime channel (`OpVectorExtractDynamic`), which
Godot 4.7's SPIR-V specialization pass (re-spirv) rejects with a console message and skips; fixed in `fe66bee7a9`
(identical arithmetic). The measured binary predates the fix.

## Regression risk

Isolated module; nothing runs unless a `FellowShallowWater3D` node is in the scene. With
`rendering/fellow/fluid/enabled = false` the node dispatches no compute work (the mesh keeps the last surface).

## Decision

**KEEP** as an opt-in node for milestone 1: the simulation meets its budget with a wide margin (0.21 ms at 512²,
0.75-0.79 ms at 1024²), costs nothing when no node is in the scene, and has no readback. Next work on this path:
water rendering cost (refraction copy, render-grid LOD), tile streaming, buoyancy readback (async, 2-3 frames latency).
