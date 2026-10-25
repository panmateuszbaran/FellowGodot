# 0116 — Light shafts (screen-space, from the sun)

Status: **KEEP as an option**, 0 by default — a visual feature (milestone 19) · Branch `fellow/geometry` · Settings
`rendering/fellow/post/light_shafts/intensity` (0: off), `threshold` (0.5, scene-linear), `length` (0.9) · Code:
`effects/fellow_bloom.*` (`MODE_SHAFTS_MASK`, `MODE_SHAFTS_BLUR`), `renderer_scene_render_rd.cpp` (the sun on the
screen), `shaders/effects/tonemap.glsl`.

## Problem

At a low sun the bright sky around it streaks past roofs, trees and clouds (crepuscular rays). Godot's volumetric fog
draws them in a froxel volume at a cost; Unreal's directional light has a cheap screen-space "light shaft bloom".

## Change

Unreal's light shaft bloom (Mitchell, "Volumetric Light Scattering as a Post-Process", GPU Gems 3): the first
directional light projected on the screen; at quarter resolution a mask of the sky's pixels (the depth buffer at the
far plane, four samples per pixel) brighter than the threshold (luma, the excess kept), fading away from the sun; then a
radial blur of it towards the sun — 48 samples over `length` of the way, each 0.97 × the previous one's weight, the
start dithered — added in the tone mapping pass (× `intensity`) before the exposure. Where roofs, chimneys or clouds
break the bright sky, the blur carries their gaps outward as rays. Faded out as the sun leaves the screen (over half a
screen beyond its edge) and as it turns away from the view. It shares the bloom's chain (its quarter level), which runs
when either is on.

## Development

The first threshold (2) left only a disk around the sun brighter than it: the blur made a glow, not rays. At 0.5 the
low sky near the sun counts, and the roofs in front of it break it into rays (lavapipe, City Lab, sun 3° high).

## Verification

At 0 (the default) the image is the previous build's bit for bit.

## Results

RTX (`m19_f258`): City Lab at an 8° sun +0.06 ms (the bloom's chain runs for it; nothing where the sun is off the
screen). At a 3° sun the roofs in front of it break the low sky into rays over the city.
