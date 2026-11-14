# 0224 — FSR 3.1's accumulation in half precision on NVIDIA GPUs

Status: **REJECTED** (measured, code removed) · Branch `fellow/geometry` · Setting (tried)
`rendering/fellow/upscaling/fsr3_accumulate_half_precision_nvidia` · Code: `servers/rendering/renderer_rd/effects/fsr3.cpp`
(`FSR3Effect::FSR3Effect()`) · Checks: the laptop's FSR 3.1 at 4K (job f436).

## Problem

Milestone 113 timed FSR 3.1 pass by pass: at 3840 × 2160 (50 %) on the RTX the accumulation is 1.40-1.45 ms of the
upscaler's 2.31-2.38 — the one pass at the output's resolution that reconstructs the frame (the jittered input's Lanczos
samples, the history reprojected, the locks), as much as Bistro's opaque pass. Every other pass runs in half precision
where the GPU supports it (`fsr3_half_precision`); the accumulation is kept in full precision on NVIDIA GPUs, as AMD's
FidelityFX backends and Godot's FSR 2.2 port do ("reduced occupancy and high VRAM throughput" on the GPUs of their time).
Whether that still holds on a Blackwell GPU had not been measured.

## Change (tried)

`fsr3_accumulate_half_precision_nvidia` (off; read at start-up): the accumulation's half-precision variants built and
chosen on NVIDIA GPUs too (with `fsr3_half_precision`); other GPUs already use them.

## Results

The RTX (job f436, the release template at 85281fe4d9; FSR 3.1 at 50 % at 3840 × 2160, three alternating runs each,
the accumulation's interval in the profiler and the upscaler's in all, ms):

| | Bistro exterior: accumulation | FSR 3.1 | Sponza: accumulation | FSR 3.1 |
|---|---:|---:|---:|---:|
| full precision (as before) | 1.451, 1.452, 1.563 | 2.37-2.47 | 1.379, 1.391, 1.394 | 2.28-2.36 |
| half precision | 1.457, 1.451, 1.454 | 2.37-2.38 | 1.429, 1.426, 1.444 | 2.31-2.39 |

No faster in Bistro, 3.5 % slower in Sponza; the GPU's frame within the runs' spread (7.14-7.30 against 7.18-7.21 ms in
Bistro). The pictures (the scenes' cameras captured at 4K after 96 still frames): half precision against full 42.4-49.6
dB, up to 61 levels apart on 50-86 % of the pixels; two captures in full precision 69.5 dB or more (Bistro's
identical, Sponza's 0-1.5 % of the pixels apart).

## Decision

Rejected: the accumulation stays in full precision on NVIDIA GPUs (AMD's reasoning holds on this GPU: halving the arithmetic's
width did not help), and the option is removed. The readiness assessment's "half precision gains nothing on
NVIDIA" (milestone 49, the other passes) now covers the accumulation too.

## Rejected / not done

* **A cheaper accumulation** (fewer taps, another of the SDK's kernel approximations): not tried — each changes the
  picture, and the pass is likely bound by its memory traffic at the output's resolution, which they keep.
