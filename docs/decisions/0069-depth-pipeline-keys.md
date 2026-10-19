# 0069 — Depth passes' pipeline keys without the colour passes' FellowEngine bits

Status: **MEASURING** · Branch `fellow/geometry` · Setting `rendering/fellow/frame/depth_pipeline_keys` (on) · Code:
`render_forward_clustered.cpp` (`_render_list_template`, `FELLOW_SPEC1_DEPTH_BITS`), `fellow_settings.*`.

## Problem

A pipeline's key holds the draw's specialization constants. The depth pre-pass takes the frame's base specialization,
so its keys hold FellowEngine's bits for the colour passes too: the shadow pyramids' early-outs (0005), the light
loop's options (0045-0049), the backfacing skips, the cluster depth slices, and so on. The depth variants of the scene
shader (`MODE_RENDER_DEPTH`: the pre-pass, shadows, the material and SDF passes) compile none of the code those bits
select: every use of a `sc_fellow_*` constant but the lists' format (`fellow_persistent_instances`, 0022) is inside
`#if !defined(MODE_RENDER_DEPTH)`. Two keys that differ only in them are two compilations of the same code.

They differ whenever autotune evaluates a shadow pyramid (0005's bits are switched in blocks of frames): lavapipe,
Stress Lab with a shortened autotune schedule and the missing pipelines logged (`FELLOW_PIPELINE_LOG`): the evaluation
of the positional pyramid compiled depth pre-pass pipelines (`version 2`, `0x6fe80` against `0x6ff80`: bit 8) and depth
pass pipelines (`version 0`) in its first off-block, drawn with the ubershader meanwhile — after 0068 had prepared the
colour passes' pipelines.

## Change

`frame/depth_pipeline_keys`: in every pass but the colour passes, a draw's specialization keeps of `packed_1` stock's
six bits (MultiMesh format, legacy fog blending, area lights) and `fellow_persistent_instances`, and clears
`fellow_no_positional_shadows`. The depth variants read nothing else of them, so the pipelines are the same code with
the same constants; the keys no longer change with autotune's evaluations or the frame's absent features (0057).

## Verification

lavapipe, captures with the demote diagnostics, 60 frames of settle, the setting on against off: PBR Lab (7 cameras),
Lights Lab (4), City Lab (3), Foliage Lab (3): **0 differing pixels**.

Stress Lab, perf mode (100-frame passes, autotune on with a shortened schedule, 0068 on), the setting on against off,
the whole process: draws with the ubershader **427** against 783, pipelines compiled 289 against 297 (startup to the
first frame 224 against 223); the two evaluations prepared in the same 12-14 frames.

RTX: (to measure — pipelines compiled per scene and the draws on the ubershader in the finals' runs)

## Decision

(pending)
