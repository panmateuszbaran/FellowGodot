# 0113 — Lens flare (ghosts and halo)

Status: **KEEP as an option**, 0 by default — a visual feature (milestone 19) · Branch `fellow/geometry` · Settings
`rendering/fellow/post/bloom/lens_flare` (0: off), `lens_flare_threshold` (20, scene-linear) · Code:
`effects/fellow_bloom.*` (`MODE_FLARE`), `shaders/effects/tonemap.glsl`. Needs the bloom (0112).

## Problem

Unreal's image-based lens flare: ghosts of the bright sources mirrored through the image's centre, tinted by a lens's
coatings, and a halo. Godot has none.

## Change

Chapman's screen-space lens flare at quarter resolution, from the bloom's 1/8 level (blurred already): six ghosts along
the line from each pixel through the image's centre (spacing 0.3 of that line; tints of a lens's coatings; each fading
towards the image's edges), the red and blue channels displaced along the line (chromatic), and a halo — a ring of 0.45
of the image's width around the centre. Only what is brighter than the threshold (luma, scene-linear) makes ghosts. The
tone mapping pass adds it (× `lens_flare`) before the exposure.

Godot's TAA blends in a tone-mapped domain (x / (1 + x)): a sun disk two or three pixels wide, half covered from jitter
to jitter, comes out of it at a few units instead of tens of thousands (lavapipe at 960 × 540: 5). The threshold is 8
(Unreal's lens flare threshold); a small sun at a low resolution may stay under it.

## Verification

At 0 (the default) the image is the previous build's bit for bit.

## Results

See milestone 19's report.
