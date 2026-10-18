# 0037 — The scene shader's register pressure (experiments)

Status: **REJECT** (no change to the shipped shaders; the experiment bits stay as diagnostics) · Branch
`fellow/geometry` · Diagnostics `rendering/fellow/debug/scene_shader_experiments` (start-up bits) · Code:
`scene_forward_lights_inc.glsl` (`SPEC_CONSTANT_LOOP_ANNOTATION`), `scene_forward_clustered.glsl` (`FELLOW_EXP_BIT2`),
`render_forward_clustered.cpp` (the defines).

## Problem

The opaque pass is the largest pass in every benchmark scene (`m6_f97`: Sponza 2.55 ms, Bistro exterior 1.92, Bistro
interior 1.48, Lights Lab 1.35 ms). The driver's statistics (`FELLOW_PIPELINE_STATS`, `m6_f95`) showed its specialized
fragment shaders at 80 registers with 112 bytes of local memory (spills), 24 of 48 warps per SM on the RTX 5070 Ti;
96 registers when the directional shadow pyramid's code is left out. 72 registers would allow 28 warps.

## Experiments (`fellow97`, `m8_f106`)

Start-up bits define names in every scene shader variant, so one build compiles each variant several ways; every
pipeline's statistics are dumped per scene (PBR Lab, Sponza, Bistro interior, Lights Lab, Foliage Lab):

| bit | change | registers (main colour-pass variants) | fragment binary | images |
|---|---|---|---|---|
| — | shipped shaders | 80 (96 without the pyramid's code) | 118-122 KB (Sponza, PBR) | |
| 1 | loops bounded by specialization constants (shadow filter taps) `[[dont_unroll]]` | unchanged, binary identical: the driver does not unroll them anyway | = | at the floor |
| 2 | the same loops `[[unroll]]` | **96**, local memory 96 B | +18 % | — |
| 4 | the directional soft shadow cascades in one loop, not unrolled (one inlined filter instead of four; the same operations) | 80 (and 80 instead of 96 without the pyramid's code), local memory 112 B | **−18 %** | at the floor (lavapipe identical) |

Bistro interior and Lights Lab have no directional soft shadows and stay at 80 registers with 112 bytes of spills in
every variant: the pressure is in the general lighting code (the material's outputs, the light accumulators, the
positional light loops), not in the shadow filters.

Bit 4 between processes (`m8_f107_cascade_loop`, 3 runs): Bistro exterior −1.6 %, City Lab −0.3 %, Foliage Lab +0.7 %,
PBR Lab +1.0 %, Sponza +2.0 % — all within noise. (The session's one-MultiMesh Foliage run is void: its spec put the
override before the scene arguments, which `run_benchmarks.py` reads as part of the override, so it ran the chunks
layout.)

## Decision

**REJECT**: no loop-level change moves the main variants below 80 registers, and the smaller binary of bit 4 gains
nothing measurable. Bringing the pressure down would mean restructuring the lighting code itself (fewer live values
across the light loops), which is a project of its own. The bits stay as diagnostics.

## Later: the indirect light after the light loops (milestone 12, not kept)

The values the light loops keep live include the indirect light computed before them (`ambient_light`,
`indirect_specular_light`, six registers). Moving the indirect light's code (sky, GI, SSAO, probes, SSIL, SSR) after the
loops, with the lights' energy compensation computed first, was tried on lavapipe: the ambient light came out black.
That code takes derivatives (SSAO's `texture()` with an implicit level, the radiance level from `dFdx(ref_vec)`), and
after loops whose invocations leave at different iterations, without maximal reconvergence, those are undefined.
Stock's order (derivatives before the loops) is what makes them defined; computing them early would keep their values
live across the loops instead. Not pursued.
