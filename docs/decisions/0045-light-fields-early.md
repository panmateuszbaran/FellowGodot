# 0045 — Positional lights: where their time goes, and their fields read together

Status: **KEEP, on by default from `fellow115`** (the same arithmetic; used in frames with omni or spot lights in view) ·
Branch `fellow/geometry` · Toggle `rendering/fellow/lights/fields_early` (runtime, specialization bit 20) ·
Diagnostics `debug/scene_shader_experiments` 65536 (no `light_compute()` for omni/spot lights), 131072 (no omni/spot
shadow lookups), 262144 with 8 (light counts as the colour; with 32 lights within range), 524288 (lights stop after
their range test) · Code:
`scene_forward_lights_inc.glsl` (`light_process_omni`, `light_process_spot`: `FELLOW_LO`, `FELLOW_LS`),
`scene_forward_clustered.glsl` (the counts), `scene_forward_clustered_inc.glsl`, `render_forward_clustered.cpp` (the bit
set after the lights are set up), `fellow_settings.*`.

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

## Measured (`fellow113`, ungated)

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

## In scenes without positional lights, and under autotune

The specialization bit changes the scene shader's code even where no omni or spot light is drawn: Sponza (none in view)
+0.5 % above. As an autotune tunable (`fellow114`, `m9_f130_perf`, between processes against `fellow110`) its
evaluations cost more than it saves: Bistro exterior +5.4 % (0/3 pairs faster), Bistro interior −0.5 %, Lights Lab
−2.5 %, all within noise. From `fellow115` it is used in the frames whose lights include an omni or spot light in view
(the count is known once the lights are set up, before the colour passes; the depth pre-pass does not light) and not
autotuned. In one process (`m9_f131_fe_ab`):

| scene | frame | opaque pass |
|---|---:|---:|
| Bistro interior | **−2.1 %** (p < 10^-55) | 1.505 → 1.420 ms |
| Lights Lab | −0.8 % (p 0.04) | 1.289 → 1.262 |
| City Lab | −0.5 % (n.s.) | 0.468 → 0.470 |
| PBR Lab, Bistro exterior, Sponza, Foliage Lab | 0.0 to −0.1 %, +0.1 % (n.s.) | ±0.004 |

Images of `fellow115`'s defaults against `fellow110`'s (`m9_f131_defaults_temporal`): Bistro exterior and Sponza bit
for bit or at the floor, Bistro interior 67.1, Lights Lab 91.5, PBR Lab 76.3 dB (their floors); **City Lab: a few
pixels one level off in some frames** (101.18 dB, max |d| 1), where `fellow95` against `fellow110` was bit for bit
(`m8_f125`). The same signature as 0040's off path: the same values in the same expressions, compiled by the driver with
a different contraction of some multiply-add; lavapipe identical.

## Decision

**KEEP, on by default** (`fellow115`, in frames with omni or spot lights): Bistro interior −2.1 %, Lights Lab −0.8 %,
nothing measurable elsewhere. The arithmetic is stock's; on RTX City Lab differs from the previous defaults by one level
in a few pixels of some frames (the driver's rounding), which this record states rather than calling it bit for bit.

Where the positional lights' time goes in Lights Lab (neither their shading, nor their shadows, nor the reads of their
fields) is measured next (`m9_f132_loop_sizing`).
