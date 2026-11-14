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

PENDING: the RTX (Sponza streamed, p99.9 and the longest frames; decision 0225's idle times again).
