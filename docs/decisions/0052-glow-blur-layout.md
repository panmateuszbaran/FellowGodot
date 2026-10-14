# 0052 — Glow's blur: bank conflicts, and 16 × 16 tiles

Status: **KEEP, 16 × 16 tiles (2) by default from `fellow130`** (exact) · Branch `fellow/geometry` · Setting `rendering/fellow/postfx/glow_blur` (runtime:
0 stock, 1 padded layout, 2 16 × 16 tiles) · Code: `shaders/effects/copy.glsl` (`FELLOW_BLUR_PAD`,
`FELLOW_GLOW_TILE16`), `effects/copy_effects.{h,cpp}` (`COPY_MODE_GAUSSIAN_GLOW*_FELLOW_*`), `fellow_settings.*`.

## Problem

Glow costs 0.15-0.19 ms per frame in every scene (`m9_f140_tc_ab`: Lights Lab 0.163, Bistro interior 0.186, City Lab
0.153 ms), before the tone mapping pass samples its levels. Forward+ builds the levels with a compute blur
(`CopyEffects::gaussian_glow`, `copy.glsl` `MODE_GAUSSIAN_BLUR` + `MODE_GLOW`): for each level, an 8 × 8 workgroup
takes 16 × 16 bilinear samples of the previous level (a 2 × 2 box at half resolution, with a border of 4), blurs them
horizontally with a 9-tap Gaussian into a 16 × 8 shared array stored by column, then vertically. Level 0 is 960 × 540
at 1080p (8 100 workgroups), levels 1-4 a quarter each of the one before.

Two costs in that shape:

* **Bank conflicts.** The horizontal results are stored at `row + 16 × column` (vec4). A quarter warp (eight
  invocations: eight columns, one row) then accesses eight entries 16 vec4 = 64 words apart: all in the same four of
  shared memory's 32 banks. Each of the vertical pass's nine reads, and the two stores before it, is an 8-way conflict:
  about 350 of the ~460 shared-memory wavefronts a warp issues, against ~130 without conflicts.
* **The border.** 16 × 16 samples for 8 × 8 outputs: 4 bilinear samples per output, of which 3 are the border's.

## Change

* **1, padded layout** (`FELLOW_BLUR_PAD`): the horizontal results stored at `row + 17 × column`: the eight entries
  of a quarter warp fall in eight different groups of four banks. The same values, summed in the same order.
* **2, 16 × 16 tiles** (`FELLOW_GLOW_TILE16`): 256 invocations take 24 × 24 samples (2.25 per output), blur 24 rows ×
  16 columns horizontally (stored at `row + 25 × column`, also without conflicts) and 16 × 16 outputs vertically. Every
  sample is taken where stock takes it: stock's thread for the quad of an even destination pixel computes its UV
  (with its clamp) and the odd neighbours add one texel to it; here each sample's UV is computed from its even
  neighbour's with the same expressions. The horizontal and vertical sums take the same operands in the same order,
  and the first level's tone mapping of the samples and its feedback are stock's.

## Verification

lavapipe, 960 × 540 (glow levels from 480 × 270 down to 30 × 17, odd sizes and borders included), each against stock's
blur: Lights Lab and PBR Lab captures, **0 differing pixels** for 1 and for 2.

RTX (`m10_f147_glow_temporal`, 60-frame sequences, each against stock's blur): Bistro exterior and City Lab **bit for
bit** for 1 and 2; Bistro interior 67.1, Lights Lab 91.2, PBR Lab 76.3, Sponza 75.5 dB, their run-to-run floors.

## Measured

In one process (`m10_f147_glow_ab`, `fellow129`, blocks of 30 frames, 4 repeats of the path):

| scene | glow region: stock → padded → 16 × 16 | GPU, padded | GPU, 16 × 16 |
|---|---:|---:|---:|
| City Lab | 0.136 → 0.072 → **0.053 ms** | −3.3 % | **−4.5 %** (p 10^-40) |
| Lights Lab | 0.167 → 0.086 → **0.063** | −3.3 % | **−3.8 %** (p 10^-46) |
| PBR Lab | 0.160 → 0.082 → **0.060** | −3.5 % | **−3.8 %** (p 10^-144) |
| Bistro interior | 0.199 → 0.103 → **0.076** | −1.7 % | **−2.0 %** (p 10^-21) |
| Bistro exterior | 0.175 → 0.094 → **0.072** | −1.3 % | **−1.7 %** (p 10^-108) |
| Sponza | 0.171 → 0.091 → **0.069** | −0.8 % | **−1.1 %** (p 10^-28) |

The bank conflicts were half of glow's cost; the border's samples a quarter of the rest. The tone mapping pass, which
reads the levels, is unchanged (±0.003 ms).

## Decision

**KEEP, 16 × 16 tiles by default** (`postfx/glow_blur` 2 from `fellow130`; 1 and 0 stay selectable). Exact (RTX bit for
bit where the sequences are deterministic, the floor elsewhere; lavapipe identical); glow 0.05-0.08 ms instead of
0.14-0.20 in every scene that uses it.

Stock's `gaussian_blur` (the screen texture's and the canvas back buffer's mipmaps) has the same layout; no benchmark
scene uses it, so it is unchanged here.

## The same in SSR's roughness filter (`postfx/ssr_filter_padded`, `fellow131`): REJECT

`screen_space_reflection_filter.glsl` stores its 16 × 16 samples and its horizontal results as `[column][row]` with 16
rows: every access of both passes is an 8-way bank conflict too. Padded to 17 rows (`FELLOW_PAD`): lavapipe City Lab
and PBR Lab captures with SSR 0 differing pixels; RTX (`m10_f151_ssr_temporal`) Bistro exterior bit for bit, Bistro
interior and Sponza at their floors. In one process (`m10_f151_ssr_ab`): the filter region 0.047-0.050 ms either way,
the frame ±0.1 % (n.s.) in Bistro interior, Bistro exterior and Sponza. The filter works on the reflection's mips of
half resolution and below, a few thousand workgroups at most: its time is the dispatches, not shared memory. Left in,
off.
