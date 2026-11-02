# FellowEngine — milestone 27 report (the scene's wind for water, gas and particles)

Status: **measured** — lavapipe (2026-11-01/02) and the RTX 5070 Ti Laptop at 1080p (`m27_f287`, 2026-11-02).
Branch `fellow/geometry`. Decision record `docs/decisions/0127-scene-wind.md`; user-facing summary
`docs/FELLOWENGINE_OPTIONS.md` ("Wind", the paragraph on milestone 27).

## 0. Result

Milestone 26's `FellowWind3D` moved vegetation and the rain's drops. Everything else that wind moves had a wind of its
own, set by hand: a lake under gusts that rolled over the meadow beside it stayed as calm as before, a campfire's
smoke leaned at one fixed angle, sparks and leaves fell straight down. Milestone 27 hands the same wind, gusts
included, to the rest, each when asked:

* **water** (`FellowWater3D.wind_from_scene`): the spectrum follows the wind's speed and direction, and the gust
  field — the one the plants read, mapped the same way — roughens the shortest cascade: darker, rougher water in a
  gust, a mirror in a lull: the "cat's paws" that run over a lake ahead of a gust (`gust_ripples` scales them);
* **gas** (`FellowGasVolume3D.wind_from_scene`): the wind at the box's centre each step, gusts included, as the
  inflow through its open faces and the relaxation inside: flames and smoke lean and waver with the gusts;
* **particles** (`ParticleProcessMaterial`'s Wind group, `wind_enabled`, `wind_influence`): the horizontal velocity
  relaxes towards the wind at the particle's position; particle shaders read it as `WIND_VELOCITY`.

## 1. Development

* `FellowWind3D::get_gust_mapping()` gives the gust texture's mapping (direction, travelled offset, scale) so that the
  water's material and the particles' compute shader sample exactly the field the vegetation and
  `get_velocity_at()` sample.
* The water's shader keeps the path it had (one uniform branch) when `wind_from_scene` is off or there is no wind.
  The shortest cascade's slopes are scaled by the local wind over the mean wind and their variance by the square,
  which keeps the glitter's width consistent with the slopes.
* Particles: the wind's state and the gust mapping go into the particles' frame parameters, the gust texture into
  their collision uniform set (binding 2, rebuilt only when the texture changes). `WIND_VELOCITY` is a process
  built-in; the Compatibility renderer reads zero.
* FellowBench: `--weather_wind=<m/s>` (`--weather_wind_dir`, `--weather_gustiness`) adds a `FellowWind3D` to any
  scene; Water Lab, Fire Lab and Particles Lab hand it to their water, gas and particles (Fire Lab's embers and
  Particles Lab's emitters with the Wind group; Fire Lab's gas `wind_rate` 0.5 when it had none); Wind Lab
  `--wind_leaves=<n>` adds falling leaves (GPU particles with the Wind group: a box emitter over the meadow,
  billboarded alpha-scissor leaves).
* A fix found on the way (`84f3a8e`): Water Lab with contact shadows crashed ("Texture (binding: 43) is not a valid
  texture") — the depth pre-pass's uniform set was built before the per-render masks it binds existed and kept a
  freed one. The pass now binds the masks only for the opaque and motion lists of a render with render data.

## 2. Verification

* Off is the previous image (lavapipe, 480 × 270, against the build before the milestone): Water Lab without a wind
  5 images with 0 pixels differing, Fire Lab without a wind 4 images with 0 pixels differing.
* Water Lab under a wind of 9 m/s (`--weather_wind=9`, 640 × 360): the same images in two runs; the glint's path wider
  and the lake's surface patchy with the gusts.
* Particles: a test scene with two emitters under a 3 m/s wind — the emitter with the Wind group drifts downwind, the
  other rises straight.

## 3. Performance

RTX 5070 Ti Laptop, 1080p, fellow215 (`0784530`), autotune off, 2 runs each with pass timestamps (`m27_f287_*`):

| scene | without wind | with the scene's wind | difference |
|---|---:|---:|---|
| Water Lab, 8 m/s, gust ripples off / on | 3.247 ms GPU | 3.494 / 3.198 ms | within the runs' spread (the still scene's two runs 3.94 / 3.34 ms of frame); the water's (transparent) pass 1.255 → 1.314 / 1.286 ms: the gust ripples' lookup is not measurable |
| Fire Lab, 4 m/s | 1.414 ms GPU | 1.389 ms | within the spread (the gas simulation −0.08 ms, the trace −0.02) |
| Particles Lab, 4 m/s | 2.545 ms GPU | 2.491 ms | the particles' update +0.149 ms (0.495 → 0.644: the gust lookup and the drag per particle), the transparent pass −0.06 |
| Wind Lab, 4000 falling leaves | 4.212 ms GPU | 4.228 ms | the leaves' update +0.025 ms, the shadows +0.025 |

Particles Lab's frame rose on the CPU, 6.05 → 7.50 ms. Part of it was the bench: it grew each emitter's visibility
box by the drift in every direction, which put 86 more emitters in view (draw calls 289 → 374, primitives 200 k →
329 k). The boxes now grow downwind only. Re-measured in `m28_f288`: 3.114 ms of frame still, 3.208 ms in the wind
(+0.09; GPU 2.412 / 2.338 ms), the particles' update +0.04 ms, draw calls 309 → 338 — in `f287` both variants had
run at about 6 ms of frame, CPU time that run lost elsewhere.

## 4. Showcase

`m27_s5_*` (1080p captures and loops, the looks of the README's showcase): the lake at sunset under gusts of 9 m/s —
the glitter path breaking up where a gust runs over it (`water_gusts_sunset.jpg`) — and a loop over the lake. The
campfire at night in the wind came out too dark with the embers drawn as square sprites, and 6000 leaves over the
meadow read as specks against the sky: neither is in the showcase.

## 5. Left for later

* The Mobile and Compatibility renderers.
* A `GPUParticles3D` with the Wind group could grow its own visibility box downwind by the wind's drift; for now the
  box is the project's (as for any particles).
* The fire's embers as round, glowing sprites (Fire Lab's look).
