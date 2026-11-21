# 0308 — Diffuse light gathered per screen probe: SDFGI's without ray tracing, DDGI's first bounce traced

Status: **KEEP**, off by default (looks: the first needs SDFGI in the environment, the second ray-traced DDGI) · Branch
`fellow/geometry` · Settings `rendering/fellow/sdfgi/screen_gather`, `screen_gather_tile` (16 px), `screen_gather_rays`
(16), `screen_gather_history` (16 frames), `screen_gather_filter` (on), `trace_max_steps` (64), `trace_surface_bias` (1
cell); `rendering/fellow/raytracing/gi_screen_gather` · Code:
* `servers/rendering/renderer_rd/effects/fellow_sdf_gather.{h,cpp}`, `shaders/effects/fellow_sdf_gather.glsl`;
* `renderer_rd/forward_clustered/render_forward_clustered.{h,cpp}` (`_fellow_screen_gather()`), `fellow_settings.*`;
* `benchmarks/project/scenes/gi_lab.gd` (GI Lab), `benchmarks/tools/gi_reference_compare.py`.

Checks: Sponza, Bistro exterior and interior and GI Lab on the RTX 5070 Ti laptop, every GI option against the
path-traced reference; GI Lab on lavapipe; the image regression suite.

## Problem

The owner: "the graphics are far from AAA". Most of what makes an image read as lit is its indirect light, and a game
on a GPU without ray tracing had only stock Godot's options: SSAO and SSIL (screen-space), VoxelGI (baked) and SDFGI.
FellowEngine's DDGI (decision 0083) needs ray queries.

None of them had been measured against the truth. Decision 0083 made a path tracer
(`debug/raytracing_gi_reference`: per pixel, the irradiance of paths through the ray tracing scene, the sun sampled at
every hit) but used it to look at, not to measure. This milestone measures every option against it:

* the captures are linear EXRs at 1280x720 with the exposure frozen, after 600 frames (1024 paths a pixel for the
  reference, 4 or 16 bounces: within 0.04 stops of each other);
* the other options differ from the reference only in the indirect diffuse light, so the ratio of their luminance to
  the reference's (averaged over 4 x 4 pixels, over the pixels the reference lights) measures that light where it
  shows: its mean log₂ is the bias in stops, its mean absolute value the error;
* GI Lab, a new procedural scene: a room lit only through a window and a door (the sun's patch on the floor lights the
  ceiling, a red and a green wall tint what faces them), a sealed box with 10 cm walls beside it (inside, any light is a
  leak) and colour bleeding outdoors.

Bias / error in stops against the reference (the cameras of each scene; GI Lab without its sealed box):

| | Sponza | Bistro exterior | Bistro interior | GI Lab | all |
|---|---:|---:|---:|---:|---:|
| no GI (the environment's ambient light) | +2.37 / 2.37 | +1.25 / 1.25 | +0.70 / 0.71 | — | +1.57 / 1.57 |
| SSAO | +2.33 / 2.33 | +1.18 / 1.18 | +0.64 / 0.64 | — | +1.52 / 1.52 |
| SSAO + SSIL | +2.23 / 2.23 | +1.06 / 1.07 | +0.58 / 0.59 | — | +1.43 / 1.43 |
| SDFGI | +0.09 / 0.47 | −0.38 / 0.55 | −0.00 / 0.17 | +0.35 / 0.41 | +0.02 / 0.41 |
| ray-traced DDGI (decision 0083) | +1.81 / 1.81 | +0.59 / 0.61 | +0.66 / 0.67 | +0.96 / 0.98 | +1.09 / 1.10 |

* Without GI, Sponza's arcades are five times as bright as the light that reaches them (the ambient light is never
  occluded); SSAO and SSIL take little of it back.
* Stock SDFGI is the closest: right on average, 0.4 stops off a pixel. Its errors are where it is coarse: GI Lab's room
  1.3 times too bright, a band of light along the top of its red wall, the sealed box lit.
* FellowEngine's ray-traced DDGI is twice as bright as the truth: a probe grid of 2 m interpolates light from probes that
  see more than the shading point does (in the room, the probes nearer the sunlit floor). With one bounce against a
  one-bounce reference it is still 25 % too bright in display terms (GI Lab on lavapipe). Its sky leak accounts for 0.18
  stops of it; counting probes hidden from a point as covering it changed nothing.

## Change

`sdfgi/screen_gather` (where the environment enables SDFGI): SDFGI's diffuse light is gathered per screen probe instead
of interpolated from its world probes, after SDFGI's own pass, into the buffer the colour passes read (SDFGI's
reflections stay as they are).

* **Probes.** One per 16 x 16 pixels of the internal resolution, at its tile's centre (where the sky is there, the first
  of its quarters' centres that is not), the same pixel every frame.
* **Rays.** 16 cosine-distributed directions per probe and frame around its shading normal (stratified, shifted per probe
  and frame; a direction below the geometric surface, from the depth buffer, mirrored above it), from the surface lifted
  along its geometric normal off its own voxels (SDFGI's normal bias), marched through SDFGI's distance field cascades:
  the finest that holds the ray, then the coarser ones.
* **Hits** take the light of the voxel they reach, as SDFGI's own probes do: its direct light from every light (with
  SDFGI's shadows), its emission and SDFGI's bounce, weighed by the field's normal over the voxel's six directions. A ray
  that leaves the last cascade sees the environment's ambient light in its direction (the sky).
* **The probe's irradiance** is the rays' mean (cosine-weighted directions: the "ambient light" the scene shader
  multiplies by albedo), averaged over up to 16 frames where the previous frame's probe at the reprojected point lies on
  the same surface (its geometric normal within 37°, its plane within the tile's footprint).
* **Filter.** Each probe's light is averaged over the probes of the 5 x 5 tiles around it on the same surface (their
  distance from its plane, their geometric normals), for the pixels; the history keeps the unfiltered light.
* **Resolve.** Each pixel of SDFGI's ambient buffer takes the probes of the 2 x 2 tiles around it, weighed bilinearly, by
  their distance from its plane and by their normal; where no probe lies on its surface SDFGI's own light stays.

**With ray tracing** (`raytracing/gi_screen_gather`, with DDGI and its screen pass): the same probes, filter and history,
the rays traced against the ray tracing scene and their hits lit as DDGI's own rays' are (the sun with a shadow ray, the
hit's albedo, its emission, and DDGI's probes at the hit for the further bounces). The first bounce, where the probes'
interpolation went wrong, is traced per screen probe; the probes keep the bounces after it, where their error is
multiplied by the surfaces' albedo. The result goes into DDGI's screen pass texture, and the sky leak (`gi_sky_leak`,
light for interiors the probes leave too dark) is then not added.

## Tried and rejected: DDGI's probes traced through SDFGI's fields

The first attempt (`sdfgi/ddgi_probes`, removed) kept DDGI's probe volumes and its lookups, the rays marched through
SDFGI's cascades instead of the ray tracing scene. On lavapipe after 120 frames it matched the reference in GI Lab's
room; converged on the GPU it was as bright as ray-traced DDGI, for the same reason (the probes' interpolation, not their
tracing):

| | Sponza | Bistro exterior | Bistro interior | GI Lab | all | GPU time (Sponza 1080p) |
|---|---:|---:|---:|---:|---:|---:|
| SDF DDGI, probes 2 m apart | +1.66 / 1.66 | +0.04 / 0.61 | +0.45 / 0.54 | +0.81 / 0.83 | +0.84 / 0.99 | SDFGI + 1.15 ms |
| SDF DDGI, 1 m | +1.03 / 1.04 | +0.17 / 0.54 | +0.29 / 0.39 | +0.84 / 0.86 | +0.63 / 0.75 | SDFGI + 1.23 ms |

Neither SDFGI's diffuse light where the probes do not cover a point (instead of the ambient light), nor counting hidden
probes as covering, changed a figure. Removed.

## Measurements

The screen gather, the same captures (bias / error in stops):

| | Sponza | Bistro exterior | Bistro interior | GI Lab | all | GI Lab's sealed box (light, linear) |
|---|---:|---:|---:|---:|---:|---:|
| SDFGI | +0.09 / 0.47 | −0.38 / 0.55 | −0.00 / 0.17 | +0.35 / 0.41 | +0.02 / 0.41 | 0.029 |
| **screen gather** (the defaults, final build) | +0.06 / 0.45 | −0.56 / 0.64 | −0.05 / 0.12 | +0.01 / **0.10** | −0.11 / **0.34** | **0.008** |
| probes jittered over their tiles (the measurements below) | +0.05 / 0.46 | −0.56 / 0.63 | −0.05 / 0.12 | +0.01 / 0.10 | −0.12 / 0.34 | 0.007 |
| without the filter | +0.05 / 0.46 | −0.59 / 0.66 | −0.05 / 0.12 | −0.01 / 0.11 | −0.13 / 0.35 | 0.008 |
| tiles of 8 px | (as 16 px within 0.01 stops) | | | | −0.13 / 0.35 | |
| 32 or 64 rays, or 32 frames of history | (as 16 rays within 0.01 stops) | | | | −0.12 / 0.34 | |
| SDFGI's bounce feedback 1 (stock 0.5) | +0.08 / 0.46 | −0.42 / 0.52 | −0.03 / 0.12 | +0.36 / 0.36 | +0.01 / 0.37 | 0.019 |
| SDFGI alone, bounce feedback 1 | +0.11 / 0.47 | −0.25 / 0.47 | +0.02 / 0.17 | +0.62 / 0.63 | +0.13 / 0.44 | 0.042 |

* In GI Lab's room the gather is right to 0.1 stops a pixel, from SDFGI's 0.41: the ceiling lit by the floor's sunlit
  patch, the walls' colours where they face each other, the corners dark. The band of light at the top of the red wall
  is gone.
* Bistro interior: 0.12 stops from 0.17. Sponza: as SDFGI (0.46). Bistro exterior: darker than SDFGI (0.63 from 0.55).
  Its streets lack light from SDFGI's voxels (they are darker than the path tracer's hits in both); SDFGI's probe
  interpolation had made up for some of it.
* The sealed box: a quarter of SDFGI's leak. Taking the hits on the voxels' faces instead of a cell short of them
  (`trace_surface_bias` 0.5) brightened the room but let the rays through the 10 cm walls (on lavapipe: five times the
  leak). The default stays SDFGI's own (1 cell).
* Visually: the probes' light was blotchy at their tiles' scale in flat areas (lavapipe, 120 frames); the filter removes
  it.

(The SDF gather's captures ran with 32 rays; 16, the default, measured the same, as did every variation above.)

With ray tracing, the same captures:

| | Sponza | Bistro exterior | Bistro interior | GI Lab | all | sealed box |
|---|---:|---:|---:|---:|---:|---:|
| ray-traced DDGI (sky leak 0.1) | +1.81 / 1.81 | +0.59 / 0.61 | +0.66 / 0.67 | +0.96 / 0.98 | +1.09 / 1.10 | 0.019 |
| ray-traced DDGI, no sky leak | +1.54 / 1.57 | +0.25 / 0.34 | +0.61 / 0.62 | +0.91 / 0.93 | +0.91 / 0.94 | 0.015 |
| **ray-traced screen gather** (the defaults, final build) | +0.11 / **0.23** | +0.15 / **0.19** | +0.21 / 0.21 | +0.56 / 0.57 | +0.24 / **0.29** | 0.016 |
| the same, probes jittered, 32 rays | +0.12 / 0.26 | +0.16 / 0.19 | +0.21 / 0.21 | +0.56 / 0.57 | +0.25 / 0.30 | 0.016 |
| the same with the sky leak 0.1 | +0.87 / 0.88 | +0.53 / 0.53 | +0.34 / 0.34 | +0.64 / 0.65 | +0.63 / 0.63 | 0.020 |
| 16 rays | +0.11 / 0.27 | +0.16 / 0.19 | +0.21 / 0.21 | +0.56 / 0.57 | +0.24 / 0.31 | 0.016 |

* The traced first bounce takes ray-traced GI from a stop too bright (0.94-1.10 stops off a pixel) to 0.29: Sponza's
  arcades and Bistro's street within a quarter of a stop.
* GI Lab's room stays too bright, +1.2 stops from DDGI's +2.1: its further bounces come from DDGI's probes, as bright
  there as they were (an enclosed white room, where the second and third bounces carry much of the light). The SDF
  gather is right there (its further bounces are SDFGI's).
* With the sky leak added it is twice as far off: it is not added with the gather.

GPU time at 1080p on the RTX (3 runs each, alternating):

| | Sponza | Bistro interior |
|---|---:|---:|
| no GI | 5.37 ms | — |
| SDFGI | 6.77-6.81 ms | 3.31 ms |
| **SDFGI + screen gather** (the defaults, final build) | **+0.26-0.27 ms** | — |
| the same, probes jittered, 32 rays | +0.30 ms | +0.27 ms |
| the same, probes jittered, 16 rays | +0.30 ms | +0.23 ms |
| the same, tiles of 8 px | +0.74 ms | — |
| ray-traced DDGI | 6.90-6.94 ms | 3.71 ms |
| **ray-traced DDGI + screen gather** (the defaults, final build) | **+0.66-0.74 ms** | — |
| the same, probes jittered, 32 rays | +0.93 ms | +0.51 ms |
| the same, probes jittered, 16 rays | +0.70 ms | — |

**Still frames' noise.** Each probe first sat at one of its tile's pixels jittered per frame, so that over frames the
tile was covered. The resolve's interpolation then changed from frame to frame: 16 frames of a still view, the share of
pixels whose standard deviation exceeds 2 levels (of 255, after tone mapping):

| | GI Lab's room (lavapipe) | GI Lab's room (RTX) | Sponza (RTX) |
|---|---:|---:|---:|
| SDFGI | 0.44 % | 0.15 % | 2.2 % |
| the gather, probes jittered (as measured above) | 3.7 % | 1.35 % | 6.8 % |
| the same with 32 or 64 frames of history | 3.6 % | | |
| **the gather, probes at their tiles' centres** | **0.55 %** | **0.16 %** | **2.7 %** |

A longer history did not help (the noise was the probes' moving, not their rays). With the probes fixed the gather is as
steady as SDFGI (the rest of Sponza's share is TAA's jitter on its foliage and edges, in both), and its error against the
reference is the same (0.34 stops; the ray-traced gather 0.29).

The ray-traced gather then still flickered in Sponza: 12.8 % of the pixels (ray-traced DDGI 3.8 %), in patches of its
tiles on the normal-mapped curtains, with 16, 32 or 64 frames of history and 16 or 32 rays alike. Its rays started a few
millimetres off the surface along the shading normal and went around it: with a normal map, some went into the surface
and hit it at once. They now start along the geometric normal (from the depth buffer, as the path-traced reference's),
and a direction below the geometric surface is mirrored above it (both gathers):

| | Sponza (RTX) | GI Lab's room (RTX) |
|---|---:|---:|
| ray-traced DDGI | 3.8 % | 0.18 % |
| ray-traced gather, the shading normal | 12.8 % | 0.16 % |
| ray-traced gather, rays by the geometric normal | 12.9 % | |
| **ray-traced gather, the history's check and the neighbours' weights by it too** | **10.9 %** | |
| the same, the rays lifted 2 cm + 0.4 % of the distance (not kept: the error 0.37 stops) | 10.7 % | |
| 16, 32 or 64 frames of history, 16 or 32 rays (shading normal) | 12.6-12.8 % | |
| SDF gather (all of the above) | 2.7 % | 0.16 % |

Not resolved: neither the rays' number, the history's length nor their origin is the cause. With TAA's jitter the
probes' pixels on the curtains' folds fall on other surfaces from frame to frame and the resolve's weights flip between
probes whose traced light differs much (one sees the sunlit courtyard, its neighbour the fold); the SDF gather's light,
from SDFGI's voxels, differs little between neighbours. A temporal filter of the resolved light (reprojected, clamped to
its neighbourhood, as Lumen's) is the next step; until then the ray-traced gather trades Sponza's steadiness for its
accuracy (it is off by default).

## Found on the way

* Bistro exterior's flythrough with SDFGI (m166, f518): two of three runs stopped on a frame's fence never signalled
  (the GPU idle at 619 MHz, 28 W): decision 0270's rare failure, far more often with SDFGI. The same flythrough in f519
  and f521 (m167a, m167c) ran three times out of three. Not followed further here.
* lavapipe's GI Lab captures after 120 frames had not converged: SDFGI's and DDGI's bounces build up over hundreds of
  frames; the GPU's captures after 600 frames are the measurements.

## Decision

KEEP both, off by default:

* without ray tracing, with SDFGI in the environment: an error a pixel of 0.34 stops from SDFGI's 0.41 (0.10 from 0.41 in
  GI Lab's room; a quarter of SDFGI's leak), for 0.3 ms at 1080p;
* with ray-traced DDGI: 0.29 stops from 0.94-1.10, the closest diffuse light to the truth FellowEngine has, for 0.7 ms;
  but it flickers on Sponza's curtains (10.9 % of the pixels of a still view, DDGI 3.8 %): a temporal filter of its
  resolved light is the next step.

16 rays per probe, as good as 32 and 64 in every capture, and in the ray-traced gather a quarter of a millisecond
cheaper. The rooftops showcase, lit by SDFGI, turns the SDF gather on (its windows' interiors and the shade under its
ledges and shutters deeper, its lit stone as it was).

## Rejected / not done

* DDGI's probes traced through SDFGI's fields (above).
* A half-cell surface bias (leaks through thin walls).
* The screen gather's own bounce: its hits take SDFGI's voxel light, whose further bounces are SDFGI's (bounce feedback
  0.5: Bistro exterior a little dark; 1: GI Lab's room too bright). An albedo per voxel would let the gather's probes
  feed its hits.
* Screen-space traces before the field (Lumen's short-range traces), for contact detail below SDFGI's 20 cm cells: SSAO
  still applies.
* The ray-traced gather's further bounces from its own probes (a radiance cache) instead of DDGI's: GI Lab's room.
* A temporal filter of the resolved light: the ray-traced gather's flicker.
* The probes' hidden-probe coverage (DDGI falls back to the ambient light where its probes are hidden from a point) as a
  setting: 0 and 0.2 gave the same figures to the hundredth of a stop. Not kept.
