# 0047 — The light loop's fields from a compact copy (storage or uniform buffer)

Status: **REJECT** (`fellow118`, `m9_f135`: exact; the storage buffer 1.4-2.4 % slower, the uniform buffer neutral in the
frame and slower in the opaque pass; off, code kept) · Branch `fellow/geometry` · Toggle
`rendering/fellow/lights/hot_fields_buffer` (runtime; 0 off, 1 storage buffer, 2 uniform buffer; specialization bits 24,
25; with `lights/fields_early`) · Code: `storage_rd/light_storage.*` (`FellowLightHot`, `_fellow_update_hot_buffers`),
`scene_forward_clustered_inc.glsl` (`FellowLightHot`, bindings 21-24, `FELLOW_OMNI_HOT`, `FELLOW_SPOT_HOT`),
`scene_forward_clustered.glsl` (the omni and spot loops), `render_forward_clustered.cpp` (set 0, the uniform buffers'
array size), `fellow_settings.*`.

## Problem

The light loop reads, for every omni or spot light it visits, its layer mask, bake mode, position, inverse radius and
(0045) the fields of its shading: 13 values for an omni light, 18 for a spot light. They live in the 240-byte
`LightData` record, at offsets 0-87 and 196-207: two or three 128-byte cache lines per light, in a storage buffer read
through the L1 cache that the shadow atlas, the material textures and the cluster share.

The loop's light index is the same for the whole warp (0046), so these are uniform loads, which a uniform buffer serves
from the constant cache.

## Change

`LightStorage` copies those fields of every omni and spot light into an 80-byte record (`FellowLightHot`: position,
inverse radius, colour, attenuation, direction, cone angle, size, specular amount, shadow opacity, cone attenuation,
layer mask, bake mode) after it fills `LightData`, and uploads them to a storage buffer (1) or a uniform buffer (2) of
`max_clustered_elements` records (512 by default: 40 KB; if they do not fit the device's uniform buffer limit, option 2
falls back to 0). With the option the light loop (and 0046's prefetch) reads them from there; the light's function
reads the rest (shadow data) from `LightData` as before.

### Why it is exact

`memcpy` of the same 32-bit values; the shader reads the same bits from another buffer.

## Verification (lavapipe)

Lights Lab captures with 1, 2, 2 with 0046's 4, PBR Lab with 2 and 4: **0 differing pixels** against 0. Sabotage check:
the copy's green scaled by 1/4 changes 1.4 million pixels with 1 and with 2 (each buffer is the one read).

## Measured (`fellow118`, `m9_f135`)

* Images (`m9_f135_temporal`, 1 and 2 against 0): Bistro interior 67.1 dB, Lights Lab 91.5-97.3 dB (their floors),
  City Lab bit for bit.
* Registers: the main colour variants go from 96 (with 48-64 bytes of local memory) to **114-117 registers** with
  either buffer (no local memory), to 124-127 with 0046's prefetch on top.
* In one process (`m9_f135_hot_ab`):

| scene | 1 (storage) | 2 (uniform) | 2 + prefetch 2 | 2 + prefetch 4 | opaque pass 0 → 1 / 2 |
|---|---:|---:|---:|---:|---:|
| Bistro interior | +1.4 % (p 10^-16) | 0.0 % | +2.0 % | +2.9 % | 1.290 → 1.371 / 1.351 ms |
| Lights Lab | +1.4 % (p 10^-12) | −0.3 % (n.s.) | +2.7 % | +4.7 % | 1.035 → 1.072 / 1.068 |
| PBR Lab | +2.4 % (p 10^-30) | +0.6 % (p 0.01) | +2.7 % | +2.6 % | 0.747 → 0.801 / 0.784 |
| City Lab | +1.4 % (n.s.) | +0.6 % (n.s.) | −0.6 % (n.s.) | −0.1 % (n.s.) | 0.467 → 0.501 / 0.494 |
| Bistro exterior | 0.0 % | 0.0 % | 0.0 % | 0.0 % | 1.640 → 1.639 / 1.642 |

The uniform buffer reads are cheaper than the storage buffer's (2 against 1: −0.02 ms of opaque pass in Bistro
interior) but the opaque pass is still 0.03-0.06 ms slower than with `LightData`, and the frame no faster.

## Decision

**REJECT** (off; code kept). The compact copy changes how the driver allocates the shader's registers (decision
0048), and that costs more than the shorter loads save.
