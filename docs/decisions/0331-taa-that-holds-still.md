# 0331 — TAA that holds still: TAA v2's history kept where the frame's jitter disagrees with it

Status: **KEEP** — `rendering/fellow/postfx/taa_v2_stability` (new, **1.0**), `taa_v2_stability_contrast` (new, 1.0),
`taa_v2_soften_untrusted` (new, **1.0**); the rooftops' facades' relief at the pixel's level of detail (showcase) ·
Branch `fellow/geometry` · Code:
* `servers/rendering/renderer_rd/shaders/effects/taa_resolve.glsl` (`temporal_antialiasing_v2()`: the stability term,
  the motion softening's condition), `effects/taa.{h,cpp}` (three push constants), `servers/rendering/fellow_settings.{h,cpp}`,
  `doc/tools/fellow_settings_docs.py` (and `taa_v2_antiflicker`'s description, which had described something else);
* `showcase/rooftops/shaders/facade_pbr.gdshader` (`filtered_relief`, on);
* `showcase/tools/flicker_look.gd` (new: the rooftops' frames with the camera still and moving, a feature off per
  sequence, `--set`, `--sun`, `--shader_param`), `benchmarks/tools/flicker/flicker_metrics.py` (new: flicker, flicker
  under motion compensated for the motion, sharpness), `benchmarks/tools/temporal_sweep.py` (`--ref-session`).

Checks: the RTX 5070 Ti laptop at 1920x1080 (the owner allowed the GPU again for this), jobs f541-f550: the rooftops
from two cameras still and moving with every rendering feature off in turn, then nineteen TAA configurations; PBR Lab's
five temporal sequences against their accumulated ground truth (2x2 SSAA, 8x MSAA, 8 jitters: decision 0001's method,
the references accumulated again, f545); lavapipe for the first sweep; the regression suite (no TAA in it: unchanged).

## Problem

The owner: "when I last ran the rooftops showcase there was a lot of shadows flickering on the buildings". The first
suspects were the shadows' own noise — the 2.5° sun's soft shadows (PCSS rotated per pixel and frame), FellowEngine's
contact shadows (a ray march dithered per frame), the directional shadow cache, SSAO and SSIL, SDFGI and its screen
gather.

Measured: none of them. A frame-to-frame measure — each pixel's second temporal difference |I(t+1) − 2 I(t) + I(t−1)|
over the buildings (`flicker_metrics.py`), with the camera still and, compensated for the motion, moving — on the RTX
with each feature off in turn changed by at most a tenth. TAA off flickered **less** with the camera still (2.2 % of the
buildings' pixels changing by more than 3 levels, against 7.6 %). The flicker was TAA v2's own: the jitter's sub-pixel
aliasing — the shutters' slats, the rusticated stone's sunlit grains, the joints' dark, which read as shadows — followed
from frame to frame instead of averaged. Stock Godot's TAA on the same frames: 1.7-1.9 % still, a quarter of v2's
flicker under motion.

Why: v2's feedback (Lottes; decision 0001) lowers the history's weight where the frame disagrees with it — right for a
real change, wrong for the jitter: a slat seen from one jitter position and missed from the next disagrees every frame,
and v2 followed it (feedback 0.88: an eighth of each new frame). Stock does the opposite ("reduce flickering": the more
the frame disagrees, the more history). And under motion v2's motion softening (decision 0001's fix for foliage) pulled
every contrasty moving pixel's history towards the current frame's 3x3 mean — the current frame's aliasing, every frame.

## Change

* **`taa_v2_stability`** (new, 0-1, default 1): where the history is trusted — inside the neighbourhood's colour box,
  v2's existing `trust` — its weight is 1 − (1 − `feedback_max`)·w², w = 1 − |tonemapped luma of the frame − of the
  history| × `taa_v2_stability_contrast` (1: stock's measure). A real change moves the box: the history is clipped, the
  trust falls and v2's responsive feedback stays.
* **`taa_v2_soften_untrusted`** (new, default 1): the motion softening (0.35, as before) only as far as the history was
  not trusted — a leaf's edge passing, a fence's gap — not a facade's detail reprojected where it was.
* `taa_v2_antiflicker` (feedback held at its maximum where trusted) stays as it was, off; its description now says so.
* The showcase's facades (`facade_pbr.gdshader`): the parallax read its height map at its finest level and the textures
  with the parallax's own derivatives (a pixel's neighbours' hits far apart: the coarsest mipmaps chosen at random); now
  the height map at the pixel's level of detail, the relief faded out where its texels are finer than an eighth of a
  pixel, the textures with the surface's gradients, the joints' darkening from the mipmapped height where the relief has
  faded (`filtered_relief`; off: as before).

## Measurements

The rooftops, RTX, 1920x1080, 24 frames still then 48 moving at 1.4 m/s (`flicker_look.gd`), the windows' glass plain:
the share of the buildings' pixels whose second temporal difference exceeds 3 levels (still) or whose
motion-compensated residual does (moving), and the sharpness (the mean |I − I blurred by a pixel| of a still and a
moving frame):

| TAA | east still | east moving | east sharpness | north still | north moving | north sharpness |
|---|---:|---:|---|---:|---:|---|
| stock Godot | 1.65 % | 2.20 % | 3.49 / 2.79 | 1.90 % | 2.47 % | 3.69 / 3.10 |
| v2, milestone 188 | 6.67 % | 8.04 % | 3.76 / 2.57 | 7.60 % | 9.63 % | 3.95 / 2.45 |
| **v2, milestone 189** | **1.75 %** | **1.63 %** | 3.68 / **2.84** | **1.92 %** | **1.92 %** | 3.94 / **3.20** |

Steadier than v2 by 4-5 times, as steady as stock still and steadier moving, and sharper than either under motion.
With the windows' rooms on (milestone 188), as the showcase ships (f550): east still 6.53 → 1.69 %, moving 7.63 → 1.54 %;
north 7.73 → 2.00 %, 9.72 → 1.82 %.

PBR Lab's temporal sequences against their ground truth (f545, f549), PSNR dB / flicker against the truth's own change:

| sequence | stock | v2, milestone 188 | v2, milestone 189 |
|---|---:|---:|---:|
| camera pan | 36.12 / 0.00644 | 37.62 / 0.00564 | 37.55 / **0.00542** |
| moving object | 37.76 / 0.00061 | 38.46 / 0.00074 | 37.79 / **0.00058** |
| thin dolly | 32.10 / 0.00841 | 34.09 / 0.00681 | 33.01 / 0.00701 |
| specular orbit | 31.63 / 0.01025 | 31.74 / 0.01042 | 31.57 / 0.01037 |
| foliage strafe | 31.51 / 0.01265 | 32.19 / 0.01261 | 30.66 / 0.01375 |

Less flicker against the truth than v2 where the camera pans and around the moving object; 0.1-1.5 dB further from the
truth, most where foliage is strafed (milestone 2's case for the motion softening: there the history is trusted, and the
softening that helped it is the one that made the facades shimmer).

Cost: none to measure — PBR Lab at 1080p (f550), three interleaved runs each: the frame's GPU time 2.04-2.07 ms against
2.07-2.08 ms with milestone 188's resolve (the terms are a few ALU operations in the same dispatch).

## How it got there

* Lavapipe first (no GPU at the start): every feature off, one process each (lavapipe's LLVM kills a rooftops run
  past 7 GB). Nothing stood out; at 640x360 the people and the banner dominated.
* On the RTX (f541): every feature off in turn, two views — only TAA mattered, and off it flickered less.
* f542: `taa_v2_antiflicker` 1 halved the still flicker (6.7 → 3.4 %), with feedback 0.96 1.2 %; stock's TAA was still
  steadier under motion. The facades' relief filter changed nothing measurable (kept: correct mipmaps, and no 32-step
  parallax where it cannot be seen).
* f543: the stability term as first written — the luma difference relative to the brighter of the two, v2's — 0.5-0.7 %
  still; under motion 6-7 % until the motion softening went (1.8 %). Against the ground truth (f545) that cost 0.4-2 dB,
  2 dB where an object moves before a still camera: weighing a sample by its relative difference weighs one darker than
  the history less than one brighter, and the resolve drifted brighter than the truth. Halving it (f547) passed stock
  in every sequence but left half the flicker.
* f548: the absolute difference of the tonemapped lumas, stock's — times 1, 2 or 4: times 1 as steady as stock and as
  accurate as stock where an object moves; more contrast steadier still, less accurate.
* f549: the motion softening only where the history was clipped: the facades under motion 2.4 → 1.6 % and sharper (the
  softening had blurred them), the truth's sequences within 0.1 dB of the same without, except foliage.

## Decision

KEEP: `taa_v2_stability` 1, `taa_v2_stability_contrast` 1, `taa_v2_soften_untrusted` 1.

## Rejected / not done

* The shadows' features (soft shadows' quality, the sun's size, contact shadows, the shadow cache, SDFGI's gather):
  measured innocent.
* The relative stability (f543) and stronger contrasts (2, 4: f548-f549): steadier when still (0.5-1.1 %), 0.3-2 dB
  further from the truth.
* No motion softening at all, or 0.1-0.2 (f543, f547-f548): as steady, foliage further from the truth still.
* `taa_v2_feedback_max` 0.96 globally: more history everywhere (lag).
* The shutters as a simpler mesh far away (their slats are 15 000 triangles a house): TAA resolves them now.
