# 0032 — Soft shadows: half the filter taps per frame under temporal antialiasing (visually lossless)

Status: **REJECT** (not merged; patch kept as `m6_shadow_temporal_taps.patch`) · Branch `fellow/geometry` · Toggle `rendering/fellow/shadows/temporal_taps` (runtime, off;
pipelines are specialised on it) · Code: `shaders/scene_forward_lights_inc.glsl` (`fellow_tap_*`), scene flag
`SCENE_DATA_FLAGS_FELLOW_SHADOW_TEMPORAL_TAPS`, specialisation bit `fellow_shadow_temporal_taps`,
`render_scene_data_rd.*`, `renderer_scene_render_rd.cpp`, `fellow_settings.*`.

"Visually identical, measured" track.

## Problem

Soft shadow filtering is a large part of the opaque pass where shadows are soft: replacing soft by hard shadows saves
0.30 ms (directional) in Bistro exterior, 0.48 ms (positional) in Bistro interior, 0.23-0.30 ms in PBR Lab and Sponza
(`m5_f69_sizing`). The benchmark's "high" preset filters with 8 PCF taps, or 12 blocker-search plus 12 filter taps
(PCSS) for lights with a size. The FellowEngine min/max pyramid (0005) already skips the taps where a filter is fully
lit or fully shadowed, so what is left is the penumbrae.

## Change

Stock already rotates each pixel's Vogel disk every frame by the TAA frame count (interleaved gradient noise of the
pixel and the frame), so under TAA the shadow in a penumbra is an average over many rotated disks. With the toggle, and
only while TAA or a temporal upscaler accumulates the render, every soft filter of at least 4 taps (directional,
spot, omni and area; PCF and both PCSS loops) takes the even taps of its disk in even frames and the odd taps in odd
frames and averages what it took. The fully lit / fully shadowed early-outs are unchanged; without temporal
antialiasing nothing changes; with the toggle off the pipelines are stock's (the loop bounds fold to constants).

Each frame's filter is the same estimate with half the samples (twice the variance per frame); over the frames TAA
blends, the samples cover the full disk as before.

## Verification plan

* lavapipe: toggle off against the previous build, bit for bit (Lights Lab captures); off vs on: captures and the walk
  sequence (PSNR).
* RTX: captures, temporal sequences, in-process A/B (Bistro interior and exterior, PBR Lab, Lights Lab, Sponza).

## Measured on lavapipe (candidate build, 960 × 540)

* Toggle off against the previous build: bit-identical (Lights Lab captures, 0 differing pixels).
* Lights Lab (soft positional shadows, 8 taps → 4): captures 79.7-90.5 dB (max |d| 3), the 60-frame walk 79.6 dB mean,
  76.6 dB min (max |d| 11).
* PBR Lab: captures **47.0-67.2 dB, max |d| 75**: the thin shadows of the emissive bars and the roof edge stay
  visibly noisier. TAA does not average the extra per-frame noise away: its history is clipped to the current frame's
  3 × 3 colour range, which the noisier frame widens only partly, so a penumbra keeps part of each frame's noise.

## Decision

REJECT at the preset's tap counts (8 PCF, 12 PCSS): not visually lossless where a penumbra is a few pixels wide. With
16-32 taps (soft high / ultra) the halved filter would look like the next lower stock quality level under TAA, which a
project can already choose; not worth a separate option. The code was not merged.
