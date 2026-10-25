# 0111 — Lens dirt (Unreal's bloom dirt mask)

Status: **KEEP as an option**, off by default — a visual feature (milestone 18) · Branch `fellow/geometry` · Setting
`rendering/fellow/post/bloom/lens_dirt` (0 = off) · Code: `shaders/effects/tonemap.glsl` (specialization constant
29), `effects/tone_mapper.*`, `renderer_scene_render_rd.cpp`; bench `--dirt_map=1`.

## Problem

Unreal's bloom has a dirt mask: a texture of specks and smudges on the lens that lights up where the bloom is (a bright
light in view), added on top of the bloom. Godot's environment has a glow map, but it multiplies the glow
(`mix(glow, glow * map, strength)`): it can only take light away, so dirt never appears over a dark part of the image
around a bright source.

## Change

The environment's glow map is the dirt texture. Where the setting is above 0, the environment has a glow map and glow
is on (any blend mode but soft light), the tone mapping pass adds `glow × map × lens_dirt` to the glow, in
place of stock's glow map mixing — Unreal's `BloomDirtMaskIntensity`. Without a glow map nothing changes. A
specialization constant: at 0 the pipelines are the ones without it.

The bench's `--dirt_map=1` makes a deterministic dirt texture (a few hundred soft specks, a few large smudges) and sets
it as the glow map at strength 0, which leaves stock's glow unchanged.

## Verification

At 0 (the default) the image is the previous build's bit for bit, with or without the bench's dirt map (lavapipe, City
Lab, 0 differing pixels).

## Results

City Lab facing a sun 8° high (lavapipe, procedural sky, intensity 4): the dirt appears only around the sun
(8 000 pixels, at most +4 levels) — the procedural sky's sun is barely above the glow's threshold. With the atmosphere's
sky and its sun in clear view (lavapipe, intensity 2) still only 1 700 pixels, at most +27 levels, also with the glow's
luminance cap raised from 12 to 256: Godot's glow of a disk 7 pixels wide is weak (a threshold, a cap, and the levels'
spread), where Unreal's bloom convolves the whole image without either. The dirt is as strong as the glow it multiplies:
an Unreal-like bloom under it is still to be done (milestone 18's report, Open).
