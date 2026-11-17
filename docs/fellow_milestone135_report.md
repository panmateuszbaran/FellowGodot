# FellowEngine — milestone 135 report (fading instances' shadows fade)

Status: **FINAL** (2026-11-17). Branch `fellow/geometry`. Lavapipe in the cloud; the laptop's RTX 5070 Ti
Laptop GPU (job f475).

## 0. Result

* The shadow passes dither the fades the view does (decision 0260): a caster fading by its visibility range (with
  `geometry/dithered_fade`) or keeping a share of its pixels (a streamed cell or mesh crossing over, decisions
  0258-0259) keeps that share of its shadow map's texels — measured from the main camera, with the same noise and on
  the same side as in the view — so its shadow fades with it, and two instances handing over cast one shadow.
* Shadow maps follow: fading casters are drawn with the directional cascades' dynamic casters (out of the caches
  meanwhile) and redraw their positional lights' shadows every frame of the fade and once after; a caster faded out
  entirely is not drawn (a positional light's casters include those their range hides).
* Not faded: an omni light's cube map (filtered into the atlas: whole while fading, gone when faded), an HLOD's
  proxies and members and a MultiMesh's per-instance fades (their shadows per cascade).
* A box fading out at 40 m (the same on lavapipe and the RTX): its shadow's depth 1.00 → 0.42 (half faded) → 0.00
  (gone) and back in the sun (cached), 1.00 → 0.46 → 0.00 for an omni light's paraboloids; handing over to a box of
  the same size, 1.00 (one shadow); a streamed ball's swap keeps its whole shadow halfway (0.997, 1.001). The image
  regression suite exact.
* Cost on the RTX: 8 000 casters all mid-fade, the same GPU time, the renderer's CPU +0.6 ms (drawn every frame
  instead of cached); Frontier Lab crossing over 4.15 ms against 4.24 ms with switching shadows — within the runs'
  spread, four more shadow draws.

## 1. Checks

`bench/dithered_fade_check.gd` section 8 (`--only=shadows` runs it alone) and its cost section's shadowed lanes; the
image regression suite (exact).

## 2. Next

FellowEngine has been tuned and checked on one GPU, the RTX. The laptop's Core Ultra has an integrated Intel GPU
(which drives its panel): Intel's Vulkan driver, a second vendor's hardware — the readiness review's largest gap
(milestone 136).
