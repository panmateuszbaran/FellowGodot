# FellowEngine — milestone 29 report (lightning)

Status: **measured** — lavapipe (2026-11-02) and the RTX 5070 Ti Laptop at 1080p (`m29_f290`, `f291`, 2026-11-02).
Branch `fellow/geometry`. Decision record `docs/decisions/0129-lightning.md`; user-facing summary
`docs/FELLOWENGINE_OPTIONS.md` ("Lightning").

## 0. Result

The weather of milestones 25-28 — rain, wind, snow — gets its storm. `FellowLightning3D` strikes at random around the
camera, or when a script asks: two to four return strokes in quick succession, each a flash that lights the scene
from the bolt's direction with shadows, a branching bolt glowing in the sky at a constant width on screen, a cloud lit
from inside, and a signal for the thunder. The same clock gives the same storm.

## 1. Development

* **A node of its own.** Lightning does not need rain (dry storms; a scripted strike), so it is not part of
  `FellowRain3D`. It owns three internal children: the flash (`DirectionalLight3D`), the bolt and the cloud's glow
  (`MeshInstance3D`s).
* **The flash.** Shining from the bolt's middle towards the camera, so the shadows fall away from the bolt; visible
  only while a stroke lasts (no cost between strikes); `SKY_MODE_LIGHT_ONLY` (no second sun disc in the sky); a meta
  tag so that `FellowWater3D` does not take it for the sun.
* **The bolt.** Midpoint displacement from the cloud to the ground, branches from its upper two thirds and some of
  theirs; ribbons built on the CPU when it strikes, turned to the camera, as wide on screen at any distance
  (0.006 rad, about 6 pixels at 1080p): a thinner bolt would be dimmed by TAA on its first frames (its 3 × 3
  neighbourhood mostly dark). Beyond the camera's far plane (Rain Lab's camera reaches 1.5 km) the bolt is drawn
  nearer and scaled to look the same.
* **Fog.** The additive bolt and glow were first fogged: fog mixes its colour into a transparent surface, and an
  additive one then adds the fog's colour where it should add nothing — a rectangle in the sky. Fog is off for both.
* **The clock.** One test per second of the node's clock, a function of the seed and the second, so the storm does
  not depend on the frame rate.
* FellowBench: `--weather_lightning=<per minute>` in any scene; `--weather_lightning_flash=<k>` strikes in the
  camera's view `k` frames before each capture (`--weather_lightning_distance`, `--weather_lightning_turn`),
  `--weather_lightning_frames=<f,...>` at those frames of each sequence (in `--extra`: a comma in the builds list
  splits the builds — `m29_f290`'s loops failed on it).

## 2. Verification

* Lavapipe (Rain Lab at dusk, TAA on): the bolt and a branch over the square, the facades and the drops lit by the
  flash, the trees' shadows away from the bolt, the flash's highlight on the wet ground.
* RTX (`m29_s8_rain_storm`): the same from the square and the street; the arcade's columns cast the flash's shadows
  across its floor.

## 3. Performance

RTX 5070 Ti Laptop, 1080p, Rain Lab at dusk raining, 2 runs each (`m29_f290`): 2.447 ms of GPU time without
lightning, 2.488 ms with a strike every 2 s on average (+0.04 ms: the flash's shadows +0.02, the opaque pass +0.03).
Between strikes nothing is drawn or lit.

## 4. Showcase

Rain Lab at dusk in a thunderstorm (`m29_s8_rain_storm`, `m29_s9_rain_storm_loop`; [SHOWCASE.md](SHOWCASE.md)): a bolt
over the square with the flash on the rain and the wet ground, the arcade's columns casting the flash's shadows, and
a loop down the street with two strikes. Bistro at night in the storm (`m29_s8`/`s9_bistro_storm`, energies 6, 2, 1,
1.5) did not make it: the raindrops between the camera and the bolt scatter the flash forward, and in Bistro's night
exposure (×6) they turned into bright bars over the whole image even at an energy of 1, while the facades were barely
lit. The drops' streaks are wider than drops (for visibility under lamplight), which a flash from in front exposes;
see "left for later".

## 5. Left for later

* The raindrops under a flash: their streaks are wider than drops so that lamplight shows them, and a bright flash
  from in front of the camera turns them into bars (Bistro at night). Their forward scattering could be scaled down
  for directional lights, or their alpha by the streak's width over a drop's.
* Thunder itself (an `AudioStreamPlayer3D` the project connects to `struck`).
* Intra-cloud flashes (sheet lightning without a bolt), and the volumetric clouds lit by the flash.
* The flash in ray-traced GI (it is a directional light: RT sun shadows take the scene's first one).
