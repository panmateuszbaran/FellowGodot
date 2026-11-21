# 0309 — Persistent screen probes; the ray-traced gather's further bounces from its own probes

Status: **KEEP**, on wherever a screen gather is on (both gathers off by default) · Branch `fellow/geometry` · Settings
`rendering/fellow/sdfgi/screen_gather_persistent` (on), `rendering/fellow/raytracing/gi_screen_gather_feedback` (on) ·
Code:
* `servers/rendering/renderer_rd/shaders/effects/fellow_sdf_gather.glsl` (the trace pass), `effects/fellow_sdf_gather.{h,cpp}`;
* `renderer_rd/forward_clustered/render_forward_clustered.cpp` (`_fellow_screen_gather()`), `fellow_settings.*`;
* `benchmarks/project/scenes/gi_arcade.gd` (GI Arcade).

Checks: lavapipe only (the owner needs the laptop: its GPU is not used for this decision): GI Arcade and GI Lab, still
frames and linear captures against the path tracer; the image regression suite. The RTX's confirmation in Sponza is
prepared (job f532), not run.

## Problem

Decision 0308's screen gathers trace a probe per 16 x 16 pixels from its tile's centre pixel and average its light over
16 frames where the previous frame's probe lay on the same surface. On the RTX the ray-traced gather flickered on
Sponza's curtains: 10.7 % of a still view's pixels changed by more than 2 levels from frame to frame (ray-traced DDGI's
3.7 %), and 32 or 64 frames of history changed nothing.

The RTX's 16 still frames of that view say why. Over the pixels that flicker, the lag-1 autocorrelation of their
luminance is −0.03 with the ray-traced gather: the flicker is white in time, a new value every frame. A probe averaging
16 frames changes slowly (DDGI's flicker: +0.39; the SDF gather's: +0.33). Those probes were starting over every frame.
The tile's centre pixel is sampled again each frame, and the TAA's jitter moves it by up to half a pixel: on a curtain's
fold, a leaf or a slat it lands on another surface from one frame to the next, the history's check (the same plane, the
geometric normal within 37 degrees) fails, and the probe's light is that frame's 16 rays alone. The ray-traced gather's
rays are noisier than the SDF gather's (a sun-lit hit or a shadowed one, against a voxel's smooth light), so its
restarts show.

## Change

**Persistent probes** (`sdfgi/screen_gather_persistent`). A probe keeps its world point, its geometric normal and its
history while that point is still seen in its tile:
* each probe looks at the previous frame's probes of the 3 x 3 tiles around where its tile's point was (nine threads of
  its workgroup, one each);
* a previous probe's point is kept by the tile it now projects into, if a pixel within one of its projection lies on its
  plane (within a pixel's size there: the jittered depth no longer matters) and its normal faces the camera; the
  nearest to the tile's centre if several are;
* otherwise the probe starts at its tile's centre pixel as before (the previous probe's history taken if it lay on the
  same surface, as before).
A still view's probes stay where they were first placed; a moving camera's move with the surfaces until they leave
their tile or are hidden. A kept probe has no pixel of its own, so its rays are distributed around its geometric normal
rather than its first pixel's normal-mapped one (for every probe while the setting is on; a probe stands for its tile).

**Further bounces from the screen probes** (`raytracing/gi_screen_gather_feedback`). A ray-traced gather ray's hit was
lit by the sun (a shadow ray) and by DDGI's probes for the light that reaches it. DDGI's 2 m grid interpolates light its
probes see and the hit does not, and GI Lab's room came out a stop too bright with it. The hit now takes the previous
frame's screen probes' light where they saw it (the 2 x 2 probes around its projection, weighted by their bilinear
weights, their plane's distance and their normal facing the ray; their coverage blends to DDGI's where they did not):
the screen probes' own light fed back from frame to frame, an infinite bounce through the probes on screen.

## Measurements

GI Arcade (new, `--scene=gi_arcade`): a colonnade (seven columns) in front of a gallery, folded double-sided curtains
hung in front of the columns and gathered at the bottom, a tree of 600 double-sided leaves, a shutter of slats; the
kinds of geometry Sponza's flicker was on, in the cloud (Sponza's assets are not). All on lavapipe at 480x270, 200
frames to settle, 16 still frames.

The flicker a GI option adds: the share of pixels changing by more than 2 levels (the standard deviation over the 16
frames), over the pixels that change by less than 1 level without GI. Every option's own pictures flicker more on the
leaves' and the curtains' edges (TAA at 480x270: 5.4 % and 7.0 % of all pixels with no GI at all), the same for all:

| GI | colonnade | the tree |
|---|---:|---:|
| ray-traced DDGI | 0.04 % | 0.18 % |
| ray-traced gather, M167 | 0.71 % | 0.49 % |
| ray-traced gather, persistent probes | 0.06 % | 0.17 % |
| ray-traced gather, persistent, further bounces from its probes | 0.07 % | 0.17 % |
| SDFGI | 0.15 % | 0.20 % |
| SDF gather, M167 | 0.73 % | 0.61 % |
| SDF gather, persistent probes | 0.17 % | 0.21 % |

The gathers no longer flicker more than the probes they replace (ten and four times less than before). The orange
patches the M167 gathers left on the curtains in the gallery's shade are gone (`flicker_arcade_along.png` in the
milestone's report).

Against the path tracer (linear captures, `gi_reference_compare.py`; 300 frames, the reference 256 frames of 16
bounces), bias / error a pixel in stops:

| | GI Lab: room | GI Lab: dark corner | GI Lab: outside |
|---|---:|---:|---:|
| ray-traced DDGI | +0.86 / 0.88 | +0.86 / 0.87 | +0.01 / 0.02 |
| ray-traced gather, M167 | +0.35 / 0.36 | +0.34 / 0.35 | +0.01 / 0.01 |
| … persistent probes | +0.36 / 0.37 | +0.35 / 0.35 | +0.01 / 0.01 |
| … and further bounces from its probes | **+0.25 / 0.27** | **+0.25 / 0.27** | +0.01 / 0.01 |
| SDFGI | −0.74 / 0.97 | −0.62 / 0.86 | −0.04 / 0.05 |
| SDF gather, M167 | −0.91 / 0.91 | −0.73 / 0.73 | −0.05 / 0.05 |
| SDF gather, persistent probes | −0.92 / 0.92 | −0.73 / 0.74 | −0.05 / 0.05 |

@ARCADE@

* Persistent probes do not change the light (within 0.01 stops a pixel).
* The further bounces from the screen probes take a third of the ray-traced gather's remaining excess in GI Lab's room
  (+0.35 to +0.25 stops; the same after 600 frames, +0.34 to +0.24: converged).

## Found on the way: the RTX and lavapipe disagree inside GI Lab's room

The path tracer's pictures of GI Lab are the same on the RTX (decision 0308's captures, 1280x720) and on lavapipe
(480x270, or 1280x720): 0.00 stops apart in every camera. The real-time options are not: inside the room the RTX's are
brighter by 1.40 stops (SDFGI), 0.89 (its gather) and 1.20 (ray-traced DDGI); outside they agree within 0.05.

| GI Lab's room, bias in stops | RTX (0308) | lavapipe |
|---|---:|---:|
| SDFGI | +0.71 | −0.74 |
| SDF gather (M167) | −0.01 | −0.91 |
| ray-traced DDGI | +2.07 | +0.87 |

Not the resolution (lavapipe at 1280x720 against the RTX's reference: +0.87), not the autotuner (off: +0.87), not
FellowEngine on lavapipe (stock SDFGI with `rendering/fellow/enabled=false`: −0.74, as with it), not the frames
(DDGI after 600: +0.84). Two independent systems, SDFGI (stock Godot's) and DDGI (FellowEngine's), move together, so
the cause is something they share on one of the two drivers. Until it is found, decision 0308's statement that the SDF
gather is right in GI Lab's room (RTX) and this decision's that it is 0.9 stops too dark there (lavapipe) stand side by
side; the comparisons between options on one platform hold on each. Job f533 (prepared, not run) renders the room on
the RTX with FellowEngine off (stock Godot) and with full barriers between the render graph's commands.

## Decision

KEEP both, on by default where a gather is on: the gathers' flicker is gone at no cost in accuracy, and the ray-traced
gather comes closer to the path tracer in an enclosed room. The cost is nine reprojections and up to 81 depth reads per
probe and frame (persistent probes) and 4 probe reads per ray (feedback); not measured on the RTX yet (f532).

## Rejected / not done

* More history (decision 0308's f528: 32 and 64 frames changed nothing: the history was not kept).
* A temporal filter of the resolved light (0308's next step): the flicker was the probes', and a filter after them
  would clamp to a neighbourhood that flickers with it.
* The previous probes' history searched by the old check over 3 x 3 tiles: a probe sampled again on another surface
  each frame finds no neighbour on its new surface either; keeping its point needs no search for it.
