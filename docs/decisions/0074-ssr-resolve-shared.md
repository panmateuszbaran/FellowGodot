# 0074 — SSR's resolve: each half-resolution sample read once per workgroup

Status: **REJECT** (left in, off) · Branch `fellow/geometry` · Setting `rendering/fellow/postfx/ssr_resolve_shared` · Code:
`shaders/effects/screen_space_reflection_resolve.glsl`, `effects/ss_effects.cpp`, `fellow_settings.*`.

## Problem

With half-size SSR (Godot's default) the resolve pass brings the half-resolution reflections back to full resolution:
for every pixel four half-resolution samples, each read from four textures (depth, normal-roughness, mip level, and the
reflection colour at that mip level) and prepared (the normal decoded and normalized, the roughness remapped, the
colour's tone mapping inverted), then weighted by depth, normal and roughness against the pixel. Each half-resolution
sample is used by up to four neighbouring pixels, so it is read and prepared up to four times. The pass takes
0.09-0.11 ms in Bistro interior, Bistro exterior and Sponza (`m13_f194`).

## Change

`postfx/ssr_resolve_shared` (a push-constant flag; stock leaves that field unset and unused): the 8 × 8 pixels of a
workgroup use a 6 × 6 tile of half-resolution samples; 36 of the 64 invocations read and prepare one sample each into
shared memory, then every pixel computes its four from the tile. The same reads at the same coordinates (including
stock's integer division of `pixel_pos - 1` towards zero at the left and top edges, and the reads past the right and
bottom edges): 2.25 instead of 16 texture reads per pixel. Only the reads are shared; each pixel computes from the
values read exactly what stock's `get_sample()` computes (`fellow164` shared the prepared samples too — the decoded
normal, the remapped roughness, the colour with its tone mapping inverted — and the RTX rounded them otherwise:
`m15_f218`, Bistro interior max |d| 3 on its warm sequence; lavapipe had been bit for bit).

## Verification

lavapipe, captures with SSR on (`--set=env.ssr_enabled=true`), demote diagnostics, autotune off, the setting off
against on: PBR Lab (7 cameras), City Lab (3), Lights Lab (4) at 480 × 270, PBR Lab at 1366 × 768 (2): **0 differing
pixels**.

RTX, `fellow164` (the prepared samples shared), `m15_f218`: in one process (blocks of 30 frames, autotune off) GPU
−0.5 % in Sponza (−0.033 ms, p 2e-5), −0.4 % in Bistro exterior (−0.017 ms), −0.5 % in Bistro interior (−0.017 ms,
p 1e-8); the warm sequences off against on **not** bit for bit (Bistro interior 93.5 dB, max |d| 3; Sponza max |d| 1).
`fellow165` shares the reads only (`m15_f219`): the warm sequences off against on **bit for bit** (Bistro interior,
Sponza), and in one process GPU −0.3 / −0.1 / −0.1 % (Sponza, Bistro exterior, Bistro interior; p ≥ 0.13): the pass
is not limited by its texture reads; what the first version saved was the preparation done four times (the
normalization, the division that inverts the tone mapping), and sharing it changes the rounding on the RTX.

## Decision

**REJECT.** The exact version saves nothing measurable; the faster one is not exact. Left in, off.
