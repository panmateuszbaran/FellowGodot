# FellowEngine — milestone 28 report (snow)

Status: **measured** — lavapipe (2026-11-02) and the RTX 5070 Ti Laptop at 1080p (`m28_f288`, 2026-11-02).
Branch `fellow/geometry`. Decision record `docs/decisions/0128-snow.md`; user-facing summary
`docs/FELLOWENGINE_OPTIONS.md` ("Snow").

## 0. Result

After rain (milestone 25) and wind (26-27), winter. `FellowRain3D` gets a snow mode, and the pass that wets the scene
can cover it with snow:

* **flakes** drift down around the camera, swing about their paths, take the scene's wind where they are — gusts
  included, through milestone 27's Wind group of the particles — and stop on the scene as seen from above (none under
  roofs); lamps light them and buildings shadow them;
* a **snow cover** (`snow`, 0-1) lies on what faces up where the snow falls: patchy with ragged edges while thin,
  everything facing up once deep, nothing on slopes over 60°, bare under roofs and under the trees' canopies (the
  shelter heightfield of the rain); fresh snow is white, rough and smooth with gentle undulations, and glints where a
  crystal faces the sun;
* the cover can grow while it snows and melt afterwards (`snow_follows_precipitation`).

## 1. Development

* One node: the snow mode of `FellowRain3D` (`precipitation` = Snow) keeps the rain's shelter heightfield, its
  camera-following and its renderer state; the snow cover (`snow`) goes through the same specialization of the depth
  pre-pass and the opaque colour pass as the wetting (`fellow_wetness`), which is on when either is. The wetting moved
  into `fellow_wet_surface`, the snow into `fellow_snow_cover`; the weather's uniform buffer grew by a vector (cover,
  sparkle).
* **Flakes in a box, not falling from above.** Raindrops fall from above the camera to the ground in a second or two;
  flakes take ten times longer, and the column would have to reach far upwind. Flakes live 8 s in a box around the
  camera, raised and upwind by half of what they fall and drift in a life, fading in and out.
* **Flakes in the wind.** With a `FellowWind3D`, the flakes' process material gets milestone 27's Wind group: each
  flake takes the wind where it is, gusts included. The flutter is in the vertex shader (a phase and rate per
  particle).
* **TAA wiped out the flakes.** The first Bistro captures (`m28_f288`) showed snow on the ground and hardly a flake in
  the air. Blended particles write no motion vectors, and a flake that moves more than its size in a frame has no
  history where it is now: TAA's neighbourhood clamp kept the dark history and the flake faded to a tenth. Drawn in
  the opaque pass with hashed alpha, the flakes get the particles' motion vectors (the vertex shader runs again with
  the previous frame's transforms and time, the flutter included) and TAA resolves their edges.
* **Rows of bare dots.** On Bistro's streets, the strings of lights across them left rows of bare spots (they looked
  like footprints): the shelter heightfield is the scene's top as seen from above, wires and bulbs included. The
  snow's shelter is now eroded by 35 cm — the lowest of five points of the heightfield — so thin things overhead
  shelter nothing; the rain's is unchanged.
* **Thin snow** first lay in soft grey smears; a third, finer noise layer (7 cm cells) and a crisper edge give its
  patches ragged edges.

## 2. Verification

* The rain unchanged (lavapipe, against the build before): Rain Lab without drops 5 images and Water Lab 5 images
  with 0 pixels differing. With drops the splashes differ between two runs of the same build (the order of the
  sub-emitter's particles): no comparison there.
* A test scene (lavapipe): covered what faces up, a 35° slope covered and a 60° one bare, bare under a roof, a sphere
  capped, thin snow in patches; no bare strip under a 5 cm wire or a 40 cm lamp head once the shelter is eroded.
* Wind Lab under snow (lavapipe): the meadow white, snow on the leaves facing up, tree wells under the canopies.
* TAA on, at night: blended flakes almost all gone; opaque with hashed alpha, as many as without TAA.

## 3. Performance

RTX 5070 Ti Laptop, 1080p, Rain Lab, 2 runs each with pass timestamps:

| Rain Lab | GPU time | against dry |
|---|---:|---|
| without `FellowRain3D` (dry) | 1.926 ms | |
| raining (24000 drops, wet, puddles) | 2.266 ms | +0.34 ms |
| snowing, flakes blended (`m28_f288`, fellow216) | 2.140 ms (dry 1.901) | +0.24 ms |
| snowing, flakes opaque with hashed alpha, the shelter eroded (`m28_f289`, fellow217) | 2.300 ms | +0.37 ms: the depth pre-pass +0.10, the opaque pass +0.10, the flakes' update +0.07 |

60000 flakes in a box of 30 m. Particles Lab re-measured with its visibility boxes grown downwind (`m28_f288`, for
milestone 27): in 4 m/s 3.208 ms of frame against 3.114 still (+0.09; GPU 2.338 against 2.412), the particles'
update +0.04 ms, draw calls 309 → 338. (In `m27_f287` both variants had run at about 6 ms of frame: CPU time that
run had lost elsewhere.)

## 4. Showcase

Bistro under snow (`m28_s7_*`, fellow217; [SHOWCASE.md](SHOWCASE.md), "Bistro in the snow"): at night — the café
with snow on its awnings, planters and flower boxes, the terrace under the awnings bare, the flakes in the lamplight
(`bistro_snow_night_cafe.jpg`, `bistro_snow_night_street.jpg`, two loops) — and after a light snowfall against a
sun 7° high, the cobbles bare under the square's tree, the flakes glowing against the sun
(`bistro_snow_gold_tree.jpg`, `bistro_snow_gold_cafe.jpg`). The first captures (`m28_s6_*`, fellow216) showed the two
problems fixed in this milestone: hardly a flake in the air (TAA) and rows of bare dots under the strings of lights.
The night loops of `m28_s6` were darker than the stills: the temporal mode turns auto exposure off unless
`--keep_exposure=1`, which the showcase loops now pass. Rain Lab at dusk in the snow and the meadow in a snowstorm
did not make the showcase (the lab's plain buildings; flakes against a bright sky looked dark).

## 5. Left for later

* Footprints and tracks: a trail texture around the camera that objects stamp, read by the cover.
* The rain's shelter eroded the same way (strings of lights keep dry dots on a wet street; hardly visible).
* Mobile and Compatibility renderers; transparent materials are not covered.
* Ray-traced reflections and GI see the scene without its snow (the cover is in the raster passes only).
