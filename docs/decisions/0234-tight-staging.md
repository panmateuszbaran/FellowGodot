# 0234 — A compressed texture's upload staged as its rows of blocks

Status: **KEEP**, on by default (the same copies and pictures; host memory) · Branch `fellow/geometry` · Setting
`rendering/fellow/frame/transfer_tight_staging` (read when the device is created) · Code:
`servers/rendering/rendering_device.cpp` (`_fellow_staging_rows()`, `_texture_initialize()`, `texture_update()`) ·
Checks: lavapipe's Streaming Lab, the regression suite, the laptop's Sponza streamed.

## Problem

Decision 0233 left the transfer workers' staging buffers unreleased: 128 MiB in each of four workers after Sponza's
streamed flythrough, 512 MiB of host memory the driver counts as video memory, for textures whose largest chain is 21
MiB (a 4K BC7 texture with its mips). RenderingDevice reserves an upload's staging space mip by mip as `pitch * height`,
the pitch being a row of blocks' bytes and the height the mip's rows of pixels: for a block-compressed format it
reserves a row of blocks for every row of pixels — 4 times the data with 4x4 blocks (BC2-BC7, ETC2 RGBA, ASTC 4x4), 2
times with BC1, BC4 and ASTC 8x8 (shifted back once by the format's half-byte shift) — and copies only a quarter (a
half) of it. A 21 MiB chain asks its worker for 85 MiB, which grows to 128 MiB. `texture_update()` reserves `pitch *
height` in the frame's staging ring without the shift: 4 times the data with 4x4 blocks and with BC1, 8 times with ASTC
8x8.

## Change

`rendering/fellow/frame/transfer_tight_staging`: the staging reserved for a mip (and an updated region) is its rows of
blocks (`pitch * height / block height`): what is copied. The copies, their offsets' alignment and their row pitches are
unchanged: the same pictures.

## Verification

A test of 30 textures created with data and then updated (BC7, BC1, BC4, BC5, RGBA8 and R8; 4×4 to 4096×2048, every
mip), read back byte for byte on lavapipe: the same with the option off and on. After the 4096×2048 BC7 texture alone
the transfer worker's staging was 64 MiB without it (43 MiB asked for 11 MiB of data) and 16 MiB with it. The regression
suite (36 scenes) with it on: the same as its goldens. Streaming Lab on lavapipe staged 32 MiB either way (its uploads
fit the first buffer), and its captures differ between two runs of the same build (streaming's timing), so they are no
check of this.

The first laptop job (f455) was aborted: the device is made before the RenderingServer registers FellowEngine's
settings, so the setting read with `GLOBAL_GET` (and `Fellow::enabled()`) when the device was created was never set. It
is read with defaults from the project settings instead.

The RTX, job f456 (the release template at 1321e55909, eight alternating runs each; Sponza streamed at 1920 × 1080):

| Sponza streamed | staging at the end | the driver's memory at the end | GPU frame | p99.9 | longest frame |
|---|---:|---:|---:|---:|---:|
| stock's staging | 512 MiB | 2 853 MiB | 5.96-6.71 ms | 10.3-12.4 ms | 13.1-22.9 ms |
| tight staging | 160-192 MiB | 2 501-2 534 MiB | 5.99-6.63 ms | 10.6-12.5 ms | 13.1-21.4 ms |

The frames over 16.7 ms (two and three) all waited for the GPU's fence (19-22 ms), in both. No allocation of device
memory in any run either way. Bistro exterior (its textures at its load, three runs each): staging 32 MiB either way,
the GPU's frame 4.01-4.04 against 4.05-4.09 ms, the load 2.9-3.1 s either way.

## Decision

Keep, on by default: Sponza's streamed flythrough ends with 320-352 MiB less host memory held by the transfer workers
(512 → 160-192 MiB), the frames and pictures the same — what decision 0225's trim was for, without freeing anything
while it plays (decision 0233). Not changed: the download path (`texture_get_data_async()`), which reserves the same
way.
