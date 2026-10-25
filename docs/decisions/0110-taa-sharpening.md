# 0110 — Sharpening after TAA (contrast-adaptive, RCAS)

Status: **KEEP as an option**, off by default — a visual feature (milestone 18) · Branch `fellow/geometry` · Setting
`rendering/fellow/post/sharpen/strength` (0 = off, up to 1) · Code: `shaders/effects/tonemap.glsl` (specialization
constant 28), `effects/tone_mapper.*`, `renderer_scene_render_rd.cpp`.

## Problem

TAA resolves edges and specular aliasing by blending each pixel with its reprojected history, which is resampled
bilinearly every frame: the image ends up softer than the frame it started from. Unreal sharpens its TAA output
(`r.TemporalAA.Sharpness`, and TSR's own sharpening); Godot's TAA has nothing after it except FSR 1's sharpening when
FSR 1 is the scaler.

## Change

Where TAA is on and the strength is above 0, the tone mapping pass (no pass added) applies AMD FidelityFX's robust
contrast-adaptive sharpening (RCAS, FSR 1's sharpening pass) before the exposure: a 5-tap cross; the negative lobe
is the largest that keeps the result within the cross's minimum and maximum (per channel, the centre included), at
most −0.1875 (RCAS's limit), times the strength. It works in a tone-mapped domain (x / (1 + x)) and maps back, so a
bright pixel beside a dark one does not ring (RCAS assumes values in [0, 1]). A specialization constant: at 0 the
pipelines are the ones without it.

## Verification

At 0 (the default) the image is the previous build's bit for bit (lavapipe, City Lab, Crowd Lab, Lights Lab with TAA).

## Results

City Lab with TAA (lavapipe, 960 × 540), the mean absolute difference between neighbouring pixels against TAA alone:
+7 % (street) to +13 % (aerial) at 0.5, +26 % to +50 % at 1, without halos at the sunlit roofs' edges.
