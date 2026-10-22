# 0079 — Lens effects in the tone mapping pass: chromatic aberration, vignette, film grain

Status: **KEEP**, off by default (a look, not an optimization) · Branch `fellow/geometry` · Settings
`rendering/fellow/post/lens/chromatic_aberration`, `vignette`, `vignette_smoothness`, `film_grain` ·
Code: `servers/rendering/renderer_rd/shaders/effects/tonemap.glsl`, `renderer_rd/effects/tone_mapper.*`,
`renderer_rd/renderer_scene_render_rd.cpp`, `fellow_settings.*`.

## Problem

Milestone 16 starts FellowEngine's work on how the image looks, not only on what it costs. Three camera effects that
games use to make a 3D image read as filmed are missing from stock Godot's Environment: chromatic aberration (a lens
focusing red and blue at slightly different magnifications, colour fringes towards the edges), vignetting (the corners
darker) and film grain. Stock users add them with a full-screen quad or a compositor effect: one more full-screen pass
reading and writing the image.

## Change

The three are folded into the tone mapping pass, which already reads every pixel once and writes the final image. Each
is a specialization constant of decision 0052's specialized tone mapping pipelines (`fellow_spec` bits 24-26), set only
when its setting is non-zero, so a frame without them runs the very pipeline it ran before. The push constant grows from
112 to 128 bytes (four values: the four settings packed as half floats, a grain seed and the aspect ratio).

* **Chromatic aberration** (`chromatic_aberration`, −0.05 to 0.05, typical 0.002-0.008): the source is read again for red at
  the centre-relative position magnified by 1 + k and for blue by 1 − k (two more bilinear reads), before exposure. Not
  in multiview (XR).
* **Vignette** (`vignette` 0-1, `vignette_smoothness` 0.01-1): after tone mapping (and glow), the colour is multiplied by
  1 − intensity · smoothstep(1 − smoothness, 1, r), r the distance from the centre in an aspect-correct frame, 1 at a
  corner.
* **Film grain** (`film_grain` 0-0.2, typical 0.02-0.05): after the conversion to sRGB, before debanding, a hash noise of
  the pixel and a seed that changes every frame (frame number mod 997), scaled by intensity and by the mid-tones
  (0.25 + 3 l (1 − l), l the luminance), so blacks and whites keep their level.

The effects are applied only by the specialized pipelines (`postfx/tonemap_specialized`, on by default); with that
setting off they are not drawn.

## Verification

lavapipe, Crowd Lab captures (3 cameras), autotune off:

* settings at their defaults (0) against the binary before the change: **0 differing pixels** (the pipelines and their
  code are the same).
* chromatic aberration 0.006, vignette 0.45, film grain 0.04: colour fringes towards the edges, darker corners, grain in
  the sky and the ground (`crowd_street` inspected).

Cost: two texture reads (aberration) and a few ALU operations per pixel in a pass that is bound by its reads and writes;
measured on the RTX when the benchmark queue runs again.

## Decision

**KEEP, off by default.** A look a project chooses; no pass added, no cost when unused.
