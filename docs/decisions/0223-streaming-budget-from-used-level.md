# 0223 — The streaming budget's bias from the level a texture uses

Status: **KEEP**, on by default (no change without a budget) · Branch `fellow/geometry` · Setting
`rendering/fellow/streaming/texture_budget_from_used_level` · Code: `servers/rendering/fellow_texture_streaming.cpp`
(`_target_at()`) · Checks: `benchmarks/tools/lavapipe/streaming_check.sh` (its budget pass), the laptop's Sponza (job
f434).

## Problem

Over `texture_budget_mb`, decision 0146 coarsens every streamed texture by the same bias, in steps of half a level,
until the targets fit: a texture's target is `floor(estimate + bias)`. The estimate is the level the cameras need, and
for a magnified texture (more pixels than texels: a wall a step away) it is below 0, by as many levels as it is
magnified. Such a texture keeps its full chain until the bias passes its estimate, while every texture that needs level
0 or coarser is coarsened by the whole bias. On the RTX (job f433) Sponza with a 256 MiB budget: wanted 1 153 MiB,
the bias climbed to 7.5 and the streamed textures held 119 MiB — most at their 128-pixel tails — where coarsening
every texture by one or two levels from the level it uses would have fitted.

## Change

* The bias coarsens the level the texture would use: `floor(max(estimate, 0) + bias)` (its first level is the finest
  it can have). Without a budget (bias 0) every target is the same as before.
* **The budget filled**: a level is a quarter of a chain's bytes, so the first bias that fits can leave up to three
  quarters of the budget unused (Sponza: 71 MiB held of 256 at bias 2). The textures then come a level finer, in
  rounds, while the next level fits: those furthest from what the cameras need first (target − estimate: a magnified
  texture, its estimate below 0, before one at its natural level), the cheaper first among equals, never finer than
  wanted.

## Verification

lavapipe, Streaming Lab (`streaming_check.sh`'s settings, 480 × 270: its views want 1.5-42.9 MiB of streamed textures),
a budget of 16 MiB, against the same without a budget (the pixels changed, the largest change in brackets; two runs
without a budget differ by up to 166 pixels by 1, TAA keeping the frames before the loads landed):

| view (wanted) | before: bias, held, changed | the bias from the level used | and the budget filled |
|---|---|---|---|
| street_mid (35.7 MiB) | 6.0, 5.4 MiB, 50 345 (66) | 1.0, 9.8 MiB, 402 (1) | 1.0, 16.0 MiB, 479 (1) |
| facade_close (42.9 MiB) | 7.0, 7.4 MiB, 12 595 (8) | 1.0, 11.5 MiB, 6 074 (4) | 1.0, 16.0 MiB, 3 374 (4) |
| street_start, aerial (13.5, 1.5 MiB) | 0: 148, 569 | 0: 129, 14 | 0: 145, 2 |

Without a budget nothing changes (the same targets: bias 0). Filling by the cheapest texture first instead (a first
try) filled the budget too but changed facade_close by 6 048 pixels: the levels went to textures out of the view's
detail; by the distance from what the cameras need, to the magnified facade's.

## Results

The RTX, Sponza's flythrough at 1920 × 1080 with a 256 MiB budget (three alternating runs each; job f434: the bias
alone, before the budget was filled; job f435: filled):

| | bias | streamed textures held | video memory (peak) | GPU frame (median) |
|---|---:|---:|---:|---:|
| streaming off | — | — | 2 597 MiB | 6.25 ms |
| before (f434) | 7.5 | 129 MiB | 1 755-1 851 MiB | 6.12 ms |
| the bias from the level used (f434) | 2.0 | 71 MiB | 1 441-1 473 MiB | 6.16 ms |
| and the budget filled (f435) | PENDING | | | |

Sponza's cameras captured with the 256 MiB budget, against streaming off (PSNR): the bias alone 36.0 / 38.9 / 42.0 dB
(cameras 01, 03, 04) against 30.7 / 43.2 / 38.0 before — camera 03 (a narrow view of the carved columns, its
textures magnified) had kept them whole before, at the others' expense. PENDING: filled.
