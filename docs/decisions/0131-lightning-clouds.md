# 0131 — Lightning lights the clouds

Status: **KEEP** (nothing changes without a flash) · Branch `fellow/geometry` · Node: `FellowLightning3D.cloud_glow`
(0.5) · Code: `modules/fellow_fluid/fellow_lightning_3d.{h,cpp}`,
`servers/rendering/renderer_rd/effects/fellow_weather.h` (`LightningState`), `fellow_clouds.{h,cpp}`,
`renderer_rd/shaders/effects/fellow_clouds.glsl` (the composite), `render_forward_clustered.cpp`.

## Problem

Decision 0129's flash lights the scene and draws a bolt and a glow of its own; the volumetric clouds (0109) stay as
dark as before it. A storm's clouds are what lights up most: sheet lightning is mostly cloud.

## Change

While a flash lasts, `FellowLightning3D` hands its position (the bolt's top), its light (colour × energy × the
stroke's envelope × π, as the colour passes' directional light) and a radius (0.6 × the cloud height) to the renderer
(`FellowWeather::LightningState`). The clouds' **composite** pass adds, to each sky pixel, the flash's light by how
near the view ray passes to the flash (a Gaussian in the miss distance over the radius, with a wider tail), times the
clouds' opacity there (one minus their transmittance) and `cloud_glow`, exposed as the scene's lights are.

In the composite, at full resolution, every frame — not in the trace: the clouds are traced at half resolution, a
quarter of the pixels per frame, and accumulated over frames (0109, 0115), which would smear and lag a flash that
lasts a few frames. The glow does not see the clouds' inner structure (it is the flash's light times their opacity),
which a flash this short does not show anyway.

## Verification

* **Unchanged without a flash** (lavapipe, Rain Lab at dusk with the clouds on, against milestone 30's build): 5 images
  with 0 pixels differing.
* **A strike in view** (lavapipe, Rain Lab at dusk, clouds at coverage 0.75): the cloud deck over the square lit
  white-grey from the bolt outwards, against a near-black deck without it (`cloud_glow` 1 here: the default became
  0.5, a little less washed out).
* **Cost**: one branch in the composite while a flash lasts (a dot product, a length, two exponentials per sky pixel);
  nothing between strikes.
* **RTX** (`m31_s12_rain_storm_clouds`, fellow221): Rain Lab at dusk under a deck at coverage 0.7 — dark between
  strikes, lit grey-white from the bolt outwards during one, holes in the deck staying dark.

## Decision

**KEEP.**
