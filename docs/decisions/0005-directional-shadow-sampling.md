# 0005 — Directional shadow sampling in the Forward+ scene shader

Status: **DRAFT (night-state measurements in; day-state confirmation pending)** · Branch `fellow/shadows` · Toggles:
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

## Decision

* `skip_backfacing_directional`, `skip_backfacing_positional`, `skip_unblended_cascade`: **KEEP** (image-identical;
  opaque pass −0.07 to −0.37 ms in the night-state profiles; day-state confirmation in `fellow-m2_perf`).
* `directional_max_mip`: **REJECT** (exact, but no net gain: build cost ≈ savings). Code kept, off by default.
* `pcss_directional_penumbra`: **KEEP as an option** — a correctness fix of stock's directional PCSS that restores
  shadows stock loses, but a look change (sharper penumbrae), so it is not enabled silently.
* `pcss_blend_band`: **KEEP as an option** (performance, look change); with the penumbra fix it no longer exposes the
  first-cascade artifact.
