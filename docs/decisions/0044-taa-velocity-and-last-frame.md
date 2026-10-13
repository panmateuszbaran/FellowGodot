# 0044 — Two post-processing passes without needless waits and copies: TAA velocities in shared memory, SSIL's last frame without its full-size level

Status: **KEEP, both on by default from `fellow110`** (exact) · Branch `fellow/geometry` · Toggles
`rendering/fellow/postfx/taa_shared_velocity` (start-up), `rendering/fellow/postfx/ss_last_frame_direct` (runtime) · Code:
`shaders/effects/taa_resolve.glsl` (`FELLOW_TAA_SHARED_VELOCITY`), `effects/taa.cpp`, `shaders/effects/copy.glsl`
(`MODE_MIPMAP` with `FLAG_SANITIZE_INF_NAN`), `effects/copy_effects.*` (`make_mipmap(..., p_sanitize_source)`),
`effects/ss_effects.*` (`copy_internal_texture_to_last_frame(..., p_skip_full_size)`), `render_forward_clustered.cpp`,
`fellow_settings.*` (stat `postfx_last_frame_direct`).

## TAA resolve v2: the closest neighbour's velocity from shared memory

The resolve loads its 10 × 10 tile of colours and depths into shared memory, finds the closest depth of each pixel's
3 × 3 neighbourhood there, and then loads that neighbour's velocity from the velocity buffer: a global load that waits
for the search, and that the history's nine Catmull-Rom fetches wait for in turn. The previous frame's velocity at
the reprojected position (the disocclusion test) is loaded at the end. With the option:

* each tile texel's velocity is loaded with its colour and depth (clamped to the screen like them), and the closest
  neighbour's (or, without dilation, the pixel's own) is read from shared memory; the velocity written for the next
  frame's history too;
* the previous velocity is fetched as soon as the reprojected position is known, in flight with the history's fetches;
  the disocclusion factor is then computed from it with the same expressions as `get_factor_disocclusion()`.

The same values reach the same arithmetic. lavapipe: `foliage_walk` (60 frames) and Lights Lab and City Lab captures
**bit for bit**. RTX (`fellow109`, `m8_f124_taa_temporal`): Bistro exterior and City Lab bit for bit, Foliage Lab 78.2 dB
and Lights Lab 94.2 dB, their floors. TAA pass (`m8_f124_taa`, 2 runs, async compute off):

| scene | TAA pass |
|---|---:|
| Bistro exterior | 0.290 → 0.257 ms (−0.033) |
| City Lab | 0.258 → 0.239 (−0.019) |
| Lights Lab | 0.248 → 0.238 (−0.010) |
| Foliage Lab | 0.239 → 0.233 (−0.006) |

A start-up option (the shader's defines); on by default.

## SSIL's copy of the last frame without its full-size level

With SSIL on, each frame's colour is copied into a texture with six levels: level 0 a sanitized copy (infinities and
NaNs replaced) of the internal texture, levels 1-5 each the 2 × 2 average of the previous one. SSIL reads only level
5; half-size SSR (stock's default) reads level 1; only full-size SSR reads level 0. With the option, where nothing
reads level 0 (SSIL on, SSR off or half size), level 0 is not written and level 1 is made from the internal texture
directly, each of its four texels sanitized exactly as the copy sanitizes them before the same sum: the same values
as a mipmap of the copy. Levels 2-5 as before.

lavapipe (City Lab with SSIL and SSR, with SSIL alone; Lights Lab with SSIL and SSR; the direct path taken 91 times in
City Lab's run): **bit for bit**. RTX (`m8_f124_lf_temporal`): Bistro exterior bit for bit, Bistro interior 67.1 dB and
Sponza 75.5 dB, their floors. In one process (`m8_f124_lf_ab`, autotune off):

| scene | copy region | frame |
|---|---:|---:|
| Bistro interior | 0.075 → 0.039 ms | **−1.0 %** (p < 10^-10) |
| Bistro exterior | 0.081 → 0.052 | **−0.7 %** (p < 10^-21) |
| Sponza | 0.080 → 0.051 | −0.5 % (p 0.07) |

The level-0 texture is still allocated (its memory could go too: the levels would shift, which every reader would
have to follow). Runtime; on by default.

## Decision

**KEEP both, on by default** (`fellow110`): exact, and each removes work that stock does for nothing.
