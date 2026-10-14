# 0045 — Positional lights: where their time goes, and their fields read together

Status: **KEEP, on by default from `fellow114` under autotune** (exact) · Branch `fellow/geometry` · Toggle
`rendering/fellow/lights/fields_early` (runtime, specialization bit 20; autotune tunable `TUNE_LIGHT_FIELDS_EARLY`) ·
Diagnostics `debug/scene_shader_experiments` 65536 (no `light_compute()` for omni/spot lights), 131072 (no omni/spot
shadow lookups), 262144 with 8 (light counts as the colour; with 32 lights within range) · Code:
`scene_forward_lights_inc.glsl` (`light_process_omni`, `light_process_spot`: `FELLOW_LO`, `FELLOW_LS`),
`scene_forward_clustered.glsl` (the counts), `scene_forward_clustered_inc.glsl`, `render_forward_clustered.cpp`,
`fellow_autotune.*`, `fellow_settings.*`.

## Where the time goes

0043: the omni and spot lights are 0.87 of Bistro interior's 1.42 ms opaque pass and 0.89 of Lights Lab's 1.17 ms.
Split further (`fellow111`, `m9_f127_lights_sizing`, async compute off, 2 runs; diagnostics, images change):

| scene | opaque pass | without `light_compute()` | without shadow lookups | without both |
|---|---:|---:|---:|---:|
| Bistro interior | 1.483 ms | −0.016 | **−0.463** | −0.504 |
| Lights Lab | 1.154 | −0.081 | −0.035 | −0.067 |
| PBR Lab | 0.747 | +0.002 | −0.017 | −0.002 |

Per pixel (lavapipe, Lights Lab captures, bits 262144 + 8 (+ 32)): the loop visits 10-16 lights (the warp's union of
the pixels' lights, almost the same as each pixel's own: 9.8-16.0), 3-8 are within range, 2.6-7.6 are shaded.

In Bistro interior the positional shadow lookups are a third of the opaque pass (after 0040's batching). In Lights Lab
neither the shading (BRDF) nor the shadows: most of the lights' 0.8 ms is spent between the loop and the BRDF, on
reading each light's fields and the tests between those reads.

## Change

`light_process_omni()` / `light_process_spot()` read a light's fields where they use them: the position and range,
then (after the range test) the attenuation, the size, the shadow opacity, then (after the shadow) the colour and the
specular amount; the spot's direction and cone in between. Each read waits behind the tests before it. With the
option the fields of the tests and the shading (omni: position, range, attenuation, colour, size, specular amount,
shadow opacity; spot: also direction, cone angle, cone attenuation) are read together at the start of the function
and used from there. The same values in the same expressions.

## Measured (`fellow113`)

* Images: lavapipe identical (Lights Lab, PBR Lab captures); RTX sequences (`m9_f129_fe_temporal`): Bistro exterior bit
  for bit, Bistro interior 67.1 dB, Lights Lab 94.2, PBR Lab 76.3, their floors.
* Registers: Bistro interior's main variants stay at 96, local memory 48 → 64 bytes (`m9_f128_pstats_bi_lf`).
* In one process (`m9_f129_fe_ab`, autotune off, blocks of 30 frames):

| scene | frame | opaque pass |
|---|---:|---:|
| Bistro interior | **−2.1 %** (p < 10^-47) | 1.491 → 1.405 ms |
| Lights Lab | −0.8 % (p 0.002) | 1.274 → 1.249 |
| PBR Lab | −0.3 % (n.s.) | 0.809 → 0.797 |
| Bistro exterior | +0.1 % (n.s.) | 1.680 → 1.688 |
| Sponza | **+0.5 %** (p 0.0001) | 2.034 → 2.066 (transparent pass +0.034) |

## Decision

TBD (`m9_f130`).
