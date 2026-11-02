# 0129 — Lightning

Status: **KEEP** (a node: nothing changes without one) · Branch `fellow/geometry` · Node: `FellowLightning3D`
(`strikes_per_minute`, `distance_range`, `cloud_height`, `energy`, `color`, `bolt_brightness`, `bolt_width`,
`shadows`, `seed`, `time`, `time_auto`; `strike()`, `struck`) · Code: `modules/fellow_fluid/fellow_lightning_3d.{h,cpp}`,
`fellow_water_3d.cpp` (the flash is no sun), `benchmarks/project/bench/main.gd`.

## Problem

Rain (0124), wind (0125, 0127) and snow (0128) make weather, but a storm has lightning, and projects make it by hand:
a light whose energy a script pulses, a bolt texture on a quad. The pieces that make a flash read — a few return
strokes in quick succession rather than one blink, a light from the bolt's direction that casts shadows, a bolt that
branches and stays the same width on screen whatever its distance, a cloud lit from inside — each take work, and a
script's timing depends on the frame rate.

## Change

`FellowLightning3D` strikes at random times (`strikes_per_minute`, 4) and distances (`distance_range`, 0.4-3 km,
uniform over the ring's area) around the current camera, or when `strike(distance, azimuth)` is called:

* **Strokes.** A strike is two to four return strokes 40-120 ms apart, each a flash decaying over about 40 ms (the
  first at full strength, the others at 50-100 %).
* **Flash.** An internal `DirectionalLight3D` shines from the bolt's middle towards the camera, at `energy` (6) for a
  strike 1 km away, nearer brighter (by the inverse of the distance, 0.3-2.5 times); with `shadows` it casts them.
  It is visible only while a flash lasts, draws no sun disc in the sky, and `FellowWater3D` does not take it for the
  sun.
* **Bolt.** A channel from the cloud (`cloud_height`, 1.2 km) to the ground by midpoint displacement (7 levels, 129
  points), branches from its upper two thirds and a few branches of theirs; ribbons turned to the camera at a
  constant angular width (`bolt_width`, 0.006 rad: about 6 pixels at 1080p — wide enough that TAA's neighbourhood
  clamp keeps it on its first frame), a soft core across them, unshaded and additive, hidden by the scene in front of
  it, not fogged (fog would add its colour to an additive surface). Beyond the camera's far plane it is drawn nearer,
  scaled to look the same.
* **Cloud.** A camera-facing glow above the bolt, lit with the flash.
* **Clock.** The random strikes are a function of `seed` and the node's clock: in each second, one with a
  probability of `strikes_per_minute` / 60 at a moment within it. The same clock gives the same storm, whatever the
  frame rate.
* **Thunder.** `struck(position, distance)` when a strike begins; the thunder arrives `distance / 343` s later.

FellowBench: `--weather_lightning=<per minute>` in any scene; `--weather_lightning_flash=<k>` strikes in the
camera's view `k` frames before each capture, `--weather_lightning_frames=<f,...>` at those frames of each sequence.

## Verification

* **Lavapipe** (Rain Lab at dusk, a strike in view two frames before each capture, TAA on): the bolt over the
  square with a branch, the flash lighting the facades and the drops, the trees' shadows cast from the bolt's side,
  the flash's highlight on the wet ground. Fogged, the additive bolt and glow had added the fog's colour where they
  were transparent (a rectangle in the sky): fog off for both.
* **RTX** (Rain Lab at dusk, `m29_s8_rain_storm`): the bolt in view from the square and the street, the arcade's
  columns casting the flash's shadows across its floor.
* **Cost** (RTX 5070 Ti Laptop, 1080p, Rain Lab at dusk raining, 2 runs each, `m29_f290`): 2.447 ms of GPU time
  without lightning, 2.488 ms with a strike every 2 s on average (+0.04 ms: the flash's shadows +0.02, the opaque pass
  +0.03); between strikes nothing is drawn or lit.
* **Bistro at night** (exposure ×6, `m29_s8_bistro_storm`): an energy of 6 blew the image out — the raindrops
  between the camera and the bolt scatter its light forward. Scenes exposed for lamplight need a far lower `energy`.

## Decision

**KEEP.**
