# 0227 — Large uploads submitted on their own thread

Status: **KEEP**, on by default (the same frames; the frame no longer waits for large copies) · Branch `fellow/geometry` · Setting
`rendering/fellow/frame/transfer_large_own_submit` · Code: `servers/rendering/rendering_device.{h,cpp}`
(`_acquire_transfer_worker()`, `_release_transfer_worker()`, `_wait_for_transfer_worker()`,
`_submit_transfer_workers()`), `fellow_settings.*` · Checks: lavapipe's Streaming Lab, the laptop's Sponza (jobs
f442-f444).

## Problem

RenderingDevice uploads through transfer workers: a thread uploading takes a worker, copies its data into the worker's
staging buffer and records the copy; the worker's recording is submitted at the frame's end if the frame draws with one
of its uploads (with semaphores the frame waits on), else later. Uploads from several threads are batched into the same
recording: a worker is taken for a new upload while an earlier one, recorded but not submitted, waits in it. When the
frame draws with that earlier upload — a streamed texture just swapped in — the frame's end (`_submit_transfer_workers()`)
must take the worker to submit it, and waits while another thread copies its own texture into the staging buffer: a
4K BC7 chain is 21 MiB, an 8K one 85 MiB. Milestone 114 measured texture streaming's longest frames at 13-22 ms on the
RTX in Sponza (9-11 without streaming) and traced decision 0225's longer frames to this wait.

## Change

* **Measured**: the frame's end counts the workers it found held (`transfer_submit_waits`) and how long it waited
  (`transfer_submit_wait_usec`) in FellowEngine's statistics.
* `transfer_large_own_submit` (on): an upload above 4 MiB made off the render thread first submits what its worker had
  recorded and waits for it, copies, then submits and waits for its own copy before releasing the worker (its texture
  barriers handed to the frame, as an immediate upload's). While it copies, nothing the frame draws with is left
  unsubmitted in that worker; the frame's end, finding a worker held, waits only while the uploads it draws with are not
  processed (`transfer_submit_skips` when that ended the wait).
* A worker's texture barriers are handed over before its operations are marked processed (they were after), so that a
  frame seeing them processed without taking the worker records them.

## Verification

lavapipe, Streaming Lab's flythrough (480 × 270, streaming on, 1 738-1 806 loads and 868-882 downgrades a run):

| | the frame's end found a worker held | its waits in all | ended early (uploads done) | large uploads submitted on their thread |
|---|---:|---:|---:|---:|
| off | 68 times | 73.0 ms | — | — |
| on | 47 times | 17.4 ms | 34 | 358 |

The four views captured without TAA the same either way (0 pixels); the regression suite (36 scenes) with it off and
with it on (milestone 116) the same as its goldens. The wait left is small uploads batched as before
(below 4 MiB they stay batched: a submission and a wait for each would slow a level's load of many small resources).

## Results

lavapipe (counted, milestone 116): without the option 238 of a flythrough's frame-end submissions carried more than
4 MiB of copies the frame waits for (4.1 GB in all); with it, 1 (4 MB).

The RTX, Sponza streamed at 1920 × 1080. Job f442 (five alternating runs each) suggested more than it held: with the
staging trimmed after 0.5 s (decision 0225) the CPU stalled in every run (3-7 frames over 16.7 ms whose GPU work was
under 60 %: 22 in five runs), with the option too in none of five, and the defaults in four of five. Job f444 (the
release template at 0c711198e5, eight alternating runs each, without and with a 256 MiB budget; stalls: frames over
16.7 ms whose GPU work is under 60 % of them):

| | the frame's own copies over 4 MiB, a flythrough | stalls in 8 runs | longest frame | p99.9 | GPU frame (mean of runs) | staging trims' buffers made again |
|---|---:|---:|---:|---:|---:|---:|
| the defaults (decision 0225: 5 s) | 22-24 | 2 | 13.3-27.3 ms | 10.8-12.2 ms | 6.24 ms | 0-4 |
| large uploads on their own thread | 0 | 3 | 13.1-28.1 ms | 10.8-12.4 ms | 6.24 ms | 0-2 |
| and the staging trimmed after 0.5 s | 0 | 2 | 13.2-23.0 ms | 10.9-11.5 ms | 6.27 ms | 16-18 |
| 256 MiB budget: the defaults | 54-60 | 3 | 13.1-22.8 ms | 11.0-12.4 ms | 6.25 ms | 0-2 |
| budget, large uploads on their own thread | 0-1 | 1 | 13.3-22.0 ms | 11.1-12.8 ms | 6.24 ms | 0-1 |
| budget, and the trim after 0.5 s | 0 | 2 | 13.2-24.6 ms | 11.0-12.8 ms | 6.25 ms | 41-55 |

So the frame no longer waits for a large copy — 22-24 such waits a flythrough without a budget, 54-60 with one, then
none — the GPU's frame the same; the CPU's rarer stalls (one in four runs, here) stay in every variant: their cause is
elsewhere (job f445 records FellowEngine's counters frame by frame). Bistro exterior (no streaming, job f443): the same
frames (its uploads at its load). What the option does change: decision 0225's short idle times no longer stall
(0.5 s: 22 stalls in five runs without the option, 2 in eight with it), so a game short of memory can trim the staging
soon (Sponza: 132 MiB of staging at the end against 257).

## Decision

Keep, on by default: no frame waits for a large copy, at no cost; decision 0225's idle time stays 5 s (0.5 s is safe
with this on, a choice for a game short of memory).
