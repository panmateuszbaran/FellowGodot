# 0106 — Local exposure (Unreal's)

Status: **KEEP as an option**, off by default — a visual feature (milestone 18) · Branch `fellow/geometry` · Settings
`rendering/fellow/post/local_exposure/enabled` (false), `highlight_contrast` (0.8), `shadow_contrast` (0.8),
`detail_strength` (1.0), `blurred_blend` (0.6), `blur_size` (0.5) · Code: `effects/fellow_exposure.*`,
`shaders/effects/fellow_exposure.glsl` (`MODE_BUILD`, `MODE_GRID_BLUR`, `MODE_TILE_BLUR`), `effects/tone_mapper.*`,
`shaders/effects/tonemap.glsl` (set 4).

## Problem

One exposure for the whole frame: a sunlit street with deep shadows, or a room with a bright window, cannot show both
— the eye (and Unreal 5's local exposure, on by default there) compresses the large-scale contrast and keeps the local
detail.

## Change

Unreal 5's method. The exposure pass's cells (32 × 32 pixels) build a bilateral grid of log luminance: per cell and per
1-EV slice of [−16, 16] the sum of the pixels' log luminance and their count (as fractions of the cell's pixels, so
that interpolating and dividing gives a weighted mean), blurred by a 3 × 3 × 3 binomial kernel; and the cells' mean
log luminance, blurred by a Gaussian half the screen wide (`blur_size`). In the tone mapping pass (specialized
pipelines only) each pixel's log luminance l is split into a base — the grid at the pixel's own luminance (the
bilateral filter: edges are kept), mixed with the wide blur (`blurred_blend`) — and detail l − base. The base's distance
from middle grey (0.18 after exposure) is scaled by `highlight_contrast` above it, `shadow_contrast` below; the detail by
`detail_strength`; the pixel is multiplied by 2^(new l − l). Without auto exposure the middle grey follows the manual
exposure.

## Verification

Defaults: bit for bit the previous build (decision 0105's check; the tone mapper's new set is bound in every pipeline,
its code only in the specialized ones with the feature on).

## Results

City Lab (lavapipe): the shadowed street lifted, the sunlit walls and ground slightly darker, no halos at the
buildings' edges against the sky. RTX cost: see milestone 18's report.
