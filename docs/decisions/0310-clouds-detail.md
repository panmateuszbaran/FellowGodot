# 0310 — The clouds' detail: a fine march inside them, a deeper erosion, the powder term

Status: **KEEP**, on wherever the clouds are on (`post/clouds/enabled`, off by default) · Branch `fellow/geometry` ·
Setting `rendering/fellow/post/clouds/detail` (on) · Code: `shaders/effects/fellow_clouds.glsl` (`cloud_density()`,
`integrate_sample()`, the trace's march), `effects/fellow_clouds.{h,cpp}`, `forward_clustered/render_forward_clustered.cpp`,
`fellow_settings_look.h`. The showcase's sky (the owner's choice): `showcase/scripts/rooftops.gd`.

Checks: lavapipe only (the owner's GPU not to be used): the image regression suite, the showcase's reference shot and
sky views. GPU time not measured yet.

## Problem

Beside the owner's reference video (a golden-hour sky over Florence: cumulus with crisp, billowed edges lit from the
side, warm against a blue sky), decision 0109's clouds were soft blobs: smooth outlines, evenly lit, grey. Three causes:

* **The erosion did nothing.** The detail noise's three Worley octaves average out: their weighted sum has a mean of
  0.48 and a deviation of 0.12 (measured over the 32³ texture), so the erosion it drives (`remap(c, erosion * 0.3, ...)`)
  varied by a tenth of the density: every edge was eroded alike, a smooth offset of the shape.
* **Its features were too small to see.** At decision 0109's frequency they are 60-250 m: a pixel or less on clouds
  5-15 km away at the showcase's resolution, averaged away by the half-resolution trace and the temporal blend.
* **The march was too coarse.** 64 even steps through up to 60 km of the layer: a step of several hundred metres, longer
  than the detail, inside a cloud.

## Change

With `post/clouds/detail` on:

* **The march**: coarse steps (the layer's span over 48, the shape alone, no detail) through clear air; on entering a
  cloud, back one coarse step and on in fine steps with the detail (25 m near the camera, 4 m longer per km of distance,
  at most a coarse step); back to coarse steps after eight empty fine samples. At most 160 samples a ray (64 before),
  most of them where there is cloud.
* **The erosion**: the detail noise's sum stretched to about [0, 1] (`(sum − 0.48) / (0.117 · 3.46) + 0.5`, as decision
  0109 stretched the weather field's), at 0.4 × the frequency (features of 150-600 m), and eroded deeper (0.45 of the
  density, 0.3 before).
* **The light**: the first three of the six samples towards the sun use the eroded density too (the billows shade one
  another; the shapes alone before), and the single scattering is darkened where the cloud is thin, the more the further
  from the sun one looks (Schneider's powder term, `mix(1, 1 − exp(−0.12 σ), (1 − cos θ) / 2)`, on the first octave
  only): the billows' sunlit sides bright in their middles, darker at their edges; towards the sun the silver lining
  kept.

Off, the march is decision 0109's 64 even steps and the density and light are its own.

## Measurements

Lavapipe, the editor build:

* The image regression suite's sky scene (`post/sky`, the clouds at the suite's defaults) with the detail off: its
  golden bit for bit (decision 0109's clouds). With it on (the default): its clouds' outlines broken into billows and
  shaded by one another, 52 % of the picture's pixels changed (by up to 61 levels, 12 % by more than the suite's
  floor); a new golden. The other 37 scenes have no clouds and are unchanged.
* The showcase's sky (the milestone 168 report, section 5): the clouds' edges crisp enough to show the
  half-resolution trace's steps in a still frame where they are dense (the showcase's layer, density 2.5).

## Decision

KEEP, on: the clouds' outlines eroded into billows instead of a smooth offset, their sides shaded by one another. The
GPU cost (up to 160 density samples a traced pixel where there is cloud, three detail fetches more per light march) is
still to be measured on the RTX.

## Rejected / not done

* The detail at decision 0109's frequency, stretched: features of a pixel or less, lost in the half-resolution trace and
  the temporal blend.
* More even steps (not tried): 128 would double the cost in clear air as well and still step 200-400 m inside distant
  clouds.
* The whole light march with the detail (not tried): six detail fetches more a sample, where the far samples (up to
  2 km away) see the shapes, not their edges.
