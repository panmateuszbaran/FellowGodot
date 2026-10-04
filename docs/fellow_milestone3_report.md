# FellowEngine — milestone 3 report

Status: **FINAL for the measured binary `fellow22`** (`96f8aa5`); additions measured after it in §7 (up to
`fellow33`, measured 2026-10-02), benchmark-scene preparation findings at the end of §7.

Base: Godot **4.7.2-stable** · Branch `fellow/geometry` · Measured binary `fellow22` · Machine: Acer Predator
PHN16S-71, Core Ultra 9 275HX, RTX 5070 Ti Laptop 12 GB, Windows 11, Vulkan, 1920×1080 (method: milestone-2 report).

Decision records: `docs/decisions/0005` (shadow sampling: min/max pyramid v3-v7), `0008` (light clusters with depth
slices, zero-attenuation skip), `0009` (frame/GPU gap: early submit, second present queue), `0010` (blended alpha-zero
skip, KEEP, on from `1b86e8b`), `0011` (shared-memory tiles, REJECT), `0012` (specialization constants, KEEP),
`0013` (TAA camera motion vectors, REJECT).

## 1. Result

Milestone 2 left the opaque pass as the largest pass in every scene, and its sizing (§2) split it into the positional
light loop, the shadow filters and the screen-space effects. Milestone 3 made the light loop visit only the lights
that can reach a pixel, made the shadow filters skip their taps where a min/max pyramid of the shadow atlas proves the
result, and tested and rejected two ways of closing the constant gap between frame time and GPU time. Every change is
**exact**: lavapipe renders bit-identical images, the RTX GPU stays at each scene's run-to-run floor (§4).

| Change | Decision | Effect |
|---|---|---|
| Light clusters with logarithmic depth slices (0008) | KEEP, default on | Lights Lab −0.27 ms (−7.9 %, p 0.002), others within noise |
| Zero-attenuation light skip (0008) | KEEP, default on | Lights Lab opaque pass −0.38 ms |
| Shadow min/max window pyramid v7, directional + positional (0005) | KEEP, default on | Sponza −14 %, Bistro interior −3 %, Bistro exterior −1 % (v7 A/B) |
| Early submit; present from a second queue (0009) | REJECT | no effect / slower |
| Editor: headless `--import` crash on exit (bug also in stock) | fixed | — |

### Frame level (`m3_final_perf`, 2026-10-01 19:07-19:59, 3 interleaved runs per configuration, 30 s flythroughs)

**stock** = Godot 4.7.2; **M2** = the milestone-2 configuration (the same binary with the milestone-3 toggles off);
**M3** = the default configuration of `fellow22`. Mean of the per-run means ± std; Welch's t-test.

| Scene | stock ms | M2 ms | M3 ms | M3 vs stock | p | M3 vs M2 | p |
|---|---:|---:|---:|---:|---:|---:|---:|
| Sponza | 13.547 ± 0.04 | 8.406 ± 0.01 | **7.201** ± 0.07 | **−46.8 %** | <0.001 | **−14.3 %** | 0.001 |
| Bistro exterior | 5.625 ± 0.05 | 5.026 ± 0.05 | **4.973** ± 0.03 | **−11.6 %** | <0.001 | −1.1 % | 0.19 |
| Bistro interior | 4.315 ± 0.01 | 4.192 ± 0.02 | **4.080** ± 0.01 | **−5.4 %** | <0.001 | **−2.7 %** | 0.004 |
| Lights Lab (242 lights) | 3.238 ± 0.02 | 3.491 ± 0.06 | **3.018** ± 0.03 | **−6.8 %** | 0.002 | **−13.6 %** | 0.001 |
| PBR Lab | 2.825 ± 0.01 | 2.805 ± 0.02 | 2.876 ± 0.02 | +1.8 % | 0.022 | +2.5 % | 0.010 |

| Scene | GPU ms stock → M3 | p99 frame ms stock → M3 | 1 % low fps stock → M3 | VRAM MB stock → M3 | load s stock → M3 |
|---|---|---|---|---|---|
| Sponza | 13.16 → 6.82 | 20.8 → **9.0** | 47 → **107** | 2533 → 2596 | 4.44 → 4.38 |
| Bistro exterior | 5.25 → 4.60 | 6.4 → 5.6 | 152 → 176 | 2109 → 2173 | 3.26 → 3.46 |
| Bistro interior | 3.96 → 3.72 | 4.9 → 4.6 | 202 → 217 | 1272 → 1286 | 2.21 → 2.45 |
| Lights Lab | 2.90 → 2.68 | 3.6 → 3.3 | 274 → 288 | 759 → 773 | 0.43 → 0.46 |
| PBR Lab | 2.41 → 2.47 | 3.6 → 3.5 | 262 → 267 | 304 → 373 | 1.19 → 1.45 |

(Load times: mean of the three runs' single loads. VRAM: the milestone-2 shadow cache, +58 MB where a sun casts
shadows, plus the milestone-3 pyramids, +5 MB.)

Two results are not improvements and stay visible:

* **PBR Lab, M3 +1.8 % vs stock** (+0.05 ms, p 0.02). The pyramids cost 0.09 ms there (both atlases rebuilt every
  frame: the camera moves and the scene's shadows are not cacheable) and their early-outs save 0.03 ms (curved
  receivers in penumbra). §7 halves the directional build.
* **Lights Lab, M2 +7.8 % vs stock** (opaque pass +0.32 ms in the profile): a milestone-2 cost that the Lights Lab
  (new in milestone 3) exposes; M3 is faster than both. The ablation in `m3_f24_ablation` (§7) isolates it.

## 2. Where the time goes after milestone 2 (sizing, `m3_sizing_profile`)

Ablation profile of the milestone-2 configuration (`fellow18`, day state, one profiled run per variant, per-pass GPU
timestamps; ±0.05-0.1 ms run-to-run): each variant removes one thing, the delta is what it costs.

| removed | Lights Lab | Bistro interior | Bistro exterior | Sponza |
|---|---:|---:|---:|---:|
| (configuration, sum of passes) | 3.22 ms | 4.08 ms | 4.89 ms | 8.15 ms |
| all omni/spot lights | −1.80 | −1.22 | – (none) | – (none) |
| their shadows | −0.22 | **−0.82** | – | – |
| soft → hard positional shadows (8 PCF taps → 1) | −0.17 | **−0.59** | – | – |
| SSAO | **−0.61** | **−0.53** | **−0.51** | **−0.44** |
| SSIL | −0.02 | **−0.46** | **−0.42** | −0.20 |
| SSR | −0.08 | −0.21 | −0.16 | −0.19 |
| reflections (sky / probes) | −0.02 | −0.11 | −0.05 | −0.24 |
| decals and probes | −0.11 | +0.07 | +0.07 | +0.01 |
| coarser LOD (threshold 4) | −0.03 | −0.10 | −0.11 | **−0.71** |

Offline estimates from the engine's own geometry (`--mode=geodump`): occlusion culling could hide 12-30 % of the
camera-pass triangles (Hi-Z of the previous frame, per instance/cluster), worth ≤ 0.15 ms in Bistro exterior and
≤ 0.1 ms in Sponza: not pursued. The sky covers 0.2-2.7 % of the screen: skipping SS effects on sky pixels is no lever.

## 3. Changes

### Light clusters with depth slices and the zero-attenuation skip (0008)

Stock spaces its 32 cluster depth slices linearly over [0, far] — 125 m each with the default 4000 m far plane — so
a pixel iterates every light whose proxy covers its screen tile at any depth (Lights Lab: 33.9 lights per pixel, 11.5
of which reach it). FellowEngine spaces the slices logarithmically between near and far and gives every light, decal
and probe the slices of its conservative view-depth range: 19.1 per pixel. The lights a pixel still visits but that
cannot reach it return before the attenuation's `pow()` (`distance × inv_range ≥ 1`, outside a spot cone, or an
attenuation ≤ `HALF_FLT_MIN`). Lights Lab opaque pass 1.709 → 1.151 ms with both.

### Shadow min/max window pyramid (0005, v3 → v7)

A pyramid of the shadow atlas whose level L holds the minimum and maximum depth of every window of 2^(L+3) texels
starting every 2^(L+2): any filter box lies in one window of one level, so one fetch proves "every tap lit" or
"every tap shadowed" (directional PCSS: the blocker search finds nothing, or the whole penumbra-bounded filter region
is closer to the light; PCF and omni/spot PCF likewise) and the taps are skipped. The milestone-2 version (v1, a
max-only pyramid rebuilt over the whole atlas) was exact but saved what it cost. v2 added the minimum and the
fully-shadowed early-out (Sponza's courtyard and the alpha-blended cypress leaves are mostly in the sun's shadow), v3
stores RG16 and rebuilds only the atlas rects written that frame, v4 looks a box up with one fetch (overlapping
windows) and adds the positional atlases, v5-v7 cut the build: gathers, the small mips in one workgroup, shared-memory
loads, and no 4-texel windows (never looked up). Build: 0.06 ms for a fully rewritten 4096² directional atlas.

### Frame/GPU gap (0009, rejected)

Every session has frame time = GPU time + 0.37-0.44 ms. Submitting the frame before the swap-chain acquisition, a
third frame in flight and a fourth swap-chain image change nothing; presenting from a second queue moves the wait into
the next frame's GPU time (slower). It is the presentation path of this hybrid-graphics laptop (the panel is driven by
the Intel iGPU): not something the engine waits for.

## 4. Image verification

| Comparison | Result |
|---|---|
| lavapipe, every change above, toggle off vs on (PBR Lab 7 cameras, Stress Lab 4 cameras and 2 motion sequences, Lights Lab walk, Blend Lab) | **0 differing pixels** (depth slices: bit-identical to the complete element set; stock's own sampled tile ranges miss one element for 2 fog pixels) |
| RTX, M2 vs M3, 60-frame sequences (`m3_final_temporal`) | Bistro exterior 99.0 dB (max \|d\| 0), Lights Lab 90.9 dB (max 4/255), Sponza 75.4 dB (max 3), Bistro interior 67.3 dB and PBR Lab 73.5 dB: their run-to-run floor (≤ 0.001 % of pixels > 2/255) |

Milestone 3 changes no pixel by design; the image-quality comparison with the ground truth is milestone 2's (TAA v2,
8/8 sequences closer to the ground truth) and still applies.

## 5. The four questions

* **Does FellowEngine render better than stock Godot?** Yes, through milestone 2's anti-aliasing (closer to the
  ground truth in 8/8 motion sequences and on every fixed camera); milestone 3 changes no pixel (§4).
* **Does it run faster at equivalent quality?** **Yes, in 4 of 5 scenes**: Sponza **−47 %** (13.5 → 7.2 ms, p99
  20.8 → 9.0 ms), Bistro exterior **−12 %**, Lights Lab **−7 %**, Bistro interior **−5 %** (all p ≤ 0.002); PBR Lab
  +1.8 % (p 0.02, the pyramids' build; after the changes in §7 within noise: −0.5 %, p 0.74, `m3_f29_perf`). Milestone 3 alone: −14 % in Sponza and the Lights Lab, −3 % in Bistro
  interior. With the additions of §7 (`fellow33`, `m3_f33_perf`): Sponza −47.4 %, Bistro
  exterior −11.5 %, Blend Lab −8.6 %, Lights Lab −5.7 %, Bistro interior −5.5 %, PBR Lab −0.5 % (within noise): **no
  scene slower than stock**.
* **Does it produce better image quality at equivalent performance?** Yes: the anti-aliasing improvement comes with a
  faster frame.
* **Can we prove those claims with reproducible measurements?** Yes, within one machine and one GPU vendor: every
  number comes from a named session in `benchmarks/results/` (`m3_final_perf`, `m3_final_profile`,
  `m3_final_temporal`, and the sessions named in the decision records), produced by the runner commands recorded in
  the job files; every exactness claim has a lavapipe pixel-identity test and an RTX sequence comparison.

## 6. Mistakes and dead ends (kept visible)

* **Three benchmark sessions ran at once** (10:29-11:05): queued jobs waited for a marker that the queue script itself
  printed. Every timing of that window was discarded and re-run; markers now count only at the start of a line and
  jobs run strictly one after another. (Noticed by the user.)
* A re-staged job file was delivered with its earlier content; job files are now checked (md5) on the machine before
  they start.
* Pyramid lookups: the first one-fetch version used float block math and an approximate `log2` that picked a level
  too small for 137 pixels (lavapipe); fixed with an integer texel box.
* Depth slices: a first version intersected the new depth ranges with stock's sampled ones (inheriting their misses)
  and used stock's signed `box_depth` for decals (cut a decal: 3187 pixels); both fixed before any measurement.
* Per-rect pyramid rebuilds (`fellow22`) raised the build from 0.06 to 0.09 ms in Sponza and Bistro exterior (one
  dispatch chain per cascade); `build_rects` merges them again when their union is not much larger.
* One Sponza run lost the device (`VK_ERROR_DEVICE_LOST`, nvlddmkm event 153). The same driver event appears 12 times
  in the system log on 18-22 September, before these sessions; it did not recur.
* A debug shader patch was committed by `git commit -a` and taken out again; files are now staged explicitly.
* Early submit was built on a hypothesis (the frame waits on the swap-chain acquisition) that the measurement refuted
  (0009); presenting from a second queue made frames slower.
* `fellow25` and `fellow26` did not build on Windows (MSVC: a copy-initialised `LocalVector`, whose copy constructor
  is explicit; GCC accepted it); both jobs stopped at the build check, no benchmark ran. Fixed in `cc2ccd5`.
* **A/A check** (`m3_f36_aa`, 2026-10-02): the same configuration under three labels in Bistro exterior differed by
  up to −1.4 % with p 0.049 over 3 interleaved runs. Frame differences of about 1.5 % or less from 3-run sessions in
  that scene (e.g. `fellow29` vs `fellow27` +1.3 %, 0013 −1.0 %, 0014 +1.7 %) are not evidence; decisions on effects
  that small need more runs or the per-pass profile.
* Blended alpha-zero skip (0010): the first version dropped single fragments and changed 109-407 pixels per image by
  up to 11/255 on lavapipe (its quad neighbours' derivatives); now quad-uniform and bit-identical.

## 7. After the measured binary

| Change | Commit | Result |
|---|---|---|
| SSAO gather with each workgroup's depths and normals in shared memory (0011) | `2be6b7c`, `4083b37` | identical; **no measurable effect** (`m3_f23_perf`: −1.1 to +0.1 %, all p > 0.1) → REJECT, default off |
| TAA v2: tonemapped YCoCg of each tile texel computed once (0011) | `cde4a0b` | identical; no measurable effect; kept |
| Pyramid rebuilt over the union of an atlas's written rects (`build_rects`, 0005) | `ee8226d` | identical; build −0.02 ms in every scene with a sun (`m3_f24_profile`: Sponza 0.109 → 0.087, Bistro exterior 0.084 → 0.061, PBR Lab 0.092 → 0.072 ms) |
| Blended alpha-zero skip (0010) | `874af3e`, default on from `1b86e8b` | RTX bit-identical (Blend Lab 99 dB, max \|d\| 0; other scenes at their floor); Blend Lab transparent pass **0.417 → 0.275 ms (−34 %)**, frame **2.738 → 2.519 ms (−8.0 %, p < 0.001**, `m3_f31_blend_az`, 5 runs); benchmark scenes within noise (`m3_f27_az`, 5 runs: Sponza −0.4 %, Bistro interior −0.3 %, PBR Lab +1.2 %, all p > 0.25) → **KEEP, on** |
| Directional pyramid over the part of each cascade the camera can look up (`max_mip_visible_region`, 0005) | `c63638d`, `cc2ccd5` | identical (lavapipe, RTX 99 dB in Bistro exterior); build −0.005 to −0.007 ms only (the build is now bound by its dependent dispatches, not by reading the atlas); frame within noise → **REJECT**, off |
| One clear of the whole directional atlas when no cascade comes from the cache (0005) | `77c8b1a` | identical; PBR Lab shadow pass 0.135 → 0.114 ms (= stock): **the PBR Lab regression is gone** |
| Every FellowEngine scene-shader feature behind specialization constants (0012) | `ead26ec`, `9deb4fa` | identical (lavapipe; RTX `m3_f29_temporal` at the floor); null test = stock (Lights Lab opaque pass 1.506 vs 1.515 ms, was 1.847); defaults unchanged (`m3_f29_perf` vs `fellow27`: all within noise except Bistro exterior +1.3 %, p 0.04, 3 runs) → **KEEP** |
| TAA motion vectors of static geometry from the depth buffer (0013, `postfx/taa_camera_motion_vectors`) | `1d21886` | velocity equal to stock's up to half-float rounding (lavapipe readback: ≤ 0.015 px), image not bit-identical (RTX 55-71 dB vs floors of 67-99 dB); opaque pass −0.15 ms (Bistro exterior) / −0.19 ms (Sponza), but frame within noise everywhere (`m3_f33_perf`: −1.2 % to +2.0 %, all p > 0.1) → **REJECT**, off |
| Cluster pack pass with one workgroup per tile (0014, `lights/cluster_pack_parallel`) | `d01ce57` | bit-identical (lavapipe; RTX at the floor); pack pass Lights Lab 0.119 → 0.069 ms (stock 0.074); frame Lights Lab −1.3 % (p 0.07), others within noise → decision pending (paused), proposed on |

### Frame level after these changes (`m3_f27_perf`, 2026-10-02 00:27-01:38, `fellow27` defaults, 3 runs each)

| Scene | stock ms | FellowEngine ms | Δ | p |
|---|---:|---:|---:|---:|
| Sponza | 13.331 | **7.024** | **−47.3 %** | <0.001 |
| Bistro exterior | 5.610 | **4.931** | **−12.1 %** | <0.001 |
| Lights Lab | 3.238 | **2.999** | **−7.4 %** | 0.011 |
| Bistro interior | 4.259 | **4.069** | **−4.5 %** | 0.009 |
| PBR Lab | 2.832 | 2.826 | −0.2 % | 0.85 (within noise; was +1.8 % in `m3_final_perf`) |

No scene is slower than stock any more.

**The Lights Lab M2 cost** (§1), ablation `m3_f24_ablation` (one profiled run each, opaque pass): stock 1.359 ms,
M2 1.660, M2 without the positional back-facing skip 1.638 — none of the milestone-2 features active in that scene
(no sun shadows, no clustered meshes) explains the +0.3 ms. Default configuration 1.094; removing one feature at a
time: back-facing skip 1.119, zero-attenuation skip 1.311, positional pyramid 1.092, directional skips 1.093. In
Bistro interior the positional pyramid saves 0.25 ms of the opaque pass and the back-facing skip 0.11 ms (`m3_f24_perf`:
without the skip +3.0 %, p 0.002). Hypothesis: code present in the shader but switched off by a runtime flag still
costs registers in the light loops; the specialization constants above remove it from the pipelines that do not use
it. `m3_f27_profile` (fellow27: pyramids and blend band already behind specialization constants; the other flags not
yet): Lights Lab opaque pass stock 1.508, null test (every FellowEngine toggle off) **1.847**, M2 1.840, default 1.197
— the null test is as slow as M2, so the cost is not a milestone-2 feature but code that the binary carries even with
every toggle off. `fellow29` removes all of it from the specialized pipelines: `m3_f29_profile`, Lights Lab opaque pass stock
**1.515**, null test **1.506** (was 1.847), default 1.199, M2 1.668. The binary's disabled code no longer costs anything;
what is left is the milestone-2 configuration itself (+0.15 ms with its features on, the zero-attenuation skip and
the depth slices off). `m3_f33a_ablation` (one profiled run per variant, Lights Lab opaque pass) finds it: M2 1.666 ms, M2 without the
positional back-facing skip 1.557 (stock 1.523), M2 without mesh clusters 1.646. Without the zero-attenuation skip
every light of the cluster reaches the back-facing test (most only to be dropped by their attenuation later), and the
test costs more than it saves. The default configuration does not pay it: default 1.202, default without the
back-facing skip 1.201, without mesh clusters 1.237 (noise ±0.05). The back-facing skip should stay paired with the
zero-attenuation skip (0005, 0008). (Bistro interior's single profiled runs differ by up to 0.3 ms between variants
that share the relevant features, so they are not used here.)

### Frame level with `fellow29` (`m3_f29_perf`, 2026-10-02 03:01-04:12, 3 interleaved runs each)

| Scene | stock ms | FellowEngine ms | Δ | p |
|---|---:|---:|---:|---:|
| Sponza | 13.431 | **7.071** | **−47.3 %** | <0.001 |
| Bistro exterior | 5.590 | **4.994** | **−10.7 %** | 0.005 |
| Lights Lab | 3.197 | **2.985** | **−6.6 %** | <0.001 |
| Bistro interior | 4.289 | **4.058** | **−5.4 %** | 0.020 |
| PBR Lab | 2.834 | 2.820 | −0.5 % | 0.74 (within noise) |

The same as `fellow27` within noise in four scenes; Bistro exterior +1.3 % against `fellow27` (p 0.04, 3 runs, CV
0.2-0.5 %): no cause identified, and smaller than the drift between sessions; re-measured in the next full
session. The milestone-2 configuration
of the same binary (`m2_29`): Lights Lab +3.4 % vs stock (p 0.047), every other scene faster than stock.

### Frame level with `fellow33` (`m3_f33_perf`, 2026-10-02 07:00-08:00, defaults, 3 interleaved runs each)

`fellow33` = `fellow29` + the blended alpha-zero skip on (0010) + 0013 (off). Image: bit-identical to `fellow29` in
Bistro exterior and Blend Lab (99 dB), every other scene at its floor (`m3_f33_temporal`).

| Scene | stock ms | FellowEngine ms | Δ | p | GPU ms stock → FE | p99 ms stock → FE |
|---|---:|---:|---:|---:|---|---|
| Sponza | 13.424 | **7.065** | **−47.4 %** | <0.001 | 13.04 → 6.69 | 20.3 → 8.9 |
| Bistro exterior | 5.614 | **4.970** | **−11.5 %** | <0.001 | 5.24 → 4.59 | 6.4 → 5.6 |
| Blend Lab | 2.814 | **2.572** | **−8.6 %** | 0.015 | 2.45 → 2.07 | 3.0 → 2.9 |
| Lights Lab | 3.188 | **3.007** | **−5.7 %** | 0.001 | 2.86 → 2.66 | 3.6 → 3.4 |
| Bistro interior | 4.289 | **4.054** | **−5.5 %** | <0.001 | 3.93 → 3.69 | 4.8 → 4.6 |
| PBR Lab | 2.830 | 2.815 | −0.5 % | 0.49 (within noise) | 2.40 → 2.39 | 3.7 → 3.5 |

Bistro exterior `fellow29` 4.994 → `fellow33` 4.970 ms: the +1.3 % against `fellow27` (above) was noise.

### Benchmark scenes: what the Bistro scenes show (questions from the user, 2026-10-01)

Two things in the Bistro captures looked wrong. Both are properties of how the benchmark scene loads the asset
(identical for stock and FellowEngine), not renderer bugs:

* **Holes in the floor and walls behind the shop windows (exterior).** The Lumberyard Bistro exterior contains the
  restaurant only as a shell: its rooms, bar and furniture are `BistroInterior.fbx`, a separate file in the same
  coordinates (probe `bistro_probe.json`: the interior's bounds x −2.1…13.6, y 0.2…7.5, z −13.9…5.5 m contain the
  windows the exterior cameras look through). Through the windows one saw the far side of an empty shell. Also, the
  window glass material (`MASTER_Glass_Dirty_MASKED`) has an alpha of 0 everywhere in its texture, so the glass was
  invisible, and 16×16 placeholder textures (table cloths, bottle labels) are transparent black.
* **Whole lamps glowing (interior).** The asset's lamp shades are emissive (the `Paris_Ceiling_Lamp` emissive texture
  covers the shade's UV area, not only a bulb); the scene multiplied emission by 4 and the glow made the shades bloom
  into bright discs.

Scene revision v2 (`a297438`, `3c0fb4b`; **the default from 2026-10-04**, `--bistro_mat=1` reproduces v1 and every
session before that date): the exterior also loads the interior (its own lights, cameras and animations removed); glass is a
tinted, mostly transparent material (alpha 0.18, back faces culled); placeholders use the imported albedo colour;
emission ×1; the material-name matching fixed ("street" no longer matches "tree"). Pathshots `m3_f32_pathshots_v2b`:
tables and the bar behind tinted glass instead of holes. Timing on v2 (3 interleaved runs each):

| Scene (v2) | stock ms | FellowEngine (`fellow29`) ms | Δ | p |
|---|---:|---:|---:|---:|
| Bistro exterior, without the interior (`m3_f30_perf_v2`) | 5.524 | 4.889 | −11.5 % | <0.001 |
| Bistro interior (`m3_f30_perf_v2`) | 4.302 | 4.058 | −5.7 % | 0.002 |
| Bistro exterior with the interior (`m3_f32_perf_v2b`) | 6.763 | **5.117** | **−24.3 %** | <0.001 |
| Bistro exterior with the interior, `fellow33` (`m3_f34_perf_v2`) | 6.641 | **5.110** | **−23.1 %** | <0.001 |
| Bistro interior, `fellow33` (`m3_f34_perf_v2`) | 4.272 | 4.061 | −4.9 % | 0.001 |

With the interior loaded, most of the difference is on the CPU: stock submits **7363 draw calls** per frame (render
thread 3.63 ms) and its frame (6.76 ms) is no longer bound by the GPU (4.85 ms); FellowEngine's cached shadow
cascades leave **2621 draws** (render thread 2.11 ms), GPU 4.61 ms, frame 5.12 ms. Without the interior: 2983 vs 965
draws, render thread 1.48 vs 0.95 ms, both frames GPU-bound. The per-pass GPU profile with the interior
(`m3_f33b_profile_v2b`, one run each) is like v1's: shadow pass 0.65 → 0.32 ms, opaque 2.41 → 1.92 ms, sum of passes
5.85 → 5.18 ms (−11.5 %). v2 is the benchmark default from 2026-10-04 (decided by the user); its baseline is
`m3_f34_perf_v2` above.

## 8. Exact files changed (milestone 3, `0ba5e35..HEAD`, generated headers excluded)

`servers/rendering/fellow_settings.{h,cpp}`, `rendering_device.{h,cpp}`, `drivers/vulkan/rendering_device_driver_vulkan.cpp`
(0009), `renderer_rd/cluster_builder_rd.{h,cpp}`, `shaders/cluster_store.glsl`, `shaders/cluster_debug.glsl`,
`environment/fog.{h,cpp}`, `shaders/environment/volumetric_fog_process.glsl` (0008),
`effects/fellow_shadow_max_mip.{h,cpp}`, `shaders/effects/fellow_shadow_max_mip.glsl`,
`shaders/scene_forward_lights_inc.glsl` (0005), `forward_clustered/render_forward_clustered.{h,cpp}`,
`forward_clustered/scene_shader_forward_clustered.cpp`, `shaders/forward_clustered/scene_forward_clustered.glsl`,
`scene_forward_clustered_inc.glsl`, `shaders/scene_data_inc.glsl`, `storage_rd/render_scene_data_rd.{h,cpp}`,
`storage_rd/light_storage.h`, `effects/ss_effects.{h,cpp}`, `shaders/effects/ssao.glsl`,
`shaders/effects/taa_resolve.glsl`, `storage_rd/fellow_mesh_clusters.{h,cpp}`, `storage_rd/mesh_storage.{h,cpp}`
(deferred cluster builds), `editor/doc/editor_help.cpp` (import crash). Benchmarks: `run_benchmarks.py` (per-variant
engine arguments and start-up overrides), `tools/ab_stats.py`, scenes `lights_lab.gd`, `blend_lab.gd`.

## 9. Next

* 0014 (cluster pack per tile): longer Lights Lab A/B, then on by default.
* TAA camera motion vectors (0013, rejected as it stands): worth retrying only with the velocity written by the depth
  downsample SSAO/SSIL already run (no separate pass).
* Cache-space directional pyramid: with the shadow cache, cascades are copied into the atlas every frame the camera
  moves, so their pyramid is rebuilt every frame although the cache did not change (0.06-0.09 ms in Sponza and
  Bistro exterior).
* SSIL (0.2-0.3 ms gather): the SSAO shared tile gained nothing (0011), so not with that approach; screen-space
  effect quality settings as an explicit, evaluated look option.
