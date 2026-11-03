# 0133 — Cloud shadows and the sky's light under the clouds

Status: **KEEP**, on with the clouds (`post/clouds/shadows` 1, `post/clouds/sky_light` 1; the clouds themselves off by
default) · Branch `fellow/geometry` · Settings: `rendering/fellow/post/clouds/shadows` (0-1: how much of the sun they
take), `rendering/fellow/post/clouds/sky_light` (0-1: how grey the sky's light turns under them) · Code:
`servers/rendering/renderer_rd/effects/fellow_clouds.{h,cpp}` (`process_shadow()`),
`renderer_rd/shaders/effects/fellow_clouds.glsl` (`MODE_SHADOW`), `effects/fellow_weather.{h,cpp}`
(`CloudShadowState`), `render_forward_clustered.{h,cpp}` (`_fellow_process_cloud_shadow()`, binding 54),
`scene_shader_forward_clustered.h` (`fellow_cloud_shadow`), `shaders/forward_clustered/scene_forward_clustered{,_inc}.glsl`,
`storage_rd/light_storage.cpp`.

## Problem

The volumetric clouds (0109) are drawn in the sky only: the ground under a cumulus is as sunlit as the ground under a
clear sky, and an overcast deck leaves the street's hard shadows. The sky's light — the environment's ambient light
from the sky and its reflections, from the sky's radiance map — is the clear sky's blue under any deck: a wet street
under rain clouds mirrors a blue sky. Milestone 32's weather controller dims the sun
evenly by a factor (`sunlight`) — a cloudy day without shadows that come and go, an overcast without sunny gaps.

## Change

**The map.** Each render, before the colour passes, a compute pass writes a shadow map: 512² texels over 16 km around
the camera (31 m each), snapped to whole texels so it does not swim. Two maps take turns: the colour passes read the
one written by the previous frame (see below). A texel is a point on the ground; its value is
the sun's transmittance through the cloud layer above it towards the sun: 32 steps through the layer of the clouds'
own density — the trace's weather field, shapes and eroded edges, at this frame's time and wind (`exp(-optical
depth)`, stopping where nothing is left). The layer is the one the clouds pass will draw: a FellowWeather3D's coverage
and density (0132) or the project settings'. The sun is the clouds' light (the first directional light that lights the
sky); its index among the frame's directional lights (as `update_light_buffers()` numbers them) goes to the shaders.

**The surfaces.** The colour passes, specialized with `fellow_cloud_shadow` (a new specialization bit: off, the code is
not compiled), multiply that light's shadow by the map, after the shadow map's and the contact shadows': a point above
the ground is moved down along the sun to the ground first (a roof 30 m up at 30° reads 52 m away), and towards the
map's edges (8 km) the clear sky's share (1 − coverage) takes over. `post/clouds/shadows` scales it. Everything lit by
the sun through the scene shader takes it — opaque, transparent and particles, the water.

**The sky's light.** Where the clouds cover the sky (from none at a third covered to all of it at nine tenths,
smoothstep, times `post/clouds/sky_light`), the colour passes grey the sky's ambient light (its own luminance) and its
reflections above the horizon (the luminance of the radiance map's blurriest level in the reflected direction: the
sky's mean brightness there, without the sun's disc, which the deck hides). Under the same specialization bit; one
more lookup of the radiance map per pixel while it is on. The radiance map itself is not redrawn.

**With the weather controller** the clouds' shadows take the place of its `sunlight` factor: `update_light_buffers()`
does not apply that factor while they are on.

## Development

* The first map marched the shapes alone (no eroded edges: cheaper, and the shadows are soft anyway). Walking under
  a broken layer looking at the sun (lavapipe), the ground stayed dark where the sun showed past a cloud's edge: the
  shapes are fatter than the clouds drawn. With the edges eroded as in the trace they agree.
* Beyond the map the lookup clamped to its edge and drew streaks across the distant ground (seen from 6 km up); the
  edge now fades to the clear sky's share.
* **The device lost with DDGI.** On the RTX every Bistro capture with the clouds' shadows and ray-traced GI on lost the
  device in its first frames (`m33_s14`, `m33_s15`: `VkResult -4`, then a crash in the driver); without DDGI, or with
  the shadows off, it ran (`m33_f302`: each ray-traced effect alone, the day's post effects). A diagnostic build
  (`m33_f303`, `m33_f304`) narrowed it down: the map's pass alone ran, the colour passes reading a white map without the
  pass ran, but reading the map written earlier in the same frame lost the device even with its effect at nothing —
  and reading the one written in the previous frame did not. So the map is double-buffered: each frame writes one map
  and the colour passes read the other, one frame behind the clouds (which move a fraction of a metre in it; lavapipe:
  at most 9 levels in a few pixels against the single map). What in the frame's graph orders that write and read
  wrongly with DDGI is not found yet.

## Verification

* **Off is the previous image** (lavapipe, against milestone 32's build): Rain Lab without drops (5 images), Water Lab
  (5), and Rain Lab at dusk under clouds at coverage 0.7 with `post/clouds/shadows` and `sky_light` 0 (5) — 0 pixels
  differing.
* **Lavapipe, a plain of 8 km** seen from 400 m under a broken layer (coverage 0.5): shadow patches of a kilometre or
  two with soft edges, the gaps in full sun; against `shadows` 0 the plain evenly lit.
* **Rain under a deck** (lavapipe, the test scene in the controller's rain, clouds on): with the sky's light greyed the
  wet ground, the puddles and the objects lose the clear sky's blue tint; with `sky_light` 0 they keep it.
* **Walking under the layer, looking at the sun** (12 places 400 m apart): where the sun is behind a cloud the ground
  around the camera is in shadow, where it shows the ground is lit; under an edge, the shadow's edge runs through the
  view.

## Results

**Cost** (RTX 5070 Ti Laptop, 1080p, Bistro exterior under the clouds at their default coverage, 2 runs each,
`m33_f305_bistro`, fellow228): the map's pass 0.12 ms; the frame 4.906 / 4.389 ms without the shadows and sky light,
4.592 / 4.555 ms with them — +0.17 ms in the second runs (the first run without warmed up), the colour passes' two
lookups the rest. Single-buffered (`m33_f298_bistro`, fellow223, shadows only): the pass 0.118 ms, the frame +0.09 ms.

**Bistro by day** (`m33_s17`, the day look with ray tracing, DDGI included, after the double buffering): the weather's
cloudy, overcast and rain under their clouds' shadows (Bistro is a few hundred metres across: at a moment it lies
under one cloud or in one gap), and in the weather time-lapse (`m34_s17_bistro_weather_loop`) the street going into
the clouds' shade as they close in and coming back into the sun as they part.

## Decision

**KEEP.**
