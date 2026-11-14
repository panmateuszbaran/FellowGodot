# 0227 — Large uploads submitted on their own thread

Status: **PENDING** (off; lavapipe measured, the RTX not yet) · Branch `fellow/geometry` · Setting
`rendering/fellow/frame/transfer_large_own_submit` · Code: `servers/rendering/rendering_device.{h,cpp}`
(`_acquire_transfer_worker()`, `_release_transfer_worker()`, `_wait_for_transfer_worker()`,
`_submit_transfer_workers()`), `fellow_settings.*` · Checks: lavapipe's Streaming Lab.

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
* `transfer_large_own_submit` (off): an upload above 4 MiB made off the render thread first submits what its worker had
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

The four views captured without TAA the same either way (0 pixels). The wait left is small uploads batched as before
(below 4 MiB they stay batched: a submission and a wait for each would slow a level's load of many small resources).

## Results

The RTX (job f442, the release template at 76ca4d3440; Sponza streamed at 1920 × 1080, five alternating runs each; the
frame's longest and its p99.9, ms; the frame's end's counted waits for held workers in all the runs):

| | longest frame, each run | p99.9 | waits counted | GPU frame (median) | staging at the end |
|---|---|---:|---:|---:|---:|
| the defaults (decision 0225: 5 s) | 23.9, 25.1, 13.1, 28.0, 27.3 | 11.0-13.7 | 4, 0.2 ms | 6.27 ms | 128-290 MiB |
| large uploads on their own thread | 26.1, 13.5, 14.2, 13.7, 33.5 | 11.1-11.7 | 6, 0 ms | 6.25 ms | 130-258 MiB |
| the staging trimmed after 0.5 s | 26.4, 30.7, 28.3, 27.6, 29.8 | 13.5-20.7 | 12, 23.8 ms | 6.27 ms | 131-132 MiB |
| both | **13.6, 13.3, 13.4, 13.0, 13.9** | **11.0-11.2** | 6, 0 ms | 6.24 ms | 132-134 MiB |

The longest frames left with both are the flythrough's shadow cascades rebuilt (1 236-1 342 draws, the GPU's 12-13 ms:
in every variant); the others' long frames are the CPU's (the GPU's 5-9 ms in 25-34 ms frames). The frame's end's
counted waits are few on the RTX (its copies are quick): where the CPU's stalls wait is not pinned down — not at the
frame's end's taking of a worker, as counted — but they are gone when the large uploads submit on their own thread and
the staging is trimmed soon (with 16-17 buffers made again in a flythrough), and not with either alone. PENDING: job f443 confirming it (and a
256 MiB budget, and Bistro).
