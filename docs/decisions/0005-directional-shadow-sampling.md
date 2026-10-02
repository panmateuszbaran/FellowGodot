# 0005 — Directional shadow sampling in the Forward+ scene shader

Status: **KEEP (skips, pyramids v7); REJECT (pyramid v1); options (penumbra, blend band)** · Branch `fellow/shadows` · Toggles:
`rendering/fellow/shadows/skip_backfacing_directional`, `skip_backfacing_positional`, `skip_unblended_cascade`
(image-identical, default **on** since milestone 2), `directional_max_mip` (image-identical, default off);
`pcss_blend_band`, `pcss_directional_penumbra` (look changes, default off) · Code: `shaders/forward_clustered/scene_forward_clustered.glsl` (directional shadow loop,
`fellow_directional_soft_cascade()`), `shaders/scene_forward_lights_inc.glsl` (`sample_directional_soft_shadow`,
omni/spot early return), `effects/fellow_shadow_max_mip.*`, scene UBO flags 8-13.

## Problem

After the shadow *maps* (decision 0004), the remaining directional-shadow cost is the *lookup* in the opaque pass.
With the benchmark suns (PCSS: `light_angular_distance` 0.5°, soft shadow quality "medium" = 12 blocker-search taps +
12 filter taps, `blend_splits` on) every lit pixel runs the PCSS kernel **twice**:

* the PCSS path of stock Godot blends a cascade into the next one over the **whole** split
  (`smoothstep(0, split0, z)`, `smoothstep(split0, split1, z)`, ...), so every pixel closer than the last split
  samples two cascades (up to 48 texture fetches for one light), and the blend replaces a good part of the
  higher-resolution cascade with the next, blurrier one;
* the PCF path blends only over the last 10 % of a split but still samples the second cascade for every pixel and
  multiplies it by a blend weight of 0;
* the lookup is done for every pixel, including pixels facing away from the light, whose direct light is exactly zero
  (`cNdotL = max(A + N·L, 0) = 0` multiplies the diffuse and specular terms in `light_compute()`).

## Evidence (before the change)

Stock with the sun overrides of the benchmark project (`m2_sizing_profile`, 1080p, GPU timestamps, night state:
relative only):

| Scene | opaque pass, stock | sun shadows off | blend splits off | hard shadows (PCF) |
|---|---:|---:|---:|---:|
| PBR Lab | 0.837 ms | 0.436 (−48 %) | 0.643 (−23 %) | 0.667 (−20 %) |
| Sponza | 3.059 | 2.144 (−30 %) | 2.755 (−10 %) | 2.679 (−12 %) |
| Bistro exterior | 2.030 | 1.134 (−44 %) | 1.605 (−21 %) | 1.434 (−29 %) |
| Bistro interior (no sun shadows) | 1.842 | 1.857 | 1.821 | 1.762 |

Directional shadow *sampling* is 30-48 % of the opaque pass where the sun casts shadows; the PCSS blend over whole
splits alone is 10-23 %. (Blend splits also enlarge every cascade's caster culling volume: Sponza's shadow-map pass
is 0.81 ms cheaper without them.) The transparent pass shows the same pattern (Sponza −0.64 ms without sun shadows).

## Proposed implementation

1. `skip_backfacing_directional` (image-identical): before the shadow lookup, compute
   `NdotL = min(A + dot(hvec3(normal), hvec3(L)), 1)` exactly as `light_compute()` does; if `NdotL <= 0` the lookup
   is skipped. Compiled out for every material where the shadow can still matter with `N·L <= 0`: custom `light()`,
   rim, backlight, transmittance, clearcoat (uses the vertex normal), Lambert-wrap and toon diffuse, toon specular,
   shadow-to-opacity, vertex lighting.
2. `skip_unblended_cascade` (image-identical): PCF path, the second cascade is sampled only when its blend weight
   is > 0 (`mix(a, b, 0.0) == a`).
3. `pcss_blend_band` (look change): PCSS path samples the cascade containing the pixel and, with blend splits, blends
   towards the next cascade over the last 10 % of the split only — the same band as the PCF path.
4. `skip_backfacing_positional` (image-identical): omni/spot lights return before the shadow lookup, projector and
   BRDF where `N·L <= 0` (same material exclusions).
5. `directional_max_mip` (image-identical by construction): a compute pass builds a max pyramid of the directional
   atlas after the shadow passes; the PCSS blocker search first reads the 2×2 pyramid texels covering its search box
   (radius `|tex_scale|` + 1.5 texels for bilinear taps); if none is closer to the light than the receiver, no tap can
   find a blocker and stock returns 1.0, which is returned directly.
6. `pcss_directional_penumbra` (look change): stock sizes the directional PCSS filter with the point-light formula
   `search radius × (d_blocker − d_receiver) / (1 − d_blocker)` (reverse Z, distances from the cascade's near plane),
   i.e. the physical penumbra `θ × (receiver − blocker distance)` multiplied by receiver distance / blocker distance.
   That overestimates every penumbra, and without bound for blockers pancaked onto the near plane (casters more than
   `shadow_pancake_size` in front of the cascade), whose shadows dissolve into the filter. The toggle uses
   `search radius × (d_blocker − d_receiver) / (1 − d_receiver)` = `θ × (receiver − blocker distance)`, always within
   the blocker search radius.

## Actual performance (night state, `m2_profile7`, 1080p, opaque pass GPU ms; ±10 % run noise)

| Scene | stock | sampling skips (1, 2, 4) | PCSS blend band |
|---|---:|---:|---:|
| PBR Lab | 0.807 | 0.739 (−0.068) | 0.629 (−0.178) |
| Sponza | 3.186 | 2.902 (−0.284) | 2.791 (−0.394) |
| Bistro exterior | 2.088 | 1.795 (−0.293) | 1.692 (−0.396) |
| Bistro interior | 1.800 | 1.515 (−0.285) | 1.806 |

(Bistro interior has no directional shadow pass; its −0.285 ms comes from omni/spot lights or noise — a variant that
cannot change anything there, the shadow cache, measured +0.187 ms in the same session.)

## Actual visual quality

* Sampling skips: identical to stock within run-to-run noise (cache + skips vs stock, 60-frame sequences after 200
  preroll frames: 67.8-94.7 dB, ≤ 0.003 % of pixels differ by more than 2/255; `m2_verify7`).
* Blend band: 36-56 dB vs stock (`m2_verify4`), i.e. a visible look change. Against the Cycles reference (PBR Lab,
  reference lighting, `m2_cyc_compare`) it is marginally closer in penumbra regions on 5 of 6 cameras (edge MAE
  −0.2 % to −4 %), equal globally. In Bistro exterior it appeared to lose a shadow near the camera: stock without
  blend splits loses it as well (34.5 dB vs stock, `m2_band_debug`), the tighter caster culling was ruled out
  (`m2_cull_debug`: disabling it changes nothing visible, 72.5 dB). **Cause confirmed: item 6.** With the
  parallel-light penumbra the shadow is back without blend splits (`m2_pen_temporal`, frame 30: stock without blend
  splits shows the lit region with a hard edge at the split, the penumbra fix without blend splits shows the shadow,
  like stock with blend splits). This is a stock Godot artifact that affects the *default* configuration
  (blend splits off) whenever PCSS is used: shadows of casters beyond the pancake distance dissolve in the first
  cascade.
* Penumbra fix vs stock: a look change — Bistro exterior 40 dB, Sponza 60.7 dB over 60 frames; PBR Lab (casters close
  to their receivers) 76-88 dB. Against Cycles (PBR Lab) unchanged (edge MAE within 0.2 %), as expected for that
  geometry.
* Max pyramid: first version wrong in two ways, found with lavapipe debug builds (brute-force maxima over the taps,
  the box and the chosen pyramid block, rendered as black where they disagree): (1) all levels were built in one
  compute list, so a level written as a storage image was read as a sampled texture without a layout transition
  (fixed: one compute list per level, like the SSR Hi-Z); (2) the level and the block indices were computed in floating
  point (`ceil(log2(box size))`, scaled box corners); the debug build showed the box's largest texel outside the four
  fetched blocks for 137 pixels — most likely an approximate `log2` choosing blocks one level too small (not isolated
  further). Fixed by an integer texel box and the smallest level `k` where `(hi >> k) − (lo >> k) ≤ 1`.
  After both fixes: lavapipe bit-identical on 3 cameras for PCSS and PCF (0 differing pixels); RTX (first version,
  `m2_mm_temporal`) 67.8-93.8 dB vs stock, no pixel off by more than 2/255.

## Max pyramid: performance (`m2_mm_profile`, night state, 1080p, GPU ms)

| Scene | shadow pass (incl. pyramid build) stock → max pyramid | opaque: skips → skips + max pyramid |
|---|---|---|
| PBR Lab | 0.107 → 0.203 (+0.096) | 0.776 → 0.791 (+0.015) |
| Sponza | 3.404 → 3.413 | 3.031 → 2.975 (−0.056) |
| Bistro exterior | 0.540 → 0.627 (+0.087) | 1.815 → 1.758 (−0.057) |

The pyramid build (4096² atlas, ~0.09 ms) costs about what the early-out saves. The early-out rarely fires: the
benchmark PCSS search radius spans tens of texels, and for any receiver not perpendicular to the light its own
surface, a few texels away along the slope, is closer to the light than the biased receiver depth, so the box
maximum exceeds the receiver depth.

## Max pyramid v2: min/max with a fully-shadowed early-out (`fellow17`, day state, `m3_mm2_profile`, 1080p, GPU ms)

v2 stores the minimum and the maximum per block and adds the second early-out: if every texel within the search
region is closer to the light than the receiver, the PCSS blocker average is at most the region's maximum, which bounds
the penumbra and the filter radius; if every texel within that radius is closer too, every filter tap compares 0
(the same reasoning, one box, for PCF). Identity on RTX (`m3_mm2_temporal`, f17 vs f17 + max pyramid): Bistro
exterior 99.0 dB (max |d| 0), Sponza 72.7-75.4 dB (max 2/255), PBR 65.7-73.4 dB (≤ 0.001 % of pixels > 2/255, the
scene's own run-to-run floor); hard-shadow pair: Bistro 99-109 dB.

| Scene | shadow pass (incl. pyramid build) | opaque pass | transparent pass | sum of passes |
|---|---|---|---|---|
| Sponza | 0.302 → 0.425 (+0.123) | 2.654 → **2.093 (−0.561)** | 1.825 → **1.348 (−0.477)** | 8.142 → **7.163 (−12.0 %)** |
| Bistro exterior | 0.311 → 0.435 (+0.124) | 2.036 → 1.889 (−0.147) | 0.051 → 0.051 | 4.918 → 5.101 (+3.7 %, noise in SSAO/SSIL/SSR) |
| PBR Lab | 0.122 → 0.229 (+0.107) | 0.895 → 0.973 (+0.078) | 0.115 → 0.106 | 2.638 → 2.791 (+5.8 %) |

The fully-shadowed early-out is what pays: Sponza's courtyard and galleries are mostly in the sun's shadow (also on
the alpha-blended cypress leaves, shaded with the same PCSS). Where the early-outs rarely fire (PBR Lab: penumbrae on
curved receivers), the four extra fetches cost a little. In every scene the pyramid is rebuilt over the whole 4096²
atlas every frame (+0.11-0.12 ms), even though with the shadow cache the atlas does not change in a static view.

**v3** (`fellow18`): RG16 UNORM pyramid (half the memory and bandwidth; exact for the 16-bit atlas, conservatively
rounded for a 32-bit one: minimum down, maximum up), rebuilt only over the atlas rects written since the last build
(none in a static view with the shadow cache; the cascade rects with dynamic casters, cache copies or stock cascades
otherwise; everything after a full clear or an atlas change). Identity: lavapipe, PBR Lab and Stress Lab captures and
the Stress Lab motion sequences (moving dynamic casters, rotating sun: partial rebuilds every frame), **0 differing
pixels** against the pyramid off; RTX (`m3_mm3_temporal`): Bistro exterior 99.0 dB (max |d| 0), Sponza 72.6-75.4 dB
(max 3/255), PBR Lab at its run-to-run floor. Timing (`fellow18`, `m3_mm3_profile`, one profiled run per variant;
`m3_mm3_perf`, interleaved, 3-6 runs):

| scene | shadow pass incl. pyramid | opaque | transparent | frame (perf A/B) |
|---|---|---|---|---|
| Sponza | 0.313 → 0.447 (+0.134) | 2.740 → 2.187 (−0.553) | 1.892 → 1.409 (−0.483) | 8.466 → **7.461 ms (−11.9 %, p 0.001)** |
| Bistro exterior | 0.300 → 0.419 (+0.119) | 1.969 → 1.860 (−0.109) | – | 5.151 → 5.205 (+1.0 %, within noise) |
| PBR Lab | 0.123 → 0.224 (+0.101) | 0.899 → 0.971 (+0.072) | – | 2.790 → **2.878 (+3.1 %, p 0.02)** |

The rebuild is not the incremental near-zero hoped for in these benchmarks: the camera moves in every flythrough, so
the shadow cache copies every cascade to a new position each frame and the whole atlas is rewritten (a static view
rebuilds nothing). One run of this session (`f18_mm`, Sponza, run 2) lost the Vulkan device 3.5 s into its warm-up
(`VK_ERROR_DEVICE_LOST`, nvlddmkm event 153 at 13:04:45); the machine logged the same event 12 times between 18 and
22 September, before these sessions, and the pyramid's dispatch sizes cannot go negative (v3: `dst_hi > dst_lo` by
construction); the run was discarded by the runner's timeout and replaced. Watched for in every later session: no
recurrence in `m3_f19_temporal`, `m3_f19_profile`, `m3_f19_perf` (every run valid, 30 of them with a pyramid).

**v4** (`fellow19`): overlapping windows instead of blocks — mip m holds the minimum and maximum of every atlas window
of 2^(m+2) texels starting every 2^(m+1) texels, so any filter box of at most 2^(m+1) texels lies inside the single
window of mip m that starts at or before it: **one fetch** instead of four (and no level search loop), which lowers the
cost where the early-outs rarely fire (PBR Lab, +0.08 ms in v2). Same memory, same incremental rebuild (a window
reads 4×4 atlas texels at mip 0, then the windows 2i and 2i + 2 of the previous mip).

**Positional atlases** (`shadows/positional_max_mip`, `fellow19`): the same pyramid per positional shadow atlas,
rebuilt over the slots of the lights whose shadows were re-rendered (an omni light writes two slots, its paraboloids);
static lights cost nothing after the first frame. Early-outs in the PCF filters: spot lights (box of half size
|filter scale| around the tap centre, as for the sun), omni and area lights (in paraboloid space the taps lie within
|offset scale| of the centre; when |centre| + |offset| < 0.999 no tap can cross the rim and flip to the other
paraboloid, so all taps are 2x2 comparisons inside one box of the atlas). Sizing that motivates it: Bistro interior's
4 shadowed omni lights spend 0.59 ms of the opaque pass on PCF taps (soft → hard, `m3_sizing_profile`).
lavapipe: identical (0 pixels) on PBR Lab, Lights Lab, Stress Lab captures and the Stress Lab motion sequences;
a debug build returning 0.25 for "fully lit" changed 77-170 k pixels per PBR camera (positional) and 182-229 k
(directional PCSS), i.e. both early-outs fire. RTX identity (`m3_f19_temporal`, 60-frame sequences, `fellow19` vs
+ positional / + directional pyramid): Bistro exterior 99.0 dB (max |d| 0), Sponza 75.5 dB (max 3/255), Lights Lab
93.9 dB (max 3), Bistro interior and PBR Lab at their run-to-run floor (≤ 0.001 % of pixels > 2/255).

Profile (`m3_f19_profile`, one run per variant, ±0.1 ms per pass between runs):

| scene | `f19` sum of passes | + positional pyramid | + directional pyramid |
|---|---:|---:|---:|
| Lights Lab | 3.253 | 3.315 (+1.9 %) | 3.281 (+0.8 %) |
| Bistro interior | 4.144 | **3.957 (−4.5 %**, opaque −0.198) | 4.115 (−0.7 %) |
| Bistro exterior | 4.982 | 4.897 (−1.7 %) | **4.656 (−6.5 %**, opaque −0.226, shadows +0.105) |
| Sponza | 7.971 | 8.073 (+1.3 %) | **6.801 (−14.7 %**, opaque −0.623, transparent −0.626, shadows +0.126) |
| PBR Lab | 2.478 | 2.682 (+8.3 %, shadows +0.059) | 2.770 (+11.8 %, shadows +0.131) |

One fetch instead of four did not change the picture: the build (+0.06 positional, +0.10-0.13 directional per frame
in flythroughs) is what the scenes without much shadow filtering pay. Interleaved A/B of everything together
(`m3_f19_perf`, with the light changes of 0008): Sponza −14.3 %, Lights Lab −13.0 %, Bistro interior −2.9 %, Bistro
exterior +1.2 % (within noise), **PBR Lab +6.0 % (p < 0.001)**.

**Build v5** (`fellow20`): mip 0 reads its 4×4 atlas window as four 2×2 gathers instead of 16 fetches, and the mips
of 32×32 texels and smaller are built by one workgroup from shared memory instead of one dispatch and one barrier each
(7 dispatches instead of 12 for a 4096² atlas); the build has its own GPU timestamp (`FellowEngine Shadow Pyramids`).
Same values: lavapipe debug builds that mark every early-out produce identical images (gathers vs fetches on the PBR
Lab; one workgroup vs one dispatch per mip with 256² atlases, where the small mips are the ones looked up), and the
non-debug build is identical to the pyramids off. RTX (`m3_f20_profile`, the build's own timestamp): directional
0.097-0.109 ms per frame in the flythroughs, positional 0.041 ms (PBR Lab) — no better than v4's +0.10-0.13: the
build is bound by memory traffic, not by fetch instructions or dispatches (a 4096² 16-bit atlas is 32 MB to read, the
4-texel windows 16 MB to write and read back).

**Build v6**: each workgroup loads the 9×9 "parts" of its 8×8 windows once into shared memory (a window
is two neighbouring parts per axis, shared with the next window) instead of 2×2 parts per window.

**Build v7** (`fellow21`): the 4-texel windows are not stored at all. Every looked-up box is widened by 1.5 texels on
each side, so it is at least 3 texels wide and the lookup never used them except for boxes clamped at the atlas edge
(those now take the 8-texel windows: still a window that contains the box, so still exact). The finest level is built
directly from the atlas (4×4-texel parts, four gathers each): the 16 MB write and re-read are gone and the pyramid's
memory drops from 21 MB to 5.3 MB for a 4096² atlas (RG16). RTX (`m3_f21_profile`): build 0.063 ms directional
(Bistro exterior, Sponza), 0.057 + 0.029 ms directional + positional (PBR Lab) — 40 % less than v5. Interleaved A/B of
the milestone-3 candidates with v7 (`m3_f21_perf`, `fellow21`, defaults vs + light depth slices, zero-attenuation
skip and both pyramids, 3 runs each):

| scene | defaults | + candidates | Δ | p |
|---|---:|---:|---:|---:|
| Sponza | 8.342 | 7.122 | **−14.6 %** | < 0.001 |
| Lights Lab | 3.493 | 3.025 | **−13.4 %** | < 0.001 |
| Bistro interior | 4.224 | 4.095 | **−3.1 %** | 0.002 |
| Bistro exterior | 5.011 | 4.939 | **−1.4 %** | 0.035 |
| PBR Lab | 2.801 | 2.824 | +0.8 % | 0.39 (within noise) |

(With v4's build the same comparison gave PBR Lab +6.0 %, p < 0.001, `m3_f19_perf`.)

## Rebuilding per written rect, merged again (`fellow22`, `fellow24`)

With the shadow cache, `fellow22` clears each stock cascade's own rect instead of the whole atlas (the cached
cascades keep their content) and rebuilds the pyramid per written rect rather than over their union. That turned one
dispatch chain into one per cascade: the build rose from 0.06 to 0.09 ms in Sponza and Bistro exterior
(`m3_final_profile`), where all four cascades are written every frame the camera moves (cache copies). `build_rects`
(`fellow24`) rebuilds the union when it is at most twice the rects' area (four cascades tiling the atlas: one
chain), else per rect. lavapipe: identical (PBR Lab captures, Stress Lab sequences, Lights Lab walk, Blend Lab).

## Only the part of each cascade the camera can look up (`shadows/max_mip_visible_region`, `fellow25`)

The build reads the whole rewritten atlas (bandwidth-bound: 32 MB for a 4096² 16-bit atlas). The scene shader looks
cascade i up only for surfaces in the camera frustum closer than the cascade's split (its own range, and the blend
into it from the previous cascade). Projected into the cascade, the frustum from the eye to that depth covers about
half of the cascade's square (stable cascades are fitted to a sphere around the frustum slice; an offline model gives
44-53 % for its bounding box at sun elevations of 30-70°). With the toggle, the finest level is computed from the
atlas only over that bounding box plus a margin (max(32, size / 16) texels, for the filter boxes near its edge and the
windows that contain them); every other finest-level window is set to **(0, 1)**. That pair bounds any depth, so every
window containing it proves nothing and no early-out fires there: **exact whatever region is chosen** — the region
only decides where the early-outs are available. A cascade whose texels were not written keeps its windows unless the
camera now needs more of it (then it is recomputed over the union); orthographic cameras and multiview compute the
whole cascade.

lavapipe: **0 differing pixels** with the toggle off vs on (PBR Lab 7 cameras, Stress Lab 4 cameras — second
viewport, moving sun — and its two motion sequences, Blend Lab sequence). Texels computed from the atlas (directional
and positional together): 59 % (PBR Lab), 64 % (Stress Lab, Blend Lab). Early-outs lost (a debug build that marks
every directional early-out, toggle off vs on): 562 of 115,597 pixels on one PBR camera, 88 of 83,013 on another,
none on the other 5 PBR and 4 Stress Lab cameras. RTX (`fellow27`; the first build, `fellow25`, did not compile with
MSVC): identical (`m3_f27_temporal`, toggle off vs on: Bistro exterior 99.0 dB, max |d| 0; Sponza, Lights Lab,
Bistro interior and PBR Lab at their run-to-run floor). Build time (`m3_f27_profile`): 0.062 → 0.055 ms (Bistro
exterior), 0.064 → 0.057 (Sponza), 0.072 → 0.067 (PBR Lab, both atlases): −0.005 to −0.007 ms, far less than the
texels it no longer reads, so after `build_rects` the build is no longer bound by reading the atlas but by its chain
of dependent dispatches. Frame time (`m3_f27_perf`, 3 runs): within noise in every scene (−0.8 to +1.2 %, p ≥ 0.09).
**REJECT** (stays off): exact, no measurable gain.

## One clear of the whole directional atlas when nothing is cached (`fellow27`)

Since `fellow22` the stock cascades clear their own rects when the lights tile the atlas (so cached cascades keep
their content). In a scene whose cascades are never cacheable (PBR Lab: the camera's cascades change texel size or
fail the cost gate in 99 % of frames), that replaced stock's single clear of the whole attachment with four partial
clears: the shadow pass took 0.135 ms instead of stock's 0.113 (`m3_final_profile`, `m3_f24_profile`). `fellow27`
clears the whole atlas, as stock, whenever no cascade of the frame comes from the cache, and per rect only when some
do. lavapipe: identical (PBR Lab and Stress Lab captures, which have both kinds of frames, and the Stress Lab
sequences). RTX: PBR Lab shadow pass 0.135 → **0.114 ms** (stock 0.114, `m3_f27_profile`); frame time PBR Lab
2.883 → 2.826 ms (−2.0 %, p 0.07; vs stock −0.2 %, within noise: the +1.8 % of `m3_final_perf` is gone).



## Decision

* `skip_backfacing_directional`, `skip_backfacing_positional`, `skip_unblended_cascade`: **KEEP** (image-identical;
  opaque pass −0.07 to −0.37 ms in the night-state profiles; day-state confirmation in `fellow-m2_perf`).
  Caveat (`m3_f33a_ablation`, `fellow29`, Lights Lab opaque pass, one profiled run each): without the zero-attenuation
  skip (0008) — the milestone-2 configuration — `skip_backfacing_positional` costs +0.11 ms (1.666 vs 1.557 ms; stock
  1.523): every light of the cluster then reaches the N·L test, most of them only to be dropped later by their
  attenuation. With the zero-attenuation skip (the default) it costs nothing there (1.202 vs 1.201 ms) and saves in
  Bistro interior (`m3_f24_perf`: +3.0 % without it, p 0.002). It should stay paired with the zero-attenuation skip.
* `directional_max_mip`, `positional_max_mip`: v1 **REJECT** (exact, no net gain: build cost ≈ savings). v7 (min/max
  windows, one fetch, no 4-texel windows, rebuilt per written rect) **KEEP, on by default from `fellow22`**: exact
  (lavapipe bit-identical, RTX at every scene's run-to-run floor), Sponza −14 to −15 %, Bistro interior −3 to −4 %
  (positional), Bistro exterior −1 %; the cost is the build, 0.06 ms per frame for a fully rewritten 4096² directional
  atlas (0.03 ms positional), which scenes with little soft-shadow filtering pay without a matching saving; with v7 no
  scene is slower (PBR Lab +0.8 %, within noise, `m3_f21_perf`). Final numbers: milestone-3 report (`m3_final_*`).
* `pcss_directional_penumbra`: **KEEP as an option** — a correctness fix of stock's directional PCSS that restores
  shadows stock loses, but a look change (sharper penumbrae), so it is not enabled silently.
* `pcss_blend_band`: **KEEP as an option** (performance, look change); with the penumbra fix it no longer exposes the
  first-cascade artifact.
