# 0070 — Textures cleared before their first use

Status: **KEEP** (on from `fellow160`) · Branch `fellow/geometry` · Setting `rendering/fellow/frame/clear_new_textures`
(on) · Code: `rendering_device.cpp` (`texture_create`, `_texture_check_pending_clear`), `fellow_settings.*`.

## Problem

A texture created without data holds whatever its memory held. Where a pass reads texels no pass wrote, the image
depends on that memory: zero for a fresh allocation on most drivers, the content of a freed resource where the
allocator reuses a block. lavapipe with poisoned allocations (`LVP_POISON_MEMORY=1`, milestone 13) found four such
reads in the labs, all in stock's code:

* SSAO's last column at widths such as 1920 (fixed at the source by 0067);
* SSIL's, the same bound (its output is cleared when created);
* the reflection probes' octahedral atlas (Lights Lab at 1366 × 768: 11 pixels of 1 level; 258 pixels of up to 6
  levels through FellowEngine's TAA);
* the positional shadow atlas outside the texels a shadow wrote (PBR Lab, one pixel of 1 level at a shadow's tip).

Stock itself clears such textures where the driver requires it: on Direct3D 12 (`API_TRAIT_TEXTURE_OUTPUTS_REQUIRE_CLEARS`)
every texture a pass can write (storage, colour or depth attachment) created without data is cleared at its first
use — in a framebuffer, a uniform set, a copy or an update.

## Change

`frame/clear_new_textures`: the same on Vulkan. Such a texture is marked for stock's clear at first use; Vulkan's clear
needs the transfer destination usage, which is added for the driver only (as stock's forced usage bits: the texture's
usage as the engine sees it does not change). Transient attachments are left out. FellowEngine's parallel recording
already defers a clear met inside a forked draw list to the join (written for Direct3D 12). Stats
`frame_textures_cleared`, `frame_textures_cleared_bytes`.

A read of texels no pass wrote then reads zero, as with a fresh allocation; the images do not change where every
read texel was written.

## Verification

lavapipe, captures with the demote diagnostics and autotune off, `LVP_POISON_MEMORY=1` against zeroed allocations,
the setting on:

| lab | resolution, settle | cameras | differing pixels | without the setting |
|---|---|---|---|---|
| PBR Lab | 480 × 270, 60 frames | 7 | **0** | |
| Lights Lab | 480 × 270, 60 | 4 | **0** | |
| City Lab | 480 × 270, 60 | 3 | **0** | |
| Lights Lab | 1366 × 768, 200 | 4 | **0** | 743 summed levels, max 9 (the reflection atlas) |
| PBR Lab | 1366 × 768, 200 | 7 | **0** | emissive: 16 summed levels, max 1 (the positional shadow atlas) |
| PBR Lab | 1920 × 1080, 60 | 7 | **0** | |

With the setting no capture depends on what memory held: no read of texels no pass wrote is left in these labs
(0067 had removed SSAO's at its source). The setting on against off without poison (lavapipe's allocations are zero):
PBR Lab, Lights Lab, City Lab **0 differing pixels** — where every read texel was written, nothing changes.

Cleared: 4 textures (1.1 MiB) while a scene is built, 22-29 textures (63-167 MiB) by the end of the first frame (the
render buffers, the shadow atlases); the reflection atlas when a probe first renders.

In perf runs with autotune on (lavapipe, 480 × 270, the setting on): no texture cleared during the measurement in
Lights Lab, PBR Lab and City Lab (39, 32 and 29 textures over the whole process: 459, 175 and 132 MiB, Lights Lab's
including the reflection atlas).

RTX (`fellow159`, `m13_f200`):

* Two runs against each other (autotune off, `m13_f180`'s warm sequences): Bistro interior, PBR Lab, Sponza bit for
  bit with the setting off and on, and on against off; Lights Lab max |d| 3 off, 1 on (a few pixels: their source is
  not memory no pass wrote, see `m14_f201`, `m14_f202`).
* Frame time, on against off between processes (`m13_f200_clear_perf`, three runs each): within the runs' spread in
  every scene (Bistro exterior −0.2 %, Bistro interior −0.9 %, City Lab −1.6 %, Foliage Lab +0.7 %, Lights Lab −2.4 %,
  PBR Lab +4.1 % (noisy, 1 of 3 pairs slower), Sponza −1.2 %, Stress Lab −1.9 %; p ≥ 0.09).
* Load to the first frame: within the runs' spread (Bistro exterior 2791 → 2653 ms, Sponza 2701 → 2728, Lights Lab
  107 → 88, PBR Lab 412 → 438).

## Decision

**KEEP, on.** No capture depends on what memory held any more, at no measurable cost; the images are those of a
fresh allocation, which is what stock gives on most drivers (and always on Direct3D 12).
