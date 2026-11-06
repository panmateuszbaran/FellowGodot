# 0115 — Clouds traced a quarter of the pixels per frame

Status: **KEEP**, on (part of the clouds, decision 0109, off by default) · Branch `fellow/geometry` · Setting
`rendering/fellow/post/clouds/interleaved` (true) · Code: `shaders/effects/fellow_clouds.glsl` (`MODE_TRACE`,
`MODE_TEMPORAL`), `effects/fellow_clouds.*`. **Fixed in decision 0155** (milestone 54): a traced pixel's history was
clamped to its own trace (no other trace of the frame in its 3 × 3 pixels) and lost — a checker of noise; the bounds
now come from the traces of the 3 × 3 blocks around (a still camera 45.3 → 51.5 dB, a pan 44.4 → 47.5 dB against the
full trace).

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

lavapipe, City Lab, a 60-frame pan along the sky at 60° per second (`city_sky_pan`), 960 × 540: every frame of the
interleaved trace within 47.7-49.0 dB of the full trace. The first version gave a pixel without history (uncovered by
the pan) its own trace of up to three frames ago, from where the screen was then: blocky trails at the leading edge
(31.8 dB); it now takes the mean of this frame's traces around it. RTX (`m19_f258`): City Lab's clouds 0.505 →
0.325 ms; Sponza 0.139 → 0.134 ms (little sky: what is left is the temporal and composite passes).
