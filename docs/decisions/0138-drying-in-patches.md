# 0138 — Wetting and drying in patches

Status: **KEEP** (`FellowRain3D.drying_patches` 1; at a wetness of 0 or 1 nothing changes) · Branch `fellow/geometry` ·
Node: `FellowRain3D.drying_patches` · Code: `servers/rendering/renderer_rd/shaders/forward_clustered/scene_forward_clustered.glsl`
(`fellow_wet_surface()`), `scene_forward_clustered_inc.glsl` (`FellowWeatherData::frost.z`), `effects/fellow_weather.{h,cpp}`
(`State::drying_patches`), `modules/fellow_fluid/fellow_rain_3d.{h,cpp}`.

## Problem

The wetness (0124) is one number for the whole scene: as a street dries after the rain (`wetness_follows_rain`, or a
FellowWeather3D's ground model, 0132) every surface turns from wet to dry at once, evenly, and the puddles fade out like
a dissolve. In a time-lapse the street dims uniformly; a real one dries in patches — the high spots first, the dips and
the puddles last, shrinking to their deepest — and wets the other way round.

## Change

Between dry and wet (`0 < wetness < 1`) and with `drying_patches` (0-1) above zero, the surface's own water (the
darkening of porous materials, the film's gloss and its ripples) takes a wetness of its own: the puddles' noise (0124)
taken as the ground's lows — where the puddles stand — and highs, with a broad noise (cells of 20 m, turned: the
puddles' 70 m repeat is not seen from above) and a fine one at the edges (9 cm); where `1.4 × wetness − 0.2` exceeds
the height (± 0.08, smoothly) the surface is wet, elsewhere dry. Walls take the noise across their face. The puddles'
amount is scaled by `smoothstep(0.15, 1, wetness)` and they appear from 0.1 to 0.3 of the wetness instead of 0.4 to
0.9: as the ground dries they shrink to their deepest instead of fading. `drying_patches` mixes the two (0: the even
wetness as before). At a wetness of 0 or 1 the branch is skipped: the same image bit for bit.

## Verification

* **Off is the previous image** at full wetness (lavapipe, against milestone 37's build): Rain Lab raining without
  drops (5 images), the wind meadow under snow (3) — 0 pixels differing.
* **Lavapipe, the test scene** at a wetness of 0.3, 0.6 and 0.85: mostly dry with a wet patch in the distance; large
  wet patches and dry ground between them; nearly all wet with the puddles — against the even, uniformly damp ground of
  0.6 without the patches. The first pattern (two noises of 80 and 20 cm) mottled the ground without patches one would
  see drying; the puddles' noise gives patches of metres. From 400 m up the puddles' 70 m repeat showed as a grid; the
  broad noise breaks it.

## Results

**Cost** (RTX 5070 Ti Laptop, 1080p, Bistro exterior half dry — wetness 0.5, the ground held — even against in
patches, 2 runs each, `m38_f315_patches`, fellow236): the depth pre-pass 0.720 → 0.818 ms, the opaque pass 1.564 →
1.634 ms; the frame 4.677 → 4.891 ms (+0.21 ms) while the ground is between wet and dry. The puddles' noise is looked up
twice on flat ground (for the patches and for the puddles): reusing the first lookup for the puddles changed the image
at full wetness too (lavapipe: up to 4 levels in 225 pixels of Rain Lab — the compiler fuses the reordered sums
differently), so the lookups stay as they are. The first measurement (`m38_f313_patches`) and stills (`m38_s23_bistro_drying`) showed a dry street: a clear
weather above freezing creates no rain node, so the ground given to the bench was dropped (fixed in FellowBench).

**Bistro half dry** (`m38_s25_bistro_drying`: wet, half dry even, half dry in patches; `bistro_drying_patches.jpg`):
the square wet in its dips and dry on its highs where the even wetness was a uniform damp sheen. **Drying after the
rain** (`m38_s23_bistro_drying_loop`, fellow234 — valid: the rain node is there in the rain; `loop_bistro_drying.webp`):
the rain stops and the street dries in 9 s, the puddles shrinking.

## Decision

**KEEP.**
