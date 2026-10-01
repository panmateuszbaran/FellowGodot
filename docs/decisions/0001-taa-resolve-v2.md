# 0001 — TAA resolve v2

Status: **KEEP** (with one documented weakness: alpha-tested thin foliage under lateral motion) · Branch `fellow/postfx` (commits `c8a1995f0d`, fix `94f43415e7`) · Toggle
`rendering/fellow/postfx/taa_resolve_v2` (default **off**) · Binary used for all numbers: FellowEngine `7f257b56ff`

## Problem

Godot's TAA (Forward+, `servers/rendering/renderer_rd/shaders/effects/taa_resolve.glsl`, derived from Spartan
Engine) is the only temporal anti-aliaser that is on by default in a typical high preset without upscaling.
Reading the code (audit `docs/fellow_renderer_audit.md` §C.5.3) shows defects that matter for third-person camera
motion:

1. **Farthest instead of closest depth.** `get_closest_pixel_velocity_3x3` keeps `depth < min_depth`, but Forward+
   uses reverse-Z (depth cleared to 0, `GREATER_OR_EQUAL`), so it selects the *farthest* neighbour, and it fetches
   the velocity one texel up-left of that neighbour (tile coordinates include the 1-texel border).
2. **No dilated reprojection.** History is fetched at `uv + velocity(pixel)`; the dilated velocity only sizes the
   clip box. Silhouettes of moving objects therefore reproject with the background velocity (trails).
3. **Clip box collapse under motion.** `box_size = variance · smoothstep(0.02, 0.0, |v|)`; from 0.02 UV/frame on,
   the box degenerates to the 3×3 mean, so the output becomes ≈ 15/16 · mean3×3 + 1/16 · current — a blur during
   camera turns (the reversed smoothstep edges are also undefined behaviour in GLSL).
4. **Variance clip in linear HDR RGB.** Bright neighbours inflate σ; the box is loose near highlights and tight
   elsewhere.
5. **No NaN/Inf sanitisation** of the neighbourhood.

## Evidence (before the change)

* Code: items 1-5 above, with line references in the audit.
* Measured cost of the stock TAA pass (GPU timestamps, preset `high`, stock `godot-4.7.2_profile`, day state): 0.26-0.32 ms at
  1080p and 0.46-0.54 ms at 1440p, i.e. 3-12 % of the frame depending on the scene (audit A.2).
* Visual evidence: see "Actual visual quality" (sequences with camera pan / strafe / dolly vs. accumulated ground
  truth).

## Stock implementation

One compute dispatch (8×8 groups, 10×10 LDS tile of colour and depth), Catmull-Rom history fetch, 3×3 variance clip
in linear RGB (`clip_aabb`), blend 1/16 with off-screen and velocity-disocclusion terms and an anti-flicker weight in
per-channel Reinhard space, followed by copies temp → colour, colour → history and velocity → previous velocity.

## Proposed implementation (shader variant `FELLOW_TAA_V2`, same dispatch, same inputs)

* Closest depth under reverse-Z (maximum depth in 3×3) with the correct texel fetch; the **dilated velocity is used
  for reprojection** and for the disocclusion test.
* History and neighbourhood are compared in **YCoCg of a luma-weighted Reinhard tonemap** (Karis 2014); variance
  clip (Salvi 2016) with γ = 1.25, intersected with the neighbourhood min/max box. The box **does not collapse with
  velocity**.
* Feedback 0.88-0.9375 from the unbiased luminance difference between clipped history and current sample
  (Lottes/Playdead); off-screen and disocclusion force the current sample as in stock.
* Non-finite / negative samples are removed before the statistics; the resolved colour is kept inside the domain of
  the inverse tonemap.

### Iteration log (kept for honesty)

* v2 as first written produced **black blotches** on flat transparent surfaces (glass pane, transparent sphere):
  stock vs v2 PSNR 20 dB on a static camera (lavapipe). Isolation experiments on the Linux build:
  stock clip inside v2 → artefacts gone (49.8 dB); v2 without any clip → gone; NaN/Inf sanitising alone → unchanged;
  output = current sample only → gone. Root cause: in flat neighbourhoods the rounded mean can land just outside
  [min, max], so the min/max ∩ variance box became inverted (`cmin > cmax`) and the sequential `clip_aabb()` divided
  by ≈ 0, amplifying the history out of gamut; the latched value decayed only slowly. Fix (`94f43415e7`): box built
  around `clamp(mean, min, max)` (valid by construction) and a ray/box clip whose per-axis parameters are clamped to
  [0, 1] (`clip_towards_center`), so the result is always a convex combination of box centre and history.
  After the fix: 45.8 dB vs stock on the static camera, dark outliers 3715 → 2 px; NVIDIA capture checked visually.

## Expected benefit

Sharper image during camera motion (no collapse to the 3×3 mean), fewer trails at moving silhouettes (dilated
reprojection), less over-clipping near highlights; roughly equal cost (same dispatch, a few more ALU ops).

## Actual performance

Day state (RTX 5070 Ti Laptop ~1.75 GHz, 70-77 °C), preset `high`, variants interleaved ABBA with stock.
`fellow_v2` = resolve v2 **and** ping-pong (decision 0002); `fellow_pp` = stock resolve with ping-pong, so
`fellow_v2 − fellow_pp` isolates the resolve.

**TAA span (GPU timestamps, `fellow-m1_profile`):**

| Scene | Res | stock | ping-pong only | v2 + ping-pong | v2 resolve − stock resolve | v2 + ping-pong − stock |
|---|---|---:|---:|---:|---:|---:|
| PBR Lab | 1920x1080 | 0.317 | 0.260 | 0.304 | +0.043 ms | −0.013 ms (−4 %) |
| PBR Lab | 2560x1440 | 0.454 | 0.373 | 0.438 | +0.065 ms | −0.015 ms (−3 %) |
| Sponza | 1920x1080 | 0.272 | 0.235 | 0.252 | +0.018 ms | −0.020 ms (−7 %) |
| Sponza | 2560x1440 | 0.537 | 0.419 | 0.454 | +0.035 ms | −0.083 ms (−15 %) |
| Bistro ext. | 1920x1080 | 0.260 | 0.215 | 0.246 | +0.032 ms | −0.013 ms (−5 %) |
| Bistro ext. | 2560x1440 | 0.481 | 0.380 | 0.457 | +0.077 ms | −0.024 ms (−5 %) |
| Bistro int. | 1920x1080 | 0.250 | 0.204 | 0.241 | +0.037 ms | −0.009 ms (−4 %) |
| Bistro int. | 2560x1440 | 0.464 | 0.380 | 0.432 | +0.052 ms | −0.032 ms (−7 %) |

The v2 resolve itself costs **+0.02 to +0.08 ms** more than the stock resolve (YCoCg + tonemap math, 9-tap
statistics on tonemapped samples, dilated-velocity fetch); combined with ping-pong the whole TAA stage is still
**3-15 % cheaper than stock** in 8/8 configurations.

**Whole frame (`fellow-m1_perf`, 3-4 runs, Welch's t-test):** all 8 configurations are **within run-to-run noise**
(Δ between −3.7 % and +2.3 %, no p < 0.05 with |Δ| > 1 %; the FellowEngine null test in the same session varies by up
to −1.9 %). VRAM: +8-9 MB at 1080p (the ping-pong velocity texture).

→ At equal settings, FellowEngine's TAA stage is not slower than stock; frame-level differences are not resolvable
with 3 runs.

## Actual visual quality

Ground truth: 2×2 SSAA × 8× MSAA × 8-16 Halton jitters per frame, TAA off (`godot-4.7.2_reference*`). All images
display-referred; captures lossless.

**Temporal sequences (`fellow-m1_temporal`, 60 frames each at 1080p, analysis `fellow-m1_temporal_analysis`;
lower is better for the error columns):**

| Sequence | what it stresses | err in motion regions stock → v2 | err static | flicker vs GT | PSNR | SSIM |
|---|---|---:|---:|---:|---:|---:|
| camera_pan (45°/s yaw) | blur/rejection under camera motion | 0.00805 → **0.00659** (−18 %) | 0.00092 → **0.00068** | 0.00645 → **0.00523** | 36.11 → **37.95** | 0.9749 → **0.9838** |
| moving_object | ghosting behind a moving box | 0.00972 → **0.00831** (−15 %) | 0.00281 → **0.00272** | **0.00061** → 0.00074 | 37.72 → **38.40** | 0.9813 → **0.9849** |
| thin_dolly | comb and sub-pixel wires | 0.02130 → **0.01753** (−18 %) | 0.00133 → **0.00129** | 0.00843 → **0.00657** | 32.09 → **33.80** | 0.9513 → **0.9675** |
| specular_orbit | specular shimmer | 0.02453 → **0.02419** | 0.00261 → **0.00258** | 0.01026 → **0.01002** | 31.62 → **31.93** | 0.9598 → **0.9619** |
| foliage_strafe | alpha-tested grass + fence, lateral | **0.01429** → 0.01513 (+6 %) | 0.00194 → **0.00171** | **0.01268** → 0.01337 (+5 %) | **31.50** → 31.11 | 0.9391 → **0.9414** |
| sponza_pan | real content | 0.00789 → **0.00703** | 0.00671 → **0.00631** | 0.01075 → **0.00908** | 38.49 → **39.59** | 0.9437 → **0.9609** |
| bistro_ext_path | real content, vegetation | 0.01933 → **0.01781** | **0.00726** → 0.00763 | 0.02178 → **0.02018** | 28.86 → **30.36** | 0.8998 → **0.9202** |
| bistro_int_path | real content | 0.00911 → **0.00851** | **0.00462** → 0.00470 | 0.00918 → **0.00848** | 35.60 → **36.87** | 0.9641 → **0.9678** |

v2 is closer to the ground truth in **7/8 sequences by PSNR, 8/8 by SSIM, 7/8 in motion regions, 6/8 in flicker**.
The largest gains are exactly where the stock defects act: camera pan (no clip-box collapse), thin geometry, and the
Bistro/Sponza flythroughs (+1.1 to +1.5 dB).

**Measured weakness — `foliage_strafe`:** stock is visibly blurrier; v2 is sharper (closer to the ground truth in the
static-region error and SSIM) but shows more aliasing on the sub-pixel chain-link fence and grass-blade edges during
the lateral strafe (+6 % motion error, +5 % flicker, −0.4 dB PSNR). v2 keeps more history on thin alpha-tested
features that jitter in and out of pixels; the stock collapse-to-mean blur hides this. This matters for the target
workload (vegetation) and is the first follow-up for TAA (candidates: local-contrast-adaptive γ, a reactive/alpha mask
for alpha-tested materials, or a slightly wider box on disocclusion only).

**Fixed cameras vs ground truth (`fellow-m1_capture_gt`, auto exposure frozen like the ground truth, mean over each
scene's fixed cameras, 1080p):**

| Scene | SSIM stock → FellowEngine | PSNR | LPIPS |
|---|---:|---:|---:|
| PBR Lab | 0.9860 → **0.9881** | 38.70 → **39.09** dB | 0.0289 → **0.0219** |
| Sponza | 0.9821 → **0.9840** | 40.68 → **41.06** dB | 0.0281 → **0.0219** |
| Bistro exterior | 0.9719 → **0.9751** | 34.37 → **34.60** dB | 0.0321 → **0.0252** |
| Bistro interior | 0.9860 → **0.9875** | 38.23 → **38.48** dB | 0.0214 → **0.0173** |

(FellowEngine = v2 + ping-pong; ping-pong alone is image-identical within the GPU's run-to-run noise, decision 0002.)

## Regression risk

Shader-only; the stock path is untouched and selected when the toggle is off. Risks: more residual shimmer on
sub-pixel geometry (a less aggressive clip keeps more history), behaviour with FSR-style upscaling (not used: TAA
only runs at native internal resolution in this configuration).

## Decision

**KEEP** as part of the FellowEngine configuration: better image quality against the ground truth in 4/4 scenes on
static cameras (LPIPS −19 to −25 %) and in 7/8 motion sequences, at no measurable frame-time cost (TAA stage 3-15 %
cheaper than stock together with ping-pong; frame time within noise). The foliage_strafe regression is real and
documented; it is the first item of the next TAA iteration and the reason the toggle stays available.

## Milestone 2 follow-up: the `foliage_strafe` regression (2026-10-01)

Three sweeps against the same accumulated ground truth (`temporal` mode, 1920×1080, all values from
`benchmarks/results/m2_taa_sweep{,2,3}_table.md`; images are deterministic, so the night machine state does not
matter here).

1. **Parameter sweep** (`m2_taa_sweep`): anti-flicker 0.5 / 1.0, clip γ 1.5, feedback 0.95. Every variant is *worse*
   than v2 on `foliage_strafe` (30.51-30.90 dB vs 31.11) and on every other sequence. The regression is not a tuning
   problem of the clip box.
2. **Structural experiments** (`m2_taa_sweep2`, new tunables in the v2 shader):
   * `taa_v2_velocity_dilation=false` (reproject with the pixel's own velocity instead of the closest pixel's):
     worse on foliage (30.95 dB) and on 4 of 6 sequences. Rejected.
   * `taa_v2_motion_soften` *s* with `taa_v2_soften_contrast` *c*: under motion (velocity 0.25-1 px, smoothstep) and
     where the 3×3 neighbourhood's relative contrast (σ/μ of luma) exceeds *c*, the clipped history is pulled towards
     the neighbourhood mean by up to *s*. This is the targeted version of what stock does everywhere (stock's
     collapsed clip box blurs every moving pixel). s = 0.5: foliage **32.43 dB** (stock 31.50, v2 31.11), but camera
     pan and Sponza lose 0.7-0.9 dB against v2.
3. **Threshold sweep** (`m2_taa_sweep3`, all 8 sequences), PSNR dB:

| sequence | stock | v2 | s 0.5 c 0.3 | s 0.5 c 0.45 | s 0.5 c 0.6 | **s 0.35 c 0.45** |
|---|---:|---:|---:|---:|---:|---:|
| camera_pan | 36.11 | **37.95** | 37.27 | 37.31 | 37.33 | 37.57 |
| moving_object | 37.72 | 38.40 | **38.42** | 38.41 | 38.41 | 38.41 |
| thin_dolly | 32.09 | 33.80 | 34.04 | 33.99 | 33.93 | **34.09** |
| specular_orbit | 31.62 | **31.93** | 31.55 | 31.62 | 31.70 | 31.73 |
| foliage_strafe | 31.50 | 31.11 | **32.43** | 32.30 | 32.12 | 32.17 |
| sponza_pan | 38.49 | **39.59** | 38.71 | 38.92 | 39.08 | 39.09 |
| bistro_ext_path | 28.86 | 30.36 | 31.18 | **31.22** | 31.16 | 31.20 |
| bistro_int_path | 35.60 | **36.87** | 35.73 | 35.86 | 36.04 | 36.17 |
| **mean** | 34.00 | 35.00 | 34.92 | 34.95 | 34.97 | **35.05** |

`s 0.35 c 0.45` is the only tested configuration that is at or above stock in PSNR **and** SSIM on all 8 sequences
(v2 alone is below stock on `foliage_strafe` PSNR; `s 0.5 c 0.3` is below stock on `specular_orbit`), and it has
the best mean PSNR. Against plain v2 it trades 0.2-0.7 dB on four sequences (camera pan, specular orbit, Sponza,
Bistro interior) for +0.3 to +1.1 dB on three (thin dolly, foliage, Bistro exterior).

What it does **not** fix (kept visible): in the sub-metrics it is still slightly worse than stock in the motion-
region error of `specular_orbit` (0.02532 vs 0.02453) and `bistro_int_path` (0.00931 vs 0.00911), in the static-
region error of both Bistro paths (+5-8 %, as v2), and in flicker on `moving_object` (0.00074 vs 0.00061, as v2).

**Decision (milestone 2):** the FellowEngine TAA configuration becomes v2 + history ping-pong +
`taa_v2_motion_soften = 0.35`, `taa_v2_soften_contrast = 0.45`. The shader cost of the soften term is one extra
mean/σ already computed by the v2 clip (no extra fetches).
