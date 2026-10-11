# 0033 — TAA direct output: the post-processing chain reads the resolve's output

Status: **KEEP** (on by default from `fellow91`) · Branch `fellow/geometry` · Toggle
`rendering/fellow/postfx/taa_direct_output` (runtime) ·
Code: `effects/taa.{h,cpp}` (`process_pingpong`), `renderer_scene_render_rd.cpp`
(`_render_buffers_post_process_and_tonemap`), `render_forward_clustered.cpp` (the TAA call),
`storage_rd/render_scene_buffers_rd.h` (`fellow_post_source`).

## Problem

With the history ping-pong (0002) the TAA resolve writes its output into this frame's history texture, and one copy
remains: history → internal colour texture, which auto exposure, glow and tone mapping then read. A full-resolution
RGBA16F copy (16 MB read, 16 MB written at 1080p) and a barrier on the main queue every frame, in every scene with TAA.

## Change

When the toggle is on, the frame has one view, no depth of field (which modifies its source in place) and no debug draw
mode (which shows or draws into the internal texture), the TAA leaves its output in the history texture and records it
for the frame (`RenderSceneBuffersRD::fellow_post_source`, with the frame number); the post-processing chain uses it as
its source for auto exposure, glow and tone mapping. Where the chain cannot (an upscaler's output, more than one view,
depth of field decided later), it makes the skipped copy itself first. The internal texture keeps this frame's
pre-TAA image, which nothing reads after the TAA (the SSIL/SSR last-frame copy is taken before it; the next frame
overwrites it).

The same texels are read, so the images are identical.

## Verification

lavapipe, City Lab (glow, TAA), off against on: captures 0 differing pixels (90 frames with the direct output), the
60-frame `city_pan` sequence 0 differing pixels.

RTX (`fellow90`, `m6_f97_temporal`, 60-frame sequences, off against on): Bistro exterior (auto exposure) and City Lab
bit-identical (0 differing pixels), Lights Lab at its run-to-run floor (max |d| 1, no pixel above 2).

## Performance (`m6_f97_taa_ab`, in one process, blocks of 30 frames)

| scene | TAA region | frame |
|---|---:|---:|
| Lights Lab | 0.280 → 0.242 ms | −1.1 % (p < 0.001) |
| PBR Lab | 0.297 → 0.257 ms | −1.2 % (p < 0.001) |
| City Lab | 0.278 → 0.243 ms | −1.2 % (p < 0.001) |
| Sponza | 0.343 → 0.305 ms | −0.5 % (p 0.05) |
| Bistro exterior | 0.302 → 0.265 ms | −0.6 % (p < 0.001) |
| Bistro interior | 0.317 → 0.277 ms | −0.8 % (p < 0.001) |

The copy cost 0.034-0.041 ms of every frame; the passes that now read the history texture (auto exposure, glow, tone
mapping) take the same time.

## Decision

**KEEP**, on by default from `fellow91`: the same images, −0.5 to −1.2 % per frame in every scene with TAA.
