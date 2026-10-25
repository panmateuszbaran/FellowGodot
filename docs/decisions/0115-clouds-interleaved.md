# 0115 — Clouds traced a quarter of the pixels per frame

Status: **KEEP**, on (part of the clouds, decision 0109, off by default) · Branch `fellow/geometry` · Setting
`rendering/fellow/post/clouds/interleaved` (true) · Code: `shaders/effects/fellow_clouds.glsl` (`MODE_TRACE`,
`MODE_TEMPORAL`), `effects/fellow_clouds.*`.

## Problem

The clouds' trace is most of their cost (milestone 18: 0.46 ms in City Lab at 1920 × 1080, where the sky fills half the
image): one ray per half-resolution pixel, 64 steps, six light samples per step in a cloud.

## Change

As the ray-traced effects' interleaving (decisions 0081, 0082): each frame one pixel of every 2 × 2 block of the
half-resolution trace is traced (the four in turn, diagonal order), the other three keep their last trace. The temporal
pass blends a fresh pixel with its reprojected history (0.7 of the history instead of 0.85, so that the response time is
about the same at a quarter of the rate) and gives the others their reprojected history alone, clamped to the 3 × 3
neighbourhood of the trace (every value at most three frames old). The first frame, and any frame after the history was
lost, traces every pixel.

## Verification

Off: the previous version's image. On: see the results.

## Results

See milestone 19's report.
