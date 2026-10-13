# 0043 — The positional light loop: what it costs, and dropping the lights that cannot reach a pixel first

Status: **REJECT** (experiment bit 32768 left in; diagnostics bits 4096 and 8192) · Branch `fellow/geometry` · Code:
`scene_forward_clustered.glsl` (`fellow_omni_reach`, `fellow_spot_reach`, `FELLOW_LIGHT_PREFILTER`; the diagnostics in
the omni, spot, decal and reflection probe loops).

## What the loops cost (`fellow107`, `m8_f120_sizing`; diagnostics, images change)

Bits 4096 (no omni and spot lights) and 8192 (no decals and reflection probes) leave the loops' code in the shader and
make them run zero iterations. Opaque pass, async compute off, 2 runs:

| scene | defaults | without omni/spot lights | without decals/probes |
|---|---:|---:|---:|
| Lights Lab (242 lights) | 1.170 ms | 0.280 (**−0.890**) | 1.102 (−0.068) |
| Bistro interior | 1.417 | 0.547 (**−0.870**) | 1.414 (−0.004) |
| PBR Lab | 0.718 | 0.580 (−0.138) | 0.719 (+0.001) |
| Bistro exterior | 1.688 | 1.708 (+0.020) | 1.712 (+0.024) |

In the light-heavy scenes the positional lights are 61-76 % of the opaque pass (their shadows included, 0040).

## Idea

Each cluster word's lights go through the loop one by one: for every light the layer-mask and bake-mode tests, then
`light_process_omni()` / `light_process_spot()` loads the position and range and returns at once when the pixel is out
of range (or outside the cone: the zero-attenuation skip, 0008). Each skipped light costs a chain of dependent loads
and branches. With bit 32768 a pass before the loop drops those lights from the pixel's mask, four at a time with all
their loads issued together (the same tests and expressions); the loop then processes the remaining ones in the same
order.

## Measured (`fellow108`)

* Images: lavapipe identical (Lights Lab, PBR Lab, City Lab captures); RTX sequences at the floors
  (`m8_f121_lp_temporal`: Bistro interior 67.1 dB, Lights Lab 94.3, PBR Lab 76.3).
* Registers: Bistro interior's main variants stay at 96, local memory 48 → 80 bytes.
* Opaque pass (`m8_f121_lp`, 2 runs, async compute off): Bistro interior **+0.105 ms**, PBR Lab +0.064, Lights Lab
  +0.009, Bistro exterior +0.005.

## Decision

**REJECT**: slower. The skipped lights' load chains are not what the loop spends its time on; its cost is the lights
that do reach the pixel (their shading and shadows). The sizing diagnostics stay.
