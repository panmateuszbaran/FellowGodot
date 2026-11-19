# 0284 — The hot workers' gate forced, and its history in a late fence's report (the RTX's rare failure)

Status: **KEEP** (diagnostics; `debug/gate_open_every` 0 by default changes nothing) · Branch `fellow/geometry` · Code:
`servers/rendering/fellow_settings.{h,cpp}` (`frame_gpu_wait_report()`, `frame_gate_describe()`),
`servers/rendering/rendering_device.cpp` (`_fellow_late_fence_report()`) · Checks: lavapipe (a scene of 1 200
transparent meshes, its transparent pass recorded in parallel); the RTX's job `m152_f517` (33 runs each way; stopped
there when the owner needed the laptop).

## Problem

Decision 0270's failure: about one run in 15 of Bistro exterior streamed on the RTX (7 in 100: `f510`, `f515`), the GPU
stops inside the transparent pass, a draw list recorded on worker threads (`cpu/parallel_recording`). The jobs since
then point at the hot workers' gate (`cpu/hot_workers_gate`, decision 0065). The gate opens while the render thread
does not wait for the GPU and closes when it does; in a frame with it open, a long draw list is replayed into secondary
command buffers on worker threads (`frame/parallel_replay`), the submit thread submits the frame (`frame/submit_thread`)
and the workers stay hot between loops (`cpu/hot_workers_usec`). In GPU-bound Bistro it is open in 1-3 % of the frames.

| job | parallel recording | the gate | failures |
|---|---|---|---:|
| `f510`, `f515` | on | as it is (open in 1-2 % of the frames) | 7 in 100 |
| `f512` | off | as it is | 0 in 40 |
| `f514` | on (the parallel replay off) | as it is | 0 in 40 |
| `f516` | on | held open (`cpu/hot_workers_gate` off) | 0 in 30 |

The reports of the nine failures since milestone 146, read again:

* In each, every submission of the last eight frames the sync log shows is the render thread's, never the submit
  thread's: the gate was closed. **The frame that failed replayed its lists whole into its primary command buffer**,
  as every frame of `f514` did, its parallel recording on cold workers (pool tasks).
* The submit thread's last frame in that slot ("the slot's job" in the report against the jobs handed over) was 289 to
  2 409 frames before: the gate had last been open 1.3-11 s earlier. The failures are not at its opening or closing.
* Each run is a warm-up (29 s, about 6 000 frames) and two passes of the same camera path (6 001 frames each). Counted
  from the end of the run, the failures came about 2 400, 4 710 (twice), 5 030-5 130 (three) and 5 830-5 930 (three)
  frames into a pass (the warm-up's frames vary a little from run to run): at a few places of the path.

Two readings fit: the cold parallel recording fails at those places (`f514`'s 40 runs without a failure were luck: 6 %
at that rate), or a frame with the gate open leaves something behind that makes a later frame fail (none in `f514`,
whose parallel replay was off; none in `f516`, never closed). A report does not say how the gate went before the frame
that failed, and the gate cannot be held closed.

## Change

* **`debug/gate_open_every`** (frames, 0: off): the gate open in one frame of every N and closed in the others, whatever
  the GPU waits; 1: always open; -1: always closed. The same commands in each frame; which frames replay their lists
  in secondary command buffers, are submitted by the submit thread and keep the workers hot changes.
* **The gate's history in a late fence's report**: its last 64 frames (`o` open, `.` closed, oldest first), how often
  it opened in the run and how many frames ago it was last open, for example
  `Hot workers' gate (cpu/hot_workers_gate on, debug/gate_open_every 3): open in 22 of the last 64 frames, oldest first
  [o..o..o..]; opened 24 times in 70 frames; open now.` — and which of those frames is the slot's.

## Checks

* Lavapipe, the scene of 1 200 transparent meshes (36 of its lists recorded in parallel in 40 frames):
  `debug/gate_open_every` 1: all 36 replayed in secondary command buffers; 4: 9; -1: none; 0: 34 (the gate as it is).
  The history, printed at the 9th and 70th frames for the check: `[o..o..o..]` for 3, `[.........]` for -1,
  `[ooooooooo]` for 0 on lavapipe (never waiting for its GPU).
* The default (0) leaves the gate's decision as it was; the history is a few integer operations per frame.

* **`m152_f517`, the RTX**: Bistro exterior streamed as `f510`, the gate held closed (-1) against the gate as it is,
  interleaved (planned 45 each; stopped after 33 each when the owner needed the laptop):

  | the gate | runs | failures |
  |---|---:|---|
  | held closed (`debug/gate_open_every` -1) | 33 | **4**: three hangs (runs 14, 22, 28), one exit at 70 s with 0x80000003 and no report (run 1) |
  | as it is | 33 | 0 |

  The three hangs' reports: `open in 0 of the last 64 frames [....]; opened 0 times in 5232 frames; never open` (5 110
  and 17 461 frames in the others) — the gate never opened in those runs. The GPU stopped as in decision 0270, frame
  slot's fence not signalled, every submission the render thread's. The frame and the GPU when it ran: 4.3-4.5 ms
  against 4.4-4.5 ms as it is.

## Decision

KEEP (diagnostics). `f517` answers the question it was made for: the failure needs no frame with the gate open — it
comes in runs where the gate never opened, at 4 in 33 (12 %, against 7 in 100 with the gate as it is and 0 in 30 held
open). What the open frames leave behind is ruled out; left is the frame with the gate closed itself: its lists recorded
in parallel on cold workers (pool tasks), replayed whole into the primary command buffer, submitted by the render thread.
Next (`f518`, held while the GPU is paused): those three split — the gate held open with `frame/parallel_replay` and
`frame/submit_thread` off (the hot workers alone), against the gate held open with `cpu/hot_workers_usec` 0 (the cold
workers with the open frame's replay and submission).

## Rejected / not done

* **Holding the gate open as the fix** (`f516`: no failure in 30, the render thread 1.26 ms against 1.43-1.47, p99
  5.5 ms against 5.9-6.3, the GPU 1.5 % longer as decision 0017 measured): not before the cause is known — 30 runs
  without a failure happen 11 % of the time at the rate seen.
