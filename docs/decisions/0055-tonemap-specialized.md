# 0055 — Tone mapping: the frame's paths as specialization constants

Status: **measuring** (`fellow135`) · Branch `fellow/geometry` · Setting `rendering/fellow/postfx/tonemap_specialized`
(runtime, off until measured) · Code: `shaders/effects/tonemap.glsl` (`sc_fellow`, `FELLOW_FLAG`, ...),
`effects/tone_mapper.cpp` (`ToneMapper::tonemapper`), `fellow_settings.*`.

## Problem

Forward+'s tone mapping pass (`tonemap.glsl`, a full-screen fragment shader) takes 0.11-0.16 ms per frame at 1080p in
every scene (`m10_f148`, without async compute: Bistro interior 0.143, Lights Lab 0.128 ms), about three times what
its memory traffic needs (an RGBA16F read and an RGBA8 write per pixel, the glow levels from small textures). Every
feature it supports is in the one shader, behind tests of the push constants: FXAA (a 12-step edge search), seven glow
levels with bicubic filtering, five glow blend modes (soft light applies the glow, and tone maps it, after the tone
mapping), five tone mappers, brightness/contrast/saturation, colour correction (1D or 3D LUT), debanding, auto
exposure. A shader is compiled for its largest live set, whatever the push constants say, and the registers it
reserves set how many warps an SM keeps in flight to hide the texture fetches. The benchmark scenes use AgX, glow with
bicubic filtering (two or three levels), 8-bit debanding, no FXAA.

Stock already has the other form for the mobile renderer (`tonemap_mobile.glsl`: specialization constants), with its
own arithmetic and resources; Forward+'s shader is not that one.

## Change

The same shader with specialization constants that carry the tests it makes: `sc_fellow` (the specialized pipeline),
the seven flags, the tone mapper (one constant per branch), the glow mode (likewise), each glow level's `> 0.0001` and
the glow map's `> 0.001`. With `sc_fellow` false (stock's pipeline: no constants set) every test reads the push
constants as before; with it true, the C++ side sets each constant from the push constant value the shader would test,
with the same comparison, so every pixel takes the same path, and the driver removes the others. The arithmetic of a
path taken is unchanged. One pipeline per combination in use (one per scene in the benchmark).

## Verification

lavapipe, 960 × 540, captures with the specialized pipeline against stock's (AgX, bicubic glow, 8-bit debanding):
Lights Lab (4), City Lab (3), PBR Lab (7) **0 differing pixels**. Negative control (the specialized pipeline with the
debanding constant cleared): Lights Lab 1 121 213 differing pixels (1 level, every capture): the specialized pipeline
is the one that draws.

RTX (`m11_f156_tm_temporal`, 60-frame sequences, specialized against stock's pipeline in the same build): City Lab
**bit for bit**; Bistro interior 67.13 dB, Lights Lab 91.74 dB, Sponza 75.48 dB: their run-to-run floors (two runs of
one configuration: 67.1, 91.8-100.5, 75.5 dB). `m11_f160` repeats them with each sequence run once before it is
captured (no pipeline compiling while it is drawn).

## Measured

In-process A/B (`m11_f156_tm_ab`, every scene, 4 × 4 blocks per configuration, profiled): the tone mapping pass
**0.115-0.147 → 0.101-0.127 ms (−12 to −14 %)** in six scenes; PBR Lab 0.120 → 0.120 (its tone mapping does the same
work either way). GPU: Bistro exterior −0.5 % (p 2e-10), Bistro interior −0.5 % (p 0.003), Lights Lab −0.6 %
(p 0.008), Foliage Lab −0.4 % (p 0.02), City Lab −1.1 % (p 0.02), Sponza −0.3 % (n.s.), PBR Lab −0.2 % (n.s.); Stress
Lab records no GPU time with this profile (frame −0.1 %, n.s.).
