# 0039 — Soft shadow filters: how often they run their taps, and a PCSS early-out after the blocker search

Status: **MEASURING** · Branch `fellow/geometry` · Toggle `rendering/fellow/shadows/pcss_filter_early_out` (runtime,
off; needs `shadows/directional_max_mip`) · Diagnostics `debug/scene_shader_experiments` bits 8/16 (path shares as the
colour), 32 (finer pyramid windows), 128 (forces the early-out) · Code: `scene_forward_lights_inc.glsl`
(`sample_directional_soft_shadow`, `fellow_shadow_window_minmax`), specialization bit 16 of `sc_packed_1`.

## Where the opaque pass's time goes (`m8_f110_sizing`, RTX, async compute off, one feature removed at a time)

| scene | removed | opaque pass |
|---|---|---:|
| Bistro exterior | soft directional filtering (hard shadows) | 2.11 → 1.67 ms (−0.44) |
| Bistro interior | positional shadows | 1.51 → 1.05 ms (−0.47) |
| Bistro interior | soft positional filtering (hard shadows) | 1.51 → 1.26 ms (−0.25) |
| PBR Lab | soft directional filtering | 0.92 → 0.69 ms (−0.23) |

The soft shadow filters are the largest part of the opaque pass in the shading-bound scenes. (Bistro interior has no
volumetric fog: the 1.06 ms "Update Volumetric Fog" region of earlier profiles was the async SS effects' overlap.)

## How often the filters run their taps (lavapipe, experiment bits 8/16)

The colour pass writes, per pixel, the shares of its shadow filter calls that (a) took the max-mip early-out (0005),
(b) ran their taps and returned a uniform result (every tap lit or every tap shadowed), (c) returned a partial result.
Captures with TAA, glow, SSR, SSIL and fog off and a linear tone map:

| | early-out | full, uniform | full, partial |
|---|---:|---:|---:|
| directional PCSS: City Lab | 8-13 % | 81-84 % | 2-10 % |
| directional PCSS: Foliage Lab | 3-62 % | 36-78 % | 2-18 % |
| directional PCSS: Blend Lab | 2-47 % | 50-98 % | 0-6 % |
| positional PCF: Lights Lab | 9-33 % | 66-89 % | 0.2-1.7 % |
| positional PCF: PBR Lab | 22-70 % | 15-52 % | 6-51 % |

Most calls that run all their taps get a uniform answer the pyramid test could not prove: its window is the box of the
whole kernel (for PCSS the blocker search radius) rounded up to the pyramid's aligned windows, and on receivers sloped
towards the light the box holds texels closer than the receiver that no tap reaches.

## Finer pyramid windows (bit 32): exact, no gain

Windows one level finer, as many as cover the box (up to 2 × 2 fetches): positional early-outs in Lights Lab 33 → 48 %,
PBR Lab +3 to +13 points; directional +1 to +13 points. RTX identity at the floor (`m8_f112`), but between processes
(`m8_f111`, with bit 128) Bistro interior +2.5 % (p 0.001), the rest within noise: the extra fetches cost what the new
early-outs save. Not pursued.

## PCSS: decide after the blocker search (bit 128 / the toggle)

After the blocker search the filter radius is the penumbra-scaled one, usually much smaller than the search radius,
and often around the receiver's own texels: if every texel within it is farther from the light than the receiver, all
filter taps are lit (1.0); if every texel is closer, all shadowed (0.0), decided from one pyramid window without the
12 filter taps. The filter loop then runs in 10-24 % of calls instead of 48-97 % in Blend Lab, 51-63 % instead of
76-79 % in City Lab, 17-66 % instead of 37-80 % in Foliage Lab.

Exactness: lavapipe identical (City, Foliage, Blend Lab captures; Foliage `foliage_walk` 60 frames). On RTX the first
version, with the 2^-20 margin of the early-outs before the search, differed beyond the floor (`m8_f112`: Foliage Lab
74.5 dB against a floor of 78.3, Bistro exterior 97.8 dB / max |d| 5 where two runs are bit for bit equal): near the
receiver's own texels the hardware comparison, which may compare at the 16-bit atlas's precision, disagrees with a
float comparison a hair away. With a margin of two 16-bit steps (2^-15) (`m8_f113`): Bistro exterior and City Lab bit
for bit equal (99 dB, max |d| 0), Foliage Lab 78.29 / Sponza 75.44 dB at their floors (78.26 / 75.47).

Between processes (`m8_f113_pcss`, 4 runs, start-up bit): Sponza −2.7 % (4/4 pairs, p 0.09), Foliage Lab −1.2 %,
Bistro exterior −0.9 %, City Lab +0.2 % — within noise. In-process measurement with the runtime toggle: pending.
