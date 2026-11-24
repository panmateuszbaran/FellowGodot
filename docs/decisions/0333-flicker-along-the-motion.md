# 0333 — Flicker under a moving camera, measured along the motion vectors; the screen gather's pixel history

Status: **KEEP** — `rendering/fellow/sdfgi/screen_gather_pixel_history` (new, 8 frames; 1: none; only where
`sdfgi/screen_gather` or `raytracing/gi_screen_gather` is on) · Branch `fellow/geometry` · Code:
* `servers/rendering/renderer_rd/shaders/effects/fellow_sdf_gather.glsl` (`FELLOW_PIXEL_HISTORY`: the resolve's
  `pixel_history()`), `effects/fellow_sdf_gather.{h,cpp}` (the resolve's variant, its four textures, the previous eye in
  the parameters' block), `forward_clustered/render_forward_clustered.{h,cpp}` (the textures in their own scope, the
  ambient buffer's size), `servers/rendering/fellow_settings.{h,cpp}`, `doc/tools/fellow_settings_docs.py`;
* the tools: `benchmarks/tools/flicker/reproj_flicker.py` (new), `showcase/tools/play_look.gd` (new),
  `showcase/tools/velocity_dump.{gd,glsl}` (new), `showcase/tools/flicker_look.gd` (`--speed`, `--turn`, `--lite`,
  `--velocity`, `no_gather`, `no_pixel_history`), `benchmarks/tools/flicker/flicker_metrics.py`.

## Problem

After decision 0331 (TAA that holds still) the owner played the rooftops again: "on this building the shadows still
flicker while moving" (a picture of the rusticated house by the square). Decision 0331's measure — a pixel's change from
one frame to the next — only means something for a still camera: under a run's speed and a turning camera every pixel
changes, and the block-matching motion of `flicker_metrics.py --motion` (milestone 189) mostly measured its own error
of compensation (each feature off in turn moved it by less than its noise).

## The measure

`showcase/tools/velocity_dump.gd` (a CompositorEffect after the transparent pass, `needs_motion_vectors`) copies the
frame's motion vectors (RG16F, the previous frame's UV minus this frame's) with a compute shader into a texture of its
own and reads it back; `play_look.gd` plays one of the showcase demo's shots (the character sprinting along the square,
the camera following it as a player's does; the character hidden) at a fixed frame rate, every frame and its motion
vectors saved, with one feature off. `reproj_flicker.py` follows each pixel to the frame before and after along its
motion vector and takes the second difference along that path, |I(t+1)(p − v) − 2 I(t)(p) + I(t−1)(p + v)|: zero for
shading that only moves with the surface, high for shading that goes back and forth; also the first difference.
Disocclusions (the motion vector where the pixel came from differing by a pixel), the sky and paths leaving the frame
are left out; the sign of the vectors' y is the one that predicts the next frame better.

## What flickers (RTX 5070 Ti Laptop, 1080p, 180 frames of the demo's run, ~128 million pixels followed)

| off | first difference > 3 levels | second difference > 8 |
|---|---|---|
| nothing | 8.79 % | 8.15 % |
| TAA | 34.00 | 28.45 |
| TAA v2 as before milestone 189 | 14.89 | 11.36 |
| the sun's shadows | 12.17 | 11.30 |
| SDFGI | 9.62 | 9.09 |
| the weathering, the facades' parallax, mesh LOD, the shadow casters' LOD, the shadow cache, the soft filter, contact shadows | 8.74-8.89 | 8.08-8.20 |
| SSAO and SSIL | 8.50 | 7.91 |
| **SDFGI's screen gather** | **7.44** | **7.25** |

The sun's shadows themselves do not flicker: without them more of the town is lit and its textures' sub-pixel detail
shows more (the residual TAA leaves: decision 0331). The maps put what is left in the shaded parts — a pillar's
shaded side, a doorway, the cast shadows on the paving, the shaded brick — where nothing but the indirect light lights
the surface: the screen gather (decision 0308) was the largest part a feature adds (15 % of it); SSAO and SSIL 3 %.

## Change

The screen gather's resolve (per pixel of SDFGI's ambient buffer, the probes around it) now averages its result over the
frames in which the pixel's point stayed in view: the point is projected into the previous frame, the 2 x 2 texels of
the previous result around it are kept where they lay at the point's distance from the previous eye (within 1 % plus
a texel's width on a surface leaning away; otherwise another surface: a disocclusion), bilinearly weighed (at least a
quarter of the weight, or none), and blended with the frame's light by 1/n up to `screen_gather_pixel_history` frames.
Under a moving camera the probes a pixel reads change — new tiles at the edges, new points as a probe's leaves its
tile, a probe's first frames of 16 rays — and on a flat shaded wall a whole area's change passes the TAA's
neighbourhood clamp; the pixel's own history smooths it where the probes' could not. The lag it adds to a change of
light is at most 8 frames over the probes' own 16 (the light of a surface that moves with the camera, the character's,
is followed by the camera's motion only: its history kept where the distance agrees).

Two results the ambient buffer's size (RGBA16F: the light and its frames) and their pixels' distances (R32F), the
previous frame's read: 24 bytes a pixel, 50 MB at 1080p (12 with `gi/use_half_resolution`). Where the setting is 1 the
resolve is the one before (its own variant, no textures).

## Measurements

* The RTX table above (milestone 191's GPU jobs). The pixel history's own effect on the RTX is not measured: the owner
  asked for no GPU work for now after this diagnosis.
* Lavapipe, `flicker_look.gd --lite=1 --view=ref --speed=5.5 --turn=12` (960x540: the square's buildings, the camera
  running past the owner's house and turning 12° a second, 90 frames after a warm-up sequence), the shaded pixels
  (below 70 of 255 in the lower 55 % of the frame): second difference > 3 levels 11.24 → **10.49 %**, > 8 4.07 →
  **3.87 %** (SDFGI's own light, the gather off: 12.18 and 4.58 %); every followed pixel 14.83 → 14.72 % and 3.24 →
  3.21 %. The frames' brightness unchanged (139.48 against 139.51 levels at the last frame).
* The regression suite: every scene exact (none enables the screen gather).

## How it got there

* `flicker_metrics.py --motion` (milestone 189: blocks matched by phase correlation): at a run's speed with the camera
  turning its residual was its own error of compensation; the scene's motion vectors replace the matching.
* A first lavapipe run seemed to cut the shaded pixels' flicker by a third: the first sequence of the run still had
  SDFGI converging (its frames 37 levels brighter); every comparison now follows a warm-up sequence.

## Decision

KEEP: `sdfgi/screen_gather_pixel_history` at 8 frames. On the RTX it is to be measured with the same jobs when the GPU
is back (`play_look.gd`'s `base` against `--set=rendering/fellow/sdfgi/screen_gather_pixel_history=1`).

## Rejected / not done

* Turning the screen gather off for the showcase: SDFGI's own light leaks through thin walls (decision 0308) and
  flickers more on lavapipe's shaded walls (12.18 against 10.49 %).
* More rays or a longer history for the probes: under a moving camera a probe's history is what is lost.
* The textures' mipmaps without TAA's −0.5 bias (sharper, more sub-pixel detail to flicker; `flicker_look.gd`'s
  `mip0`, and `mip_quarter` halfway): lavapipe's lit pixels 18.2 → 17.2 % (> 3 levels), the shaded ones unchanged
  (10.5 → 10.9 %), every followed pixel 14.7 → 14.2 % — a twentieth of the flicker for every texture softer.
* A neighbourhood clamp on the pixel history: the probes' own history already lags the light; clamped to the 2 x 2
  probes' range, a flat wall's change would pass as it does the TAA's.
* The residual TAA leaves on the textures' sub-pixel detail (most of what is left): decision 0331's territory.
