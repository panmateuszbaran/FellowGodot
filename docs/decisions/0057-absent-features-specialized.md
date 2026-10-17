# 0057 — The colour passes without the code of what the frame does not have

Status: **measuring** (v1 `fellow137`, v2 `fellow138`) · Branch `fellow/geometry` · Setting
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

**v2, `fellow_keep`** (`fellow138`). Leaving a block out changes the code around it in one way that matters: in stock's
pipeline the values the block can change (the decal loop: albedo, normal, ambient occlusion, roughness, metallic,
emission; the probe loop: its three accumulators; the light loops: the diffuse and specular sums, and alpha with shadow
to opacity; the positional shadow branches: the shadow factor and the transmittance depth) leave it through a phi,
across which the compiler cannot fuse the multiply that computed a value with an add that uses it after the block. With
the block left out, the multiply meets the add, and the compiler may fuse them into one multiply-add, rounded once
instead of twice. So where a block is left out, those values now pass through `fellow_keep`, an XOR with a zero read
from the implementation UBO (`fellow_zero`, stock's `volumetric_fog_pad`, written 0): the compiler cannot see through
it, and fuses what stock's compiler fuses. It costs one integer operation per component, with the constant read from
the UBO.

## Verification

lavapipe, 960 × 540, captures against stock's pipelines (v1: without `fellow_keep`):

| scene | bits that apply | bits set | v1 | v2 |
|---|---|---|---|---|
| Lights Lab (decals, probes, omni and spot lights, shadowed ones) | directional | 63 | 0 | **0** |
| City Lab (directional, unshadowed omni lights) | decals, probes, spot, positional shadows | 1 | 2 px, 1 level | **0** |
| City Lab | | 63 (2, 16, 32 alone in v1: 0) | 2 px, 1 level | **0** |
| PBR Lab, `discard` as demote (below) | decals, probes, directional | 7 | 9 px, 1 level | **0** |
| Blend Lab (blended, alpha-hash, fog), demote | | 31 | 1 px, 2 levels | **0** |
| Foliage Lab (alpha scissor), demote | | 31 | 0 | **0** |
| PBR Lab | | 7 | 105 219 px, up to 38 | 104 822 px, up to 37 |

The pixels v1 changed were isolated and of the size one rounding makes; that `fellow_keep` removes them all confirms the
cause (multiply-adds fused where stock's compiler could not fuse them). Negative controls (v1): the omni bit set
although the cluster holds omni lights, 1 443 433 pixels, up to 223 levels; the positional shadow bit set although lights
are shadowed, 25 576 pixels, up to 19 levels.

PBR Lab without demote: all on the alpha-scissor fence and grass and next to them; bit 2 (probes) alone: 0; bit 1
(decals) alone: the same differences; with the screen-space roughness limiter off: 9 426 pixels, up to 7 levels. The
limiter takes `dFdx(normal)`/`dFdy(normal)` after the alpha scissor's `discard`: in a quad where a neighbour was
discarded (OpKill), the derivative is undefined, and lavapipe's value depends on the shape of the code around it
(removing the decal block, with its subgroup operations, changes it); with `discard` compiled as a demotion to a helper
invocation (diagnostics bit 4194304, both sides), where the derivatives are defined, 0. Whether the RTX's driver gives
these derivatives the same value whatever the code around them is for its sequences to show.

RTX, v1 (`fellow137`): `m11_f159_abs_temporal` (63 against 0, 60-frame sequences) Sponza 75.4 dB, Bistro interior 67.1,
Lights Lab 91.5, Foliage Lab 78.2, PBR Lab 76.1, City Lab **94.8** (min 86.0, at most 9 levels); with each sequence run
once before it is captured (`m11_f160`): City Lab **95.6 dB** (at most 3 levels) where two runs of the defaults are bit
for bit, PBR Lab 75.6 against its 76.2-76.3, the others at their floors. v1 is not exact on the RTX either.

RTX, v2 (`fellow138`, `m11_f162`, warm sequences, 63 against the defaults, two runs of each): Sponza 75.4-75.5 dB,
Bistro interior 67.1, Lights Lab 94.4-95.0, Foliage Lab 78.2 (floors: 75.5, 67.1, 95.0, 78.2); City Lab **98.4 dB** (min
92.7, at most 1 level) and Bistro exterior **93.8 dB** (min 86.4, at most 2 levels), both bit for bit between two runs;
PBR Lab 75.6-75.7 against 76.3. Closer than v1 (City Lab 95.6), not exact: what the NVIDIA compiler does differently
when the blocks are left out is not only the fusion at their boundaries. `m11_f164` looks for the bit.

## Measured

**Registers**, v1 (`m11_f159_pstats_*`, `FELLOW_PIPELINE_STATS`, the opaque colour pass's main fragment variants, 0 against
63; what the frame's cluster holds sets the bits, so a scene can use more than one pipeline):

| scene | bits that apply | registers | local memory (bytes) | code size (KB) |
|---|---|---|---|---|
| Lights Lab | directional (decals, probes in some views) | 96 → 96 (shadowed lights in view), 72 (none) | 64 → 64, 48, 16 | 93 → 52-60 |
| Bistro interior | decals, probes, directional, spot | 96 → **80** | 64 → 64 | 92 → 33 |
| City Lab | decals, probes, spot, positional shadows | 96 → **80** | 64 → 32 | 126 → 89 |
| Sponza | decals, probes, omni, spot, positional shadows | 96 → **72-80** | 48 → 48 | 126 → 83 |
| Foliage Lab | likewise | 96 → **72** | 48 → 48 | 126 → 82 |

At 96 registers an SM keeps 21 warps; at 80, 25; at 72, 28. Lights Lab's main variant, with omni, spot and shadowed
lights in view, keeps its 96. The ubershader (168 registers) is unchanged.

**In-process A/B**, v1 (`m11_f159_abs_ab`, `fellow137`, 4 × 4 blocks per configuration, profiled; 7: decals, probes,
directional; 63: all six):

| scene | GPU 7 | GPU 63 | opaque pass (ms) 0 → 63 | transparent pass (ms) 0 → 63 |
|---|---|---|---|---|
| Sponza | −7.1 % | **−9.2 %** | 2.120 → 1.744 (−18 %) | 1.357 → 1.001 (−26 %) |
| Bistro interior | −5.1 % | **−6.7 %** | 1.262 → 1.026 (−19 %) | 0.192 → 0.160 |
| Bistro exterior | −4.3 % | **−6.0 %** | 1.644 → 1.363 (−17 %) | 0.060 → 0.052 |
| City Lab | −2.2 % | **−5.7 %** (frame +0.3 %, n.s.: limited by the CPU) | 0.444 → 0.346 (−22 %) | |
| Foliage Lab | −3.4 % | **−4.4 %** | 0.952 → 0.821 (−14 %) | |
| PBR Lab | −3.5 % | **−3.5 %** | 0.742 → 0.678 (−9 %) | 0.084 → 0.074 |
| Lights Lab | −1.1 % | **−1.0 %** | 0.899 → 0.876 (−3 %) | |

Every p < 0.001. Stress Lab records no GPU time with this profile (frame −0.6 %, n.s.).

v2 (`m11_f162_keep_ab`, `fellow138`, 63 against 0): Sponza −9.2 %, Bistro exterior −6.4 %, Bistro interior −5.9 %,
City Lab −4.7 %, Foliage Lab −4.5 %, PBR Lab −3.4 %, Lights Lab −1.3 % (every p < 1e-6): `fellow_keep` costs up to a
point in City Lab and Bistro interior (their tile-culled variants at 96 registers instead of 80; v3 keeps fewer values).

