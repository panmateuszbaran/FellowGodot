# FellowEngine renderer audit — Godot 4.7.2-stable, Forward+ / Vulkan

Status: milestone 1 (2026-09-30). Base: Godot **4.7.2-stable** (`ed1daf0bf`, tag `4.7.2-stable`, branch
`fellow/baseline`); newest stable 4.x at the time of writing (checked with `git ls-remote --tags`; `4.7` has
cherry-picks for a future 4.7.3 but no stable tag).

This document has three parts:

* **Part A — measured evidence and ranked findings.** What the benchmark suite measured on stock Godot, which findings
  are confirmed by measurement, and which were chosen for milestone 1.
* **Part B — renderer core (code audit).** Architecture, threading, pass order, shadows, clustered lighting, culling,
  instancing, LOD, VRAM, pipeline compilation, timestamps, draw submission.
* **Part C — post-processing and screen-space / volumetric effects (code audit).** HDR pipeline, tonemapping,
  exposure, glow, TAA/FSR2/SMAA/MSAA, SSAO/SSIL, SSR, volumetric fog, frame order, experiment candidates.

Parts B and C were written from the source before any measurement; their classifications are kept as written, and
Part A states where measurements confirmed, changed or could not yet test them.

## Classification legend

| Tag | Meaning |
|---|---|
| **CONFIRMED BOTTLENECK** | measured on the benchmark machine to be a major share of frame time (or the frame limiter) in at least one benchmark scene |
| **LIKELY BOTTLENECK** | code structure makes it expensive in the target workload, not yet isolated by a measurement |
| **VISUAL QUALITY ISSUE** | a defect or limitation visible in images (code-level or captured) |
| **ARCHITECTURAL LIMITATION** | a missing capability that cannot be fixed locally |
| **NOT A PROBLEM** | checked, no action needed |
| **NEEDS BENCHMARK** | plausible, but no measurement yet; not acted on |

## Path abbreviations

Core (Part B): `RFC` = `servers/rendering/renderer_rd/forward_clustered/render_forward_clustered.cpp`, `RSC` =
`servers/rendering/renderer_scene_cull.cpp`, `RD` = `servers/rendering/rendering_device.cpp`, `RDG` =
`servers/rendering/rendering_device_graph.cpp`, `RSRD` = `servers/rendering/renderer_rd/renderer_scene_render_rd.cpp`,
`LS` = `servers/rendering/renderer_rd/storage_rd/light_storage.cpp`, `SSFC` =
`servers/rendering/renderer_rd/forward_clustered/scene_shader_forward_clustered.cpp`.

Post-processing (Part C): `fc.cpp` = `RFC`, `srr.cpp` = `RSRD`, `rsb.cpp/.h` =
`servers/rendering/renderer_rd/storage_rd/render_scene_buffers_rd.*`, `fx/` = `servers/rendering/renderer_rd/effects/`,
`sh/` = `servers/rendering/renderer_rd/shaders/effects/`, `env/` / `shenv/` = `renderer_rd/environment/` and
`renderer_rd/shaders/environment/`, `scene.glsl` = `renderer_rd/shaders/forward_clustered/scene_forward_clustered.glsl`,
`envst.cpp/.h` = `servers/rendering/storage/environment_storage.*`, `Environment.h/.cpp` =
`scene/resources/environment.*`, `rs.cpp` = `servers/rendering/rendering_server.cpp`.

---

# Part A — Measured evidence and ranked findings

## A.1 Method

* Benchmark suite `benchmarks/` (methodology in `benchmarks/README.md`): four scenes (PBR Validation Lab, Intel New
  Sponza with curtains/ivy/trees, Amazon Lumberyard Bistro exterior and interior), deterministic flythroughs at a
  fixed 60 Hz virtual time step, 1920×1080 and 2560×1440 native, VSync off, no FPS cap, ≥ 10 s warm-up incl. every
  fixed camera and one full flythrough, ≥ 30 s of whole flythrough passes, ≥ 3 fresh processes per configuration,
  runs interleaved ABBA, more runs when the coefficient of variation exceeds 3 %.
* Per-pass GPU time from the engine's own `RENDER_TIMESTAMP` markers (`--gpu-profile`), in separate profile runs
  (timestamps are fences in the render graph and serialise it, so profile totals are slightly higher than normal
  runs and are never mixed with them).
* Machine: Acer Predator PHN16S-71 laptop, Intel Core Ultra 9 275HX, **RTX 5070 Ti Laptop GPU (12 GB)**, driver
  617.14 (32.0.16.1714), 64 GB DDR5-6400, Windows 11 26200, Vulkan 1.4.351. Hybrid graphics: the internal panel is
  driven by the iGPU, frames are copied across adapters for presentation (`frame_ms` includes that, `gpu_ms` does not).
  Under sustained load the GPU settles at **~1.0-1.1 GHz, 86-88 °C, 43-50 W** (thermal/power limited; throttle reason
  0x20 = SW thermal slowdown). Every perf session starts with a discarded heat-soak run, and clock/temperature/power
  are logged per run, so all numbers are for this steady state, not for boost clocks.
* Preset `high` (identical for stock and FellowEngine): TAA, 16× anisotropic filtering, soft shadows quality 3,
  SSAO quality 3, SSIL/SSR in the external scenes, volumetric fog in Sponza/Bistro exterior, glow, AgX tonemapping
  with auto exposure in the external scenes (full list in every result file, `effective_settings`).

### Measurement problems found and fixed during the milestone (results discarded, not mixed)

1. **Thermal drift** in the first stock session (frame time drifting 3.45 → 4.33 ms at constant content) → session
   aborted (`raw/_aborted_thermal_drift_*`), heat-soak phase and per-run telemetry added.
2. **Sponza textures were imported uncompressed and without mipmaps**: a headless `--import` never runs the editor's
   "detect 3D" reimport. The first Sponza baseline (6.8 GB VRAM, 23.6 ms GPU at 1080p) therefore measured a texture
   configuration no shipping game uses. `import_assets.py` now applies the editor's detection result (VRAM
   compressed + mipmaps, BC5 normal maps) and Sponza was re-measured (2.5 GB VRAM). The superseded numbers are kept
   in the session files under `superseded`. *This is also a real tooling pitfall for FellowEngine users (headless or
   CI imports) and is listed as a finding below.*
3. **Console windows stealing focus**: when the runner was started without a console, each 1 Hz `nvidia-smi`
   telemetry call opened a console window, producing ~480 ms stalls every ~1.03 s (GPU time unaffected). Those runs
   were discarded; all child processes now start with `CREATE_NO_WINDOW`.
4. **Reference renders**: the first ground-truth setting (16 jitters × 4 frames) took ~1.8 s per sample (~3 h for the
   PBR sequences); references now use 8 jitters with 2×2 SSAA × 8× MSAA each and short settling between sub-pixel
   jitters (fixed PBR cameras keep their 16-jitter references).

## A.2 Where the frame goes (stock Godot 4.7.2, preset `high`)

All four scenes are **GPU-bound** on the benchmark machine at both resolutions: GPU time is within about 0.4 ms of the
frame time (the rest is the cross-adapter present of the hybrid-graphics laptop) and the render-thread CPU time is
0.2-1.6 ms. No pipeline compilation happened during any measured run (warm-up works).

#### Frame summary (godot-4.7.2_perf, build stock)

| Scene | Res | Frame ms | GPU ms | Render-thread CPU ms | p99 ms | 1% low FPS | Avg FPS | Draw calls | Primitives | VRAM MB | Runs | CV |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| PBR Lab | 1920x1080 | 2.96 | 2.53 | 0.23 | 3.75 | 252 | 338 | 235 | 0.43 M | 304 | 3 | 1.2% |
| PBR Lab | 2560x1440 | 4.58 | 4.19 | 0.23 | 5.77 | 166 | 218 | 235 | 0.43 M | 426 | 3 | 0.8% |
| Sponza | 1920x1080 | 13.71 | 13.32 | 0.63 | 20.79 | 48 | 73 | 1104 | 27.02 M | 2533 | 3 | 0.9% |
| Sponza | 2560x1440 | 18.32 | 17.89 | 0.65 | 26.35 | 37 | 55 | 1104 | 29.26 M | 2708 | 3 | 0.1% |
| Bistro ext. | 1920x1080 | 5.96 | 5.58 | 1.43 | 6.85 | 144 | 168 | 2983 | 4.60 M | 2109 | 3 | 1.8% |
| Bistro ext. | 2560x1440 | 9.14 | 8.71 | 1.58 | 10.46 | 94 | 109 | 2983 | 4.83 M | 2285 | 3 | 1.5% |
| Bistro int. | 1920x1080 | 4.53 | 4.16 | 0.59 | 5.20 | 188 | 221 | 619 | 0.19 M | 1272 | 3 | 1.2% |
| Bistro int. | 2560x1440 | 7.38 | 6.97 | 0.66 | 8.25 | 119 | 136 | 619 | 0.21 M | 1448 | 3 | 0.0% |

#### Per-pass GPU time, 1920x1080 (godot-4.7.2_profile)

| Pass (1920x1080, GPU ms and share of frame) | PBR Lab | Sponza | Bistro ext. | Bistro int. |
|---|---:|---:|---:|---:|
| Render Depth Pre-Pass | 0.10 (4%) | 1.70 (13%) | 0.52 (9%) | 0.15 (4%) |
| Render Directional/SpotLight Shadows | 0.12 (4%) | 3.74 (28%) | 0.59 (10%) | – |
| Render OmniLight Shadows | 0.11 (4%) | – | – | – |
| Render Opaque Pass | 0.96 (36%) | 3.58 (27%) | 2.31 (41%) | 2.08 (49%) |
| Render Sky | 0.05 (2%) | 0.01 (0%) | 0.01 (0%) | 0.01 (0%) |
| Render 3D Transparent Pass | 0.13 (5%) | 2.09 (16%) | 0.08 (1%) | 0.27 (7%) |
| Process SSAO | 0.46 (17%) | 0.42 (3%) | 0.50 (9%) | 0.45 (11%) |
| Process SSIL | – | 0.30 (2%) | 0.43 (8%) | 0.28 (7%) |
| Process SSR | – | 0.37 (3%) | 0.27 (5%) | 0.19 (4%) |
| Prepare Depth for SSAO/SSIL | 0.03 (1%) | 0.04 (0%) | 0.03 (1%) | 0.03 (1%) |
| Copy Final Framebuffer (SSIL/SSR) | – | 0.06 (0%) | 0.06 (1%) | 0.06 (1%) |
| Render Fog | – | 0.06 (0%) | 0.08 (1%) | – |
| Integrate Fog | – | 0.02 (0%) | 0.03 (0%) | – |
| TAA | 0.31 (12%) | 0.28 (2%) | 0.28 (5%) | 0.26 (6%) |
| Auto exposure | – | 0.10 (1%) | 0.10 (2%) | 0.10 (2%) |
| Glow | 0.17 (6%) | 0.15 (1%) | 0.16 (3%) | 0.15 (4%) |
| Tonemap | 0.15 (6%) | 0.13 (1%) | 0.14 (2%) | 0.13 (3%) |
| **sum of timestamped passes** | **2.69** | **13.14** | **5.68** | **4.21** |

#### Per-pass GPU time, 2560x1440 (godot-4.7.2_profile)

| Pass (2560x1440, GPU ms and share of frame) | PBR Lab | Sponza | Bistro ext. | Bistro int. |
|---|---:|---:|---:|---:|
| Render Depth Pre-Pass | 0.15 (3%) | 2.18 (12%) | 0.73 (8%) | 0.24 (3%) |
| Render Directional/SpotLight Shadows | 0.13 (3%) | 4.06 (22%) | 0.64 (7%) | – |
| Render OmniLight Shadows | 0.12 (3%) | – | – | – |
| Render Opaque Pass | 1.84 (42%) | 5.00 (27%) | 3.59 (42%) | 3.18 (45%) |
| Render Sky | 0.05 (1%) | 0.01 (0%) | 0.01 (0%) | 0.01 (0%) |
| Render 3D Transparent Pass | 0.21 (5%) | 3.44 (19%) | 0.11 (1%) | 0.42 (6%) |
| Process SSAO | 0.87 (20%) | 0.88 (5%) | 1.07 (12%) | 0.78 (11%) |
| Process SSIL | – | 0.55 (3%) | 0.62 (7%) | 0.54 (8%) |
| Process SSR | – | 0.67 (4%) | 0.33 (4%) | 0.38 (5%) |
| Prepare Depth for SSAO/SSIL | 0.06 (1%) | 0.06 (0%) | 0.06 (1%) | 0.05 (1%) |
| Copy Final Framebuffer (SSIL/SSR) | – | 0.15 (1%) | 0.11 (1%) | 0.10 (1%) |
| Render Fog | – | 0.05 (0%) | 0.06 (1%) | – |
| Integrate Fog | – | 0.02 (0%) | 0.02 (0%) | – |
| TAA | 0.46 (10%) | 0.54 (3%) | 0.50 (6%) | 0.48 (7%) |
| Auto exposure | – | 0.18 (1%) | 0.18 (2%) | 0.17 (2%) |
| Glow | 0.27 (6%) | 0.27 (1%) | 0.28 (3%) | 0.27 (4%) |
| Tonemap | 0.19 (4%) | 0.23 (1%) | 0.24 (3%) | 0.23 (3%) |
| **sum of timestamped passes** | **4.44** | **18.38** | **8.61** | **7.04** |

#### Cost of the TAA path: preset `high` minus `high_noaa` (godot-4.7.2_noaa_profile)

| Scene | Res | GPU ms high (TAA) | GPU ms high_noaa | Δ total | Δ depth pre-pass (motion vectors) | TAA pass | Δ other |
|---|---|---:|---:|---:|---:|---:|---:|
| PBR Lab | 1920x1080 | 2.69 | 2.17 | +0.52 (+24%) | +0.01 | 0.31 | +0.20 |
| PBR Lab | 2560x1440 | 4.44 | 3.50 | +0.94 (+27%) | +0.01 | 0.46 | +0.46 |
| Sponza | 1920x1080 | 13.14 | 11.90 | +1.24 (+10%) | +0.07 | 0.28 | +0.89 |
| Sponza | 2560x1440 | 18.38 | 17.30 | +1.08 (+6%) | +0.06 | 0.54 | +0.48 |
| Bistro ext. | 1920x1080 | 5.68 | 4.79 | +0.89 (+19%) | +0.05 | 0.28 | +0.56 |
| Bistro ext. | 2560x1440 | 8.61 | 8.38 | +0.24 (+3%) | +0.03 | 0.50 | -0.30 |
| Bistro int. | 1920x1080 | 4.21 | 3.56 | +0.65 (+18%) | +0.01 | 0.26 | +0.39 |
| Bistro int. | 2560x1440 | 7.04 | 5.95 | +1.09 (+18%) | +0.01 | 0.48 | +0.60 |

Draw calls and primitives per frame (all passes; profile session): PBR Lab 235 draws (201 of them in shadow passes),
0.4 M primitives; Sponza 1104 draws (861 shadow), 27-29 M primitives; Bistro exterior 2983 draws (2136 shadow),
4.6-4.8 M primitives; Bistro interior 619 draws (0 shadow: the shadowed omni lights are static and Godot's
positional-shadow cache does not re-render them), 0.2 M primitives.

Sponza per fixed camera (1080p): directional shadows cost 2.8-5.8 ms, i.e. **up to 39 % of the frame**
(`sponza_cam_04`: 5.75 of 14.8 ms); the opaque pass 2.8-4.2 ms; the alpha-blended foliage/curtain pass 0.9-2.8 ms.

## A.3 What the measurements confirm

| # | Finding | Evidence | Classification | Part |
|---|---|---|---|---|
| 1 | **Directional shadow rendering is the largest single pass in the geometry-heavy scene.** 4 cascades re-render every caster every frame (no caching, no static/dynamic split, cascade culling is a linear scan). | Sponza: 3.74 ms = 28 % at 1080p (up to 39 % per camera), 4.06 ms = 22 % at 1440p; 78 % of all draw calls are shadow draws. Bistro exterior 10 %. | **CONFIRMED BOTTLENECK** (Sponza); LIKELY for open worlds | B.4 |
| 2 | **Opaque shading dominates everywhere else** (clustered Forward+ with SSAO/SSIL/SSR inputs, separate specular). | 36-49 % of the frame in PBR Lab, Bistro exterior and interior at both resolutions. | **CONFIRMED BOTTLENECK** (largest cost), no single fix — per-material and per-light work | B.1, B.5 |
| 3 | **Alpha-blended foliage** in Sponza goes through the transparent pass: the cypress leaves (`LeafSpring`) and the floor `dirt_decal` are the only glTF `alphaMode: BLEND` materials (all others OPAQUE). Blended geometry is sorted, skips the depth pre-pass and SSAO, and writes no TAA motion vectors. | Transparent pass 2.09 ms = 16 % (1080p), 3.44 ms = 19 % (1440p); 0.9-2.8 ms per camera depending on how much tree is in view. | **CONFIRMED BOTTLENECK** (content + pipeline); VISUAL QUALITY ISSUE (no MVs → ghosting) | C.5.2 |
| 4 | **SSAO at quality 3 is expensive** relative to what it does (no temporal accumulation). | 0.42-0.50 ms (1080p) / 0.78-1.07 ms (1440p): 9-20 % of the frame; SSIL another 7-8 %. | **CONFIRMED** significant cost; candidate for a temporal GTAO-style rework | C.6 |
| 5 | **The TAA path costs more than the resolve**: with TAA on, every opaque vertex is transformed twice and a velocity target is written in the opaque pass. | Preset `high` − `high_noaa` (same scenes, profile runs): +0.24 to +1.24 ms per frame (3-27 %); the resolve is 0.26-0.54 ms of that, the opaque pass +0.1 to +0.6 ms, the depth pre-pass only +0.01 to +0.07 ms (single profile run per configuration, ±0.2 ms noise on large passes). | **CONFIRMED** cost; camera-only motion vectors for static geometry (C.12 candidate 9) is the follow-up | B.3, C.5.2 |
| 6 | **Post chain has avoidable full-screen copies.** TAA resolve + 3 copies, glow, tonemap, auto-exposure. | TAA span 0.26-0.31 ms (1080p) / 0.46-0.54 ms (1440p) = 3-12 % of the frame; glow + tonemap + auto exposure another 0.4-0.7 ms. | **CONFIRMED** measurable; addressed by experiment 0002 | C.5.3 |
| 7 | **TAA image defects** (farthest-depth dilation under reverse-Z, no dilated reprojection, clip-box collapse under motion, clip in linear HDR). | Code (C.5.3) + temporal sequences vs ground truth (A.4). | VISUAL QUALITY ISSUE; addressed by experiment 0001 | C.5.3 |
| 8 | **Headless/CI imports leave glTF textures uncompressed and without mipmaps** (the editor's "detect 3D" reimport never runs). | First Sponza import: 6.8 GB VRAM and 23.6 ms GPU at 1080p (thermally limited state); after applying the editor's settings: 2.5 GB. | **CONFIRMED** tooling pitfall (VRAM and bandwidth); documented, worked around in `import_assets.py` | B.9 |
| 9 | **Positional (omni/spot) shadow caching works** for static lights. | Bistro interior: 0 shadow draw calls per frame with shadowed static omni lights. | NOT A PROBLEM (for static scenes; wind-animated casters still invalidate it, B.4) | B.4 |
| 10 | **Per-draw CPU cost is not the limiter on this machine** at these draw counts. | Render-thread CPU 1.4-1.6 ms for ~3000 draws/frame (Bistro exterior) vs 5.6-8.7 ms GPU. | NEEDS BENCHMARK at open-world draw counts (10k+) — not reached by these scenes | B.12 |
| 11 | **Pipeline compilation stutter** during normal play. | 0 compilations in every measured run after the warm-up (cameras + full path). | NOT A PROBLEM for pre-warmed content; first-use stutter remains untested (B.10) | B.10 |
| 12 | **Thermal/power state dominates absolute numbers on laptops.** | Same content: PBR Lab 5.05 ms at 1.0 GHz / 45 W (night) vs 2.96 ms at 1.75 GHz / 75 W (day). Even within the day state, stock differs between sessions by up to 6 % (Bistro exterior 1080p 5.96 vs 5.58 ms) while run-to-run CV inside a session is ≤ 3 %. | Methodology finding: only interleaved A/B within one session is comparable | A.1 |

Items of Parts B/C that are still **NEEDS BENCHMARK** (not exercised by these scenes): 10k+ draw calls, open-world
far planes (clustered-light depth slicing), reflection-probe update spikes, texture streaming, first-use shader
compilation, wind-animated shadow casters, MultiMesh vegetation at scale, skinned crowds.

## A.4 Why the first experiment is the TAA pass (and what was measured)

The biggest measured costs (directional shadows in Sponza, opaque shading everywhere) are architectural: fixing
them properly means shadow caching / cascade scrolling, or GPU-driven culling and submission — the GPU-scene work
that the task schedules *after* milestone 1. The post-processing stage was chosen for the first isolated
experiment because it has **both** a measured cost (3-12 % of the frame, larger share in lighter scenes and at
higher resolution) **and** concrete image defects in the code, it is fully contained in two files, and it can be
switched at runtime for a clean A/B:

* **0001 TAA resolve v2** — image quality at (ideally) equal cost.
* **0002 TAA history ping-pong** — two fewer full-screen copies at identical output.

**Performance (per-pass GPU timestamps, `fellow-m1_profile`):**

| Scene | Res | stock TAA span | ping-pong (stock resolve) | Δ | v2 + ping-pong | Δ vs stock | v2 resolve vs stock resolve |
|---|---|---:|---:|---:|---:|---:|---:|
| PBR Lab | 1920x1080 | 0.317 ms | 0.260 ms | -0.056 (-18 %) | 0.304 ms | -0.013 (-4 %) | +0.043 ms |
| PBR Lab | 2560x1440 | 0.454 ms | 0.373 ms | -0.081 (-18 %) | 0.438 ms | -0.015 (-3 %) | +0.065 ms |
| Sponza | 1920x1080 | 0.272 ms | 0.235 ms | -0.037 (-14 %) | 0.252 ms | -0.020 (-7 %) | +0.018 ms |
| Sponza | 2560x1440 | 0.537 ms | 0.419 ms | -0.118 (-22 %) | 0.454 ms | -0.083 (-15 %) | +0.035 ms |
| Bistro ext. | 1920x1080 | 0.260 ms | 0.215 ms | -0.045 (-17 %) | 0.246 ms | -0.013 (-5 %) | +0.032 ms |
| Bistro ext. | 2560x1440 | 0.481 ms | 0.380 ms | -0.101 (-21 %) | 0.457 ms | -0.024 (-5 %) | +0.077 ms |
| Bistro int. | 1920x1080 | 0.250 ms | 0.204 ms | -0.046 (-19 %) | 0.241 ms | -0.009 (-4 %) | +0.037 ms |
| Bistro int. | 2560x1440 | 0.464 ms | 0.380 ms | -0.084 (-18 %) | 0.432 ms | -0.032 (-7 %) | +0.052 ms |

**Whole frame (`fellow-m1_perf`, 3-4 interleaved runs):** FellowEngine with all toggles off (null test) is within
noise of stock everywhere (largest Δ −1.9 %, p = 0.28). Ping-pong: faster in 7/8 configurations by 0.03-0.37 ms,
individually significant only where run-to-run spread is tiny. v2 + ping-pong: within noise in 8/8 (−3.7 % … +2.3 %).

**Image quality vs. the accumulated ground truth:** v2 + ping-pong is closer to the ground truth in 4/4 scenes on
fixed cameras (LPIPS −19 to −25 %, SSIM +0.0015 to +0.003, PSNR +0.2 to +0.4 dB) and in 7 of 8 60-frame motion
sequences (camera pan: motion-region error −18 %, flicker −19 %, PSNR +1.8 dB; Sponza/Bistro flythroughs +1.1 to
+1.5 dB). Regression: `foliage_strafe` (alpha-tested grass + chain-link fence, lateral motion) +6 % motion error,
+5 % flicker, −0.4 dB — stock's collapse-to-mean blur hides sub-pixel aliasing that v2 keeps.

Decisions: [0001 TAA resolve v2](decisions/0001-taa-resolve-v2.md) **KEEP** (documented foliage weakness),
[0002 TAA history ping-pong](decisions/0002-taa-history-pingpong.md) **KEEP**,
[0003 GPU shallow water](decisions/0003-gpu-shallow-water.md) **KEEP** (opt-in node).

## A.5 Next subsystem (recommendation)

1. **Directional shadows** (B.4) — the largest measured single pass in the geometry-heavy scene (28 % of Sponza at
   1080p, up to 39 % per camera; 78 % of all draw calls are shadow draws). Candidates, each behind a toggle and measured
   the same way: static/dynamic caster split with cached far cascades, cascade scrolling (re-render only the newly
   exposed slice), and GPU culling of shadow casters per cascade. This is where GPU-scene work (Phase 4) should start,
   because GPU-driven culling pays off in the shadow passes first.
2. **SSAO/SSIL** (C.6) — 9-20 % of the frame at quality 3: a temporally accumulated GTAO-style AO at half resolution
   is the cheapest large win in the post chain.
3. **TAA follow-ups** — foliage shimmer of v2 (reactive/alpha mask or local-contrast γ), camera-only motion vectors for
   static geometry (item 5 above: up to 0.6 ms in the opaque pass).
4. **Fluid rendering** — the refraction screen copy costs more than the 512² simulation (decision 0003).

---

# Part B — Renderer core (code audit, Forward+)

Written from the 4.7.2 source before measuring. Classifications are the pre-measurement ones; Part A.3 lists which of them the measurements confirmed or changed.

### B.0 Key default values (project settings and node defaults)

| Setting | Default | Where defined |
|---|---|---|
| `rendering/driver/threads/thread_model` | `1` = Safe (enum `Unsafe(deprecated)=0, Safe=1, Separate=2`) | `main/main.cpp:2776`, enum `core/os/os.h:52-55`, hint `core/config/project_settings.cpp:1775-1777` |
| `rendering/rendering_device/vsync/frame_queue_size` | 2 (range 2–3) | `core/config/project_settings.cpp:1823` |
| `rendering/rendering_device/vsync/swapchain_image_count` | 3 | `core/config/project_settings.cpp:1824` |
| `rendering/rendering_device/staging_buffer/block_size_kb` / `max_size_mb` | 256 KB / 128 MB | `core/config/project_settings.cpp:1825-1826` |
| `rendering/rendering_device/pipeline_cache/enable` / `save_chunk_size_mb` | true / 3.0 | `core/config/project_settings.cpp:1829-1830` |
| `debug/settings/profiler/max_timestamp_query_elements` | 256 | `core/config/project_settings.cpp:1793` |
| `rendering/driver/depth_prepass/enable` | true | `servers/rendering/rendering_server.cpp:3715` (`disable_for_vendors` at :3716 is only read by GLES3, `drivers/gles3/storage/config.cpp:204`) |
| `rendering/lights_and_shadows/directional_shadow/size` | 4096 (mobile 2048) | `rendering_server.cpp:3673-3674` |
| `.../directional_shadow/soft_shadow_filter_quality` | 2 = Soft Low | `rendering_server.cpp:3675` |
| `.../directional_shadow/16_bits` | true | `rendering_server.cpp:3677` |
| `.../positional_shadow/soft_shadow_filter_quality` | 2 = Soft Low | `rendering_server.cpp:3679` |
| `.../positional_shadow/atlas_16_bits` | true | `rendering_server.cpp:3681` |
| `.../positional_shadow/atlas_size` | 4096 (mobile 2048) | `scene/main/scene_tree.cpp:2161-2162` |
| `.../positional_shadow/atlas_quadrant_{0,1,2,3}_subdiv` | 2,2,3,4 → 4, 4, 16, 64 shadows | `scene/main/scene_tree.cpp:2164-2167` |
| `rendering/lights_and_shadows/tighter_shadow_caster_culling` | true | `RSC:4524` |
| `rendering/limits/cluster_builder/max_clustered_elements` | 512 per element type | `rendering_server.cpp:3805` |
| `rendering/limits/spatial_indexer/threaded_cull_minimum_instances` | 1000 | `rendering_server.cpp:3803` (applied at `RSC:4516-4517`) |
| `rendering/limits/spatial_indexer/update_iterations_per_frame` | 10 | `rendering_server.cpp:3802` |
| `rendering/occlusion_culling/use_occlusion_culling` | false | `scene/main/scene_tree.cpp:2134` |
| `rendering/occlusion_culling/occlusion_rays_per_thread` | 512 | `rendering_server.cpp:3776` |
| `rendering/occlusion_culling/bvh_build_quality` / `jitter_projection` | 2 (High) / true | `core/config/project_settings.cpp:1805-1806` |
| `rendering/mesh_lod/lod_change/threshold_pixels` | 1.0 | `scene/main/scene_tree.cpp:2137` |
| `rendering/shader_compiler/shader_cache/enabled` | true (compress, zstd, strip_debug.release=true) | `rendering_server.cpp:3690-3694` |
| `rendering/reflections/sky_reflections/roughness_layers` / `ggx_samples` | 8 / 32 | `rendering_server.cpp:3696,3699` |
| `rendering/reflections/reflection_atlas/reflection_size` / `reflection_count` | 256 / 64 | `rendering_server.cpp:3702,3704` |
| `rendering/anti_aliasing/quality/msaa_3d` | 0 (disabled) | `rendering_server.cpp:3743` |
| `rendering/anti_aliasing/quality/use_taa` / `screen_space_aa` | false / 0 | `scene/main/scene_tree.cpp:2125-2128` |
| `rendering/scaling_3d/scale` | 1.0 | `rendering_server.cpp:3769` |
| `rendering/environment/ssao/quality`, `half_size` | 2 (Medium), true | `rendering_server.cpp:3725-3726` |
| `rendering/environment/volumetric_fog/volume_size` / `volume_depth` | 64 / 64 | `rendering_server.cpp:3798-3799` |
| `rendering/textures/default_filters/anisotropic_filtering_level` | 2 (4×) | `rendering_server.cpp:3719` |
| DirectionalLight3D shadow | PSSM 4 splits, `max_distance`=100 m, splits 0.1/0.2/0.5, `blend_splits`=false, pancake 20, bias 0.1, normal bias 2.0, fade start 0.8 | `scene/3d/light_3d.cpp:498-507, 612-623` |
| OmniLight3D shadow mode | CUBE | `scene/3d/light_3d.cpp:661-664` |
| GeometryInstance3D | `visibility_range_fade_mode`=DISABLED, `lod_bias`=1.0, `ignore_occlusion_culling`=false | `scene/3d/visual_instance_3d.h:132-145` |
| Environment (`scene/resources/environment.h:124-209`) | SSR/SSAO/SSIL/SDFGI/glow/fog/volumetric fog all **off**. `environment.cpp` defines no `GLOBAL_DEF`. | — |

---

### B.1 Overall renderer architecture

**Call chain (verified):**
- `RenderingServerDefault::_draw()` (`servers/rendering/rendering_server_default.cpp:75-120`) runs `RSG::scene->update()`, `canvas->update()`, `particles_storage->update_particles()`, `scene->render_probes()`, `viewport->draw_viewports()`, `canvas_render->update()` and `rasterizer->end_frame()`.
- `RendererViewport::draw_viewports` (`servers/rendering/renderer_viewport.cpp:782`) → `_draw_3d` (`:304-334`) → `RSG::scene->render_camera(...)` (`:332`).
- `RendererSceneCull` implements `RenderingMethod` (`RSC.h:52`) and owns `RendererSceneRender *scene_render` (`RSC.h:54`). `render_camera` (`RSC:2670`) updates the occlusion buffer (`RSC:2779-2781`), then calls `_render_scene` (`RSC:3302`). That runs the CPU culling and shadow setup, then calls `scene_render->render_scene(...)` (`RSC:3706`).
- `RendererSceneRenderRD::render_scene` (`RSRD:1359`) fills `RenderDataRD`/`RenderSceneDataRD`, then calls the virtual `_render_scene` (`RSRD:1504`).
- `RenderForwardClustered : RendererSceneRenderRD` (`RFC.h:60`) is created by `RendererCompositorRD` (`servers/rendering/renderer_rd/renderer_compositor_rd.cpp:382,386`). `RenderForwardClustered::_render_scene` is at `RFC:1704`.

**RenderingDevice (RD) abstraction:**
- `RenderingDevice` (`servers/rendering/rendering_device.h:67`) exposes an API-agnostic, RID-based interface: resources, `draw_list_*`, `compute_list_*`, uniform sets and pipelines.
- It sits on top of `RenderingDeviceDriver` backends in `drivers/vulkan/rendering_device_driver_vulkan.cpp`, `drivers/d3d12/rendering_device_driver_d3d12.cpp` and `drivers/metal/rendering_device_driver_metal*.cpp`.
- No RD command goes to the API directly. Each one is appended to `draw_graph` (a `RenderingDeviceGraph`) and replayed at frame end: `RD::_end_frame` → `draw_graph.end(RENDER_GRAPH_REORDER == 1, ...)` (`RD:8123`).
- Uploads: `buffer_update`/`texture_update` go through ring staging buffers (`RD:930-1000`). Initial texture data created from loader threads uses "transfer workers" that submit on a transfer queue (`RD:7203, 7279`). There is no async-compute queue: graphics and compute all execute on `main_queue` (`RD:8175`).

**RenderingDeviceGraph (how it works, `RDG`):**
- **Recording.** Every command (draw list, compute list, copy, clear, timestamp) is serialized into `command_data`. Each resource carries a `ResourceTracker` that records its last usage, last writer and reader lists.
  - `_add_command_to_graph` (`RDG:366`) adds adjacency edges for RAW, WAW and WAR hazards (`RDG:564-700`), including sub-rect/slice tracking for texture slices and partial render regions.
  - It emits texture layout-transition barriers and buffer memory barriers. Buffer barriers are compiled in: `USE_BUFFER_BARRIERS 1`, `servers/rendering/rendering_device_graph.h:47`.
- **Draw lists.** Draw-list instructions (bind pipeline, bind sets, push constants, draws) accumulate in `draw_instruction_list.data`. At `add_draw_list_end` the whole block is `memcpy`'d into the command buffer (`RDG:2384`). Resource usages of every bound uniform set are deduplicated per draw list (`RDG:2300-2322`).
- **Reordering (`end()`, `RDG:2583-2760`).**
  - Kahn topological sort (`RDG:2596-2641`).
  - Each command's *level* is set to its longest dependency path (`RDG:2667-2680`).
  - Within a level, commands are sorted by a type priority table: buffer ops, then texture ops/timestamps, then draw lists, then compute (`RDG:2644-2663`), with `_boost_priority_for_render_commands` (`RDG:1329`).
  - Barriers are grouped once per level (`_group_barriers_for_render_commands`, `RDG:2740`). Independent passes at the same level (for example non-overlapping shadow-atlas regions and GI compute) are recorded back-to-back without barriers between them.
- **Recording threads.** Secondary-command-buffer recording on worker threads exists (`RDG:1060-1072`) but is **disabled**: `SECONDARY_COMMAND_BUFFERS_PER_FRAME 0` (`RD:154`). The comment there cites "strange issues with certain IHVs".
- **Sync points.** Timestamps and "synchronization" points are full fences in the graph. A timestamp depends on every command since the previous timestamp, and every later command depends on it (`RDG:376-387`). `add_synchronization` behaves the same way (`RDG:389-401`) and is triggered when a resource first becomes mutable (for example `RD:1079, 2385, 2641`).

Findings:
- **Double recording.** The RD graph records every command twice: once into the intermediate buffer (plus the draw-list memcpy), then a replay into the native command buffer. It also runs a sort over all commands every frame. All of this happens on the render thread inside `end_frame`. The code comment admits the "extra CPU overhead" (`RD:134-139`). **Classification:** NEEDS BENCHMARK
- **Single-threaded recording.** Command-buffer recording uses one thread: no parallel draw-list recording, and secondary command buffers are compiled out (`RD:154`). **Classification:** ARCHITECTURAL LIMITATION
- **No async compute.** The "(Parallel)" GI+shadow and GI+prepass overlaps (`RFC:1583, 2123`) are only barrier-free overlap on one queue. **Classification:** ARCHITECTURAL LIMITATION
- **Automatic barriers.** Barrier and transition generation is automatic and correct by construction, which removes a class of bugs from fork work. **Classification:** NOT A PROBLEM

---

### B.2 Render-thread / threading model

**Thread model:**
- The default is **Safe**. `main/main.cpp:2776` sets `separate_thread_render` only when the setting equals `RENDER_SEPARATE_THREAD`.
- In Safe mode `server_thread = Thread::MAIN_ID` and `draw()` calls `_draw()` synchronously on the main thread (`rendering_server_default.cpp:285-288, 443-452`).
- The whole renderer CPU cost (culling, list build, recording, graph replay, submit) therefore runs serially after game logic in `Main::iteration`: `RenderingServer::sync()` at `main/main.cpp:5077`, then `draw()` at `:5089/5093`.
- Separate mode:
  - It runs `_thread_loop` as a high-priority WorkerThreadPool pump task (`rendering_server_default.cpp:277-289, 415-423`).
  - `draw()` then only enqueues `_draw` (`:448-449`), and the main thread blocks in `sync()` on the previous frame.
  - The editor forces Safe (`main/main.cpp:2779-2782`).
  - The engine prints "The separate rendering thread feature is experimental" (`main/main.cpp:3517-3521`).

**Frames in flight:**
- `frame_count = MAX(2, frame_queue_size)` (`RD:8366-8369`), default 2, with 3 swapchain images.
- Timestamp and query results are read back when a frame slot is reused (`RD:8090-8098`), so profiling data lags about 2 frames.

**WorkerThreadPool usage (render thread waits for all of these):**
- Main culling: `_scene_cull_threaded` group task when `instance_data.size() > thread_cull_threshold` (`RSC:3438-3450`). The threshold comes from `threaded_cull_minimum_instances` (default 1000, clamped to at least the thread count, `RSC:4516-4517`). The member default of 200 (`RSC.h:1015`) is overwritten. Each thread scans a contiguous slice of the flat instance array (`RSC:2883-2890`), and results are merged serially with `append_from` (`RSC:3447-3449`).
- Visibility-range dependency pass: `_visibility_cull_threaded` (`RSC:3337-3340`).
- Occlusion culling: camera ray generation and Embree `rtcIntersect16` over tiles (`modules/raycast/raycast_occlusion_cull.cpp:105, 480-506`).
- Pipeline compilation (`servers/rendering/renderer_rd/pipeline_hash_map_rd.h:110-150`) and SPIR-V shader compilation (`servers/rendering/renderer_rd/shader_rd.cpp:716-737`).
- **Not threaded:**
  - render-list fill, sort, instance-data upload and draw recording (`RFC` contains zero `WorkerThreadPool` references);
  - light and decal buffer builds (`LS`);
  - MultiMesh uploads (`servers/rendering/renderer_rd/storage_rd/mesh_storage.cpp`).

Findings:
- **Safe mode serializes CPU work.** With the default Safe model, gameplay CPU time and renderer CPU time add up serially on the main thread. Separate mode helps but is flagged experimental. **Classification:** LIKELY BOTTLENECK
- **Per-draw work is single-threaded.** All per-draw CPU work after culling (list build, sort, record, graph replay) runs on one thread. **Classification:** LIKELY BOTTLENECK
- **Culling is parallel.** Frustum culling is parallelized once instance count exceeds 1000. **Classification:** NOT A PROBLEM

---

### B.3 Frame pass ordering (`RenderForwardClustered::_render_scene`, `RFC:1704-2569`) and RENDER_TIMESTAMP markers

The ordering below follows the CPU recording order. The RD graph can move work between two consecutive timestamps (see §1, §11).

**Before `_render_scene`:**
1. `"Prepare Render Frame"` (`rendering_server_default.cpp:84`).
2. `"Update GPUParticles"` (`servers/rendering/renderer_rd/storage_rd/particles_storage.cpp:1447`).
3. `"Render ReflectionProbes"` / `"Render ReflectionProbe, Face N"` / `"Post-Process ReflectionProbe, Step N"` (`RSC:3862, 3839, 3846`).
4. `"> Render Viewports"`, `"> Render Viewport N"`, `"> Render 3D Scene"` (`servers/rendering/renderer_viewport.cpp:808, 872, 307`).
5. `"Update Occlusion Buffer"` (`RSC:2779`, synchronous on the render thread).
6. `"Update Visibility Dependencies"` (`RSC:3317`).
7. `"Cull 3D Scene"` (`RSC:3346`).
8. `"Cull DirectionalLight3D, Split N"` (`RSC:2199`). This covers only the cascade frustum setup; the per-instance cascade tests happen inside the "Cull 3D Scene" loop.
9. Per positional light that needs a redraw: `"> Render Light3D i"` / `"Cull OmniLight3D Shadow Cube, Side N"` | `"... Paraboloid, Half N"` | `"Cull SpotLight3D Shadow"` | `"Cull AreaLight3D Shadow Paraboloid"` / `"< Render Light3D i"` (`RSC:3634-3638, 2387-2603`).
10. `"Render 3D Scene"` (`RSC:3705`).

**Inside `_render_scene` (in order):**
1. `"Prepare 3D Scene"` (`RFC:1726`). If SDFGI is active, `_update_sdfgi` first emits `"Render SDFGI"` (`RFC:1266`, called at `RFC:1734`). Cluster builder selection, SDFGI cascade and light update, VoxelGI setup.
2. `"Setup 3D Scene"` (`RFC:1778`). Label "Render Setup": lightmaps/VoxelGI, UBO, `_fill_render_list(RENDER_LIST_OPAQUE)` (`RFC:1913`), sorts (`RFC:1914-1916`), `_fill_instance_data` ×3 (`RFC:1919-1921`), `_update_dirty_geometry_pipelines()` (`RFC:2004`).
3. `"Setup Sky"` (`RFC:2071`): `sky.setup_sky`, radiance and half/quarter-res updates. Sky emits `"Setup Sky Resolution Buffers"` (`servers/rendering/renderer_rd/environment/sky.cpp:1417`).
4. `"GI + Render Depth Pre-Pass (Parallel)"` (`RFC:2123`, when SDFGI is present) or `"Render Depth Pre-Pass"` (`RFC:2125`). Depth prepass over the OPAQUE list (`RFC:2138-2141`). It becomes a depth+normal/roughness(+VoxelGI) MRT pass when SSAO/SSR/SSIL/SDFGI/VoxelGI are used (`RFC:1924-1940`). The SDFGI probe update runs here (`RFC:2132`).
5. `"Resolve Depth Pre-Pass (MSAA)"` (`RFC:2146`).
6. `"Process Pre Opaque Compositor Effects"` (`RFC:2172`).
7. `_pre_opaque_render` (`RFC:1517`):
   - `"Setup Shadows"` (`RFC:1530`).
   - `"Render OmniLight Shadows"` (`RFC:1562`). Cube shadows, each with its own begin/process/end.
   - The directional atlas is fully cleared (`RFC:1570-1574`).
   - `"Render GI + Render Directional/SpotLight Shadows (Parallel)"` / `"Render Directional/SpotLight Shadows"` / `"Render GI"` (`RFC:1583-1587`). All directional cascades, spot, dual-paraboloid omni and area shadows, plus `gi.process_gi` (`RFC:1607`).
   - `"Prepare Depth for SSAO/SSIL"` (`RFC:1624`), `"Process SSAO"` (`RFC:1430`), `"Process SSIL"` (`RFC:1456`), `"Process SSR"` (`RFC:1490`).
   - `"Pre Opaque Render"` (`RFC:1644`): reflection-probe, light and decal buffer builds (`RFC:1663-1669`) and cluster bake (`RFC:1674`) with `"> Bake 3D Cluster"`, `"Render 3D Cluster Elements"`, `"Pack 3D Cluster Elements"`, `"< Bake 3D Cluster"` (`servers/rendering/renderer_rd/cluster_builder_rd.cpp:439, 470, 519, 544`).
   - `"Update Volumetric Fog"` (`RFC:1678`) → `"> Volumetric Fog"`, `"Render FogVolumes"`, `"Render Fog"`, `"Filter Fog"`, `"Integrate Fog"`, `"< Volumetric Fog"` (`servers/rendering/renderer_rd/environment/fog.cpp:572, 580, 1171, 1198, 1219, 1228`).
8. `"Render Opaque Pass"` (`RFC:2188`).
9. `"Render Motion Pass"` (`RFC:2245`, only with FSR2/MetalFX, `using_motion_pass` `RFC:1840`).
10. `"Process Post Opaque Compositor Effects"` (`RFC:2269`).
11. `"Render Sky"` (`RFC:2297`). Sky is drawn after opaque.
12. `"Resolve MSAA"` (`RFC:2309`), `"Process Post Sky Compositor Effects"` (`RFC:2330`).
13. `"Sub-Surface Scattering"` (`RFC:2337`), `"Merge Specular"` (`RFC:2345`), `"Clear Separate Specular (Canvas Background Mode)"` (`RFC:2353`).
14. `"Copy Screen Texture"` (`RFC:2367`), `"Copy Depth Texture"` (`RFC:2378`).
15. `"Process Pre Transparent Compositor Effects"` (`RFC:2407`).
16. `"Render 3D Transparent Pass"` (`RFC:2411`).
17. `"Resolve"` (`RFC:2431`), `"Copy Final Framebuffer (SSIL/SSR)"` (`RFC:2451`).
18. `"Process Post Transparent Compositor Effects"` (`RFC:2457`).
19. `"FSR2"` (`RFC:2471`) or `"TAA"` (`RFC:2539`).
20. `"Tonemap"` (`RFC:2548`), then inside `_render_buffers_post_process_and_tonemap` (`RSRD:455`): `"Depth of Field"` (`RSRD:504`), `"Auto exposure"` (`RSRD:557`), `"Glow"` (`RSRD:581`), `"Tonemap"` (`RSRD:676`, the name is duplicated), `"SMAA"` (`RSRD:820`).

**After `_render_scene`:** `"< Render 3D Scene"`, `"< Render Viewport N"`, `"< Render Viewports"` (`servers/rendering/renderer_viewport.cpp:334, 959, 977`).

**Other markers:**
- `"Setup GPUParticlesCollisionHeightField3D"`, `"Render Collider Heightfield"`, `"Setup Rendering 3D Material"`, `"Render 3D Material"` (×2), `"Setup Rendering UV2"`, `"Render SDFGI"` (`RFC:2910-3100`).
- SDFGI: `"Average SDFGI Probes"`, `"> SDFGI Update SDF"`, `"SDFGI Scroll SDF"`, `"SDFGI Jump Flood (Half-Size)"`, `"SDFGI Jump Flood Optimized (Half-Size)"`, `"SDFGI Jump Flood (Full-Size)"`, `"SDFGI Jump Flood Optimized (Full-Size)"`, `"SDFGI Occlusion"`, `"SDFGI Store"`, `"< SDFGI Update SDF"` (`servers/rendering/renderer_rd/environment/gi.cpp:1436-2417`).
- `"Render VoxelGI"` (`RSC:3909`), `"Render Empty 3D Scene"` (`RSC:3768`).

Findings:
- **SSR and SSIL lag one frame.** Both run before the opaque pass (`RFC:1613-1640`) and reproject the previous frame's color. That color is captured by `"Copy Final Framebuffer (SSIL/SSR)"` after transparents (`RFC:2452`; reprojection `RFC:1495-1503`). Expect 1-frame lag and disocclusion artifacts under fast camera or time-of-day changes. **Classification:** VISUAL QUALITY ISSUE
- **Shadow maps render after the depth prepass.** The depth prepass does not wait on them, and the graph may overlap them. **Classification:** NOT A PROBLEM
- **Dynamic time of day and the sky.**
  - With a sky shader that reads `LIGHT*` or sun scatter, AUTOMATIC mode selects INCREMENTAL (`servers/rendering/renderer_rd/environment/sky.cpp:1003-1012`).
  - Any sun direction, energy or color change marks the radiance dirty (`sky.cpp:1107-1141`).
  - An INCREMENTAL rebuild restarts only after the previous cycle finishes (`sky.cpp:1264-1275, 1360-1367`), so a continuously moving sun keeps the radiance octmap refiltering permanently. Reflections lag by roughly `roughness_layers` (8) frames.
  - **Classification:** NEEDS BENCHMARK
- **Reflection-probe updates spike.** A probe update renders **6 full `_render_scene` calls in one frame**, each with its own culling and shadows (`RSC:3822-3840`). UPDATE_ALWAYS probes do this every frame (`RSC:3883-3891`). Time-of-day-driven probe refreshes will spike. **Classification:** LIKELY BOTTLENECK

---

### B.4 Shadow system

**Directional (PSSM):**
- **Split distances.** Splits are linear fractions of `min(shadow_max_distance, z_far)`: `distances[i+1] = min + split_offset_i * range` (`RSC:2155-2186`). With defaults this gives 10 / 20 / 50 / 100 m (`scene/3d/light_3d.cpp:499-501, 614`).
- **Blend splits** (`RSC:2191`) extend each split's frustum back to the previous split start (`RSC:2211-2215`). The shader then samples two cascades in the blend region (`servers/rendering/renderer_rd/shaders/forward_clustered/scene_forward_clustered.glsl:2358, 2489-2523`).
- **Atlas layout.**
  - One atlas of `directional_shadow/size` (4096) is shared by all shadowed directional lights. It is split into equal rects per light (`LS:2789-2813`).
  - 4-split mode quarters the rect, so each cascade is 2048² with one light (`RFC:2647-2664`, `LS:2815-2835`).
  - Every cascade has the same resolution.
- **Fitting.** Each cascade uses a bounding-sphere fit with texel snapping, which stabilizes the shadow "at the cost of some wasted resolution" (`RSC:2279-2319`). The snap unit is `(radius+expand)*4/texture_size` (`RSC:2315`).
- **Light frustum near plane** is `z_max + 1e6` (`RSC:2334`), so all casters up-sun are included.
- **Caster submission:**
  - Cascade culling happens **inside the main linear `_scene_cull` loop**. For every instance, every cascade: optional tighter light-culler test (`RSC:3242`), `IN_FRUSTUM(cascade frustum)`, visibility-range check, layer mask, cast-shadows flag. Survivors go into `cascade_geometry_instances[k]` (`RSC:3240-3253`).
  - Occlusion culling is **not** applied to casters.
  - Each cascade becomes a `render_shadow_data` entry (`RSC:3480-3492`) with its own `_render_shadow_append`. That call re-runs `_fill_render_list`, a sort and `_fill_instance_data` into the shared SECONDARY list (`RFC:2806-2880`). The pass is recorded as its own draw list into the atlas sub-rect (`RFC:2898-2907`).
- **Caching.** None. Every cascade of every shadowed directional light is culled, re-listed and redrawn every frame: `RSC:3478-3492` has no dirty check, and the whole atlas is cleared at `RFC:1570-1574`.

**Positional (omni/spot/area):**
- **Atlas.** A 4096² atlas with 4 quadrants; defaults give 4, 4, 16 and 64 slots.
  - The slot size is chosen from screen coverage (`RSC:3511-3600`, `LS:2591-2656`).
  - Reallocation is limited by `shadow_atlas_realloc_tolerance_msec = 500` (`servers/rendering/renderer_rd/storage_rd/light_storage.h:395`).
- **Caster culling.** Casters are found by a `DynamicBVH::convex_query` per face or paraboloid half (`RSC:2417, 2500, 2568, 2634`), followed by the optional tighter culler (`cull_regular_light`).
  - The filter checks only `visible`, geometry type, `can_cast_shadows` and layer/caster masks (`RSC:2427, 2510, 2578, 2644`).
- **Caching.** A shadow is redrawn only when the light's `last_version` changes. The light is marked dirty when:
  - a paired caster moves (`RSC:1705-1713`);
  - its layer mask changes (`RSC:949-954`);
  - the light itself changes;
  - an atlas reallocation happens (`LS:2649-2655`);
  - or **any caster uses an animated material** (`RSC:3635-3636`).
- "Animated" means `(uses_fragment_time && uses_discard) || (uses_vertex_time && uses_vertex)` (`SSFC:250-252`). A double-dirty counter supports multi-camera setups (`RSC.h:712-749`). At most `MAX_UPDATE_SHADOWS = 512` shadow passes are allowed per frame (`RSC.h:60`).
- **Omni CUBE mode (the default).** It renders 6 cubemap faces into a temporary cube, then runs 2 copy passes into dual-paraboloid atlas halves (`RFC:2716, 2772-2789`). Each cube light also gets its own shadow begin/end pass, outside the "Parallel" group (`RFC:1561-1566`).

**Filtering:**
- Sample counts are specialization constants from `RSRD:1172-1263`. With `penumbra/soft` samples, the levels are:
  - Hard: 4/0
  - Soft Very Low: 4/1
  - **Soft Low (default): 8/4, radius 2.0**
  - Medium: 12/8
  - High: 24/16
  - Ultra: 32/32
- They are pushed through `_update_shader_quality_settings` → `scene_shader.set_default_specialization` (`RFC:5080-5100`), and that **clears every material's pipeline cache** (`SSFC:1015-1021`).

Findings:
- **No directional caching.** There is no directional-shadow caching, no per-cascade update throttling, and no static/dynamic caster split. With dense vegetation and props, 4 cascades × all casters are drawn every frame. **Classification:** LIKELY BOTTLENECK
- **Cascade resolution.** Cascades all share one resolution and use bounding-sphere fitting. Defaults are 100 m max distance with linear splits. Covering "large distances" means either raising `max_distance` (the far cascade texel grows linearly) or adding lights. There is no cascade-count or per-cascade-resolution control. **Classification:** VISUAL QUALITY ISSUE
- **Wind-animated vegetation defeats positional caching.** Any `TIME`-in-`vertex()` vegetation inside an omni/spot range makes that light's shadow re-render every frame (`RSC:2430, 3635`). **Classification:** LIKELY BOTTLENECK
- **Positional casters ignore visibility ranges.** Positional-light caster culling never checks `visibility_range`/HLOD state (`RSC:2427` has no `VIS_CHECK`, unlike the directional path at `RSC:3245`). Hidden HLOD levels and far-hidden props still cast into omni/spot shadow maps: double shadows and extra cost. **Classification:** VISUAL QUALITY ISSUE
- **Cascade culling cost.** Cascade culling costs O(instances × cascades) per frame in the linear cull loop. **Classification:** NEEDS BENCHMARK
- **Omni cube cost.** CUBE mode (default) costs 6 passes plus 2 full-screen-quad copies per updated omni light. **Classification:** NEEDS BENCHMARK
- **Quality changes stall.** Changing shadow filter quality at runtime (a graphics-options menu) wipes all pipelines, and the next frames block on ubershader recompiles (§10). **Classification:** LIKELY BOTTLENECK
- **16-bit directional depth.** Directional depth is 16-bit by default (`rendering_server.cpp:3677`). Depth range per cascade is `2*radius + pancake` (`RSC:2339, 2357`), so precision falls on long cascades. **Classification:** NEEDS BENCHMARK

---

### B.5 Clustered lighting

**Cluster builder (`servers/rendering/renderer_rd/cluster_builder_rd.*`):**
- **Grid.** Tiles are 32×32 px (`cluster_size = 32`, `cluster_builder_rd.h:193`) with **32 depth slices linear in `z/z_far`**:
  - `cluster_render.glsl:147-149`: `unit_depth = depth * inv_z_far; z_bit = floor(unit_depth*32)`.
  - Shading at `scene_forward_clustered.glsl:1546` and `:553`: `cluster_z = clamp(-z/z_far*32, 0, 31)`.
- **Build.** Elements (omni, spot, area, decal, reflection probe) are CPU-transformed into view space (`add_light`/`add_box`, `cluster_builder_rd.h:236-414`). They are then rasterized as proxy meshes (icosphere, cone or box):
  - one draw per element, `instances = 1` (`cluster_builder_rd.cpp:470-512`);
  - into an empty framebuffer at 1/4 screen with 4× MSAA (`divisor = DIVISOR_4`, `use_msaa = true`, `cluster_builder_rd.h:194-195`; `cluster_builder_rd.cpp:315-320`);
  - then packed by a compute pass (`:519-541`).
- **Capacity.** Max elements per type = `max_clustered_elements` (512, aligned to 32). The same value caps omni/spot/area light buffers, reflection probes and decals (`servers/rendering/renderer_rd/storage_rd/light_storage.h:477-481`; `RSRD:1779-1812`).
- **Per-frame buffer clear.** The cluster render buffer is `tiles × (max*5/32 + max*5) × 4 B` (`cluster_builder_rd.cpp:300-305`) and is `buffer_clear`'d each frame when any element exists (`:448`). That is about 21.5 MB at 1080p and about 86 MB at 4K with default 512.
- **Overflow.** When `update_light_buffers` exceeds the cap, lights are **dropped in cull order before the distance sort** (`LS:866-869` early `continue`, sort afterwards at `LS:946-959`). Reflection probes are likewise capped before sorting by size (`LS:1963-1985`).
- **Decals and reflection probes** are in the same cluster structure as types 3 and 4 (`scene_forward_clustered.glsl:1554, 2046`). The reflection atlas holds 64 probes at 256² by default (`rendering_server.cpp:3702-3704`).

Findings:
- **Linear depth slicing.** Depth slicing is linear over the whole `z_far`. For open-world far planes (1–10 km) each slice is 30–300 m deep, so all local lights near the camera collapse into slices 0–1 and depth culling of lights is ineffective. Per-pixel loops then visit every light overlapping the 32 px tile. **Classification:** LIKELY BOTTLENECK
- **Arbitrary light dropping.** Past 512 lights per type, arbitrary (not farthest) lights disappear. **Classification:** VISUAL QUALITY ISSUE
- **Buffer clear scales with resolution.** The per-frame clear of the cluster render buffer scales with resolution × `max_clustered_elements`. **Classification:** NEEDS BENCHMARK
- **One draw per element** in the cluster bake is cheap at hundreds of elements. **Classification:** NOT A PROBLEM

---

### B.6 Culling

**Frustum culling:**
- `RendererSceneCull::_scene_cull` (`RSC:2892`) is a **linear scan over `scenario->instance_data`**, a flat array containing every instance in the scenario. It does a scalar p-vertex AABB-vs-plane test (`InstanceBounds::in_frustum`, `RSC.h:195-225`; `RSC:2930`). There is no BVH or octree traversal for camera culling.
- The per-instance test order is: `LAYER_CHECK && IN_FRUSTUM && VIS_CHECK && !OCCLUSION_CULLED` (`RSC:2917-2930`).
- `DynamicBVH` (`RSC.h:334`) is used for the geometry and volume indexers, which serve pairing, positional shadow caster queries and region queries. It is optimized incrementally, 10 iterations per frame (`RSC:4374-4375`).

**Visibility ranges / HLOD:**
- A threaded pre-pass (`_visibility_cull`, `RSC:2795-2832`) sets per-instance HIDDEN / HIDDEN_CLOSE_RANGE / FADE_CHILDREN flags from camera distance, including parent-dependency chains (`_visibility_parent_check`, `RSC:2874-2880`).
- **Fade.** Fading is computed as `fade_alpha` in `_fill_render_list` (`RFC:980-990`). **Any instance with `fade_alpha < 0.999` is forced into the transparent list** (`RFC:49, 1140-1147`): alpha-blended, depth-sorted, no depth prepass, no SSAO/SSR input.

**Occlusion culling (`modules/raycast/raycast_occlusion_cull.cpp`):**
- It is **CPU ray casting with Embree** against user-authored `OccluderInstance3D` meshes only. There is no GPU raster and no automatic occluders.
  - Rays are generated per buffer pixel and traced with `rtcIntersect16` on the WorkerThreadPool (`:480-506`).
  - A CPU HZB mip chain is built (`update_mips`, `:619`), and AABBs are tested against it (`servers/rendering/renderer_scene_occlusion_cull.h:115-160`, capped at 512 samples).
- **Resolution** is `occlusion_rays_per_thread × thread_count` pixels, clamped to between `viewport/(32²)` and `viewport/(2²)` (`servers/rendering/renderer_viewport.cpp:316-327`). For example, 16 threads × 512 = 8192 px, about 121×68 at 16:9.
- **Scheduling.**
  - The buffer update runs **synchronously on the render thread before culling** (`RSC:2779-2781`).
  - The Embree BVH rebuild after occluder changes is committed on a separate thread and swapped in later, so moved occluders lag at least one frame (`raycast_occlusion_cull.cpp:405-477`).
  - With jitter enabled, an instance stays visible for 9 frames after last being seen (`renderer_scene_occlusion_cull.h:170-199`).
- **Defaults and scope.** Disabled by default (`scene/main/scene_tree.cpp:2134`). Never used for shadow casters (§4).

Findings:
- **Linear camera culling.** Culling is O(all instances) per camera per frame, with no spatial hierarchy. It is repeated for each viewport and for each of the 6 reflection-probe faces. It is threaded, but the serial merge and memory bandwidth over 100k+ instances remain. **Classification:** LIKELY BOTTLENECK
- **Occlusion culling is limited.** It needs hand-authored occluders, runs at low resolution on the CPU synchronously, and does not cull shadow casters. **Classification:** ARCHITECTURAL LIMITATION
- **HLOD fading goes through the alpha pass.** Crossfading draws both LODs alpha-blended for the fade duration: sorting artifacts, no SSAO/SSR on them, higher fill cost. It uses no dithered fade. **Classification:** VISUAL QUALITY ISSUE
- **The p-vertex frustum test** is cheap per instance. **Classification:** NOT A PROBLEM

---

### B.7 MultiMesh / instancing

**Automatic instancing (verified):**
- `_fill_instance_data` merges consecutive elements whose `sort_key1` and `sort_key2` are both equal (and whose `mirror` matches) into one instanced draw with `repeat` count up to `MAX_REPEATS = 2^20-1` (`RFC:877-896`, `RFC.h:455`).
- The draw issues `instance_count = repeat` (`RFC:598`), and the shader offsets `draw_call.instance_index + INSTANCE_INDEX` (`scene_forward_clustered.glsl:770-773`).
- `geometry_id` is the mesh RID's local index (`RFC:4271`), so separate MeshInstance3D nodes sharing mesh+material do batch.
- **What breaks batching:**
  - different `lod_index`;
  - different `depth_layer`, where depth is quantized into 16 bands over `z_far` (`RFC:968`);
  - different softshadow/projector/forward-GI/lightmap bits;
  - skinned meshes (`mesh_instance`);
  - MultiMesh/particles (`cant_repeat`, `RFC:877`).
- Instance data (176 B per element: transform, prev transform, AABB, flags; `RFC.h:334-344`) is written every frame for every element of every list.

**MultiMesh:**
- One geometry instance: it is culled, LOD-selected and sorted as a single object, with LOD chosen from the nearest point of the whole MultiMesh AABB (`RFC:1086-1093`).
- One draw per surface with `instance_count = multimesh instances` (`RFC:4388-4402, 598`). It supports indirect draws via `use_indirect` (`RFC:603, 611`, `draw_list_draw_indirect`), so a user compute shader can write instance counts.
- **Uploads.** CPU-side dirty regions of 512 instances are tracked. If more than 32 regions, or more than half of the visible regions, are dirty, the whole buffer is uploaded (`servers/rendering/renderer_rd/storage_rd/mesh_storage.cpp:1718, 2257-2310`). The AABB is recomputed on the CPU when dirty (`:2296-2302`). Motion vectors double the buffer (`:1604-1640`).

**GPUParticles:**
- One compute update per active system: `update_particles` (`servers/rendering/renderer_rd/storage_rd/particles_storage.cpp:1442`).
- Processing is requested only when the system is visible (`RSC:3000-3005`).
- There is a hardcoded `screen_hz = 60` TODO for trails and fixed-fps (`particles_storage.cpp:1492`).

Findings:
- **Auto-instancing works but is fragile.** Instancing of identical mesh+material exists, but the 16 depth bands and LOD index split batches, and sorting does not group by LOD first. Actual draw-call counts should be measured with `VIEWPORT_RENDER_INFO_DRAW_CALLS_IN_FRAME` (`RFC:890`). **Classification:** NEEDS BENCHMARK
- **MultiMesh is all-or-nothing.** It has no per-instance frustum or occlusion culling and no per-instance LOD. A large vegetation MultiMesh is all-or-nothing per surface and uses the LOD of its closest point. Chunking is mandatory. **Classification:** ARCHITECTURAL LIMITATION
- **No GPU-driven path.** There is no GPU culling or compaction pipeline for MultiMesh; only the raw indirect hook exists. **Classification:** ARCHITECTURAL LIMITATION

---

### B.8 Mesh LOD

**Generation:**
- `ImporterMesh::generate_lods` uses meshoptimizer via `SurfaceTool::simplify_with_attrib_func` (`scene/resources/3d/importer_mesh.cpp:570-846`). Import option `generate/lods` (Default/Enable/Disable) and `lods/normal_merge_angle=20` (`editor/import/3d/resource_importer_scene.cpp:2298-2299`).
- **Iteration.** It halves the index count per iteration with `SIMPLIFY_SPARSE | SIMPLIFY_LOCK_BORDER` (plus PRUNE and REGULARIZE when deformable) (`importer_mesh.cpp:763-786`). Normals are weighted (and colors when present).
- **Stopping.** It stops at cumulative error > 1.0, at fewer than 25% reduction, or at 12 indices. Error is forced to grow at least 1.5× per level (`:757-835`).
- **Storage.** LODs are **index buffers only**, sharing the full vertex buffer (`:836-841`; `mesh_surface_get_index_array`, `servers/rendering/renderer_rd/storage_rd/mesh_storage.h:500-508`).

**Selection:**
- Selection is per surface in `_fill_render_list` (`RFC:1106-1110`) → `mesh_surface_get_lod` (`mesh_storage.h:480-498`). It picks the coarsest LOD whose `edge_length * model_scale * lod_bias / (distance * lod_mult)` is at most the threshold.
- `distance` is the nearest point of the instance AABB to the main camera (`RFC:1086-1093`).
- `lod_mult = 2/P[0][0] / scaling_3d_scale` (`core/math/projection.cpp:893-898`; `RSRD:1399-1404`).
- The threshold is `threshold_pixels / viewport_width` (`servers/rendering/renderer_viewport.cpp:331`), 1.0 px by default.

**Shadows:**
- Shadow LOD uses the same function with the **main camera** position (`scene_data.main_cam_transform`, `RFC:2821, 1090`), but with `cam_projection.get_lod_multiplier()` **without** the `1/scaling_3d_scale` factor (`RFC:1546` vs `RSRD:1404`).

Findings:
- **LODs keep the full vertex buffer.** Vertex count and bandwidth do not drop with LOD, and `LOCK_BORDER` limits reduction on open-border meshes (foliage cards, terrain chunks). **Classification:** ARCHITECTURAL LIMITATION
- **Discrete LOD switches.** There is no crossfade or dither between LODs. Popping is controlled only by the pixel threshold. **Classification:** VISUAL QUALITY ISSUE
- **Shadow and main-pass LOD can disagree.** When `scaling_3d/scale ≠ 1`, main-pass LOD and shadow LOD can differ, which can cause self-shadowing artifacts on LOD'd meshes. **Classification:** VISUAL QUALITY ISSUE
- **Per-surface LOD loop** (a few iterations) on the CPU is cheap. **Classification:** NOT A PROBLEM

---

### B.9 Texture streaming / VRAM

- A search for `stream`/`streaming` across `servers/rendering` returns **no matches**. No mip streaming, residency, sampler feedback or virtual texturing exists.
- **Loading.** Textures are uploaded whole, with all mips, at resource load time. Loader-thread creation goes through RD transfer workers (`RD:861-883, 2141-2171`), enabled by `can_create_resources_async() == true` (`servers/rendering/renderer_rd/renderer_compositor_rd.h:149`). The only size control is import-time `process/size_limit` (`editor/import/resource_importer_texture.cpp:258`).
- **VRAM reporting.**
  - `RENDERING_INFO_TEXTURE_MEM_USED`, `BUFFER_MEM_USED` and `VIDEO_MEM_USED` report RD-tracked bytes and VMA `allocationBytes` (`servers/rendering/renderer_rd/storage_rd/utilities.cpp:299-301`; `drivers/vulkan/rendering_device_driver_vulkan.cpp:7247-7260`).
  - The VMA heap budget is queried only for statistics. No code path reacts to budget pressure; oversubscription is left to the driver or OS.
- **Staging stalls.** Render-thread uploads above 128 MB per frame trigger `STAGING_REQUIRED_ACTION_FLUSH_AND_STALL_ALL` / `STALL_PREVIOUS` (`RD:955-1033`).

Findings:
- **No texture streaming.** A large open world must fit all loaded textures (full mip chains) in VRAM, or manage load and unload at resource granularity. **Classification:** ARCHITECTURAL LIMITATION
- **Upload bursts can stall.** Bursty render-thread uploads (big MultiMesh or texture updates) can hit the staging cap and stall the frame. **Classification:** NEEDS BENCHMARK

---

### B.10 Shader compilation / pipeline caching

**Layers:**
1. **SPIR-V per material shader.** `ShaderRD` compiles variant groups asynchronously on the WorkerThreadPool (`servers/rendering/renderer_rd/shader_rd.cpp:716-737`). It blocks in `_compile_version_end` when a variant is first needed (`:739-745`). A disk cache is used (`_load_from_cache`/`_save_to_cache`, `:614-700`; settings `rendering_server.cpp:3690-3694`), and the export-time shader baker precompiles it (`editor/export/shader_baker_export_plugin.cpp`).
2. **Variants.** Each material shader has **2× variants**: normal and `#define UBERSHADER` (`SSFC:646-656`).
3. **Pipelines (PSOs).** Each material's `PipelineHashMapRD` stores `RBMap<uint32_t hash, RID>` (`servers/rendering/renderer_rd/pipeline_hash_map_rd.h:53`). The key hashes vertex format, framebuffer format, cull, primitive, version, color-pass flags, specialization constants, wireframe and ubershader (`servers/rendering/renderer_rd/forward_clustered/scene_shader_forward_clustered.h:191-215`).
4. **Draw-time lookup** (`RFC:496-532`):
   - Try the specialized pipeline without waiting (`get_pipeline(..., wait=false, SPECIALIZATION)`). On a miss, this queues a background compile.
   - Fall back to the **ubershader pipeline with `wait=true`** (source DRAW). The ubershader takes specialization constants from push constants (`RFC:586-591`) and emulates face culling with `discard` (`scene_forward_clustered.glsl:3119-3122`).
5. **Precompilation.**
   - At mesh creation, `mesh_generate_pipelines` compiles ubershader pipelines for all passes. It waits only when called from a non-render thread (`servers/rendering/rendering_server_default.h:362-386`; `RFC:4982-5036`).
   - At surface-cache creation (`PRELOAD_PIPELINES_ON_SURFACE_CACHE_CONSTRUCTION 1`, `RFC:47, 4284-4292`).
   - Whenever `global_pipeline_data_required` changes, **all** surfaces are recompiled (`RFC:4844-4873`). That key flips lazily at runtime on first use of reflection probes, motion vectors (TAA/FSR), multiview, normal-roughness, lightmaps, VoxelGI, separate specular (SSS), SDFGI, cube or DP shadows (`RFC:1859-2000, 3115, 4144-4157`).
   - Shader groups (ADVANCED, MULTIVIEW) are enabled lazily and trigger recompiles of every shader version (`shader_rd.cpp:931-948`; `RFC:1865, 1891, 1983`).
6. **Disk PSO cache.** `user://vulkan/pipelines.<...>.cache` (`RD:8580-8595`). `update_pipeline_cache` runs after **every** pipeline creation under the RD mutex (`RD:4962-4963, 8617-8660`), and saves asynchronously every 3 MB of growth.

**Counters:** `RENDERING_INFO_PIPELINE_COMPILATIONS_{CANVAS, MESH, SURFACE, DRAW, SPECIALIZATION}` (`servers/rendering/rendering_server.cpp:3549-3553`; semantics in `doc/classes/RenderingServer.xml:6224-6238`) come from the per-source counters incremented in `compile_pipeline` (`pipeline_hash_map_rd.h:121-124`).
- **DRAW** = blocking ubershader compiles during rendering (stutter).
- **SURFACE** = on surface-cache build or requirement change.
- **SPECIALIZATION** = background, non-blocking.
- They are also exposed as `Performance.PIPELINE_COMPILATIONS_*` (`main/performance.cpp:109-113`).

Findings:
- **Blocking ubershader compiles cause stutter.** First use of any new vertex format, framebuffer format or pass combination not predicted by precompilation blocks the render thread. Examples: MSAA set per viewport rather than in project settings (texture samples come only from the project setting, `RFC:4145-4150`), first reflection probe, first TAA enable, first SSS material. These show up in `PIPELINE_COMPILATIONS_DRAW`. **Classification:** LIKELY BOTTLENECK
- **Requirement flips recompile everything.** A lazy global-requirement flip re-queues pipelines for every registered surface at once, and the same frame waits on the ones it needs. **Classification:** LIKELY BOTTLENECK
- **32-bit hash collisions.** The pipeline map is keyed by a 32-bit hash only, with no key equality check (`pipeline_hash_map_rd.h:179-205`). A collision silently returns the wrong PSO. **Classification:** ARCHITECTURAL LIMITATION
- **Ubershader draws are slower until specialization lands.** They pay for fragment-shader culling via discard and dynamic branching. **Classification:** NEEDS BENCHMARK
- **Pipeline-cache size queries.** `update_pipeline_cache` queries the driver cache size after every PSO creation under a global mutex. **Classification:** NEEDS BENCHMARK

---

### B.11 GPU timestamp support

- **Macro.** `RENDER_TIMESTAMP(name)` expands to `if (RSG::utilities->capturing_timestamps) RSG::utilities->capture_timestamp(name)` (`servers/rendering/storage/utilities.h:160-170`).
- **Capture.** `RenderingDevice::capture_timestamp` (`RD:8699-8711`):
  - records the CPU `get_ticks_usec()` value and inserts a graph `TYPE_CAPTURE_TIMESTAMP` command;
  - is written with `vkCmdWriteTimestamp(BOTTOM_OF_PIPE)` (`drivers/vulkan/rendering_device_driver_vulkan.cpp:6811-6814`);
  - is forbidden inside an active draw, compute or raytracing list (`RD:8702-8704`), so there is no per-draw timing;
  - is capped at `max_timestamp_query_elements` = 256 per frame (`RD:8705`); excess markers error out and are dropped.
- **Enabling:**
  - `--gpu-profile` or `debug/settings/stdout/print_gpu_profile` (`main/main.cpp:1982, 2273, 3571-3572`) calls `set_print_gpu_profile(true)`, which sets `capturing_timestamps` and prints averages once per second. Each name's duration is the GPU delta to the *next* marker; names starting with `<`/`>` are not printed (`rendering_server_default.cpp:165-200`).
  - The editor Visual Profiler calls `set_frame_profiling_enabled` / `get_frame_profile` (`servers/debugger/servers_debugger.cpp:364, 377`).
- **Exposed to GDScript:**
  - `RenderingServer.viewport_set_measure_render_time` and `viewport_get_measured_render_time_cpu/gpu` (`rendering_server.cpp:2908-2911`). These are implemented by always-on `vp_begin_<id>`/`vp_end_<id>` timestamps (`servers/rendering/renderer_viewport.cpp:339-341, 760-762, 1696-1716`).
  - `get_rendering_info`, `get_frame_setup_time_cpu` (`rendering_server.cpp:3511, 3539`).
  - `Viewport.get_render_info` (objects, primitives, draw calls; visible vs shadow).
  - `RenderingDevice.capture_timestamp`, `get_captured_timestamps_count/_frame`, `get_captured_timestamp_gpu_time/_cpu_time/_name` (`RD:9192-9197`), reachable via `RenderingServer.get_rendering_device()` on the render thread.
  - **`get_frame_profile`/`set_frame_profiling_enabled` are not bound** (no `bind_method` in `rendering_server.cpp`).
- **CPU zones.** A separate CPU zone profiler is available through the build option `profiler=tracy|perfetto|instruments` (`SConstruct:207`, `core/profiling/profiling.h:44-131`). `GodotProfileZoneGrouped` zones cover `_draw` and `_end_frame` (`rendering_server_default.cpp:76-133`, `RD:8117-8127`).

Findings:
- **Profiling changes the schedule.** Every timestamp is a full dependency barrier in the RD graph (`RDG:376-387`). Enabling profiling therefore disables cross-segment reordering and overlap, so profiled numbers can differ from unprofiled frames. **Classification:** NEEDS BENCHMARK
- **Coarse granularity.** One marker covers all directional cascades plus spot shadows plus GI. CPU-only markers such as "Cull DirectionalLight3D, Split N" have near-zero GPU spans. The CPU cost of graph reorder, replay and submit (`end_frame`) falls outside every marker, so Tracy or Perfetto is required for it. **Classification:** ARCHITECTURAL LIMITATION
- **The 256 cap is too small.** Scenes with many updated shadowed lights (up to 8 markers per cube light) or reflection probes can overflow the 256 cap. **Classification:** NOT A PROBLEM (raise `debug/settings/profiler/max_timestamp_query_elements`)

---

### B.12 Draw-call submission cost

**Render lists:**
- One `RenderList` per type: OPAQUE, MOTION, ALPHA, SECONDARY (shadows) (`RFC.h:78-84`). Elements are `GeometryInstanceSurfaceDataCache*` pointers.
- **Sort key (128-bit)** (`RFC.h:507-531`), sorted by `sort_key2` then `sort_key1` (`RFC.h:694-698`):
  - `sort_key2` = `priority:8 | shader_id:32 | material_id_lo:24` (most significant first);
  - `sort_key1` = `material_id_hi:8 | geometry_id:32 | surface_index:8 | depth_layer:4 | flags:4 | lod_index:8`.
- **Sorting.**
  - Opaque and motion use `SortArray` introsort on pointers, dereferencing each comparison (`RFC.h:692` comment: "should eventually be replaced by radix").
  - Alpha is sorted by priority then back-to-front instance depth (`RFC.h:722-731`).
  - Shadows are sorted per pass by key (`RFC:2849`).
- **Opaque is not front-to-back.** `depth_layer` is a low-order key, so the depth prepass gets essentially no front-to-back ordering.

**Per visible surface per list (CPU):**
- `_fill_render_list` (`RFC:921-1204`): distance, fade, GI flags (small linear searches over at most 8 lightmaps or VoxelGIs), LOD loop, list push.
- `_fill_instance_data` (`RFC:806-915`): builds a 176 B `InstanceData`, reads mesh format, AABB and UV scale, writes to a persistently-mapped per-frame buffer, runs repeat detection.
- This repeats for each shadow pass that contains the object.

**Per draw (after auto-instancing), in `_render_list_template` (`RFC:305-624`):**
- murmur3 `PipelineKey::hash()`;
- spin-locked vertex-format version lookup, `mesh_surface_get_vertex_arrays_and_format` with a `SpinLock` (`mesh_storage.h:109, 510-540`);
- on hash change, an RBMap lookup (plus mutex on miss);
- conditional binds of vertex array, index array (changes with LOD), pipeline, transforms set (MultiMesh) and material set (one descriptor set per material);
- **always** a push constant (16 B, or 16 B + ubershader block);
- **always** a draw.
- Each RD call does an RID-owner lookup and appends a graph instruction. `draw_list_draw` walks the bound sets and appends usage trackers for rebound sets (`RD:5996-6100`).
- At `draw_list_end` the instruction stream is memcpy'd (`RDG:2384`) and later replayed into the native command buffer.
- Three uniform sets are bound once per list: scene, render pass and transforms (`RFC:312-314`).
- **Pipelines and shadows.** Depth-only and shadow passes use `shader_shadow`/`surface_shadow` (`RFC:359-363`). These share the default material only for "simple" materials (`uses_shared_shadow_material`, `servers/rendering/renderer_rd/forward_clustered/scene_shader_forward_clustered.h:298-301`). Alpha-clip, discard, vertex-animated, double-sided and world-coordinate materials (typical vegetation) keep their own shader in the prepass and shadows.
- **Not present:** bindless textures, multi-draw-indirect, GPU culling or compaction, and draw merging beyond the auto-instancing above (a search for `bindless|multi_draw|meshlet` in forward_clustered finds nothing).

Findings:
- **Single-threaded per-draw CPU cost.** Per-draw CPU cost (fill, sort, instance data, record, graph copy, replay) is linear and single-threaded, and it is multiplied by up to 1 prepass + 1 color + N shadow passes. At 10k+ draws per frame this is the likely CPU limiter. **Classification:** LIKELY BOTTLENECK
- **Pointer-chasing sort.** Pointer-chasing introsort over 128-bit keys stored in paged allocations is cache-unfriendly at high element counts. **Classification:** NEEDS BENCHMARK
- **No front-to-back prepass ordering.** Alpha-tested vegetation may cause heavy prepass overdraw. **Classification:** NEEDS BENCHMARK
- **No bindless or GPU-driven submission.** Material changes cost descriptor-set binds, and object count scales CPU cost directly. **Classification:** ARCHITECTURAL LIMITATION
- **Redundant state is filtered.** Redundant binds are filtered at both the renderer and RD level (for example `RD:5700-5702`), and pipeline binds happen only on hash change. **Classification:** NOT A PROBLEM

---

### B.13 Summary of the code-audit priorities (before measurement)

1. **Directional shadows** have no caching: all 4 cascades are redrawn every frame, with uniform resolution and a linear cull. LIKELY BOTTLENECK.
2. **Per-draw CPU path** (fill, sort, record, RD graph double-recording) is single-threaded, with secondary command buffers disabled. LIKELY BOTTLENECK.
3. **Pipeline stutter**: blocking ubershader compiles and lazy global-requirement flips. LIKELY BOTTLENECK. Track `PIPELINE_COMPILATIONS_DRAW`.
4. **Clustered lighting** uses linear Z over the whole `z_far`. LIKELY BOTTLENECK for large far planes with many lights.
5. **Camera culling** is a linear scan of all instances, with no hierarchy and no GPU occlusion. LIKELY BOTTLENECK at open-world counts.
6. **Wind-animated vegetation** forces per-frame positional shadow redraws. LIKELY BOTTLENECK.
7. **Positional shadows ignore visibility ranges (HLOD).** VISUAL QUALITY ISSUE.
8. **HLOD fade** is forced through the alpha pass. VISUAL QUALITY ISSUE.
9. **No texture streaming.** ARCHITECTURAL LIMITATION.
10. **MultiMesh** gets no per-instance culling or LOD. ARCHITECTURAL LIMITATION.
11. **Profiling via RENDER_TIMESTAMP** perturbs the RD graph schedule and does not cover `end_frame` CPU cost. Use Tracy (`profiler=tracy`) for CPU and raise `max_timestamp_query_elements`.

---

# Part C — Post-processing, screen-space and volumetric effects (code audit)

Written from the 4.7.2 source before measuring (path abbreviations above). The TAA items C.5.3 are the ones addressed by experiments 0001/0002.

### C.0 Key findings at a glance

| # | Finding | Where | Classification |
|---|---|---|---|
| A | TAA "closest depth" search picks the **farthest** sample under reverse-Z. The dilated velocity is also fetched at a (-1,-1) offset. | `sh/taa_resolve.glsl:145-171`, `:379` | VISUAL QUALITY ISSUE |
| B | TAA reprojects history with the pixel's own velocity. Dilated velocity is used only to size the clip box. | `sh/taa_resolve.glsl:316-330`, `:280` | VISUAL QUALITY ISSUE |
| C | The TAA clip box collapses to 0 when \|v\| ≥ 0.02 UV/frame. The output then becomes about 94% of the 3x3 mean, which blurs the image during fast camera turns. | `sh/taa_resolve.glsl:280-286` | VISUAL QUALITY ISSUE |
| D | The TAA anti-flicker term shrinks the blend factor even when history is off-screen or disoccluded. | `sh/taa_resolve.glsl:333-358` | VISUAL QUALITY ISSUE |
| E | Auto-exposure reduction drops partial 8x8 blocks at every level. The metered area depends on resolution: at 1080p it covers only the left 80% × top 95% of the screen. | `fx/luminance.cpp:91-93,244-248`, `sh/luminance_reduce.glsl:44,72-80` | VISUAL QUALITY ISSUE |
| F | Auto-exposure meters with the arithmetic mean of max(R,G,B): no log average, no histogram, no weighting. Adaptation is a linear lerp in linear space with an unclamped factor. | `sh/luminance_reduce.glsl:46-47,73-78`, `srr.cpp:567` | VISUAL QUALITY ISSUE |
| G | FSR2 receives the *average luminance* buffer where FFX expects an *exposure multiplier*. Reset is never signalled. | `fc.cpp:2465-2468,2493`, `fx/fsr2.cpp:851,859` | VISUAL QUALITY ISSUE |
| H | With FSR2, glow (and auto-exposure) read the jittered pre-upscale internal buffer, not the upscaled output. | `srr.cpp:570,609` vs `:491` | VISUAL QUALITY ISSUE |
| I | Filmic, ACES and Reinhard use `tonemap_white = 1.0` by default. Filmic/ACES therefore clip at scene value 1.0, and Reinhard with white 1 is the identity (the same as Linear). The default operator is Linear. | `Environment.h:116-118`, `envst.cpp:236-261,286`, `tonemap.glsl:96` | VISUAL QUALITY ISSUE |
| J | The 3D/1D color-correction LUT is sampled without a half-texel remap. | `sh/tonemap.glsl:456-467` | VISUAL QUALITY ISSUE |
| K | The SSR half-res bilateral upsample weights *raw* reverse-Z depth differences (×2048). It loses edge awareness at distance. | `sh/screen_space_reflection_resolve.glsl:42-47` | VISUAL QUALITY ISSUE |
| L | The TAA path shades every opaque vertex twice for motion vectors. FSR2/MFX have a camera-only path for static objects; TAA does not use it. | `scene.glsl:777-816,847`, `fc.cpp:1840,2221-2240` | NEEDS BENCHMARK |
| M | Full-res RGBA16F copies per frame: TAA 2 copies plus the velocity copy, and the SSR/SSIL last-frame copy plus mips. | `fx/taa.cpp:122-126`, `fx/ss_effects.cpp:398-421` | NEEDS BENCHMARK |
| N | Volumetric-fog temporal reprojection blends 0.9 with no history rejection. Jitter is the same for all froxels. | `shenv/volumetric_fog_process.glsl:274-290,329-355` | VISUAL QUALITY ISSUE |
| O | `RoughnessLimiter` (compute) is dead code. Specular AA runs in-shader instead (Tokuyoshi–Kaplanyan). | `fx/roughness_limiter.cpp` (never included), `scene.glsl:1657-1672` | NOT A PROBLEM |

---

### C.1 HDR pipeline, buffer formats, color space, HDR output

**What the code does**
- **Scene color** is `R16G16B16A16_SFLOAT` for Forward+. `_render_buffers_get_preferred_color_format()` returns RGBA16F (`srr.cpp:1160-1162`), and `get_base_data_format()` returns RGBA16F whenever `force_hdr` is set (`rsb.h:195`). The color texture is created at `rsb.cpp:186`, with the MSAA variant at `rsb.cpp:199`. No R11G11B10 (`B10G11R11_UFLOAT`) buffer exists anywhere in the Forward+ scene-color path. The only A2B10G10R10 case is the Mobile/non-storage path (`srr.cpp:731`).
- **Derived buffers** use the same base format:
  - blur/glow chain `RB_TEX_BLUR_0/1` (`rsb.cpp:534-535`)
  - TAA history/temp (`fx/taa.cpp:101-102`)
  - SSR/SSIL last-frame buffer, hard-coded RGBA16F (`fx/ss_effects.cpp:393`)
  - upscaled texture (`rsb.cpp:724`)
  - volumetric fog 3D maps, RGBA16F (`env/fog.cpp:469`)
  - velocity RG16F (`fx/taa.cpp:104`, `sh/taa_resolve.glsl:48`)
  - luminance chain R32F (`fx/luminance.cpp:95`)
- **`luminance_multiplier`** is 1.0 on Forward+ because `can_be_storage` is true (`rsb.cpp:861-865`, `srr.cpp:1164-1166`). The ×2 range packing applies only to Mobile UNORM buffers.
- **Tonemapping happens exactly once**, in a fullscreen *raster* pass (`fx/tone_mapper.cpp:211-220`) inside `_render_buffers_post_process_and_tonemap` (`srr.cpp:675-817`). It runs after TAA/FSR2, DOF, auto-exposure and glow. Its input is the internal texture, or the upscaled texture for FSR2/MetalFX (`srr.cpp:491`).
- **Output target** (`TextureStorage::render_target_get_color_format`, `servers/rendering/renderer_rd/storage_rd/texture_storage.cpp:5285-5291`):
  - `RGBA8_UNORM` without `use_hdr_2d`. The sRGB OETF is applied in the shader (`sh/tonemap.glsl:942-944`, flag set at `srr.cpp:752`).
  - `RGBA16F` with `use_hdr_2d`, and the output stays linear.
  - Project default `rendering/viewport/hdr_2d = false` (`rs.cpp:3740`).
- **HDR display output exists in 4.7.** `renderer_viewport.cpp:375-381` queries `window_get_hdr_output_enabled` and sets `window_output_max_value`. That value becomes the tonemapper `max_value` only if the viewport uses HDR 2D (`srr.cpp:733-746`). The final blit supports only `REC709_LINEAR` (scRGB-style: clamp to `output_max_value` × reference-white multiplier) or `REC709_NONLINEAR_SRGB` (`servers/rendering/renderer_rd/shaders/blit.glsl:88-90,154-190`; `servers/rendering/renderer_rd/renderer_compositor_rd.cpp:54-58`).
- **Working space** is linear Rec.709 throughout. AgX alone transforms internally to Rec.2020 (`sh/tonemap.glsl:198-222`).

**Weaknesses / notes**
- Every full-screen pass reads and writes 8 B/px. The per-frame chain includes: TAA resolve plus 2 color copies and 1 velocity copy (`fx/taa.cpp:121-126`); the last-frame copy plus up to 5 mips for SSIL (`fx/ss_effects.cpp:398-421`); the optional screen-texture copy plus a full mip chain (`srr.cpp:376-399`); glow; and tonemap.
  **Classification:** NEEDS BENCHMARK (the redundancy is certain from the code; the cost at 1440p/4K is not known)
- The 3D path has no PQ/HDR10 or wide-gamut output encoding. The blit knows only 2 color spaces (`blit.glsl:88-90`), and AgX's comment itself says it is tuned for Rec.709 (`sh/tonemap.glsl:191-196`).
  **Classification:** ARCHITECTURAL LIMITATION
- With HDR output but `hdr_2d=false`, the scene is tonemapped to SDR (`max_value = 1.0`, `srr.cpp:736`) and then only scaled by the reference multiplier in the blit. You must enable HDR 2D to get real HDR range.
  **Classification:** NOT A PROBLEM (configuration requirement to document for the fork)
- The FIXME at `srr.cpp:468` says the effects gate checks target size, not internal size.
  **Classification:** NOT A PROBLEM

---

### C.2 Tonemapping (`sh/tonemap.glsl`, `fx/tone_mapper.cpp`, `envst.cpp`)

**Order inside the tonemap fragment shader** (`sh/tonemap.glsl:854-955`):
1. Fetch source; multiply by `luminance_multiplier` (:856-860).
2. Exposure: `exposure = env.exposure`. With auto-exposure it is multiplied by `auto_exposure_scale / avg_lum` (:864-870).
3. FXAA on the *exposed, pre-tonemap linear HDR* signal (:872-876).
4. Glow composite before tonemapping for Add/Screen/Replace/Mix modes (:878-889).
5. `apply_tonemapping` (:893). Negative values are clamped to 0 except in Linear (:246-264).
6. Soft Light glow is applied *after* tonemapping, and the glow itself is tonemapped (:897-907).
7. BCS adjustments (:911-931):
   - brightness multiplies linear values;
   - then linear→sRGB;
   - contrast around 0.5 in sRGB;
   - saturation around the unweighted mean in sRGB;
   - optional LUT with a [0,1] clamp (:933-935);
   - back to linear only if the output is HDR (:939-941).
8. If BCS is off: sRGB encode (:942-944).
9. 8-bit dither, static Iestyn pattern with no time component (:843-852, :949-952).

The desktop tonemapper uses **runtime push-constant flags** with dynamic uniform branches (`fx/tone_mapper.cpp:126-171`). Mobile uses specialization constants (`:253-260`). Shader variants exist only for bicubic glow, 1D LUT and multiview (`:76-86`).

**Operators (exact code)**

| Operator | Formula (`x` = exposed linear RGB, per channel) | White handling |
|---|---|---|
| Linear (0) | `x`, unclamped (:247-249) | `white = output_max_value` (`envst.cpp:236-237`) |
| Reinhard (1) | `x·(1 + x/ws)/(1 + x/max)`, `ws = white²/max` (:93-97, `envst.cpp:285-286`) | `white = max(output_max, env.white)` (`envst.cpp:254-261`) |
| Filmic (2) | Hable/Uncharted2 with exposure bias 2.0 baked in: A=0.88, B=0.6, C=.1, D=.2, E=.01, F=.3; `f(x)/f(white)` (:99-115, `envst.cpp:287-300`) | `white = max(1, env.white)` (`envst.cpp:238-241`) |
| ACES (3) | MJP/Hill fitted RRT+ODT: `rgb_to_rrt` (bias 1.8) → `(x(x+A)-B)/(x(Cx+D)+E)` → `odt_to_rgb`, divided by the fitted curve at white×1.8 (:117-144, `envst.cpp:301-312`) | same as Filmic |
| AgX (4) | Rec.709→Rec.2020+inset matrix → "allenwp" curve (sigmoid-power toe below 0.18, Reinhard-like shoulder above) → `min(max_value)` → outset+Rec.2020→709. Negative output is allowed (:146-228). Curve parameters are computed on the CPU (`envst.cpp:313-341`). | `white = max(2, agx_white)·max_value`. Forced to 2.0 for A2B10G10R10 buffers (`envst.cpp:242-253`, `srr.cpp:731`) |

**Defaults**
- `tone_mapper = LINEAR`, `tonemap_exposure = 1.0`, `tonemap_white = 1.0`, `tonemap_agx_white = 16.29`, `tonemap_agx_contrast = 1.25` (`Environment.h:116-120`).
- The editor preview environment toggles Filmic (`editor/scene/3d/node_3d_editor_plugin.cpp:10217`).
- Debanding is off by default (`rs.cpp:3751`). It is 8-bit only on desktop (`fx/tone_mapper.cpp:165-167`) and disabled when output is HDR (`srr.cpp:794-808`).

**Weaknesses**
- **Default whitepoint makes three operators clip.**
  - Filmic and ACES normalise the curve so that `white` maps to 1.0. With the default `white = 1.0`, every scene value above 1.0 maps to more than 1 and is then clipped by the UNORM target. The asymptotes are about 1.67 for Filmic and about 1.30 for ACES after normalisation, so all highlight roll-off is lost unless `tonemap_white` is raised to roughly 6-16.
  - Reinhard with `ws = 1`, `max = 1` reduces exactly to `x`, i.e. Linear.
  - Only AgX has a sane default (white 16.29).

  **Classification:** VISUAL QUALITY ISSUE (defaults, not code; the fork should ship AgX or a raised white)
- **Per-channel curves** (Reinhard, Filmic, ACES) produce hue skew and saturated-highlight clipping toward primaries. AgX mitigates this with its inset/outset matrices.
  **Classification:** VISUAL QUALITY ISSUE
- **LUT half-texel error.** `textureLod(lut, color, 0)` is called with color in [0,1] (:464-466), and the 1D variant does the same (:457-461). Correct sampling needs `c·(N-1)/N + 0.5/N`. The mismatch shifts mid-range lookups by up to ±0.5 texel. For a 16³ LUT that is about 3% of the domain, so an identity LUT is not an identity.
  **Classification:** VISUAL QUALITY ISSUE (check with the identity-LUT test in §12)
- **FXAA runs pre-tonemap on HDR** (:872-876). `rgb2luma = sqrt(dot)` on values above 1, and its fixed thresholds (0.0312 / 0.125, :542-545) were designed for perceptual LDR input. High-contrast HDR edges also blend in linear space before the curve.
  **Classification:** VISUAL QUALITY ISSUE
- **Contrast and saturation are applied in sRGB-encoded space** with unweighted luma (:925-931), which is kept for compatibility. There is no lift/gamma/gain, white balance, vignette, grain or chromatic aberration.
  **Classification:** ARCHITECTURAL LIMITATION
- **Tonemap cost** is 1 full-screen fragment pass with up to 7×4 bilinear glow taps (bicubic) plus the FXAA loop (≤12 iterations).
  **Classification:** NOT A PROBLEM

---

### C.3 Exposure and auto-exposure (`fx/luminance.cpp`, `sh/luminance_reduce.glsl`, `srr.cpp:554-578`)

**Algorithm**
- Compute path: 8×8 group reduction (`sh/luminance_reduce.glsl:7-9`).
- The first pass reads `max(r,g,b)` of the internal HDR texture (:45-47). Each group computes the **arithmetic mean** of its valid texels (:58-73).
- Levels shrink by `/8` with truncation until 1×1 (`fx/luminance.cpp:91-121`). At 1080p that is 240×135 → 30×16 → 3×2 → 1×1, i.e. 4 dispatches.
- Final pass: `avg = clamp(prev + (avg − prev)·adjust, min, max)` (:76-79), with `adjust = speed·Δt` (`srr.cpp:567`). The result is swapped into `current` (`fx/luminance.cpp:256`).
- Tonemap exposure: `exposure·scale/avg` (`sh/tonemap.glsl:866-868`). Glow uses the same (`sh/copy.glsl:191-195`).
- There is **no log average, no histogram/percentile rejection, and no centre or spot metering.**
- Timing: runs after TAA and DOF, before glow (`srr.cpp:556-578`). The source is `rb->get_internal_texture()` at internal resolution (`srr.cpp:570`).

**Defaults**
- `CameraAttributes`: speed 0.5, scale 0.4 (`scene/resources/camera_attributes.h:53-54`).
- Practical: min/max sensitivity 0/800 ISO, converted to a luminance clamp of `ISO·0.125/100/100`, giving [0, 1.0] (`scene/resources/camera_attributes.cpp:254-255,312-313`).
- Physical: EV100 −8..10, giving `2^EV·12.5/ISO` (`camera_attributes.cpp:447-456,506-507`).

**Weaknesses**
- **Partial blocks are silently dropped.** Destination sizes are `floor(src/8)` (`fx/luminance.cpp:92-93`), but the dispatch covers the whole source (`:247`), so the last partial group writes out of bounds and is discarded (`sh/luminance_reduce.glsl:75,80`).
  - 1080p: level 2 drops texel rows 128-134 of 135 (bottom ~5%), and level 3 drops columns 24-29 of 30 (right 20%).
  - 1440p: roughly the bottom 29% is lost.
  - Exposure therefore depends on resolution and is biased toward the top-left of the screen.
  - A related bounds test uses `any(lessThan(...))` instead of `all(...)` (:44), so partial groups also read out of bounds. Robust image access is not enabled (`drivers/vulkan/rendering_device_driver_vulkan.cpp:810`); those groups are then dropped, but it is UB.

  **Classification:** VISUAL QUALITY ISSUE
- **Mean of max-channel** is dominated by the sun, sky and specular outliers. In an open world, looking at the sky versus the ground swings exposure strongly, and saturated colors read brighter than their luminance.
  **Classification:** VISUAL QUALITY ISSUE
- **Adaptation lerps in linear space** (:78), so darkening and brightening speeds are asymmetric. The factor `speed·Δt` is not `1−e^{−speed·Δt}` and is **not clamped**: `speed·Δt > 1` overshoots and `> 2` oscillates, for example on a frame hitch with a high speed setting.
  **Classification:** VISUAL QUALITY ISSUE
- **First frame / version change** (`set_immediate`, `srr.cpp:563-570`) takes the non-WRITE path (`fx/luminance.cpp:224-236`) with *no min/max clamp*. The Practical default min is 0, so a black first frame gives `exposure = scale/0`, i.e. Inf/NaN for one frame in tonemap and glow.
  **Classification:** VISUAL QUALITY ISSUE (edge case)
- **Cost** is 4 tiny dispatches, one full-res read.
  **Classification:** NOT A PROBLEM

---

### C.4 Glow / bloom (`fx/copy_effects.cpp:814-863`, `sh/copy.glsl:96-205`, `sh/tonemap.glsl:266-454`, `srr.cpp:580-673`)

**Structure (Forward+ compute path)**
- Textures: `RB_TEX_BLUR_1` is half of the blur size, with a mip chain (`rsb.cpp:520-535`). The blur size is target size when a temporal upscaler is active (`rsb.cpp:520-524`).
- Pass count: glow levels count from half resolution (`glow_levels[0]` is mip 0 = ½ res). The number of passes is `max_glow_index+1` (`srr.cpp:589-598,607-618`).
- Defaults: levels `[0, 0.8, 0.4, 0.1, 0, 0, 0]` (`Environment.cpp:1642-1649`). That gives **4 compute dispatches** at ½, ¼, ⅛ and 1/16 res; the ½-res pass runs even though its weight is 0.
- **Each pass** (one dispatch, 8×8 threads):
  - loads a 16×16 tile via 4 bilinear taps per thread, which is effectively a 2×2 box downsample of the previous level (`sh/copy.glsl:98-105`);
  - applies a separable 9-tap Gaussian in LDS, weights {.2024, .1790, .1240, .0672, .0285}, sum ≈ 1 (`:116`, `:124-180`);
  - on the first pass only: Karis-style firefly weighting before the blur and its inverse after (`:108-115,183-186`); then `×strength`, `×exposure` (including auto-exposure) (`:188-195`); then the threshold `feedback = max(smoothstep(T, T+S, max(rgb)), bloom)` and a cap (`:197-200`).
- **Composite:** in the tonemap pass, each level is sampled directly from its mip. There is **no progressive upsample chain** on desktop; the Mobile raster path has one (`srr.cpp:621-672`).
  - Sampling is bilinear or a 4-tap cubic **B-spline** (smoothing, not interpolating) (`sh/tonemap.glsl:266-359`). Bicubic is the desktop default (`rs.cpp:3778`, read at `srr.cpp:1829`).
  - Level sum × intensity (`:371-406,879`).
  - Blend modes: Add, Screen (`c + g − c·g/white`), Soft Light (post-tonemap), Replace, Mix (`c·(1−mix) + glow`) (`:408-454,884-887`).

**Defaults** (`Environment.h:166-177`)
- enabled = false, intensity 0.3, strength 1.0, mix 0.05, bloom 0.0
- blend **Screen**, `hdr_threshold` 1.0, `hdr_scale` 2.0 (so the knee runs 1→3), `luminance_cap` 12, normalized = false
- The RenderingServer-level default `glow_mix` is 0.01 (`envst.h:130`), a harmless mismatch because the scene resource pushes its own value.

**Weaknesses**
- **Not energy-conserving.**
  - Add and Screen modes add `0.3 × (0.8+0.4+0.1) = 0.39×` of the thresholded signal on top of the image.
  - The threshold multiplies the colour (`color·feedback`) instead of subtracting the knee, so energy near the threshold is scaled, not redistributed.
  - Only Mix mode with normalized levels and `bloom = 1` behaves like a PSF convolution.
  - Screen mode is nonlinear in HDR and depends on `white` (`sh/tonemap.glsl:418-436`, `envst.cpp:234-241`).

  **Classification:** VISUAL QUALITY ISSUE
- **Blur-then-threshold on the first level.** The threshold applies to the blurred ½-res value (`sh/copy.glsl:116-200`). Small bright sources that average below 1.0 after blur never bloom, and the threshold tests max(R,G,B), not luminance.
  **Classification:** VISUAL QUALITY ISSUE
- **Temporal stability of the source.**
  - With TAA, glow reads the TAA-resolved buffer: TAA copies its result into the internal texture (`fx/taa.cpp:122`) before glow (`srr.cpp:609`).
  - With FSR2/MetalFX, glow still reads `get_internal_texture` (`srr.cpp:609`), i.e. the **jittered, un-antialiased** pre-upscale image, while the tonemap composites over the upscaled image (`srr.cpp:491`). Expect bloom shimmer on sub-pixel highlights.
  - DOF is applied to the upscaled texture in that mode (`srr.cpp:519`), so glow also ignores DOF.

  **Classification:** VISUAL QUALITY ISSUE
- **`strength` compounds per level:** `color *= glow_strength` runs on every pass (`sh/copy.glsl:188`, `srr.cpp:617`).
  **Classification:** NOT A PROBLEM (1.0 default)
- **Cost:** about 0.33 full-res pixel-equivalents of compute plus the tonemap taps. Each level opens its own compute list (`fx/copy_effects.cpp:849-862`).
  **Classification:** NOT A PROBLEM (NEEDS BENCHMARK only if levels 5-7 are enabled at 4K)

---

### C.5 Anti-aliasing: TAA, FSR2, FSR1, SMAA, FXAA, MSAA, motion vectors

#### C.5.1 Jitter
- Source: Halton(2,3) values in [-1,1] divided by viewport size (NDC), so ±0.5 px (`servers/rendering/renderer_scene_cull.cpp:55-64,2678-2692`).
- Phases: **16 for TAA**, or `8·(target/render)²` for FSR2/MetalFX (`servers/rendering/renderer_viewport.cpp:269-277`).
- Index 0 is used, giving (-1,-1), a pixel-corner sample. The index is the global frame number modulo the phase count.
- Motion vectors subtract the current and previous jitter (`scene.glsl:3105-3112`).

**Classification:** NOT A PROBLEM

#### C.5.2 Motion vectors
- **Target:** RG16F velocity MRT written in the opaque color pass when TAA, an upscaler, a compositor effect or the MV debug view needs it (`fc.cpp:1806-1821,1863-1869`).
- **Computation:** the vertex shader runs **twice**, once with previous-frame data (prev transform, `prev_data.time`, previous skinned buffer, previous multimesh/particle offsets) and once for the current frame (`scene.glsl:777-816,847`). Velocity is `prev_uv − cur_uv`, unjittered (`scene.glsl:3105-3112`).
- **Coverage:**
  - skinned/blend-shape meshes are double-buffered when changed within 2 frames (`servers/rendering/renderer_rd/storage_rd/mesh_storage.cpp:1165-1194`);
  - MultiMesh uses current/previous offsets (`mesh_storage.cpp:1644-1657,1755-1787`);
  - particles via `particles_get_instance_buffer_motion_vectors_offsets` (`fc.cpp:578`).
  - All three **do** write motion vectors.
- **Transparent pass explicitly does not write MVs** (`fc.cpp:2420-2422`).
- **FSR2/MetalFX path:**
  - static geometry is drawn *without* MVs, and the velocity buffer is cleared to the sentinel (-1,-1) or, for MFX, filled from depth by `motion_vectors_store` (`fc.cpp:2221-2240`, `sh/motion_vectors_store.glsl`);
  - only dynamic objects are drawn in a "Render Motion Pass" (`fc.cpp:1154-1156,2242-2253`);
  - FSR2 reprojects the sentinel pixels with `reprojectionMatrix` (`fc.cpp:2495-2502`, `thirdparty/amd-fsr2/ffx_fsr2.h:149`).
- **The TAA path does not use this optimisation:** `using_motion_pass = … && using_upscaling` (`fc.cpp:1840`), so every opaque surface pays double vertex work.

**Classification (double VS in TAA path):** NEEDS BENCHMARK (can matter with dense open-world geometry and skinned crowds)
**Classification (no MVs for transparents):** ARCHITECTURAL LIMITATION

#### C.5.3 TAA (`fx/taa.cpp`, `sh/taa_resolve.glsl`, derived from Spartan Engine)

**Pass structure** (one compute dispatch at internal resolution, `fx/taa.cpp:50-87`):
- 8×8 groups with a 10×10 LDS tile of color and depth (`sh/taa_resolve.glsl:76-139`).
- History fetch: **Catmull-Rom 9-tap** (bilinear-optimised) (`:177-228`).
- 3×3 mean/σ **variance clipping** (`clip_aabb`) in **linear HDR RGB** (`:264-292`).
- Blend: `1/16` + off-screen (+1) + "velocity disocclusion" (`:332-343`), then an anti-flicker weight (`:352-358`).
- The blend happens in per-channel Reinhard space (`:349-364`).

**Post-resolve copies:**
- temp → internal (`fx/taa.cpp:122`)
- internal → history (`:125`)
- velocity → prev_velocity (`:126`, using a *target_size* rect on internal-size textures)

**Parameters:**
- `variance_dynamic = clamp(1.1·1080/H, 0.75, 1.0)`
- `disocclusion_threshold = 2.5` texels, `DISOCCLUSION_SCALE = 0.01` (`fx/taa.cpp:62-72`, `sh/taa_resolve.glsl:42`)
- No sharpening. History is reset only on (re)allocation (`fx/taa.cpp:97-106,120`).

**Weaknesses (all read directly from code)**
1. **Farthest-depth "closest" search.** `get_closest_pixel_velocity_3x3` keeps `depth < min_depth`, starting at 1.0 (`:145-168`). Forward+ uses reverse-Z: the depth compare is `GREATER_OR_EQUAL` (`servers/rendering/renderer_rd/forward_clustered/scene_shader_forward_clustered.cpp:359`) and depth clears to 0.0 (`fc.cpp:2129`). The search therefore selects the **farthest** neighbour.
   In addition, the velocity is loaded at `group_top_left + min_pos`. `group_top_left = gid·8 − 1` (`:379`) while `min_pos` is border-free local coordinates, so the fetch lands at `global + offset − (1,1)` (`:170`).
   **Classification:** VISUAL QUALITY ISSUE (a confirmed code defect; the impact is limited because this velocity only sizes the clip box)
2. **No dilated-velocity reprojection.** History is fetched at `uv + velocity(pixel)` (`:316-325`). At silhouettes of moving characters over the background, the background velocity is used, which produces trails.
   **Classification:** VISUAL QUALITY ISSUE
3. **Clip box collapse under motion.** `box_size = variance·smoothstep(0.02, 0.0, |v|)` (`:280`).
   - Reversed smoothstep edges are undefined in the GLSL spec.
   - At \|v\| ≥ 0.02 UV/frame (≈38 px at 1920 wide; about 90°/s yaw at 60 fps with 75° hFOV), the box degenerates to the mean, so the output ≈ 15/16·mean3x3 + 1/16·current.
   - The result is a visible blur during camera rotation. This is typical in 3rd-person mouse-look.

   **Classification:** VISUAL QUALITY ISSUE
4. **Clipping in linear HDR RGB**, not YCoCg or a tonemapped space (`:277-286`). Bright neighbours inflate σ, so the box is too loose near highlights and too tight elsewhere.
   **Classification:** VISUAL QUALITY ISSUE
5. **Anti-flicker term overrides reset factors.** `blend *= (1 − |ΔL|/1.001)²` (the denominator is always 1.001 because the values are in Reinhard space) is applied *after* the off-screen and disocclusion terms (`:333-358`). A reprojection that falls off-screen (factor 1) with a large luminance change is still blended about 25-75% from clamped edge history, which smears screen edges when panning.
   **Classification:** VISUAL QUALITY ISSUE
6. **Weak disocclusion.** A full reset needs `Δv ≥ 102.5` texels (`(Δv−2.5)·0.01`, `:306-312`). There is no depth-based test even though depth is loaded into LDS, and `prev_velocity` is read with nearest `imageLoad` at unclamped coordinates (`:307`).
   **Classification:** VISUAL QUALITY ISSUE
7. **No reactive/transparency mask.** Transparent objects are reprojected with opaque velocity (5.2), so particles, water and glass ghost. FSR2 does get a reactive mask from the color alpha (`fc.cpp:2485`, `rsb.h:263-270`).
   **Classification:** ARCHITECTURAL LIMITATION
8. **No NaN/Inf sanitisation** of the input. A single NaN poisons the 3×3 mean/σ of its neighbours (`:277-289`) and persists through history. The glow and mip copies do sanitise (`sh/copy.glsl:90-91,233-236`).
   **Classification:** VISUAL QUALITY ISSUE (only if upstream shading emits NaN)
9. **No history reset on camera cuts or teleports.** There is no API; only reallocation resets (`fx/taa.cpp:97-106`). FSR2 has the same gap (`fc.cpp:2493` FIXME).
   **Classification:** ARCHITECTURAL LIMITATION
10. **Redundant copies.** A ping-pong history would remove 2 full-res RGBA16F copies (`fx/taa.cpp:121-125`).
    **Classification:** NEEDS BENCHMARK
11. **Tight γ** (0.75-1.0, `fx/taa.cpp:62-72`): less ghosting but more residual shimmer on thin geometry such as foliage and fences.
    **Classification:** NEEDS BENCHMARK (tuning; measure with the flicker metric)

#### C.5.4 FSR2 (`fx/fsr2.cpp`, FidelityFX FSR 2.2.1 per `thirdparty/README.md:32-36`)
- Flags: `HIGH_DYNAMIC_RANGE | DEPTH_INVERTED` (`fx/fsr2.cpp:805`).
- `preExposure = 1` (`:864`). Auto-reactive and transparency/composition are off (`:852,869-874`). Reactive is the color alpha (`fc.cpp:2485`).
- Sharpness `= clamp(1 − fsr_sharpness/2)`, which is **0.9** with the default 0.2 (`fc.cpp:2481`, `rs.cpp:3770`).
- TAA is disabled when a temporal upscaler is active (`renderer_viewport.cpp:220-224`).
- **Exposure semantic mismatch.** `params.exposure = luminance->get_current_luminance_buffer()` (`fc.cpp:2465-2468`) holds the *average scene luminance*. FFX multiplies color by `Exposure()` (`thirdparty/amd-fsr2/shaders/ffx_fsr2_common.h:493-509`, `ffx_fsr2_callbacks_glsl.h:582-590`), so FSR2's internal tonemapping and lock heuristics see `color·L_avg` instead of `color·k/L_avg`. The final color cancels, but the heuristics are mis-scaled. Without auto-exposure no texture is bound, and FFX falls back to 1.0.
  **Classification:** VISUAL QUALITY ISSUE (confirmed mismatch; measure the impact)
- `reset_accumulation = false` always (`fc.cpp:2493`).
  **Classification:** ARCHITECTURAL LIMITATION

#### C.5.5 FSR1, SMAA, FXAA, MSAA
- **FSR1** runs after tonemapping on sRGB-encoded data, as EASU then RCAS in 2 compute dispatches (`fx/fsr.cpp:60-126`; `srr.cpp:864-875`).
  **Classification:** NOT A PROBLEM
- **SMAA 1x** is 3 raster passes (edge/weight/blend) after tonemapping (`fx/smaa.cpp:230,244,259`): luma edge threshold 0.05 (`rs.cpp:3749`), search 32 / diagonal 16 / corner rounding 25 (`sh/smaa_weight_calculation.glsl:85-87`), stencil-masked. There is no T2x. It stacks on TAA without conflict.
  **Classification:** NOT A PROBLEM
- **FXAA** runs inside tonemap before glow and tonemapping (see §2).
  **Classification:** VISUAL QUALITY ISSUE
- **MSAA:**
  - Color is resolved with a hardware box average of HDR values (`fc.cpp:2438`), which is not a tonemapped resolve.
  - Depth is resolved by **averaging samples** (`sh/resolve.glsl:42-49`), creating false depths at edges that feed SSAO, SSR, DOF and TAA.
  - The GI resolve picks a representative sample correctly for reverse-Z (`sh/resolve.glsl:114-115`).
  - Default: MSAA off (`rs.cpp:3743`).

  **Classification:** VISUAL QUALITY ISSUE (only if MSAA is used)

---

### C.6 SSAO and SSIL (`fx/ss_effects.cpp`, `sh/ssao*.glsl`, `sh/ssil*.glsl`)

**Shared setup**
- The effects run in `_pre_opaque_render` after shadows and GI, before the opaque color pass (`fc.cpp:1614-1642`).
- **SSAO requires the depth prepass** (`fc.cpp:2118`); it is on by default except on the vendors listed at `rs.cpp:3715-3716`.
- Depth: a linear depth pyramid, deinterleaved into 4 slices, R16F, at half size with up to 4 mips (`fx/ss_effects.cpp:502-624`).

**SSAO algorithm**
- An Intel **ASSAO** port (`sh/ssao.glsl:26-68`).
- Quality mapping `quality = max(0, q−1)` (`fx/ss_effects.cpp:1201`). Taps per quality (`sh/ssao.glsl:38-40`), each tap = 2 samples:
  - Very Low: 3 taps, 2 of 4 slices only (`fx/ss_effects.cpp:1066`)
  - Low: 3
  - **Medium (default): 5**
  - High: 12
  - Ultra: adaptive, 5 base + up to 27, using an importance map with 3 extra dispatches (`:1252-1305`)
- Buffer resolution: with `half_size = true` (default, `rs.cpp:3726`) each slice is ¼×¼ of full, so the effective result is ½ res (`fx/ss_effects.cpp:1101-1105`).
- Blur: 2 edge-aware "smart" passes by default, 4 dispatches each (`:1318-1375`, `rs.cpp:3728`).
- Interleave: full-res smart upsample (`:1378-1414`).
- Distance fade from 50 m to 300 m (`rs.cpp:3729-3730`, `fx/ss_effects.cpp:1191-1192`).
- **Environment defaults:** radius 1.0, intensity 2.0, power 1.5, detail 0.5, horizon 0.06, sharpness 0.98, light/AO-channel affect 0 (`Environment.h:132-140`).

**SSIL algorithm**
- Same framework, but samples the **previous frame's HDR color at mip 5** (1/32 res). It uses camera-only reprojection (`sh/ssil.glsl:204-214`; the matrix is built at `fc.cpp:1467-1479`).
- Defaults: 4 blur passes (`rs.cpp:3735`), radius 5 m, intensity 1, normal rejection 1 (`Environment.h:144-148`).
- Composite: `ambient *= 1 − a; ambient += ssil·albedo` in the forward shader (`scene.glsl` around 2192-2195).

**Weaknesses**
- **No temporal accumulation or rotation.** The sample rotation matrices are built once in the constructor (`fx/ss_effects.cpp:72-99`) and indexed by pixel parity (`sh/ssao.glsl:296-297`). The noise is static, so TAA cannot average it. The result at Medium is 10 samples per ½-res pixel, blurred.
  **Classification:** VISUAL QUALITY ISSUE
- **SSIL lags one frame and is very low frequency.** It reads 1/32-res last-frame color with camera-only reprojection, so moving emitters and objects lag and their bounce colour smears. It feeds back into itself because the last frame already contains SSIL.
  **Classification:** VISUAL QUALITY ISSUE
- **Dispatch count:** SSAO at defaults is 1 downsample + 4 gathers + 8 blurs + 1 interleave, in one compute list. SSIL adds 4 blur passes × 4 slices and the last-frame 6-mip build (`fx/ss_effects.cpp:366-421`).
  **Classification:** NEEDS BENCHMARK

---

### C.7 SSR (`fx/ss_effects.cpp:1424-1728`, `sh/screen_space_reflection*.glsl`)

**Algorithm (rewritten vs. 4.3)**
- Runs **pre-opaque** from depth-prepass depth and normals plus the **previous frame's** color, with camera-only reprojection (`fc.cpp:1483-1508,1639-1641`; the last-frame copy is taken after the transparent pass, `fc.cpp:2449-2454`).
- Passes:
  1. (half-size) depth and normal downsample (`:1503-1547`)
  2. **Hi-Z max-pyramid** (closest under reverse-Z), one dispatch per mip, each in its own compute list (`:1560-1609`, `sh/screen_space_reflection_hiz.glsl:26-41`)
  3. the trace
  4. a "roughness filter" mip chain of the SSR result: 7-tap Gaussian weighted by validity, one dispatch per mip (`:1658-1686`, `sh/screen_space_reflection_filter.glsl`)
  5. (half-size) bilateral resolve to full res (`:1688-1727`)
- **Trace:**
  - one mirror ray per pixel (no stochastic/GGX sampling);
  - parametric Hi-Z stepping, `num_steps` = `ssr_max_steps` (default 64);
  - depth tolerance 0.5;
  - validity from hit confidence, screen-edge margin, and fade-in/fade-out on screen ray length (`sh/screen_space_reflection.glsl:97-329`).
- **Roughness:** rays are skipped at roughness ≥ 0.7 (`:118-129`), and the scene shader fades between 0.6 and 0.7 (`scene.glsl:2224`). Blur is emulated by picking a mip from a cone-fit radius (`:294-324`).
- **Composite** in the forward shader: `indirect_specular = indirect_specular·(1−a) + ssr` (`scene.glsl:2197-2228`).
- **No temporal accumulation** inside SSR.
- Defaults: max steps 64, fade in 0.15, fade out 2.0, depth tolerance 0.5 (`Environment.h:124-128`); `half_size` true (`rs.cpp:3781`). `environment_set_ssr_roughness_quality` is a deprecated no-op (`fc.cpp:4009-4011`).

**Weaknesses**
- **Half-res upsample loses edge awareness at distance.** Its weights use `exp(−|d−d_half|·2048)` on *raw* reverse-Z depth (`sh/screen_space_reflection_resolve.glsl:42-47`). Raw depth differences shrink roughly as 1/z², so edge rejection vanishes for distant geometry and reflections bleed across silhouettes. This matters for far water or wet roads in an open world.
  **Classification:** VISUAL QUALITY ISSUE
- **One-frame latency and camera-only reprojection.** Reflections of moving characters and vehicles lag and ghost, and the source is pre-TAA and jittered.
  **Classification:** ARCHITECTURAL LIMITATION
- **Rough reflections are a mip blur, not sampled.** There is a hard cut at 0.7 roughness and pyramid artifacts noted in code (`:321-324`).
  **Classification:** VISUAL QUALITY ISSUE
- **Many small dispatches.** Hi-Z and filter each have about 10 mips, each in its own compute list.
  **Classification:** NEEDS BENCHMARK (the RD graph may overlap them; see §12 on profiling)

---

### C.8 Volumetric fog (`env/fog.cpp`, `shenv/volumetric_fog_process.glsl`, `shenv/volumetric_fog.glsl`)

**Grid**
- Width/height follow the aspect ratio: `volume_size·(w/((w+h)/2))` (`fc.cpp:1331-1334`). Depth is `volume_depth`.
- Defaults: size 64, depth 64, filter on (`rs.cpp:3798-3800`). At 16:9 that is **81×50×64 ≈ 259k froxels**.
- Slices are distributed as `z^detail_spread` over `volumetric_fog_length`, with spread 2.0 and **length 64 m** (`Environment.h:209-210`, `shenv/volumetric_fog_process.glsl:319,339`).
- Storage: RGBA16F 3D textures plus R32UI atomic density/light/emissive maps (`env/fog.cpp:462-517`).

**Passes** (`env/fog.cpp:568-1229`)
1. Per-FogVolume density dispatches.
2. **Density + lighting** over the whole 3D grid, in one dispatch (`:1174-1184`). Per froxel:
   - directional lights: one shadow-atlas tap with an exponential falloff (`volumetric_fog_process.glsl:399-440,432-433`);
   - clustered omni/spot lights;
   - GI/SDFGI and sky injection;
   - temporal reprojection with a 16-entry Halton jitter shared by all froxels (`:274-290,329-355`), blend 0.9 (`Environment.h:214-215`).
3. History copy, a 3D dispatch (`env/fog.cpp:1186-1192`).
4. 2 separable filter dispatches, X then Y (`:1194-1216`).
5. Integrate: a 2D dispatch marching depth (`:1218-1224`).

The result is applied per pixel in the forward shader by sampling the 3D texture with trilinear filtering (`scene.glsl:1091-1099,1499-1512`) and clamps beyond `length`.

**Weaknesses**
- **No history rejection.** Reprojection accepts any in-bounds sample and blends 0.9 (`:341-345`). Moving lights and shadows smear across about 10 frames. Jitter is the same for every froxel, so there is no per-cell decorrelation.
  **Classification:** VISUAL QUALITY ISSUE
- **Coverage vs. resolution.** The 64 m default leaves distant fog to the analytic exponential/height fog. Stretching the length to hundreds of metres makes froxels huge, causing blocky shafts and light leaking; this is inherent to the froxel approach.
  **Classification:** ARCHITECTURAL LIMITATION
- **Cost:** 259k froxels × (lights + shadows) + 4 full-grid passes.
  **Classification:** NEEDS BENCHMARK (scales with light count in view)

---

### C.9 Sky, reflections, GI (summary)

- **Sky**
  - Radiance maps are **octahedral** in 4.7: `*_octmap` copy/filter/roughness shaders (`env/sky.cpp:281-481`).
  - Reflection probes render a cubemap and convert it to an octmap (`servers/rendering/renderer_rd/storage_rd/light_storage.cpp:1840`).
  - Modes: Automatic → Realtime if the shader uses TIME/POSITION and radiance size is 256; Incremental if it uses lights, sun scatter or uniforms; otherwise Quality (`env/sky.cpp:1002-1011`).
  - Project settings: roughness layers 8, GGX samples 32 (`rs.cpp:3696-3700`); reflection atlas 256 px with 64 probes (`rs.cpp:3702,3704`).
  - The sky is drawn after the opaque pass (`fc.cpp:2296-2306`). Optional ½ and ¼-res sky passes (`env/sky.cpp:73-79`).
  - Dynamic time of day re-filters radiance every frame in Realtime mode.

  **Classification:** NEEDS BENCHMARK
- **GI**
  - SDFGI, VoxelGI and lightmaps. All are off unless enabled: `sdfgi_enabled = false` (`Environment.h:153`).
  - SDFGI: 4 cascades, 128³ cascade size (`servers/rendering/renderer_rd/environment/gi.h:562`), 16 rays, 30 frames to converge (enum index 5), `use_half_resolution = false` (`rs.cpp:3707,3793-3794`, `env/gi.cpp:3810`).
  - The GI gather runs pre-opaque, in parallel with shadows (`fc.cpp:1582-1612`).

  **Classification:** ARCHITECTURAL LIMITATION (out of scope here; audit separately)

---

### C.10 Specular AA, SSS, DOF, specular merge

- **Roughness limiter.**
  - Runs in-shader in the forward pass: Tokuyoshi–Kaplanyan "Improved Geometric Specular AA" using `dFdx/dFdy` of the *final* normal. `variance = amount·(|dn/dx|²+|dn/dy|²)` and `r² += min(2·variance, limit)` (`scene.glsl:1657-1672`).
  - Defaults: enabled, amount 0.25, limit 0.18 (`rs.cpp:3745-3747`).
  - Quad derivatives cannot see sub-pixel normal-map variance. The import-time "roughness from normal map" (`editor/import/resource_importer_texture.cpp:246,717`) is the complement for that.
  - The compute `RoughnessLimiter` class (`fx/roughness_limiter.cpp`) is **never included or instantiated** anywhere; it is dead code.

  **Classification:** VISUAL QUALITY ISSUE (sub-pixel normal-map sparkle with TAA off)
- **Subsurface scattering.**
  - A Jimenez-style separable screen-space blur with 2 full-res compute dispatches (`fx/ss_effects.cpp:1745-1811`). Kernels are 11/17/25 samples (`sh/subsurface_scattering.glsl:9-88`); the default is Low = 11 (`rs.cpp:3783`).
  - Active only if a material uses SSS, which forces the separate-specular MRT and a raster "Merge Specular" pass (`fc.cpp:1960-1971,2335-2348`).
  - The `MODE_SSR` variant of `specular_merge.glsl` is unused in Forward+ because the reflection argument is `RID()` (`fc.cpp:2346`).

  **Classification:** NOT A PROBLEM
- **DOF.**
  - A gather bokeh in compute: generate a CoC ("blur size") pass, then box/hexagon as 2 directional passes (6/12/12/24 steps), or circle (`fx/bokeh_dof.cpp:109-240`, `quality_samples` at `:184`).
  - Very Low / Low run at half size plus a composite (`:188-197`).
  - Defaults: hexagon, Low, no jitter (`rs.cpp:3721-3723`).
  - It runs **after TAA/FSR2 and before exposure/glow** (`srr.cpp:503-552`). There is no temporal handling of near-field bleed.

  **Classification:** VISUAL QUALITY ISSUE (half-res, no temporal; typical for gather DOF)

---

### C.11 Frame order in Forward+ with GPU timestamp labels

Main viewport, from `RenderForwardClustered::_render_scene` (`fc.cpp:1704-2570`) and `_render_buffers_post_process_and_tonemap` (`srr.cpp:455-898`). `RENDER_TIMESTAMP` strings are quoted.

1. `"Prepare 3D Scene"` (`fc.cpp:1726`): SDFGI update, cluster builder, VRS.
2. `"Setup 3D Scene"` (`:1778`): MV/AA decisions and render lists; label `Render Setup`.
3. `"Setup Sky"` (`:2071`): sky UBO, radiance update, ½/¼-res sky buffers.
4. `"Render Depth Pre-Pass"` or `"GI + Render Depth Pre-Pass (Parallel)"` (`:2123-2125`), depth + normal/roughness (+VoxelGI). Then `"Resolve Depth Pre-Pass (MSAA)"` (`:2146`).
5. `"Process Pre Opaque Compositor Effects"` (`:2172`).
6. `_pre_opaque_render` (`:1517-1682`):
   - `"Setup Shadows"`
   - `"Render OmniLight Shadows"`
   - `"Render GI + Render Directional/SpotLight Shadows (Parallel)"`, `"Render Directional/SpotLight Shadows"` or `"Render GI"`
   - `"Prepare Depth for SSAO/SSIL"`
   - `"Process SSAO"`
   - `"Process SSIL"`
   - `"Process SSR"`
   - `"Pre Opaque Render"` (cluster bake, light/decal buffers)
   - `"Update Volumetric Fog"`, containing `"> Volumetric Fog"`, `"Render FogVolumes"`, `"Render Fog"`, `"Filter Fog"`, `"Integrate Fog"`, `"< Volumetric Fog"` (`env/fog.cpp:572-1228`)
7. `"Render Opaque Pass"` (`fc.cpp:2188`): color + separate specular + velocity MRT.
8. FSR2/MFX only: velocity clear or `motion_vectors_store`, then `"Render Motion Pass"` (`:2229-2253`).
9. `"Process Post Opaque Compositor Effects"` (`:2269`).
10. `"Render Sky"` (`:2297`).
11. `"Resolve MSAA"` (`:2309`, conditional).
12. `"Process Post Sky Compositor Effects"` (`:2330`).
13. `"Sub-Surface Scattering"` (`:2337`), then `"Merge Specular"` (`:2345`) if separate specular is in use.
14. `"Copy Screen Texture"` (+mip chain) and `"Copy Depth Texture"` (`:2367,2378`), if any material uses them.
15. `"Process Pre Transparent Compositor Effects"` (`:2407`).
16. `"Render 3D Transparent Pass"` (`:2411`), with no MVs.
17. `"Resolve"` (`:2431`): MSAA color, depth, velocity.
18. `"Copy Final Framebuffer (SSIL/SSR)"` (`:2451`): last-frame copy plus mips for next frame's SSR/SSIL.
19. `"Process Post Transparent Compositor Effects"` (`:2457`).
20. `"FSR2"` (`:2471`), or MetalFX, or `"TAA"` (`:2539`).
21. `"Tonemap"` (`:2548`). This outer label actually spans the whole post chain:
    - `"Depth of Field"` (`srr.cpp:504`)
    - `"Auto exposure"` (`:557`)
    - `"Glow"` (`:581`)
    - `"Tonemap"` (`:676`; includes FXAA, glow composite, BCS/LUT, dither)
    - `"SMAA"` (`:820`)
    - spatial upscaler (`"FSR 1.0 Upscale"`, `fx/fsr.h:45`) or `"3D Viewport Scaling"` copy (`srr.cpp:864-895`)
22. Debug draws (`fc.cpp:2553-2569`).

Notes:
- `"Tonemap"` appears twice (`fc.cpp:2548`, `srr.cpp:676`), so profiler rows need care.
- Capturing timestamps makes each timestamp a **graph fence**: every command since the previous timestamp becomes adjacent to it (`servers/rendering/rendering_device_graph.cpp:375-386`). Profiling therefore removes RD-graph reordering and async overlap such as "GI + shadows parallel".

**Classification:** NOT A PROBLEM (profiling caveat)

---

### C.12 Isolated-experiment candidates (ranked)

**Common test harness (applies to all candidates)**
- Deterministic camera and animation paths at a fixed timestep (`--fixed-fps`), captured losslessly with Movie Maker `--write-movie out.png` (`main/main.cpp:629,1961`).
- GPU totals via `RenderingServer.viewport_set_measure_render_time` / `viewport_get_measured_render_time_gpu` (`rs.cpp:2908,2911`). Per-pass breakdown via RGP or Nsight using the existing `draw_command_begin_label` names; avoid in-engine timestamps (§11).
- Reference images: 64-frame accumulated jittered supersample with TAA off, or 4× render scale.
- Metrics:
  - FLIP / SSIM vs. reference
  - **temporal flicker:** mean |Δ| between consecutive frames of a static camera, luminance in PQ or sRGB space
  - **ghost length:** pixels behind a moving object whose error exceeds a threshold
  - **motion sharpness:** MTF or edge-slope on a slanted-edge chart during a constant-rate yaw

Global-toggle precedent to copy: `glow_bicubic_upscale` = a ProjectSetting (`rs.cpp:3778`) read in `RendererSceneRenderRD::init` (`srr.cpp:1829`) plus a runtime setter (`srr.cpp:82-84`, `rs.cpp:3083`).

1. **TAA resolve v2** (highest value for 3rd-person camera motion; fully contained)
   - **Toggle:** ProjectSetting `rendering/anti_aliasing/quality/taa_mode` (legacy / fellow), read like `glow_bicubic_upscale`. Pass it into `TAA::process`, or add a second shader variant in `TAA::TAA()` (`fx/taa.cpp:39-44`) and select it by push constant.
   - **Files:** `fx/taa.cpp/.h`, `sh/taa_resolve.glsl`, `rs.cpp`, `srr.cpp`, `fc.cpp:2537-2541` (call site).
   - **Changes:**
     - reverse-Z closest depth with a correct fetch offset;
     - reproject with the dilated velocity;
     - YCoCg (or tonemapped) variance clip, with γ≈1.0-1.25 that does not collapse with speed;
     - depth-based disocclusion using previous depth;
     - apply anti-flicker only to the base term;
     - NaN guard;
     - ping-pong history (removes 2 copies, `fx/taa.cpp:121-125`);
     - optional RCAS;
     - `reset_history()` API.
   - **Tests:**
     - yaw sweep at 45/90/180°/s: motion-sharpness metric;
     - character strafing in front of a high-contrast background: ghost length;
     - static foliage: flicker;
     - GPU ms of the `TAA` label.
2. **Auto-exposure v2** (a correctness fix plus metering)
   - **Toggle:** a `CameraAttributes` property (a new enum `auto_exposure_mode`) through `camera_attributes_set_auto_exposure`, or a ProjectSetting. Branch in `Luminance::luminance_reduction` (`fx/luminance.cpp:159-257`).
   - **Files:** `fx/luminance.cpp/.h`, `sh/luminance_reduce.glsl` (or a new `luminance_histogram.glsl`), `srr.cpp:556-578`, `scene/resources/camera_attributes.*`.
   - **Changes:**
     - ceil-sized levels or weighted partial blocks (fixes finding E);
     - log2-luminance (Rec.709 Y) histogram with low/high percentile rejection and optional centre weight;
     - adaptation in EV with `1−exp(−Δt·speed)`, separate up/down speeds;
     - clamp on reset.
   - **Tests:**
     - identical scene at 1080p/1440p/4K must give the same exposure (read back the 1×1 texture);
     - sky↔ground and tunnel-exit sweeps: log-exposure trajectory, overshoot, settle time;
     - frame-hitch injection (Δt = 0.5 s) must not oscillate.
3. **Bloom v2** (energy-conserving, stable)
   - **Toggle:** a new `Environment.glow_blend_mode` value or a ProjectSetting `rendering/environment/glow/algorithm`. Branch at `srr.cpp:580-673`.
   - **Files:** `fx/copy_effects.cpp/.h` (a new `bloom_downsample/upsample`), a new `sh/bloom.glsl` or a `copy.glsl` mode, `sh/tonemap.glsl` `gather_glow`, `Environment.*`.
   - **Changes:**
     - 13-tap (Jimenez 2014) downsample with Karis average on mip 0, tent upsample-accumulate chain;
     - `lerp(scene, bloom, k)` composite; no threshold (or a luminance soft-knee *before* blur);
     - under FSR2, source the upscaled texture (fixes finding H).
   - **Tests:**
     - energy: Σ luminance before and after with `k` (should match within 1%);
     - a 1-px bright light moving 0.1 px/frame: bloom flicker metric;
     - GPU ms of the `Glow` label.
4. **Tonemap/grade fixes** (trivial risk; single shader)
   - **Toggle:** a push-constant flag bit in `fx/tone_mapper.cpp:126-171`.
   - **Files:** `sh/tonemap.glsl`, `fx/tone_mapper.cpp/.h`, `envst.cpp` (default white policy).
   - **Changes:**
     - LUT half-texel remap;
     - optional FXAA after tonemap (needs a separate pass or LDR input);
     - warn or clamp when `white ≤ 1` for Filmic/ACES/Reinhard.
   - **Tests:**
     - identity 16³/32³/64³ LUT round-trip on a gradient chart: max |error| < 0.5/255;
     - HDR ramp chart per operator: verify roll-off above 1.0.
5. **SSR resolve plus temporal**
   - **Toggle:** ProjectSetting `rendering/environment/screen_space_reflection/temporal` and `…/linear_depth_upsample` via `SSEffects::ssr_set_*` (`fx/ss_effects.cpp:1424-1426`).
   - **Files:** `fx/ss_effects.cpp/.h`, `sh/screen_space_reflection_resolve.glsl`, a new `sh/screen_space_reflection_temporal.glsl`.
   - **Changes:** linear-depth bilateral weights (fixes finding K); reprojected history with neighbourhood clamp.
   - **Tests:**
     - distant water or wet road with silhouettes at 50-500 m: edge bleed via FLIP vs. full-res SSR;
     - static-camera flicker.
6. **SSAO temporal / GTAO**
   - **Toggle:** ProjectSetting `rendering/environment/ssao/algorithm` via `ssao_set_quality` (`fx/ss_effects.cpp:1045-1052`).
   - **Files:** `fx/ss_effects.cpp/.h`, `sh/ssao*.glsl` (or a new `gtao.glsl`); the composite is unchanged.
   - **Changes:** per-frame rotation (frame index in push constant) plus temporal accumulation, or GTAO with a visibility bitmask.
   - **Tests:** static-scene noise (σ of AO over 60 frames); ghosting behind a moving character; GPU ms of the `Process SSAO` label.
7. **FSR2 integration fixes**
   - **Toggle:** none needed (bug fix), or a ProjectSetting for the reset heuristic.
   - **Files:** `fc.cpp:2461-2507`, `fx/fsr2.cpp`, and a tiny compute pass that writes `scale/L_avg` into an R32F 1×1.
   - **Changes:** pass a true exposure; set `reset` on camera cut or teleport (new `Camera3D`/RS API); optionally enable auto-reactive.
   - **Tests:** tunnel-exit ghosting and lock artifacts with auto-exposure on vs. off; cut-scene cut: frames until error < threshold.
8. **Volumetric fog history rejection and decorrelated jitter**
   - **Toggle:** an `Environment` flag next to `volumetric_fog_temporal_reprojection_*` (`Environment.h:214-215`).
   - **Files:** `shenv/volumetric_fog_process.glsl` (`:274-355`), `env/fog.cpp` (params UBO).
   - **Changes:** per-froxel blue-noise offset on the Halton index; clamp history against the current froxel's neighbourhood, or luminance-difference rejection.
   - **Tests:** moving spotlight through fog: trail length; static flicker; GPU ms of `Render Fog`.
9. **Camera-only MVs for static geometry in the TAA path** (perf; less isolated)
   - **Toggle:** ProjectSetting `rendering/anti_aliasing/quality/taa_static_mv_from_depth`.
   - **Files:** `fc.cpp:1840` (`using_motion_pass` also true for TAA), `fc.cpp:2229-2240` (use `motion_vectors_store` like the MFX path), `sh/taa_resolve.glsl` (no sentinel needed if the store pass fills velocity).
   - **Tests:** GPU ms of `Render Opaque Pass` + `Render Motion Pass` with TAA on, in a dense static scene and a skinned crowd; MV equivalence (max |Δv| vs. the legacy path on static geometry < 1e-3 px).

**Hard to isolate (architectural):**
- SSR and SSIL use previous-frame color and camera-only reprojection (they run pre-opaque).
- There are no MVs or reactive mask for transparents in TAA.
- There is no HDR10/PQ output path.
- Volumetric fog is limited to froxels.
- The MSAA depth resolve averages samples.

These need cross-pass changes in `fc.cpp` and are not good first experiments.
