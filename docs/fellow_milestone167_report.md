# FellowEngine — milestone 167 report (diffuse light gathered per screen probe)

Status: **FINAL** (2026-11-21). Branch `fellow/geometry`. A night on the laptop's GPU (RTX 5070 Ti Laptop, jobs
f518-f531, 00:30-04:55) and the cloud (lavapipe); the laptop's queue paused again before 08:00.

## 0. Result

The owner: "is the graphics tip-top AAA now?" No: indirect light, most of what makes an image read as lit, had never
been measured against the truth. This milestone measures every diffuse GI option FellowEngine has against its path
tracer, finds where each goes wrong, and adds a screen-space gather that is closer to the truth than all of them, with
and without ray tracing (decision 0308).

Bias / error in stops against the path-traced reference (Sponza, Bistro exterior and interior, GI Lab; per camera, the
log₂ of the luminance ratio over the lit pixels, then the means):

| | bias | error a pixel | GPU time at 1080p (Sponza) |
|---|---:|---:|---:|
| no GI | +1.57 | 1.57 | 5.37 ms |
| SSAO + SSIL | +1.43 | 1.43 | +0.7 ms |
| SDFGI (stock) | +0.02 | 0.41 | +1.4 ms |
| **SDFGI + screen gather** (`sdfgi/screen_gather`, new) | −0.11 | **0.34** | +0.3 ms on SDFGI |
| ray-traced DDGI (decision 0083) | +1.09 | 1.10 | +1.6 ms |
| **ray-traced DDGI + screen gather** (`raytracing/gi_screen_gather`, new) | +0.24 | **0.29** | +0.7 ms on DDGI |

* Without GI, Sponza's arcades are five times as bright as the light that reaches them.
* FellowEngine's ray-traced DDGI was a stop too bright: a 2 m probe grid interpolates light the probes see and the shaded
  point does not, then feeds it back through the bounces.
* The screen gather traces the first bounce from the surfaces on screen (a probe per 16 x 16 pixels, 16 rays a frame,
  averaged over frames and neighbours on the same surface): through SDFGI's distance fields without ray tracing, against
  the ray tracing scene with it.
* Without ray tracing, in GI Lab's sunlit room it is right to 0.10 stops a pixel (SDFGI 0.41) and leaks a quarter of
  SDFGI's light through thin walls; Bistro's street is a little darker than SDFGI's (both are too dark there).
* With ray tracing, Sponza's arcades and Bistro's street come within a quarter of a stop (from 1.5 stops). It flickers on
  Sponza's curtains, though (10.9 % of a still view's pixels change by more than 2 levels from frame to frame, DDGI's
  3.8 %); a temporal filter of its resolved light is the next step. Off by default, as the SDF gather.
* Tried and removed: DDGI's probes traced through SDFGI's fields (as bright as ray-traced DDGI, +1.2 ms).

## 1. The night on the GPU

| job | what | result |
|---|---|---|
| f518 | m166: no GI, SSAO, SSIL, SDFGI, RT DDGI, the reference (4 bounces); GPU times | the baseline above; Bistro exterior's flythrough with SDFGI stopped twice on a fence never signalled (decision 0270's failure, far more often with SDFGI; not seen again in f519-f525) |
| f519 | m167a: SDF DDGI (2 m, 1 m, bounce feedback 1), RT DDGI with and without the sky leak, references 4 and 16 bounces | SDF DDGI +0.84 stops; the references within 0.04 stops of each other |
| f520 | m167b: SDF DDGI with SDFGI's light where its probes do not cover, hidden probes counted as covering | no change: SDF DDGI removed |
| f521 | m167c: the SDF screen gather (16 and 8 px, 32 and 64 rays, bounce feedback 1) | 0.35 stops; +0.28 ms |
| f522 | m167d: the gather filtered over its neighbours; 32 frames of history, 16 rays | 0.34 stops; 16 rays the same |
| f523 | m167e: the ray-traced gather, with and without the sky leak, 16 rays | 0.30 stops without the leak, 0.63 with it |
| f524 | m167f: the SDF gather in GI Lab (as f522's to 0.0001 stops); still frames' noise; the rooftops showcase | the jittered probes shimmered: 6.8 % of Sponza's pixels changed by more than 2 levels from frame to frame (SDFGI 2.2 %) |
| f525 | m167g: the ray-traced gather without the sky leak by itself; its time in Bistro interior | as f523's no-leak run; +0.51 ms (32 rays) |
| f526 | m167h: the probes at their tiles' centres, 16 rays: captures, noise, GPU time, the showcase | the same accuracy (0.34 and 0.29 stops), Sponza's noise 2.7 % (SDFGI 2.2 %) |
| f527 | m167h: the ray-traced still frames f526 had refused (an `--extra` of one argument) | the ray-traced gather flickers on Sponza's curtains: 12.8 % (DDGI 3.8 %) |
| f528 | m167h: the ray-traced gather with 32 and 64 frames of history, 32 rays | the same flicker (12.6-12.7 %) |
| f529 | m167i: the rays lifted along the geometric normal and kept above it; captures, still frames, GPU time | the same accuracy and flicker (12.9 %) |
| f530 | m167j: the history's check and the neighbours' weights by the geometric normal | 10.9 %; the same accuracy |
| f531 | m167k: the ray-traced rays lifted 2 cm + 0.4 % | 10.7 %, the error 0.37 stops: reverted |

## 2. Checks

* The image regression suite on lavapipe (38 scenes, the editor build): every picture identical to its golden, before
  and after the screen gather and the removal of SDF DDGI; ray-traced DDGI's pictures (the `rt` group) unchanged.
* GI Lab on lavapipe: the SDF gather against SDFGI and the reference (four cameras), the ray-traced gather against DDGI,
  a 90-frame pan through the room (frame-to-frame change as SDFGI's: 1.27 against 1.28 levels; the red wall's colour
  the same at either end), 16 still frames for 16, 32 and 64 frames of history and for the probes fixed.
* The rooftops showcase's reference shot (the video's frames 10, 45, 70, 84 and 93) on the RTX with SDFGI as it was and
  with the gather: the windows' interiors and the shade under ledges and shutters deeper, the lit stone as it was
  (1.5-7 levels darker on average). The showcase now has the gather on (`showcase/project.godot`).

## 3. Next (graphics)

* A temporal filter of the screen gather's resolved light (reprojected, clamped to its neighbourhood): the ray-traced
  gather's flicker on Sponza's curtains.
* The ray-traced gather's further bounces from its own probes (a radiance cache) instead of DDGI's: GI Lab's enclosed
  room is still 1.2 stops too bright with it (DDGI 2.1).
* SDFGI's voxel light in Bistro's streets (darker than the path tracer's hits): its sun shadows through coarse cells.
* Screen-space traces before the field for contact detail below SDFGI's 20 cm cells.
* Specular: SDFGI's reflections and ray-traced reflections measured against a reference the same way.
