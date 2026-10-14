# 0047 — The light loop's fields from a compact copy (storage or uniform buffer)

Status: **under test** (`fellow118`, job `m9_f135`) · Branch `fellow/geometry` · Toggle
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

## Measured

Pending (`m9_f135`: pipeline statistics, images against 0, in-process A/B of 0, 1, 2, 2 with prefetch 2 and 4).
