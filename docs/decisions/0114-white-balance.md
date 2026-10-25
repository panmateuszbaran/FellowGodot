# 0114 — White balance (temperature and tint)

Status: **KEEP as an option**, 6500 K and 0 by default (off) — a visual feature (milestone 19) · Branch
`fellow/geometry` · Settings `rendering/fellow/post/white_balance/temperature` (6500 K), `tint` (0) · Code:
`renderer_scene_render_rd.cpp` (`_fellow_white_balance_matrix`), `shaders/effects/tonemap.glsl` (specialization
constant 30).

## Problem

A scene lit only by a clear sky is as blue as the sky (milestone 18: Sponza's courtyard and Bistro's shaded streets
with the sky atmosphere); a camera, or Unreal's post-process white balance, adapts to the light. Godot's colour
correction (a LUT, adjustments) has no white balance.

## Change

As Unreal's: the scene's white — CIE daylight at the temperature (the Planckian locus below 4000 K; 6500 K taken as
D65), moved along the isotherm by the tint (Krystek's locus in CIE 1960 uv; a positive tint moves it towards green, so
the image turns magenta) — is adapted to D65 by the Bradford transform: a 3 × 3 matrix in linear sRGB, computed on the
CPU, applied in the tone mapping pass before the bloom and the exposure. Semantics of a camera's setting: 8000 K makes
a scene lit by bluish light neutral (the image warmer), 5000 K makes it bluer.

## Verification

At 6500 K and 0 (the default) the image is the previous build's bit for bit. The matrix (Python, the same formulas):
white at 6500 K → (1.001, 1, 1.001); 8000 K → (1.10, 0.99, 0.83); 5000 K → (0.83, 1.02, 1.37); tint +0.3 →
(1.20, 0.93, 1.19).

## Results

RTX (`m19_f258`): free (−0.002 ms, n.s.). At 7000 K Sponza's sky-lit floor turns from blue to grey and Bistro's red
awnings stay red in the shade (milestone 19's report).
