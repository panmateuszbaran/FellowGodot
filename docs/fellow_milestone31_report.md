# FellowEngine — milestone 31 report (lightning lights the clouds)

Status: **measured on lavapipe**; the RTX showcase (`m31_s12`) is pending (2026-11-02). Branch `fellow/geometry`.
Decision record `docs/decisions/0131-lightning-clouds.md`.

## 0. Result

Milestone 29's flashes lit the scene but not the sky: under the volumetric clouds (0109) a strike left the cloud deck
dark. Now the clouds glow around each flash — by how near the view passes to it and how opaque they are — so a strike
lights the whole deck from inside, and a strike behind the clouds is sheet lightning.

## 1. Development

* `FellowLightning3D` hands its flash (the bolt's top, its light, a radius of 0.6 × the cloud height) to the renderer
  while it lasts (`FellowWeather::LightningState`); `cloud_glow` (0.5) scales it.
* The glow is added in the clouds' composite, at full resolution and every frame. The traces themselves are at half
  resolution, a quarter of the pixels a frame, accumulated over frames: a flash lasting a few frames would have come
  out smeared and late.

## 2. Verification

* Without a flash the clouds are bit for bit milestone 30's (lavapipe, Rain Lab at dusk with the clouds on, 5 images).
* Lavapipe, a strike in view under a deck at coverage 0.75: the deck lit from the bolt outwards, against near black.

## 3. Performance

One branch in the composite while a flash lasts (per sky pixel a dot product, a length and two exponentials); nothing
between strikes.

## 4. Showcase

Pending: Rain Lab at dusk in a thunderstorm under volumetric clouds (`m31_s12_rain_storm_clouds`, stills and a loop),
queued on the RTX after the nine-scene check.

## 5. Left for later

* The glow ignores the clouds' inner structure (the flash's light times their opacity): a light inside the trace
  would show it, if the trace could follow a flash of a few frames.
