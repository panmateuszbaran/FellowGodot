# FellowEngine — milestone 113 report (Sponza streamed on the RTX; FSR 3.1's passes)

Status: **IN PROGRESS** (2026-11-14). Branch `fellow/geometry`. The readiness assessment's streaming row listed "Sponza
not yet streamed on the RTX" (milestone 45 measured Streaming Lab only); streaming it found two costs, both fixed
(decisions 0222 and 0223). And FSR 3.1's pass timed pass by pass at 4K.

## 0. Result

PENDING.

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
  shadow casters drawn (the frame's objects 293 against 276). Section 2.
* **The budget** coarsened every texture by 7.5 levels (8.5 at 4K): the streamed textures held 129 MiB of the 256 —
  nearly all at their 128-pixel tails — and kept swapping (332 downgrades). Section 3.
* **Video memory's peak** with streaming and no budget is higher than without it (+193 to +257 MiB at 1080p), though
  the textures' peak is lower: the swaps' new textures beside the old ones until they are freed, and the uploads'
  staging (milestone 45 saw the same in Streaming Lab's flythrough).

## 2. The swaps and the shadow caches (decision 0222)

PENDING.

## 3. The budget (decision 0223)

PENDING.

## 4. FSR 3.1's passes at 4K

PENDING.

## 5. Next

PENDING.
