# 0074 — SSR's resolve: each half-resolution sample read once per workgroup

Status: **MEASURING** (off) · Branch `fellow/geometry` · Setting `rendering/fellow/postfx/ssr_resolve_shared` · Code:
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
shared memory, then every pixel weights its four from the tile. The same reads at the same coordinates (including
stock's integer division of `pixel_pos - 1` towards zero at the left and top edges, and the reads past the right and
bottom edges), the same expressions: 2.25 instead of 16 texture reads per pixel.

## Verification

lavapipe, captures with SSR on (`--set=env.ssr_enabled=true`), demote diagnostics, autotune off, the setting off
against on: PBR Lab (7 cameras), City Lab (3), Lights Lab (4) at 480 × 270, PBR Lab at 1366 × 768 (2): **0 differing
pixels**.

RTX: (to measure — the pass in one process, and the warm sequences of Bistro interior and Sponza bit for bit, on
against off)

## Decision

(pending)
