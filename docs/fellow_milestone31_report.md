# FellowEngine — milestone 31 report (lightning lights the clouds)

Status: **measured** — lavapipe and the RTX 5070 Ti Laptop (`m31_s12`, 2026-11-02). Branch `fellow/geometry`.
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

Rain Lab at dusk in a thunderstorm under volumetric clouds (`m31_s12_rain_storm_clouds`, fellow221;
[SHOWCASE.md](SHOWCASE.md)): the deck dark between strikes and lit from inside during one (`storm_clouds_square.jpg`),
and a loop down the street with two strikes (`loop_storm_clouds.webp`, in the README in place of milestone 29's).

## 5. Left for later

* The glow ignores the clouds' inner structure (the flash's light times their opacity): a light inside the trace
  would show it, if the trace could follow a flash of a few frames.

## 6. The defaults are not slower (milestones 27-31)

The nine scenes, stock Godot against fellow212 (milestone 26's final build) and fellow220 (milestone 30, `9356d62`)
with their defaults, 3 runs each after a pass filling the shader caches (`m30_f294_final`, the procedure of
`m26_f285_final`). Frame time against stock:

| scene | fellow212 | fellow220 |
|---|---:|---:|
| Sponza | −54.5 % | −54.2 % |
| Bistro exterior | −34.8 % | −33.4 % |
| City Lab | −26.7 % | −25.8 % |
| Crowd Lab | −23.5 % | −24.0 % |
| Bistro interior | −23.2 % | −23.6 % |
| Lights Lab | −22.7 % | −23.4 % |
| Foliage Lab | −18.1 % | −17.8 % |
| PBR Lab | −17.2 % | −16.4 % |
| Stress Lab | −5.7 % (noisy) | −2.7 % (noisy, 2 of 3 pairs) |

fellow220 was faster than stock in every scene and every pair of runs but Stress Lab, whose runs spread by 4 % this
time for both builds (−11.5 % for fellow212 in `m26_f285_final`). Against fellow212, Bistro exterior was 2.1 % and
Sponza 0.6 % slower in every run — but fellow220 always ran right after fellow212. With the order reversed and pass
timestamps (`m30_f296_order`, 3 runs): Bistro exterior +0.2 % and Sponza +0.8 % over the second and third runs (the
first warming up for both), Bistro's third run and Sponza's third faster with fellow220, and no pass consistently
slower — within the spread. Nothing of milestones 27-31 runs without its nodes: the weather's code in the colour
passes sits behind a specialization constant that is off.

