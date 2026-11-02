# 0127 — The scene's wind for water, gas and particles

Status: **KEEP** (off until a node or material asks for it) · Branch `fellow/geometry` · Nodes and materials:
`FellowWater3D.wind_from_scene`, `gust_ripples`; `FellowGasVolume3D.wind_from_scene`; `ParticleProcessMaterial`'s Wind
group (`wind_enabled`, `wind_influence`) · Code: `modules/fellow_fluid/fellow_water_3d.{h,cpp}`,
`fellow_water_shader.h`, `fellow_gas_volume_3d.{h,cpp}`, `fellow_wind_3d.{h,cpp}` (`get_gust_mapping()`),
`scene/resources/particle_process_material.{h,cpp}`, `servers/rendering/renderer_rd/storage_rd/particles_storage.{h,cpp}`,
`renderer_rd/shaders/particles.glsl`, `servers/rendering/shader_types.cpp` (`WIND_VELOCITY`),
`drivers/gles3/storage/material_storage.cpp`.

## Problem

Milestone 26's `FellowWind3D` moves vegetation, and `FellowRain3D` takes its drops' wind from it. Everything else that
the wind should move had a wind of its own, set by hand. `FellowWater3D`'s spectrum had a fixed `wind_speed` and
`wind_direction`. A `FellowGasVolume3D` had a constant `wind`. GPU particles had nothing but their gravity. A scene with
gusts rolling over the meadow had a lake that did not feel them, smoke that leaned the same way at the same angle, and
leaves and sparks that fell straight down.

## Change

**Water.** With `wind_from_scene` on, the water follows the active `FellowWind3D`:

* **The mean wind drives the spectrum.** `wind_speed` and `wind_direction` follow its speed and direction, and the
  spectrum is rebuilt when either changes by more than 2 % or a degree.
* **The gusts roughen the short waves.** The shortest cascade's slopes are scaled by the local wind over the mean
  wind. That is the gust field the plants read, mapped the same way (`FellowWind3D::get_gust_mapping()`), and its
  variance is scaled by the square. In a gust the short waves grow and the water darkens and roughens. In a lull it
  smooths and mirrors the sky. These are the "cat's paws" that run over a lake ahead of a gust.
* **`gust_ripples`** (1) scales the effect. Without `wind_from_scene`, or without a wind, the shader takes the path it
  had before.

**Gas.** With `wind_from_scene` on, a `FellowGasVolume3D`'s wind each step is the active `FellowWind3D`'s velocity
at the box's centre, gusts included, instead of `wind`. It reaches the gas as `wind` did: as an inflow through the
open faces it blows into, and relaxed towards inside at `wind_rate`. Flames and smoke lean with the gusts.

**Particles.** `ParticleProcessMaterial` gets a **Wind** group, `wind_enabled` with `wind_influence` (1/s). The
particles' horizontal velocity relaxes towards the scene's wind at their position, gusts included, at that rate.
For particle shaders the process function gets a built-in `WIND_VELOCITY`. Forward+ and Mobile compute it in the
particles shader from the wind's state (`FellowWeather`, which the scene shaders already read) and its gust texture
(set 2, binding 2). Without a `FellowWind3D` it is zero, and on the Compatibility renderer it is zero. Particles with
`local_coords` read the wind at their local position.

FellowBench: `--weather_wind=<m/s>` (`--weather_wind_dir`, `--weather_gustiness`) adds a `FellowWind3D` to any
scene. Water Lab, Fire Lab and Particles Lab hand it to their water, gas and particles. `--wind_leaves=<n>` adds
falling leaves to Wind Lab.

## Verification

* **Off is the previous image.** On lavapipe at 480 × 270, Water Lab without a wind gives 5 images and Fire Lab 4
  images with 0 pixels differing from the build before.
* **Particles.** A test scene with two emitters under a 3 m/s wind on lavapipe: the emitter with the Wind group
  drifts downwind, and the other rises straight.
* **Cost** (RTX 5070 Ti Laptop, 1080p, 2 runs each, `m27_f287`): Water Lab under 8 m/s 3.494 ms of GPU time without
  gust ripples and 3.198 ms with them (still air 3.247 ms; the runs spread by up to 0.6 ms) — the water's pass
  1.314 / 1.286 ms, the gust lookup not measurable. Fire Lab in 4 m/s 1.389 ms (still 1.414). Particles Lab in
  4 m/s 2.491 ms (still 2.545): the particles' update +0.149 ms (0.495 → 0.644), the drawing a little less. Wind Lab
  with 4000 falling leaves +0.016 ms.
* **Visibility boxes.** Particles that drift need a visibility box that reaches as far as they drift. Particles
  Lab first grew its emitters' boxes by the drift in every direction: 86 more emitters counted as visible (draw calls
  289 → 374). Grown downwind only, the boxes cover the drift and nothing more: Particles Lab in 4 m/s 3.208 ms of
  frame against 3.114 still, the particles' update +0.04 ms (`m28_f288`).

## Decision

**KEEP.** Everything is off until a node or material asks for it.
