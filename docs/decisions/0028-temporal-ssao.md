# 0028 — Temporal SSAO: half the taps per frame (visually lossless)

Status: **MEASURING** · Branch `fellow/geometry` · Toggle `rendering/fellow/postfx/ssao_temporal` (runtime, off) · Code:
`shaders/effects/ssao.glsl` (`FELLOW_TEMPORAL`), `effects/ss_effects.{h,cpp}`, `render_forward_clustered.cpp`
(`_process_ssao`), `fellow_settings.*`.

## Problem

SSAO is the most expensive screen-space effect in every scene (`m5_f69_sizing`: 0.5-0.7 ms, 25-35 % of the GPU frame in
the labs and City Lab, 8-16 % in Sponza and Bistro); exact work on its gather found nothing (0011, 0024). Stock's own
cheaper setting, half size, is not visually lossless: against full size, captures 46 dB in Sponza and Bistro exterior
(5-7 % of the pixels off by more than 2 levels: the fine AO in creases and folds is lost), temporal sequences 47.5 dB
in Bistro exterior (`m6_f81_*`).

## Change

The gather (high quality: 12 taps per pixel, each a mirrored pair) takes, each frame, every other tap: even taps in even
frames, odd taps in odd frames. Their obscurance and weight sums are written, per deinterleaved slice, into a history
(RGBA16F: sums, view depth, which half). Each pixel then adds the other half read from the previous frame's history at
the pixel its point was in (reprojected with the previous camera, without TAA jitter), if the depth there matches within
2 % and that texel holds the other half; otherwise it uses its own half twice. The detail AO, edges, fade and the rest
of the pipeline (blur, interleave) are unchanged.

A still view sums every tap of the stock gather each frame (the two halves come from consecutive frames, whose depth
differs only by the TAA jitter); a moving view gets each pixel's other half from where its surface was a frame before,
the same estimate with a different noise; newly visible pixels use half the taps for one frame.

## Verification plan

* lavapipe: still captures (temporal off vs on), the `city_pan` sequence: PSNR.
* RTX: captures and temporal sequences against stock full size (and stock half size for scale); in-process A/B.
