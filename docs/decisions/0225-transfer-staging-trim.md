# 0225 — The transfer workers' staging buffers trimmed

Status: **KEEP**, off by default since decision 0233 (on from milestone 114 to 116: 5 s, the largest kept; host memory; a freed block of the memory allocator holds a frame up ~30 ms) · Branch `fellow/geometry` · Setting `rendering/fellow/frame/transfer_staging_trim` · Code:
`servers/rendering/rendering_device.{h,cpp}` (`_fellow_trim_transfer_workers()`, `fellow_transfer_staging_bytes()`,
`fellow_device_local_memory_used()`), `drivers/vulkan/rendering_device_driver_vulkan.cpp`, `fellow_settings.*` ·
Checks: lavapipe's Streaming Lab, the laptop's Sponza and Bistro (jobs f436, f437).

## Problem

Milestone 113 found Sponza's video memory, as the driver counts it (every heap: `RENDERING_INFO_VIDEO_MEM_USED`), higher
with texture streaming than its textures and buffers explain, by as much as the streamer swaps: beyond the textures'
and buffers' 131 MiB without streaming, 451-515 MiB with a 256 MiB budget before decision 0223 (408 loads and 331
downgrades a run), 163 MiB after it (213 and 99). RenderingDevice uploads through transfer workers, one per thread
uploading at once (up to the CPU's thread count); each keeps a staging buffer in host memory the next power of two of
the largest upload it made — a streamed 4K BC7 chain is 21 MiB, its staging 32 MiB — for good. Streamed loads run on
worker threads, so workers multiply and each grows to the largest texture it ever loaded.

## Change

* **Measured**: FellowEngine's statistics report `memory_device_local_bytes` (the heaps local to the device),
  `memory_total_bytes` (every heap, as the video memory statistic), `transfer_staging_bytes` and `transfer_workers`;
  FellowBench samples them every 30 frames (their peaks) and at the end of a run.
* `transfer_staging_trim` (on): at the start of a frame, a transfer worker unused for `transfer_staging_trim_idle_s`
  (5 s), holding no recorded commands, releases a staging buffer above 4 MiB — all but the largest of all — freed by a
  worker-thread task (the next large upload makes one again on its thread). The statistics count `transfer_staging_grows` and
  `transfer_staging_trims`; the debugger's Monitors tab shows `FellowEngine/Video memory (device)` and
  `FellowEngine/Upload staging`.

## Verification

lavapipe, Streaming Lab (480 × 270, streaming on; the trim with 60 frames unused): its four views captured without TAA
(`streaming_check.sh`'s settings) the same with the trim as without it (0 pixels); the flythrough's 1 817 loads and 843
downgrades without an error. Lavapipe uploads through one worker (two CPUs): its staging buffer 32 MiB after the
flythrough either way (never 60 frames unused while it loads), 32 MiB → 128 KiB after the captures (released when the
views had settled, made again at 128 KiB by the next small upload). The final version (5 s, the largest kept) keeps
lavapipe's one worker's buffer as the largest; its captures the same as without the trim. The regression suite (36
scenes, the release template against the editor build) with the trim on, in each version: the same as its goldens, no
error.

The RTX (job f436, the release template at 85281fe4d9; Sponza's flythrough at 1920 × 1080, streaming on without a budget,
three alternating runs each; video memory sampled at the flythrough's passes' ends, as milestone 113's figures):

| | video memory (peak) | textures | buffers | the rest | at the first frame: staging, workers | GPU frame |
|---|---:|---:|---:|---:|---:|---:|
| streaming off | 2 597 MiB | 1 631 MiB | 834 MiB | 131 MiB | 128 MiB, 1 | 6.09-6.54 ms |
| streaming on | 2 854 MiB | 1 504 MiB | 834 MiB | 515 MiB | 64 MiB, 1 | 6.20-6.23 ms |
| on, trimmed | **2 348 MiB** | 1 504 MiB | 834 MiB | **10 MiB** | 64 MiB, 1 | 6.24-6.37 ms |

The rest — host memory the driver counts in its total — was the staging buffers: without streaming one worker's 128 MiB
from the scene's load; with the streamer's loads, 515 MiB; trimmed, 10 MiB. The same loads (185-187)
and downgrades (38).

## Results

First with 60 frames unused (about 0.4 s here). The RTX (job f437, the release template at d6e26daa31; Sponza at
1920 × 1080, four alternating runs each; memory read from FellowEngine's statistics every 30 frames along the
flythrough and at its end; the GPU's frame, median):

| | staging at the end (workers) | staging's peak | video memory: end, peak | the device's heaps | GPU frame |
|---|---:|---:|---:|---:|---:|
| streaming off | 128 MiB (1) | 128 MiB | 2 597, 2 597 MiB | 2 468 MiB | 6.15 ms |
| off, trimmed | 0-64 MiB (1) | 0-64 MiB | 2 469-2 533, 2 469-2 533 MiB | 2 468 MiB | 6.05 ms |
| streaming on | 512 MiB (4) | 512 MiB | 2 854, 2 897 MiB | 2 341-2 384 MiB | 6.23 ms |
| on, trimmed | 5 MiB (4) | 320 MiB | **2 347**, **2 704** MiB | 2 341-2 384 MiB | 6.21 ms |

Streamed, four workers each kept 128 MiB — the next power of two of the largest upload any of them made — for good;
trimmed, they hold 5 MiB at the end, up to 320 MiB while loads overlap. The device's own heaps are the same either way
(the trim releases host memory only); without streaming the scene's load leaves 128 MiB, released once no upload needs
it. The GPU's frame the same (the first run after the heat soak, 6.74 ms, slow in every job: the medians). Bistro
exterior (no streaming, three runs each): one worker's 32 MiB from the load released (the driver's total 2 938 → 2 906
MiB, the device's heaps 2 905 either way), the GPU's frame 4.18 ms either way.

**But the streamed flythrough had longer frames**: p99.9 14-20 ms against 11-12, the longest 24-31 ms against 13-22, the
GPU's time in them 5.5-9 ms (the CPU waiting). Freeing the buffers on a worker thread instead of the render thread
(job f438, five runs each) left it so (p99.9 15.6-16.7 against 11.2-12.2 ms). A buffer made again is fresh host memory,
its pages first touched while a streaming thread copies a texture into it — holding its worker — and the render thread
waits for a worker whose earlier upload the frame draws with (stock's `_submit_transfer_workers()`). Job f439 (four runs
each) counted them:

| idle time | buffers trimmed, made again in the measured flythrough | p99.9 | longest frame | staging at the end |
|---|---:|---:|---:|---:|
| off | 0, 0 | 11.2-11.8 ms | 13.4-14.9 ms | 512 MiB (4 workers) |
| 0.5 s | 16, 40 | 14.7-20.3 ms | 24.0-30.2 ms | 5 MiB |
| 5 s | 0, 0 | 11.2-12.0 ms | 13.4-26.1 ms (one) | 384 MiB (3) |
| 20 s | 0, 0 | 11.0-12.5 ms | 13.2-22.5 ms | 512 MiB (4) |

With 5 s a flythrough that keeps loading mostly keeps its workers' buffers. Not always: job f440 (four runs each) saw
2-4 released and 2-7 made again in the flythrough, staging at its end 129-258 MiB, and in three runs of four one frame
of 23-27 ms (13-17 ms without the trim). **The largest buffer of all is now kept** (the most recently used among
equals), so that the next large upload after a pause can find it. Job f441 (the release template at ebbb76f3ff, four
runs each):

| Sponza streamed | staging at the end | buffers made again | p99.9 | longest frame | GPU frame |
|---|---:|---:|---:|---:|---:|
| the trim off | 416-512 MiB | 0 | 10.9-12.4 ms | 13.5-21.1 ms | 6.23-6.28 ms |
| on (5 s, the largest kept) | 168-384 MiB | 1-7 | 11.1-13.3 ms | 13.4-30.7 ms | 6.23-6.29 ms |

The frames within the runs' spread (one long frame in a run of each); the staging lower while the flythrough still
loads, and once uploads pause for 5 s down to the largest buffer (128 MiB here) from four of them. Without streaming a
scene's load fills one worker, whose buffer is then the largest and stays (the 128 MiB of Sponza's and the 32 MiB of
Bistro's, released by job f437's first version). Decision 0226 (large uploads reusing a big worker) changed nothing
measurable.

## Decision

Keep, on by default (5 s, the largest kept): the frames are the same (a staging buffer only carries uploads), their
times within the runs' spread, and the memory the driver reports (Godot's video memory statistic and monitor) no longer
keeps a large staging buffer per worker for good once uploads from several threads pause — the case texture streaming
makes.

## Rejected / not done

* **A short idle time** (60 frames, 0.5 s, 1 s): released during a flythrough, made again as fresh memory the render
  thread waits on (above; 1 s with decision 0226's reuse, job f441: p99.9 13.7-18.5 ms, the longest frames 26-34 ms).
* **Releasing every buffer, the largest too**: frees a scene's load's staging, but the next large upload after a pause
  fills fresh memory (the long frames above).
* **Exactly sized staging for large uploads** (a buffer per upload, freed when its copy is done): bounds the staging to
  the uploads in flight, but makes and frees host memory per large upload; the trim keeps a worker's buffer while it is
  in use.
* **Streamed loads uploading mip by mip** (a smaller largest upload): the texture is created with its data in one call.
