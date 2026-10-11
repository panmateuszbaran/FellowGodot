# 0030 — Temporal SSIL: half the taps per frame (visually lossless)

Status: **KEEP as an option** (off by default) · Branch `fellow/geometry` · Toggle `rendering/fellow/postfx/ssil_temporal` (runtime, off) · Code:
`shaders/effects/ssil.glsl` (`FELLOW_TEMPORAL`), `effects/ss_effects.{h,cpp}`, `render_forward_clustered.cpp`
(`_process_ssil`), `fellow_settings.*`.

"Visually identical, measured" track (as 0027, 0028): not bit-exact, judged by image comparisons against the stock
gather.

## Problem

SSIL is the third most expensive screen-space effect where it is used (`m5_f69_sizing`: 0.32 ms in Bistro exterior,
0.40 in Sponza, 0.49 in Bistro interior, blur and interleave included). Its gather is the same scheme as SSAO's (ASSAO:
four deinterleaved slices, mirrored tap pairs, 12 taps per pixel at high quality), and every tap reads three textures
(a depth mip, the normal, the previous frame's colour mip), so the gather dominates.

## Change

As temporal SSAO (0028): each frame takes every other tap (even taps in even frames, odd in odd frames); the half's
colour and obscurance sums (RGBA16F) and its weight sum and view depth (RG16F; the weight sum's sign says which half: it
is never zero) are written per deinterleaved slice into one of two pairs of histories; each pixel adds the other half
read from the previous frame's histories at the texel where its point was (reprojected with the full previous camera,
no TAA jitter), if the depth there matches within 2 % and the texel holds the other half; otherwise it uses its own half
twice. Edges, fade, the adaptive (ultra) path, blur and interleave are unchanged.

Supported: low, medium and high quality, full and half size, perspective, one view (very low skips two of the four
slices; ultra's adaptive gather is not split). Temporal SSAO (0028) now also supports half size (Godot's default) and
excludes very low for the same reason: the history lookup works in the gather's own grid (a slice texel is a 2 × 2 block
position of the full- or half-resolution grid; `params.size_multiplier`).

## Verification

lavapipe, City Lab with SSIL enabled (test copy of the project), 960 × 540, still captures after 30 frames, temporal off
vs on (`fellow82` candidate):

| case | aerial | roofs | street |
|---|---:|---:|---:|
| SSIL half size (benchmark preset) | 60.4 dB (max \|d\| 23) | 60.6 dB (24) | 63.9 dB (14) |
| SSIL full size | 62.0 dB (23) | 62.4 dB (43) | 65.1 dB (13) |
| SSAO half size (0028, generalised) | 73.5 dB (20) | 68.8 dB (25) | 71.6 dB (11) |

SSIL comes out lower than SSAO (70-73 dB at full size): its taps read the previous frame's lit image, which itself
carries the indirect light, so the two halves of a still view see slightly different inputs (and different TAA
jitter); the differences are spread thin (9 % of the pixels differ, almost all by 1-3 levels).

RTX: captures, temporal sequences and in-process A/B in the next job.

## Measured on RTX (`fellow82`/`fellow84`, half size as in the preset)

Captures against stock (`m6_f86_capture`): Bistro exterior 58.6 dB mean (57.8 min), Bistro interior 62.9 (59.3),
Sponza 59.7 (58.4); the other scenes have no SSIL (identical). Temporal sequences (`m6_f89_temporal`): Bistro exterior
57.7 dB (56.7 min, 0.18 % > 2), Sponza 58.7 (52.1 min, 0.19 % > 2).

## Between processes, with the other visually lossless options (`m6_f94_final`, `fellow87`)

Adaptive shading (autotune), temporal SSAO, temporal SSIL and fog at half rate together, against the FellowEngine
defaults, 3 runs each: Lights Lab −24.9 %, Bistro interior −9.0 %, PBR Lab −7.0 % (p 0.09), Bistro exterior −3.4 %,
Sponza −3.4 % (p 0.13), City Lab −3.2 % (p 0.07).

## Decision

**KEEP as an option, off by default** (`postfx/ssil_temporal`): visually lossless within the limits above, never slower in one process
beyond its noise. A project turns it on deliberately (`docs/FELLOWENGINE_OPTIONS.md`).
