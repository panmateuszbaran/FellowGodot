# 0130 — Snow tracks

Status: **KEEP** (on with a snow cover; nothing to do without trackers or stamps) · Branch `fellow/geometry` · Nodes:
`FellowSnowTracker3D` (`radius`, `length`, `depth`, `contact_height`, `enabled`); `FellowRain3D.tracks`,
`track_window`, `track_resolution`, `snow_depth`, `track_refill_time`, `stamp()` · Code:
`modules/fellow_fluid/fellow_snow_tracker_3d.{h,cpp}`, `fellow_snow_tracks.glsl`, `fellow_rain_3d.{h,cpp}`,
`servers/rendering/renderer_rd/effects/fellow_weather.{h,cpp}`,
`renderer_rd/shaders/forward_clustered/scene_forward_clustered{,_inc}.glsl` (`fellow_snow_cover`),
`renderer_rd/forward_clustered/render_forward_clustered.cpp` (binding 53).

## Problem

Milestone 28's snow covers the scene, but nothing marks it: a character walks over fresh snow, a cart drives down a
snowy street, and the snow stays untouched behind them. Tracks are what makes snow read as a material one can walk
in.

## Change

**Where the tracks live.** How deep the snow is pressed down, in metres (negative: a rim pushed up beside a track), in
an R16F texture of `track_resolution` cells (2048) over a window of `track_window` (48 m: 2.3 cm cells) around the
camera. The texture is anchored to the world and wraps: texel *i* holds the world cell *c* with *c* ≡ *i* (mod the
size), *c* within the window. Sampled with repeat at `wp.xz / window`, it needs no copy when the window moves: the
cells that enter the window are reset to fresh snow, the rest stay where they are.

**Who presses them.**

* `FellowSnowTracker3D` — a foot, a wheel, a runner. Each frame it presses a capsule of `radius` from where it was to
  where it is, lengthened by `length` along its forward axis: a print when it stands, a groove when it moves. Only
  while it touches the scene's top within `contact_height`: the GPU reads the shelter heightfield at the capsule,
  eroded as the snow's shelter (0128), so a foot in the air presses nothing and wires overhead do not lift the
  ground.
* `FellowRain3D.stamp(from, to, radius, depth)` — once, when asked (a script's footprints, a path laid in advance).
  Up to 256 stamps a frame (the trackers first); the rest wait for the next frames.

**The pass.** A compute pass updates only what changed: the strips the window entered (reset), then the stamps'
bounds; the whole window only when the falling snow has filled the tracks back by a tenth of a millimetre
(`track_refill_time`, 900 s for a track of `snow_depth` in full snowfall — smaller steps would be lost in the half
floats). A stamp presses to its depth with walls over the outer half of its radius, and pushes a rim of a quarter
of its depth up around it.

**What the cover shows.** In `fellow_snow_cover`, the track's depth here and its slope (two more lookups, one cell
apart): the walls turn the normal; the snow at the bottom is packed — 30 % greyer, smoother (roughness 0.45), without
glints; where a track reaches `snow_depth` the ground shows (a cart's ruts down to the cobbles).

FellowBench: `--weather_snow_footprints=<x,z;...|...>` (footprints along polylines, 0.72 m strides, 24 cm apart),
`--weather_snow_ruts=<x,z;...>` (two wheel ruts 1.5 m apart, to the ground), `--weather_snow_depth`; Rain Lab
`--rain=snow --rain_tracks=1` (a cart's wheels driving down the street, footprints).

## Verification

* **The rest unchanged** (lavapipe, against milestone 27's build): Rain Lab without drops (wet, puddles) 5 images and
  Water Lab 5 images with 0 pixels differing.
* **A test scene** (lavapipe): footprints laid with `stamp()` — ovals with sloping walls, packed bottoms and a rim —
  and a tracker rolling 7 m over the ground, its groove (deeper than the snow) down to the ground. Under a wire the
  groove first stopped: the contact test read the raw heightfield, whose top there is the wire; it now reads it eroded
  as the snow's shelter does. Walls over the outer quarter of the radius stepped on the 3 cm cells close up; over the
  outer half they do not, and the window shrank to 48 m (2.3 cm cells).
* **Rain Lab** (lavapipe, `--rain=snow --rain_tracks=1`, walking down the street): the cart's two ruts to the ground
  in the far lane, footprints in the near one.
* **Cost** (RTX 5070 Ti Laptop, 1080p, Rain Lab snowing, 2 runs each, `m30_f292`): 2.258 ms of GPU time without
  tracks, 2.249 ms with the cart's two wheels pressing every frame and 120 footprints, the camera moving along the
  flythrough — not measurable (the pass updates the strips the window enters and the stamps' bounds).
* **Bistro** (RTX, `m30_s10`): a cart's ruts down the snowy street at night, footprints beside them; seen from above,
  the tracks lie where the polylines put them.

**Tried and not kept:** a one-step offset parallax in the cover (the floor of a track seen farther from the camera):
at grazing views it moved a deep rut's samples out of the rut — a thin line instead of a groove — and drew
footprints as outlines (lavapipe).

## Decision

**KEEP.**
