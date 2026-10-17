# 0057 — The colour passes without the code of what the frame does not have

Status: **measuring** (`fellow137`) · Branch `fellow/geometry` · Setting
`rendering/fellow/shading/absent_features_specialized` (runtime bits: 1 decals, 2 reflection probes, 4 directional
lights, 8 omni lights, 16 spot lights, 32 positional shadows; 0, off, until measured) · Diagnostics `debug/scene_shader_experiments` 4194304 (discard as demote) · Code:
`forward_clustered/scene_shader_forward_clustered.h` (`ShaderSpecialization::packed_1` bits 27-31, `packed_2` bit 0,
a third specialization constant), `scene_shader_forward_clustered.cpp`, `shaders/scene_forward_lights_inc.glsl`
(`FELLOW_POSITIONAL_SHADOWS_ON`), `storage_rd/light_storage.*` (`fellow_positional_shadowed`),
`shaders/forward_clustered/scene_forward_clustered_inc.glsl` (`sc_fellow_no_*`), `scene_forward_clustered.glsl`,
`render_forward_clustered.cpp` (`render_scene`), `fellow_settings.*`.

## Problem

The opaque colour pass is the largest pass of every scene (0.45-2.06 ms at 1080p). Its shader holds, for every
fragment, the decal loop (atlas lookups of albedo, normal, ORM and emission with mipmaps, blending), the reflection
probe loop (box projection, blending, two cubemap lookups), and the directional lights' block (four PSSM cascades with
blending, PCSS or PCF filters, the shadow pyramid early-outs, transmittance) before the omni, spot and area loops. The
driver compiles the shader for its largest live set: 0048 found the main variants at 96 registers with spills, at the
edge where any added live value makes the driver allocate 114-128 registers and keep fewer warps in flight. Stock
already removes the area light loop when the light cluster holds no area light (`cluster_has_area_light`). The other
three are compiled whether or not the frame has any: six of the eight benchmark scenes have no decal and no
reflection probe (Lights Lab and Stress Lab have both); Lights Lab and Bistro interior, the scenes with the most
positional lights, have no directional light; Sponza and Foliage Lab have no omni or spot light, and City Lab no spot
light, while the omni and spot loops (shadows, PCSS, projectors, the FellowEngine prefetch and tile culling paths) are
the largest code of the shader; City Lab's 48 omni lights have no shadows, and the omni and spot shadow lookups (PCF,
PCSS, the pyramids' early-outs, transmittance) are compiled in all the same.

## Change

Six bits of the colour pipelines' specialization constants (`packed_1` 27-31 and `packed_2` 0, which stock leaves
unused and which becomes a third specialization constant; 0 is stock's shader): no decals (the
decal block is not compiled), no reflection probes (the probe loop is not compiled; the blending of the accumulated
probes with the ambient and sky light after it is unchanged, and with no probe it takes the same values), no
directional lights (the directional block, shadows and lighting, is not compiled), no omni lights, no spot lights (their
blocks are not compiled), no shadowed positional light (the omni and spot shadow lookups, and the transmittance's,
are not compiled). They are set per frame from the light cluster (`get_cluster_count_by_type` of
`ELEMENT_TYPE_DECAL`, `_REFLECTION_PROBE`, `_OMNI_LIGHT`, `_SPOT_LIGHT` equal to 0: every word of every cell is zero,
the loops have no iteration), the frame's directional light count (0: the loops' bound) and the number of omni and
spot lights whose shadow opacity the light buffers hold nonzero (0: every shadow test, `> 0.001`, is false).
The ubershader reads the same bits from its push constants. Every path that runs is unchanged.

## Verification

lavapipe, 960 × 540, captures against stock's pipelines:

* Lights Lab (decals, probes, omni and spot lights, shadowed ones among them, present: only the directional bit
  applies), 7, 31 and 63: **0 differing pixels**. Negative controls: the omni bit set although the cluster holds omni
  lights, 1 443 433 pixels, up to 223 levels; the positional shadow bit set although lights are shadowed, 25 576
  pixels, up to 19 levels.
* Foliage Lab (no positional light, decal or probe; alpha-scissor foliage), 31, with `discard` as demote (below):
  **0**.
* City Lab (directional and unshadowed omni lights: the decal, probe, spot and positional shadow bits apply), 7, 31 and
  63: 2 pixels, 1 level each (110 dB), the same two.
* Blend Lab (blended and alpha-hash materials, volumetric fog), 31, with demote: 1 pixel, 2 levels (102 dB).
* PBR Lab, 7: 105 219 pixels, up to 38 levels, all on the alpha-scissor fence and grass and next to them. Bit 2 (probes)
  alone: 0; bit 1 (decals) alone: the same differences. With the screen-space roughness limiter off: 9 426 pixels, up to
  7 levels. The limiter takes `dFdx(normal)`/`dFdy(normal)` after the alpha scissor's `discard`: in a quad where a
  neighbour was discarded (OpKill), the derivative is undefined, and lavapipe's value depends on the shape of the code
  around it (removing the decal block, with its subgroup operations, changes it). Whether the RTX's driver gives these
  derivatives the same value whatever the code around them is for its sequences to show. With `discard` compiled as a demotion to a
  helper invocation (diagnostics bit 4194304, both sides), where the derivatives are defined: **9 pixels of 1 level** in
  the seven captures (3.6 million pixels), isolated, on the spheres.

The remaining pixels (City Lab 2, PBR Lab 9, Blend Lab 1) are isolated and of the size the compiler's fusion of multiplies
into multiply-adds produces when the code around a value changes (NIR fuses `a * b + c` when the product has a single
use, and removing the decal and probe code removes uses): the likely cause, not checked further. Not bit-exact on
lavapipe, then: the RTX sequences decide whether it stays at the run-to-run floors there.

## Measured

Pending (`FELLOW_PIPELINE_STATS` registers and local memory of the main variants; in-process A/B).
