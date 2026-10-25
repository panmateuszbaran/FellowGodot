# 0105 — Auto exposure from a luminance histogram (Unreal's eye adaptation)

Status: **KEEP as an option**, off by default — a visual feature (milestone 18) · Branch `fellow/geometry` · Settings
`rendering/fellow/post/exposure/histogram` (false), `low_percent` (10), `high_percent` (90), `speed_up` (3),
`speed_down` (1) · Code: `servers/rendering/renderer_rd/effects/fellow_exposure.*`,
`shaders/effects/fellow_exposure.glsl` (`MODE_BUILD`, `MODE_ADAPT`), `renderer_scene_render_rd.cpp`.

## Problem

Godot's auto exposure (camera attributes, `auto_exposure_enabled`) averages the brightest channel of every pixel (a
chain of 8 × 8 reductions) and moves the average towards it linearly. The arithmetic mean is dominated by the
brightest pixels — the sky, a window, the sun's reflections — so a view with a little sky is exposed for the sky, and
the step is linear in luminance (slow from dark to bright, fast the other way, and overshooting with large steps).
Unreal meters a histogram of log luminance, ignores the darkest and the brightest pixels (by default the bottom 10 %
and the top 10 %), and adapts exponentially in exposure stops with separate speeds.

## Change

Where auto exposure is on and the setting is on, one compute pass over the image (one workgroup per 32 × 32 cell,
shared with local exposure's grid, decision 0106) histograms the pixels' log2 Rec. 709 luminance (64 bins over
[−14, 14] EV); a 64-thread pass takes the mean luminance of the pixels between the two percentiles (Unreal's
`SumWithoutOutliers`, each bin at its centre), clamps it to the camera attributes' minimum and maximum, and adapts in
log2: `log L += (log L_target − log L) · (1 − 2^(−dt · speed))`, with `speed_up` where the exposure rises (the
scene got darker) and `speed_down` where it falls. The result is written where stock's reduction writes the frame's
average, so the tone mapper, glow's threshold and the camera attributes' `auto_exposure_scale` work unchanged. The
first frame (or a changed camera attributes resource) takes the target at once, as stock.

## Verification

With every setting at its default the image is the previous build's bit for bit (lavapipe, City Lab and Crowd Lab
with auto exposure on, 3 cameras each: 0 differing pixels).

## Results

City Lab with auto exposure (lavapipe, 960 × 540): the bright sky and the sunlit street no longer set the exposure —
mean image value 0.55 → 0.60 (street), 0.64 → 0.70 (aerial). RTX cost: see milestone 18's report.
