# FellowEngine — milestone 114 report (FSR 3.1's accumulation in half precision; the transfer workers' staging)

Status: **FINAL** (2026-11-14). Branch `fellow/geometry`. Milestone 113 left two measurements open: FSR 3.1's
accumulation, 61 % of the upscaler at 4K and kept in full precision on NVIDIA GPUs; and the video memory that follows
texture streaming's swaps beyond its textures and buffers, likely RenderingDevice's transfer workers' staging buffers.

## 0. Result

* **Video memory as the driver counts it, explained**: Sponza streamed ended with four upload workers holding 128 MiB of
  host staging each — 512 MiB of the 2 854 MiB Godot reports as video memory; the GPU's own heaps are 127 MiB lower with
  streaming than without. FellowEngine's statistics and the debugger's monitors now show the device's heaps and the
  staging apart.
* **The staging released when uploads pause** (decision 0225, on: 5 s, the largest buffer kept), its frees on a worker
  thread; the frames and their times the same. Shorter idle times emptied it during play but made frames wait for fresh
  buffers (p99.9 +3-8 ms) — measured, and rejected.
* **FSR 3.1's accumulation stays in full precision on NVIDIA** (decision 0224, rejected): half precision no faster on the
  RTX (1.45 ms either way in Bistro, 1.43 against 1.39 in Sponza at 4K), its pictures 42-50 dB from full precision's.
* Rejected too: large uploads reusing a big worker (decision 0226).

## 1. FSR 3.1's accumulation in half precision (decision 0224, rejected)

FSR 3.1's passes run in half precision where the GPU supports it, but its accumulation — 1.40-1.45 ms of the upscaler's
2.31-2.38 at 4K (milestone 113) — stays in full precision on NVIDIA GPUs, as AMD's backends and Godot's FSR 2.2 port
keep it ("reduced occupancy and high VRAM throughput"). Built in half precision on the RTX too (job f436, FSR 3.1 at
50 % at 3840 × 2160, three alternating runs each), the accumulation took 1.451-1.457 ms in Bistro exterior either way
(one full-precision run 1.563) and 1.43-1.44 ms against 1.38-1.39 in Sponza: no faster, 3.5 % slower in Sponza. Its
pictures (the scenes' ten cameras after 96 still frames) 42.4-49.6 dB from full precision's, up to 61 levels apart,
where two full-precision captures are identical (Bistro) or 69.5-78.5 dB apart (Sponza). Rejected and removed: the
accumulation stays in full precision on NVIDIA GPUs.

## 2. The transfer workers' staging buffers (decisions 0225, 0226)

Godot's video memory statistic is the driver's total over every heap, the host's included. FellowEngine's statistics
now split it — `memory_device_local_bytes` (the GPU's own heaps), `transfer_staging_bytes` and `transfer_workers`
(RenderingDevice's upload workers and their staging buffers, host memory), `transfer_staging_grows` and `_trims` — the
debugger's Monitors tab shows `FellowEngine/Video memory (device)` and `Upload staging`, and FellowBench samples them
every 30 frames along a run (`fellow_memory_peak`, `memory_peak_sampled`; its `memory_peak` is sampled at the passes'
ends only).

On the RTX, Sponza streamed (job f437, 1920 × 1080): **four workers kept 128 MiB of staging each** — a worker's buffer
grows to the next power of two of the largest upload it ever made and stays — 512 MiB of the driver's 2 854; without
streaming one worker kept the scene's load's 128 MiB. The device's own heaps are 127 MiB *lower* with streaming than
without (2 341 against 2 468 MiB): milestone 113's "video memory higher with streaming" was this host memory.

`rendering/fellow/frame/transfer_staging_trim` releases idle workers' staging. Releasing after 60 frames (about 0.4 s)
emptied it (5 MiB at the flythrough's end, the driver's total 2 854 → 2 347 MiB) with the GPU's frame the same, but
made frames wait: a buffer made again is fresh memory, its pages first touched while a streaming thread copies a texture
into it holding its worker, and the render thread waits for a worker whose earlier upload the frame draws with (p99.9 14-20
ms against 11-12, frames of 24-31 ms; freeing on a worker thread instead of the render thread did not change it, job
f438). Counted in job f439: 16 buffers released and 40 made again in a flythrough at 0.5 s, none at 5 s or 20 s. The
setting now waits `transfer_staging_trim_idle_s` (5 s) and keeps the largest buffer of all (job f441, four runs each):

| Sponza streamed | staging at the end | buffers made again | p99.9 | longest frame | GPU frame |
|---|---:|---:|---:|---:|---:|
| the trim off | 416-512 MiB | 0 | 10.9-12.4 ms | 13.5-21.1 ms | 6.23-6.28 ms |
| on (5 s, the largest kept) | 168-384 MiB | 1-7 | 11.1-13.3 ms | 13.4-30.7 ms | 6.23-6.29 ms |

The frames within the runs' spread; the staging lower while the flythrough still loads, and down to the largest buffer
once uploads pause. Large uploads preferring a worker whose staging fits over growing a free one (decision 0226) changed
nothing measurable and was removed. The regression suite with the trim on: the same as its goldens (section 3).

## 3. Checks and next

The regression suite (36 scenes, the release template against the editor build, lavapipe) with the trim on: the same
as its goldens, no error — with 60 frames and its frees on the render thread, then on a worker thread, then with 5 s
and the largest kept. Milestone 113's full run had one difference: `render_thread/rain`'s release picture against the
editor build's (max 149 on 9.8 % of the pixels, Godot's separate render thread); it did not come back in four reruns
of that scene nor in this milestone's three full runs — a timing-dependent capture under load, not found.

Next: the render thread waiting at the frame's end for a transfer worker another thread is filling (stock's
`_submit_transfer_workers()` takes each worker's lock when the frame draws with one of its earlier uploads): streaming's
own long frames (13-22 ms against 9-11 without streaming) come from it, and it is why a fresh staging buffer costs
frames; `render_thread/rain`'s one difference; the first measured run after FellowBench's heat soak, slow in every job
(6.7-7.0 against 6.2-6.3 ms in Sponza: the GPU's clock settling), which the medians absorb.
