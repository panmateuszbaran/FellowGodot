# 0112 — Bloom (energy-conserving, of the whole image)

Status: **KEEP as an option**, off by default — a visual feature (milestone 19) · Branch `fellow/geometry` · Settings
`rendering/fellow/post/bloom/enabled` (false), `intensity` (0.05), `scatter` (0.7) · Code: `effects/fellow_bloom.*`,
`shaders/effects/fellow_bloom.glsl`, `effects/tone_mapper.*`, `shaders/effects/tonemap.glsl` (specialization
constant 30), `renderer_scene_render_rd.cpp`.

## Problem

Godot's glow takes only what is above a threshold (`glow_hdr_threshold`, 1 by default), caps it
(`glow_hdr_luminance_cap`, 12) and adds a blur of it: the sun's disk — tens of thousands of times brighter than a lit
wall — glows like a white wall, and the lens dirt that lies on the glow (decision 0111) barely shows (milestone 18).
Unreal's bloom blurs the whole image without threshold (its default) or cap, and its convolution bloom keeps the image's
energy: a bright source spreads its light around it in proportion to its brightness.

## Change

Jimenez's bloom ("Next Generation Post Processing in Call of Duty: Advanced Warfare", SIGGRAPH 2014), without
threshold or cap: from the colour the tone mapper reads (after TAA and motion blur, scene-linear), a chain of six
half-size downsamples (1/2 to 1/64 of the internal size) with the 13-tap filter, then upsamples with a 3 × 3 tent from
the lowest level up, each level mixing its own downsample with the wider level below it by `scatter`. Every level's
weights sum to 1, so the bloom has the image's energy. The tone mapping pass mixes it in before the exposure:
`colour = mix(colour, bloom, intensity)` — a uniform image is unchanged, a bright source's light is moved around it.
The first downsample sanitises its input (NaN to 0; half float's infinity — a sun disk beyond its range — to its
largest value). With the bloom on, the lens dirt (0111) lies on the bloom instead of the glow:
`colour += dirt × bloom × lens_dirt`. Godot's glow is independent (a project turns it off for the bloom alone).

## Development

The first version used Karis's average in the first downsample (each 2 × 2 box weighted by 1 / (1 + its luma), against
fireflies): it removed a sun disk a few pixels wide from the bloom altogether — the box holding the sun weighs 65 000
times less than its neighbours. The input is TAA's (fireflies resolved); the average was dropped.

## Verification

Off (the default) the image is the previous build's bit for bit.

## Results

RTX (`m19_f258`, 1920 × 1080): City Lab +0.14 ms (the passes 0.116 ms, the tone mapping pass +0.016 ms); with the lens
flare and dirt, Sponza +0.13 ms. At a low sun the sun glows over the roofs in proportion to its brightness.
