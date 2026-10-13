# 0042 — Directional PCSS after 0040: two cascades fetched together, a blocker search shared by pixel quads

Status: **REJECT** both (left in: experiment bit 2048; option `rendering/fellow/shadows/pcss_quad_search`, off) ·
Branch `fellow/geometry` · Code: `scene_forward_lights_inc.glsl` (`sample_directional_soft_shadow_pair`,
`sample_directional_soft_shadow`'s quad path), `scene_forward_clustered.glsl` (the pair's call site; the cascade index
passed to the filter), `scene_forward_clustered_inc.glsl` (`sc_fellow_pcss_quad_search`, specialization bit 19,
`GL_KHR_shader_subgroup_quad`), `render_forward_clustered.cpp` (the option needs subgroup quad and ballot operations
in fragment shaders), diagnostics bit 16384 (the quad path's depth guard).

## What is left of directional PCSS after 0040

Diagnostic bit 64 (PCSS returns lit at once, its code kept) against the defaults with batched taps (`fellow106`,
`m8_f119_pcss`, async compute off): the opaque pass loses 0.51 ms in Bistro exterior (1.91 → 1.40 ms), 0.21 in Sponza,
0.22 in PBR Lab, 0.09 in Foliage Lab. With blend splits (Bistro, PBR Lab, Sponza) stock blends every pixel of a cascade
with the next one over the whole split, so most pixels run the search and the filter twice: about 50 fetches per pixel
with the pyramid's.

## Two cascades through one PCSS (experiment bit 2048, exact)

`sample_directional_soft_shadow_pair()` runs a blended pixel's two cascades together: the pyramid tests of both, then
both blocker searches with their batches of taps fetched together, both penumbrae, both filters; per cascade the same
arithmetic as the single function. Images: lavapipe identical (PBR Lab, Stress Lab captures; also with batched taps
off); RTX (`m8_f119_pair_temporal`) Bistro exterior bit for bit, PBR Lab and Sponza at their floors. Registers 96 with
64-96 bytes of local memory (48 without it). Opaque pass (`m8_f119_pcss`, 2 runs): Bistro exterior **+0.135 ms**, Sponza +0.212, PBR
Lab +0.098, Foliage Lab +0.031 (no blend splits there): slower everywhere. **REJECT.**

## A blocker search shared by the 2 × 2 pixel quad (`shadows/pcss_quad_search`, visually lossless candidate)

Where all four pixels of a quad reach the search in the same cascade (ballot and quad swaps of the cascade index),
each takes a quarter of the kernel's search taps (i % 4 = its place in the quad), around its own coordinate with its
own rotation, compares them with its own receiver depth, and the quad adds the four partial sums (quad swaps). The
filter stays per pixel. A quarter of the search fetches.

Images against off (`fellow108`, `m8_f121_quad_temporal`, RTX, 60-frame sequences):

| sequence | PSNR mean / min | max \|d\| | pixels > 2 | with the depth guard (16384) |
|---|---:|---:|---:|---|
| `bistro_ext_path` | 57.6 / 56.6 dB | 66 | 0.34 % | 77.5 / 74.7, 0.001 % |
| `city_pan` | 58.3 / 56.0 | 169 | 0.09 % | 69.2 / 58.8, max \|d\| 169 |
| `foliage_walk` | **50.8 / 48.9** | 123 | **0.90 %** | 53.0 / 50.3, 0.50 % |
| `specular_orbit` | 69.2 / 65.6 | 72 | 0.01 % | 73.8 / 71.2 |
| `sponza_pan` | 68.7 / 63.1 | 62 | 0.04 % | 71.4 / 65.3 |

(lavapipe captures: City Lab 51.3-56.3 dB, Foliage Lab 50.7-72.9, PBR Lab 54.0-73.7, Blend Lab 75.5-84.4.) The
differences follow the penumbrae and contact shadows: the penumbra comes from four neighbours' samples, and a pixel
whose own taps find no blocker runs the filter when a neighbour's do. The guard (receivers of the quad within 1/8192 of
the light's depth range) removes most of it outside Foliage Lab, but also most of the sharing.

Time (`m8_f121_quad_ab`, one process, without the guard): Bistro exterior −0.8 %, PBR Lab −2.0 %, Foliage Lab −0.9 %,
Sponza −0.2 % (n.s.), City Lab +0.1 % (n.s.); opaque pass −0.01 to −0.05 ms. A quarter of the search's fetches is
worth a few hundredths of a millisecond: with batched taps the search is no longer what directional PCSS waits for,
and the remaining cost is the texture work itself (filter, both cascades).

**REJECT**: below the visually lossless bar in Foliage Lab (0.9 % of pixels off by more than two levels) for at most
2 %. The option stays, off.
