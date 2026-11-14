# 0226 — Large uploads reusing a transfer worker whose staging fits

Status: **REJECTED** (measured, code removed) · Branch `fellow/geometry` · Setting (tried)
`rendering/fellow/frame/transfer_large_reuse` · Code: `servers/rendering/rendering_device.cpp`
(`_acquire_transfer_worker()`) · Checks: the laptop's Sponza (jobs f440, f441).

## Problem

Decision 0225 found Sponza's streamed flythrough ending with four transfer workers holding 128 MiB of staging each.
RenderingDevice picks, for an upload, a worker nobody holds: first one whose staging fits and whose last submission has
been waited for; then one whose last submission has been waited for but whose staging must grow; then one that fits but
whose submission is still to be waited for. A worker submitted at the end of a frame stays "busy" until someone waits
for its fence, however long ago the GPU finished, so a large upload grows a free worker rather than reuse a big one, and
in time every worker grows to the largest upload.

## Change (tried)

`transfer_large_reuse` (off): an upload above 4 MiB prefers a worker whose staging fits, waiting for its last submission
(long finished, as a rule), to a free one that would have to grow.

## Results

The RTX (jobs f440 and f441, Sponza streamed at 1920 × 1080, four alternating runs each; staging at the end of the
flythrough, buffers made again in it):

| | staging at the end | buffers made again | p99.9 | longest frame |
|---|---:|---:|---:|---:|
| the trim, 5 s (f440) | 129-258 MiB | 2-7 | 11.2-12.2 ms | 13.0-27.1 ms |
| and the reuse (f440) | 1-258 MiB | 1-6 | 11.0-12.3 ms | 13.6-26.0 ms |
| the trim, 5 s, the largest kept (f441) | 168-384 MiB | 1-7 | 11.1-13.3 ms | 13.4-30.7 ms |
| and the reuse (f441) | 129-258 MiB | 1-3 | 10.9-11.6 ms | 13.2-25.9 ms |

Within the runs' spread: the workers still grew (concurrent loads each hold a worker, so a large upload finds the big
ones taken), and the frames are the same.

## Decision

Rejected and removed: no measurable change in the staging kept or in the frames. Decision 0225's trim stays the way to
release the workers' staging.
