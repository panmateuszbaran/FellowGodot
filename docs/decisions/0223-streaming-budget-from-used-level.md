# 0223 — The streaming budget's bias from the level a texture uses

Status: **KEEP**, on by default (no change without a budget) · Branch `fellow/geometry` · Setting
`rendering/fellow/streaming/texture_budget_from_used_level` · Code: `servers/rendering/fellow_texture_streaming.cpp`
(`_target_at()`) · Checks: `benchmarks/tools/lavapipe/streaming_check.sh` (its budget pass), the laptop's Sponza (job
f434).

## Problem

Over `texture_budget_mb`, decision 0146 coarsens every streamed texture by the same bias, in steps of half a level,
until the targets fit: a texture's target is `floor(estimate + bias)`. The estimate is the level the cameras need, and
for a magnified texture (more pixels than texels: a wall a step away) it is below 0 — Sponza's flythrough has textures
at −3 to −6. Such a texture keeps its full chain until the bias passes its estimate, while every texture that needs
level 0 or coarser is coarsened by the whole bias. On the RTX (job f433) Sponza with a 256 MiB budget: wanted 1 153 MiB,
the bias climbed to 7.5 and the streamed textures held 119 MiB — most at their 128-pixel tails — where coarsening
every texture by one or two levels from the level it uses would have fitted.

## Change

The bias coarsens the level the texture would use: `floor(max(estimate, 0) + bias)` (its first level is the finest it
can have). Without a budget (bias 0) every target is the same as before. The steps, the order and the rest of the
budget are unchanged.

## Verification

PENDING.

## Results

PENDING.
