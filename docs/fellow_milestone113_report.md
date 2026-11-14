# FellowEngine — milestone 113 report (Sponza streamed on the RTX; FSR 3.1's passes)

Status: **FINAL** (2026-11-14). Branch `fellow/geometry`. The readiness assessment's streaming row listed "Sponza
not yet streamed on the RTX" (milestone 45 measured Streaming Lab only); streaming it found two costs, both fixed
(decisions 0222 and 0223). And FSR 3.1's pass timed pass by pass at 4K.

## 0. Result

* **Sponza streamed on the RTX** (the readiness assessment's open item): with a 256 MiB budget, video memory's peak
  1 626 MiB against 2 597 without streaming (−37 %), the GPU's frame the same (6.17 against 6.16 ms in one job), the
  first frame 20-25 % sooner, the cameras' captures 36-47 dB from streaming off (47.7-48.6 dB with 512 MiB).
* **Two costs found and fixed**: a texture's swap invalidated the directional shadow caches through every material
  using it (decision 0222: 71 → 32 invalidations a run, only the alpha-tested materials' left; the same shadows), and
  the budget's bias dropped nearly every texture to its tail, holding half the budget (decision 0223: the bias from the
  level used, then the budget filled: 255 MiB held of 256 instead of 129, half the swaps, camera 03 43.2 → 46.8 dB).
* **FSR 3.1 timed pass by pass**: at 4K (50 %) the accumulation is 1.40-1.45 ms of the upscaler's 2.31-2.38.
* Video memory beyond the textures and buffers follows streaming's swaps (131 MiB without streaming, up to 515 with
  them): the transfer workers' staging buffers, the likely cause, are milestone 114's.

## 1. Sponza with texture streaming (job f433)

Milestone 45's texture streaming (`rendering/fellow/streaming/textures`, opt-in) keeps of each VRAM-compressed texture
only the mips the cameras need; milestone 49 measured it on the RTX in Streaming Lab, never in Sponza (87 streamed
textures, 1 320 MiB of them at full resolution). FellowBench's perf mode on Sponza (its 42-second flythrough, warm-up
5 s, 20 s measured; the release template of milestone 112, autotune off, three runs each; the GPU's frame, ms):

| | 1920 × 1080 | video memory (peak) | textures (peak) | first frame | 3840 × 2160 | video memory |
|---|---:|---:|---:|---:|---:|---:|
| streaming off | 6.27 | 2 597 MiB | 1 631 MiB | 2 958-3 447 ms | 18.23 | 3 299 MiB |
| on, no budget | 6.62 (+5.6 %) | 2 790-2 854 MiB | 1 504 MiB | 2 192-2 699 ms | 18.35 | 3 429-3 557 MiB |
| on, 256 MiB budget | 6.59 | 1 755-1 851 MiB | 502 MiB | 2 208-2 510 ms | 18.33 | 2 349-2 445 MiB |

* **The first frame** 20-25 % sooner (the textures' tails loaded, not their chains).
* **The frame** 5.6 % longer at 1080p (0.7 % at 4K): the flythrough keeps the streamer swapping (187 loads and 38
  downgrades a run), and each swap made its materials' instances update — logging an event for decision 0004's
  directional shadow cache: 72 invalidations a run against none, the cascades built 260 times against 174, and more
  shadow casters drawn (the objects drawn a frame, shadow passes included, 293 against 276). Section 2.
* **The budget** coarsened every texture by 7.5 levels (8.5 at 4K): the streamed textures held 129 MiB of the 256 —
  nearly all at their 128-pixel tails — and kept swapping (332 downgrades). Section 3.
* **Video memory's peak** with streaming and no budget is higher than without it (+193 to +257 MiB at 1080p), though
  the textures' peak is lower. The likely cause (not measured apart): the streamer's loads create their textures on
  worker threads, and RenderingDevice's transfer workers (one per thread in use, up to the CPU's thread count) each
  keep a staging buffer the next power of two of the largest upload they made — 16 or 32 MiB for a 2K or 4K BC mip
  chain — for good; that memory is the host's, but the driver's allocator counts it. Milestone 45 saw the same in
  Streaming Lab's flythrough. Video memory beyond the textures' and the buffers' (834 MiB in Sponza) follows the swaps:
  131 MiB without streaming, 451-515 MiB with the budget before section 3 (408 loads and 331 downgrades a run), 163 MiB
  after (213 and 99).

## 2. The swaps and the shadow caches (decision 0222)

A swap erases the old data's uniform sets; each material using them rebuilds its set and tells its instances "the
material changed", twice (at the erasure, and when the set is made again). RendererSceneCull updated those instances as
for any change, and an update logs an event that invalidates every directional shadow cascade holding the instance.
For a material whose shadow pass cannot read its textures (the Forward+ scene shader without alpha, discard, depth
prepass alpha or vertex code), the swap's two notifications now carry a mark, and the instances' updates they cause log
no shadow event; any other cause in the same frame does. On lavapipe, Streaming Lab without TAA: the same pictures (0
pixels), the invalidations 17 → 0 a run, the cascades built 49 → 33 times; the regression suite's thirteen scenes with
shadows the same as their goldens. On the RTX (job f434, Sponza's flythrough at 1920 × 1080, streaming on without a
budget, three alternating runs each): the invalidations 71 → 32 a run (those left are the alpha-tested plants' and
curtains', whose shadows do read their textures), the cascades built 262 → 214 times (174 without streaming), the
objects drawn a frame 293 → 280 (276); the GPU's frame 6.37 → 6.30 ms (6.25 without streaming), within the runs'
spread.

## 3. The budget (decision 0223)

Over `texture_budget_mb` every streamed texture is coarsened by one bias: its target was `floor(estimate + bias)`, and a
magnified texture's estimate is below 0 (as many levels as it is magnified), so it kept its full chain until the bias
passed that, while the rest were coarsened by the whole bias — in Sponza 7.5 levels, the textures at their tails with
half the budget unused. The bias now coarsens the level the texture would use, `floor(max(estimate, 0) + bias)`; without
a budget nothing changes. On lavapipe, Streaming Lab with 16 MiB: the views wanting 35.7 and 42.9 MiB now held at one
level coarser (bias 1, 9.8 and 11.5 MiB) instead of six and seven (5.4 and 7.4 MiB); against no budget 402 pixels
changed by 1 instead of 50 345 by up to 66 in one, 6 074 by up to 4 instead of 12 595 by up to 8 in the other.

The first bias that fits can leave up to three quarters of the budget unused (a level is a quarter of a chain's bytes):
on the RTX (job f434) the bias fell from 7.5 to 2, but the streamed textures held 71 MiB of the 256. **The budget is now
filled**: the textures come a level finer, in rounds, while the next level fits — those furthest from what the cameras
need first (target − estimate: a magnified texture before one at its natural level), never finer than wanted. On the
RTX (jobs f434 and f435, Sponza's flythrough at 1920 × 1080, a 256 MiB budget, three runs each):

| | bias | streamed textures held | loads, downgrades a run | video memory (peak) | GPU frame |
|---|---:|---:|---:|---:|---:|
| streaming off | — | — | — | 2 597 MiB | 6.16-6.25 ms |
| before | 7.5 | 129 MiB | 408, 331 | 1 755-1 851 MiB | 6.06-6.12 ms |
| the bias from the level used | 2.0 | 71 MiB | | 1 441-1 473 MiB | 6.16 ms |
| and the budget filled | 2.0 | 255 MiB | 213, 99 | 1 626 MiB | 6.17 ms |

Sponza's cameras 01, 03 and 04 captured with the budget, against streaming off: 30.7 / 43.2 / 38.0 dB before, 36.1 /
46.8 / 46.7 dB filled (47.7 / 48.6 / 47.9 with 512 MiB). The swaps halved; video memory beyond the textures' and the
buffers' peaked at 163 MiB filled against 451-515 MiB before (131 without streaming) — the swaps' uploads (section 1's
staging buffers, measured in milestone 114).

## 4. FSR 3.1's passes at 4K

Milestone 111 measured FSR 3.1's pass at 2.3 ms at 4K and 50 % (one timestamp around the upscaler's eight dispatches).
The FSR 3.1 port now writes a profiler timestamp before each dispatch, named by its pass (`FSR 3.1: prepare inputs`,
`luma pyramid`, `shading change pyramid`, `shading change`, `prepare reactivity`, `luma instability`, `accumulate` or
`accumulate and sharpen`, `RCAS`), so FellowBench's `--profile` (and Godot's visual profiler) splits it. On the RTX
(job f434; 3840 × 2160 at 50 %, 1920 × 1080 rendered, two runs each; the GPU's ms, each pass's interval to the next
timestamp):

| pass | Bistro exterior | Sponza |
|---|---:|---:|
| prepare inputs | 0.16 | 0.16 |
| luma pyramid | 0.04 | 0.04 |
| shading change pyramid | 0.18 | 0.15 |
| shading change | 0.03 | 0.01 |
| prepare reactivity | 0.18 | 0.18 |
| luma instability | 0.11 | 0.09 |
| accumulate and sharpen | **1.45** | **1.40** |
| RCAS | 0.21 | 0.26 |
| FSR 3.1 in all (its setup's 0.01 included) | 2.38 | 2.31 |
| the frame | 7.14 | 9.48 |

The accumulation — the pass at the output's resolution that reconstructs the frame (the jittered input's Lanczos
samples, the history reprojected, the locks) — is 61 % of the upscaler, as much as Bistro's opaque pass; RCAS, the other
pass at 4K, 0.2-0.26 ms; the six passes at the input's resolution 0.6 ms together. The accumulation runs in full
precision on NVIDIA GPUs, as Godot's FSR 2.2 port does (AMD's reason: occupancy and memory traffic); whether half
precision is faster on this GPU is the next measurement (section 5).

## 5. Next

The transfer workers' staging buffers (the likely cause of streaming's higher video memory peak: kept at the largest
upload's power of two, for good); the swaps' churn along a flythrough under a budget (hundreds of loads and downgrades a
run: a hysteresis on the budget's targets); a per-texture shadow dependency (an alpha-tested material is invalidated on
any of its textures' swaps); FSR 3.1's heaviest pass (section 4).
