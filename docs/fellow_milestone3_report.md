# FellowEngine — milestone 3 report

Status: **FINAL for the measured binary `fellow22`** (`96f8aa5`); additions measured after it in §7.

Base: Godot **4.7.2-stable** · Branch `fellow/geometry` · Measured binary `fellow22` · Machine: Acer Predator
PHN16S-71, Core Ultra 9 275HX, RTX 5070 Ti Laptop 12 GB, Windows 11, Vulkan, 1920×1080 (method: milestone-2 report).

Decision records: `docs/decisions/0005` (shadow sampling: min/max pyramid v3-v7), `0008` (light clusters with depth
slices, zero-attenuation skip), `0009` (frame/GPU gap: early submit, second present queue), `0010` (blended alpha-zero
skip, in test).

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
  +1.8 % (p 0.02, the pyramids' build, §7). Milestone 3 alone: −14 % in Sponza and the Lights Lab, −3 % in Bistro
  interior.
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
* Blended alpha-zero skip (0010): the first version dropped single fragments and changed 109-407 pixels per image by
  up to 11/255 on lavapipe (its quad neighbours' derivatives); now quad-uniform and bit-identical.

## 7. After the measured binary

| Change | Commit | Status |
|---|---|---|
| SSAO gather with each workgroup's depths and normals in shared memory (`postfx/ssao_shared_tile`, on) | `2be6b7c`, `4083b37` | identical; `m3_f23_perf` |
| TAA v2: tonemapped YCoCg of each tile texel computed once | `cde4a0b` | identical; `m3_f23_perf` |
| Pyramid rebuilt over the union of an atlas's written rects (`build_rects`) | `ee8226d` | identical; `fellow24` |
| Blended alpha-zero skip (0010, off) | `874af3e` | lavapipe identical; `m3_f24_*` |
| Directional pyramid computed only over the part of each cascade the camera can look up (`shadows/max_mip_visible_region`) | — | lavapipe identical; `fellow25` |

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

* The Lights Lab M2 cost (§1): which milestone-2 feature, and whether the default configuration still pays part of it.
* Cache-space directional pyramid: with the shadow cache, cascades are copied into the atlas every frame the camera
  moves, so their pyramid is rebuilt every frame although the cache did not change (0.06-0.09 ms in Sponza and
  Bistro exterior).
* SSIL (0.2-0.3 ms gather) with the SSAO shared-tile approach; screen-space effect quality settings as an explicit,
  evaluated look option.
