# 0078 — Motion blur: a reconstruction filter after TAA, from the velocity buffer

Status: **KEEP**, off by default (a look, not an optimization) · Branch `fellow/geometry` · Settings
`rendering/fellow/post/motion_blur/enabled`, `amount`, `samples`, `max_radius` ·
Code: `servers/rendering/renderer_rd/shaders/effects/fellow_motion_blur.glsl`, `renderer_rd/effects/fellow_motion_blur.*`,
`renderer_rd/renderer_scene_render_rd.*` (the pass), `renderer_rd/forward_clustered/render_forward_clustered.cpp` (motion
vectors without TAA), `fellow_settings.*`.

## Problem

Milestone 16 starts FellowEngine's work on how the image looks. Stock Godot 4.7 has no motion blur: a camera turning
at 60 fps shows sharp, stepped images where a film camera, and the engines games are compared with, smear what moves
across the exposure. Godot already computes per-pixel motion (the velocity buffer TAA reprojects with), so the missing
part is the blur.

## Change

The reconstruction filter of McGuire et al. ("A Reconstruction Filter for Plausible Motion Blur", I3D 2012) with
Jimenez's refinements ("Next Generation Post Processing in Call of Duty: Advanced Warfare", SIGGRAPH 2014), three compute
passes on the HDR image after TAA and depth of field, before auto exposure, glow and tone mapping:

1. **Tile max**: per 32 × 32 tile, the longest blur vector (one 8 × 8 workgroup per tile, 4 × 4 pixels per invocation,
   a shared-memory reduction). A blur vector is the pixel's motion during the exposure in pixels, half to each side:
   −velocity · resolution · amount / 2, clamped to `max_radius` (≤ 32, the tile size).
2. **Neighbour max**: per tile, the longest of its 3 × 3 tiles — how far any blur can reach into the tile.
3. **Gather**: tiles whose neighbourhood moves less than half a pixel are copied. Elsewhere `samples` samples (12) along
   the neighbourhood's vector, offset by interleaved gradient noise; each weighed by whether it is in front of the pixel
   or behind it (a soft depth test over 2 % of the depth), and whether its own blur reaches the pixel (cone) or both
   blur over each other (cylinder); the pixel itself weighs `samples / (2 |v|)`.

Where nothing was drawn (the depth buffer's clear value: the sky, the background) the velocity buffer holds 0; there the
blur takes the camera's motion, reprojected exactly as the renderer's own camera motion vectors are
(`MotionVectorsStore`, `derive_motion_vector()`), so a turning camera smears the sky as well.

The velocity buffer exists with TAA or an upscaler. With motion blur on and neither, the colour pass writes it for the
blur (stock's motion-vector path, as for a compositor effect that asks for motion vectors), MSAA included (resolved).
Not in multiview, and not with FSR 2 / MetalFX (the image is upscaled before the post chain).

Settings: `enabled` (false), `amount` (0.5: the fraction of the frame the shutter is open, a 180° shutter), `samples` (12,
2-64), `max_radius` (32 px).

## Verification

lavapipe:

* **Off (default) is unchanged**: the milestone-16 binary against the one before it, autotune off: City Lab's warm
  `city_pan` sequence, 60 frames, and Crowd Lab's 3 captures: **0 differing pixels**.
* **On**: the `city_pan` sequence at 960 × 540, frames 10/30/50, on against off: with TAA 41 762-57 747 pixels differ by
  more than 2 levels (max 66-75), without anti-aliasing 60 949-74 411 (max 100-111), with MSAA 4× 53 956-67 902 (max
  87-99). Inspected: the buildings smear horizontally along the pan, the near ones more (parallax), edges against the
  sky and the street stay coherent, no tile seams.
* Captures of a still frame (`--mode=capture` holds the scene time) are unchanged with the setting on: nothing moves, no
  tile passes the half-pixel test.

Cost: three compute passes; the gather only where something moves. Measured on the RTX when the benchmark queue runs
again.

## Decision

**KEEP, off by default.** A look a project chooses (`rendering/fellow/post/motion_blur/enabled`); with it off the frame is
the one before, bit for bit.
