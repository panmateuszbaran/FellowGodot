# FellowEngine — milestone 2 report

Status: **DRAFT** — performance final (day-state session `fellow-m2_perf`); ground-truth image metrics and the
`fellow17` load-time check pending.

Base: Godot **4.7.2-stable** · Branch `fellow/geometry` (contains `fellow/shadows`) · Measured binary `fellow16`
(`0ba5e35`) · Machine: Acer Predator PHN16S-71, Core Ultra 9 275HX, RTX 5070 Ti Laptop 12 GB, Windows 11, Vulkan.

Decision records: `docs/decisions/0001` (TAA, milestone-2 follow-up), `0004` (directional shadow cache), `0005`
(shadow sampling), `0006` (temporal SSAO, parked, branch `fellow/ssao`), `0007` (mesh clusters).

## 1. Result

Milestone 1 named directional shadows the largest measured pass of the geometry-heavy scene and listed opaque
shading, alpha-blended foliage and the TAA foliage regression as the next targets. Milestone 2 addressed the shadow
maps, the shadow lookups, the geometry submitted by large meshes, and the TAA regression; every change is exact
(image-identical within the GPU's run-to-run variation) except the TAA change, which is measured against the ground
truth, and two optional look changes that stay off by default.

| Change | Decision | Effect (per-pass GPU timestamps, 1080p) |
|---|---|---|
| Directional shadow static cache (0004) | KEEP | Sponza shadow pass 3.40 → 0.28 ms; Bistro exterior 0.52 → 0.26 ms; render-thread CPU −42 % |
| Shadow sampling skips: N·L = 0 pixels, unblended cascade (0005) | KEEP | opaque pass −0.07 to −0.29 ms |
| Mesh clusters: frustum culling inside large surfaces (0007) | KEEP | Sponza depth pre-pass −43 %, opaque −22 % (−1.2 ms GPU) |
| TAA v2 motion soften (0001 follow-up) | KEEP | foliage regression fixed; ≥ stock on 8/8 sequences |
| PCSS blend band, parallel-light penumbra (0005) | optional (look) | opaque −0.18 to −0.40 ms (band); penumbra fixes dissolving cascade-0 shadows |
| Max-pyramid early-out (0005) | REJECT (v1) | exact, no net gain; v2 with a fully-shadowed early-out under test |
| Temporal SSAO (0006) | REJECT / parked | no iso-quality saving |

Sum of timestamped passes, stock → FellowEngine configuration (night profiles, relative): Sponza 11.8 → 7.2 ms
(−39 %), Bistro exterior 5.0 → 4.4 ms (−12 %), Bistro interior 3.7 → 3.6 ms (−3 %), PBR Lab 2.4 → 2.3 ms (within
noise).

### Frame level, day state (`fellow-m2_perf`, 2026-10-01 07:01-09:34, GPU 1.7-1.85 GHz, 71-76 °C)

Fresh process per run, ABBA-interleaved, 10 s warm-up + 30 s flythrough, mean ± standard deviation of the per-run
means (number of runs; the runner adds runs when CV > 3 %), Welch's t-test vs stock. **FellowEngine = the default
configuration** of `fellow16`; null test = the same binary with `rendering/fellow/enabled=false`; look changes = PCSS
blend band + parallel-light penumbra on top (optional, not default).

| Scene | Res | stock frame ms | FellowEngine (`fellow_m2`) | Δ | p | null test | p99 ms | 1 % low fps | + look changes |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|
| PBR Lab | 1920x1080 | 3.07 ± 0.02 (3) | 2.96 ± 0.03 (3) | **-3.4 %** | 0.009 | +0.2 % | 3.9 → 3.6 | 247 → 259 | 2.83 (-7.8 %) |
| Sponza | 1920x1080 | 13.93 ± 0.08 (3) | 8.63 ± 0.03 (3) | **-38.1 %** | 0.000 | -2.0 % | 21.4 → 12.4 | 46 → 77 | 7.89 (-43.4 %) |
| Bistro exterior | 1920x1080 | 5.75 ± 0.21 (6) | 4.96 ± 0.04 (6) | **-13.7 %** | 0.000 | -1.8 % | 6.7 → 5.5 | 147 → 176 | 4.58 (-20.4 %) |
| Bistro interior | 1920x1080 | 4.56 ± 0.03 (3) | 4.41 ± 0.04 (3) | **-3.3 %** | 0.011 | +0.7 % | 5.2 → 5.0 | 185 → 192 | 4.39 (-3.7 %) |
| PBR Lab | 2560x1440 | 4.68 ± 0.04 (3) | 4.58 ± 0.08 (3) | **-2.3 %** | 0.121 | – | 6.0 → 5.6 | 160 → 171 | – |
| Sponza | 2560x1440 | 18.62 ± 0.15 (3) | 12.98 ± 0.11 (3) | **-30.3 %** | 0.000 | – | 26.7 → 18.3 | 37 → 54 | – |
| Bistro exterior | 2560x1440 | 9.15 ± 0.20 (3) | 8.07 ± 0.03 (3) | **-11.8 %** | 0.010 | – | 10.6 → 9.1 | 92 → 106 | – |
| Bistro interior | 2560x1440 | 7.51 ± 0.04 (3) | 7.11 ± 0.02 (3) | **-5.3 %** | 0.001 | – | 8.5 → 8.0 | 116 → 119 | – |

GPU time (`gpu_ms`) moves the same way: Sponza 13.54 → 8.23 ms (−39.2 %), Bistro exterior 5.37 → 4.59 (−14.6 %),
Bistro interior 4.19 → 4.03 (−3.9 %), PBR Lab 2.63 → 2.54 (−3.4 %) at 1080p. The null test stays within noise in
every scene (−2.0 to +0.7 %, p > 0.2): the FellowEngine binary with its features off is stock.

Costs: **VRAM +52 MB** where a sun casts shadows (the directional shadow cache: Sponza 2656 → 2717 MB, Bistro
exterior 2212 → 2273, PBR 319 → 380 incl. TAA ping-pong +9 MB); none in Bistro interior. **Load time: Sponza
3.6 → 7.6 s** with `fellow16`, whose cluster builds still ran during loading (+2.8 s in the night measurement, +4.0 s
here); the deferred build that never blocks the render thread (`1a7fc2e`, decision 0007) is in `fellow17` and its
load times are measured in `m3_mm2_profile` — *pending*. Other scenes: +0.06 to +0.18 s.

## 2. The four questions

* **Does FellowEngine render better than stock Godot?** Yes, in anti-aliasing: closer to the ground truth in **8/8
  motion sequences** by PSNR and SSIM (+0.1 to +2.3 dB), including the alpha-tested foliage strafe that regressed in
  milestone 1. Everything else is image-identical to stock within the GPU's run-to-run variation (shadow cache,
  sampling skips, mesh clusters: §4). Fixed cameras against the ground truth: *pending*.
* **Does it run faster at equivalent quality?** **Yes.** Frame time at 1920×1080, day state, interleaved runs:
  Sponza **−38 %** (13.9 → 8.6 ms; p99 21.4 → 12.4 ms), Bistro exterior **−14 %**, Bistro interior **−3 %**, PBR Lab
  **−3 %** (all p < 0.02); at 2560×1440 −30 %, −12 %, −5 % and −2 % (PBR not significant). Costs: +52 MB VRAM where a
  sun casts shadows; Sponza loaded 4 s slower with the measured binary (cluster builds during loading), which the
  deferred build in `fellow17` removes — *verification pending*.
* **Does it produce better image quality at equivalent performance?** Yes: the TAA improvement comes with a faster
  frame, not at a cost.
* **Can we prove those claims with reproducible measurements?** Yes, with the limits of milestone 1 (one machine, one
  GPU vendor, 3-6 runs per configuration): every number above comes from a named session whose raw data is in
  `benchmarks/results/` (`fellow-m2_perf`, `fellow-m2_profile`, `fellow-m2_temporal`, the identity sessions of §4),
  produced by the commands in the decision records; the null test of the same binary stays within noise.

## 3. Per-pass evidence (night profiles; ±10 % machine noise, relative per-pass deltas reliable)

* Shadows: `benchmarks/results/m2_profile7_table.md` (stock / cache / cache without gate / sampling skips / blend band /
  cache + skips) and `m2_mm_profile_table.md` (max pyramid).
* Geometry: `benchmarks/results/m3_cl_profile_table.md` (stock / clusters / milestone-2 shadows / both) and
  `m3_geo_sponza_table.md`, `m3_geo_bistro_table.md` (stock with coarser LOD thresholds and without scene parts —
  the depth pre-pass, the opaque pass and the alpha-blended foliage are geometry-bound in Sponza).

## 4. Image verification

| Comparison | Result |
|---|---|
| stock vs shadow cache (`m2_verify7`, 60-frame sequences, pre-roll 200) | 67.8-94.6 dB, ≤ 0.003 % pixels > 2/255 |
| stock vs cache + sampling skips | 67.8-94.7 dB, ≤ 0.003 % |
| FellowEngine without vs with mesh clusters (`m3_cl_temporal`) | Sponza 80.4 dB, Bistro ext. 101.2, Bistro int. 69.3, max diff ≤ 5/255 (PBR null test: 67.8-82.1 dB) |
| mesh clusters, every variant on lavapipe (stress setting: every surface ≥ 256 triangles in 64-triangle clusters) | 0 differing pixels |
| PCSS blend band (`m2_verify4`) | 36.3-56.1 dB, up to 5.7 % pixels (look change) |
| TAA motion soften vs ground truth (`m2_taa_sweep3`) | ≥ stock in PSNR and SSIM on 8/8 sequences; mean PSNR stock 34.00, v2 35.00, chosen 35.05 dB |

### Motion sequences against the ground truth (`fellow-m2_temporal`, 60 frames each, day state)

Every frame of 8 sequences compared with the accumulated ground truth (2×2 SSAA × 8× MSAA × jitter; milestone-1
method), FellowEngine configuration vs stock (`benchmarks/results/fellow-m2_temporal_table.md`):

| sequence | PSNR stock → Fellow | SSIM stock → Fellow | motion-region error | flicker |
|---|---:|---:|---:|---:|
| camera_pan | 36.11 → **37.57** | 0.9749 → **0.9822** | −12 % | −12 % |
| moving_object | 37.72 → **38.41** | 0.9813 → **0.9849** | −14 % | +21 % (0.0006 → 0.0007) |
| thin_dolly | 32.09 → **34.09** | 0.9513 → **0.9673** | −16 % | −19 % |
| specular_orbit | 31.62 → **31.73** | 0.9598 → **0.9606** | +3 % | +2 % |
| foliage_strafe | 31.50 → **32.17** | 0.9391 → **0.9468** | +0.5 % | −0.3 % |
| sponza_pan | 38.49 → **39.09** | 0.9437 → **0.9576** | −7 % | −11 % |
| bistro_ext_path | 28.86 → **31.20** | 0.8998 → **0.9209** | −9 % | −7 % |
| bistro_int_path | 35.60 → **36.17** | 0.9641 → **0.9647** | +2 % | +1 % |

FellowEngine is closer to the ground truth in **8/8 sequences by PSNR and SSIM** (+0.1 to +2.3 dB). The milestone-1
regression (`foliage_strafe`, −0.4 dB, +6 % motion error) is gone (+0.67 dB, +0.5 % motion error). Weaker spots,
kept visible: slightly more error in motion regions in `specular_orbit` (+3 %) and `bistro_int_path` (+2 %), more
error in static regions of the Bistro paths (+5 to +8 %, the history keeps a little more of the jittered aliasing
there), and more flicker in `moving_object` (from a very low base). The look changes score below the default in
7/8 sequences by PSNR (equal in the eighth): they sharpen penumbrae, and the ground truth is rendered with stock's
shadow formula, so this measures difference from stock's look rather than error.

Fixed cameras against the ground truth (`fellow-m2_capture_gt`): *pending (report generator).*

## 5. Mistakes and dead ends (kept visible)

* Cache lattice first derived independently of stock's snapping: 47-51 dB vs stock → re-derived from stock's exact
  snapping; irregular frames fall back.
* Thrash guard counted stock frames as misses (cache stuck off) and a 2 m LOD-travel invalidation forced constant
  rebuilds; fixed (texel LOD, guard ignores stock frames).
* A run was killed as "hung" after 30 s because elapsed time was misjudged (the Windows clock is now the reference).
* A bundle reused a staged file name and delivered stale content to Windows; bundles now always get new names.
* Night state (display off, session locked): ~490 ms stalls every ~1.03 s and GPU clocks jumping 770-1770 MHz; per-run
  GPU totals vary by up to ±10 %. Night numbers are used only for relative per-pass comparisons.
* The second version of the cost gate was motivated by a Bistro interior +7.4 % that was noise; the change itself
  stands on its own.
* The PCSS blend band seemed to lose a shadow near the camera in Bistro; the cause was stock's point-light PCSS
  formula with blockers pancaked onto the first cascade's near plane (decision 0005), not the band.
* Max pyramid v1 was not exact at first on lavapipe: one compute list for all levels (layout transitions only happen
  between lists) and float block math; both fixed, then exact — and rejected for no net gain.
* TAA: a parameter sweep (anti-flicker, clip γ, feedback) could not fix the foliage regression; velocity dilation off
  made it worse.
* Mesh clusters v1 kept the original index buffers and added reordered copies: +446 MB of VRAM in Sponza; replaced by
  in-place reordering decided at the first set-up. Its synchronous build made Sponza's load 2.8 s longer; moved to
  worker threads.
* An offline estimate of stock's LOD (position-only simplification) predicted far fewer triangles than the engine
  actually submits; the engine's own counters are the reference.
* The temporal SSAO experiment found no iso-quality saving (decision 0006).

## 6. Exact files changed

*to be listed from `git diff` at the final commit.*

## 7. Remaining high-value opportunities (measured) and what milestone 3 starts with

After milestone 2 the **opaque pass is the largest pass in every scene** (day profile, FellowEngine: Sponza 2.78 ms,
Bistro exterior 1.96, Bistro interior 1.74, PBR 0.85), and with almost no geometry (`lod1000` sizing, stock) it still
costs 1.75 ms in Sponza and 1.77 ms in Bistro exterior: it is shading-bound.

1. **Light loop (started: decision 0008, toggle off until measured).** Stock's clusters slice depth linearly over
   [0, far] = 125 m per slice with Godot's default far plane, so every pixel visits every light whose proxy covers its
   32-px tile at any depth. Logarithmic slices with conservative per-element depth ranges cut the lights visited per
   pixel by 44 % in the new Lights Lab (offline count), image-identical on lavapipe. RTX timing and the Bistro counts
   are in the `m3_sizing` session.
2. **Shading sizing** (`m3_sizing_profile`, queued): ablation profile of the FellowEngine configuration — positional
   shadows off / hard, positional lights off, SSR / SSAO / SSIL off, reflections off, probes and decals off, coarser
   LOD — to split the opaque pass before choosing the next change.
3. **PCSS lookup**: hard sun shadows (1 tap) save 0.6 ms of Bistro exterior's opaque pass; the min/max pyramid with
   both early-outs (fully lit, fully shadowed) is being measured (`m3_mm2`).
4. **Geometry** still costs ~1.9 ms of Sponza's camera passes after clusters (pre-pass ~0.4, opaque ~0.6, alpha-blended
   cypress leaves ~0.9). Offline estimates: GPU two-phase occlusion −24 % of those triangles on average (−65 % in the
   galleries), per-cluster LOD with locked borders −25 %; Bistro estimates from the engine's own geometry dump are in
   the `m3_sizing` session (`occlusion_geodump.py`). Ordered-chunk clusters for blended surfaces: `m3_mm2` (`f17_nobl`).
5. **SSAO + SSIL + SSR**: 1.0-1.3 ms in every scene.
6. **GPU idle per frame**: frame time exceeds GPU time by a constant ~0.4 ms and GPU utilisation is 87-92 % in every
   scene and configuration (windowed presentation through the desktop compositor; not yet separated from the
   engine's own frame pacing).
