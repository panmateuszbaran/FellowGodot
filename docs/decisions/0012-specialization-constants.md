# 0012 — FellowEngine shader features as specialization constants

Status: **in test** (`fellow27`: shadow pyramids and PCSS blend band; `fellow29`: every feature) · Branch
`fellow/geometry` · No toggle of its own: each feature's existing toggle now also selects its pipelines · Code:
`forward_clustered/scene_shader_forward_clustered.h` (`ShaderSpecialization::packed_1` bits 6-14),
`render_forward_clustered.cpp` (`_render_scene`, `_update_shader_quality_settings`),
`shaders/forward_clustered/scene_forward_clustered_inc.glsl` (`sc_fellow_*`), `scene_forward_clustered.glsl`,
`shaders/scene_forward_lights_inc.glsl`, `shaders/forward_mobile/scene_forward_mobile_inc.glsl`.

## Problem

`m3_f24_ablation` (one profiled run per variant, opaque pass): the Lights Lab's milestone-2 configuration takes
1.660 ms against stock's 1.359 (+0.30 ms, +22 %), and none of the milestone-2 features active there explains it — the
scene has no sun shadows and no clustered meshes, and removing the positional back-facing skip, the only milestone-2
feature in its light loop, gives 1.638. The milestone-2 configuration is the FellowEngine binary with the milestone-3
toggles off, so its scene shader still contains every milestone-3 code path (cluster depth slices, zero-attenuation
skip, pyramid lookups in four shadow filters) and the PCSS blend band (a second, complete PCSS lookup), each behind a
runtime flag that is false. Code that cannot run still costs registers: the shader is compiled for its largest live
set, and fewer registers per invocation mean more warps in flight, which is what a long light loop waits on.

## Change

Each feature's toggle also sets a bit of the scene shader's specialization constants (`packed_1`, bits 6-14: PCSS
blend band, directional and positional pyramids, zero-attenuation skip, positional and directional back-facing skips,
unblended-cascade skip, cluster depth slices, blended alpha-zero skip). Every runtime test becomes `sc_bit && flag`:
a specialized pipeline whose bit is 0 has no code for the feature; with the bit 1 the per-frame flag still decides (a
pyramid that is not valid this frame, a camera that is not a perspective one). The ubershader (used while specialized
pipelines compile) reads the same bits from push constants. The mobile renderer keeps only the runtime flags for the
code it shares (`sc_fellow_*` there return true).

## Why it is exact

For any configuration the executed code is the same: a feature whose toggle is off never ran (its flag was false), a
feature whose toggle is on runs as before.

## Verification

lavapipe: PBR Lab captures (7 cameras) and the Lights Lab walk (60 frames) bit-identical to the previous build in the
default configuration; each toggle off vs on gives what it gave before (zero-attenuation skip and blended alpha-zero
skip identical; depth slices: the 2 fog pixels by 1/255 that stock's sampled tile ranges miss, decision 0008); the
PCSS blend band still changes the image when on. RTX: `m3_f27_*`, `m3_f29_*` (pending).

## Decision

Pending `m3_f29_profile` / `m3_f29_perf`: whether the milestone-2 configuration and the null test come back to stock
in the Lights Lab, and whether the default configuration gains from dropping the PCSS blend band's code.
