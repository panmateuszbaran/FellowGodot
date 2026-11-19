# 0270 — A late frame fence reported, and the Vulkan driver's record of its queue operations

Status: **KEEP** (diagnostics; the hang's cause under measurement) · Branch `fellow/geometry` · Code:
`servers/rendering/rendering_device.cpp` (`_fellow_late_fence_report()`), `servers/rendering/rendering_device_driver.h`
(`fellow_fence_wait_timeout()`, `fellow_fence_report()`, `fellow_sync_log_enable()`, `fellow_command_queue_label()`),
`drivers/vulkan/fellow_vulkan_sync_log.{h,cpp}` (new) and `drivers/vulkan/rendering_device_driver_vulkan.cpp`;
settings `rendering/fellow/debug/late_fence_report_s` (2 s) and `rendering/fellow/debug/sync_log` (off, start-up);
FellowEngine's statistics `late_fences`, `sync_log_anomalies`.

## Problem

Milestone 145 (decision 0269): three of 28 runs of Bistro exterior streamed with releases held 10 s hung — the main
thread in `RenderingDevice::_stall_for_frame()` waiting without end for a frame's fence, the GPU idle, FellowEngine's
present thread idle with nothing to issue, the workers idle. A GPU idle with a fence unsignalled is a queue waiting for
something that will not come (a semaphore no signal is pending for) or a fence no submission signals; the hang reports
showed where the CPU waited, not what the GPU's queue waited for.

## Change

* **A late fence reported** (`late_fence_report_s`, 2 s; changes no frame): the frame's fence is waited for with a time
  limit; past it the device prints the frame slots, the present and submit thread's jobs (handed over, issued, the
  slot's), the transfer workers (recording, submitted, their operations counted, submitted and processed against what
  the frame uses) and the driver's account of the fence, then waits as before. The first four in a run; the hang
  report's log (decision 0206) carries them.
* **The driver's record** (`sync_log`, off): every submission, presentation and acquisition in the order the driver
  makes them (one lock around `vkQueueSubmit()` while it is on), with the semaphores each waits on and signals and its
  fence. Each wait is matched with the signal it consumes as it is submitted; a wait with no signal pending, a semaphore
  signalled twice without a wait between, a fence submitted twice, a wait for a fence nothing will signal are printed
  when they happen. A late fence is traced to its submission, each of its waits to the operation that signalled it (and
  that operation's fence's state), with the last 32 operations; and every queue's swap chain semaphores (waited on by
  its next submission, released by its next fence, held by fences).
* **What a lost device says** (since `a384d21365`): a fence wait that finds the device lost prints
  `VK_EXT_device_fault`'s report, as a lost submission does — milestone 145's longest hang ended in a lost device after
  five minutes, with no word of why.
* **The acquisitions' images and fences** (since `c0fffa27e0`, with the record on): each acquisition's image and a fence
  passed to `vkAcquireNextImageKHR()`, signalled with its semaphore when the presentation engine releases the image
  (reset only once seen signalled); each presentation's image and result; the swap chain's images and present mode
  printed when it is made.

## Checks

* Lavapipe: Streaming Lab with the record on and a fence counted late after 0.5 ms (reports at frames 3 and 41: the
  slots, the present thread, the transfer workers, the fence's submission, its wait traced to the transfer worker's
  submission and its signalled fence), Slice Lab and World Lab (no anomaly in any); Streaming Lab under Vulkan's core
  and synchronization validation with the record on (no message, once the acquisitions' fences were reset only when
  seen signalled).
* The RTX, Bistro exterior streamed with releases held 10 s (`m146_f502`, 12 runs each): the record costs nothing
  measurable (frame 4.28 ms with it, 4.32 ms without; p99 5.89 and 5.86 ms); no anomaly in any run (59 407 operations
  in the hung one, 18 085 frames' jobs in each whole run).

## What the hang is

One of the 12 runs with the record on hung (the others' 23 runs none), 14 548 frames in. Its report:

* the main thread waiting for frame slot 0's fence (signalled by the frame's second submission, `#59402`), the present
  thread with every job issued, the four transfer workers idle with all they were given submitted;
* `#59402` waits only on the frame's first submission (`#59401`), which waits on the frame's swap chain acquisition
  (`#59398`) and on two transfer submissions — whose fences are signalled, so their semaphores were;
* the queue's two acquisition semaphores both held by fences not yet waited for; the next frame (`#59404`-`#59407`)
  acquired, submitted and presented behind it.

It looked like the main queue waiting on the acquisition's semaphore (the presentation engine not releasing the image).
`m146_f504` caught the second hang (the FellowEngine arm's second run) with each acquisition's fence recorded — **at the
same frame** (14 542 by the hang report, 14 548 frames drawn by the device, in both) — and there the acquisition's
fence *was* signalled (image 0 released), as were both transfer submissions' fences: every wait of the stuck
submission had been signalled. The GPU stopped inside the frame's own work, not at a wait between submissions. The
runner's telemetry over a hung run: about 15-20 W and 340-880 MHz against 80 W and 1 600 MHz in play — the GPU nearly
idle, not looping at full power. The swap chain: 3 images (2 at least), IMMEDIATE present mode (`--disable-vsync`),
two frames in flight; the presentation of each frame its own image (0, 1, 2 in turn), nothing returned an error.
`m146_f504`'s third arm and a third hang (run 7 of the same arm) at the same frame again: the frame is the 2 464th of
the camera path's third flight (the warm-up's one and the measurement's two, 6 001 frames each) — the same view, and
in the runs that did not hang the same view passed without a long GPU frame.

| Bistro exterior streamed, releases held 10 s (RTX) | runs | hung |
|---|---:|---:|
| FellowEngine as is, the record on (`f502`, `f504`) | 32 | 3 (all at frame 14 548) |
| the same, the record off (`f502`) | 12 | 0 |
| without the present and submit threads (`f504`) | 20 | 0 |
| FellowEngine off (`f504`: nothing streamed; two flights, not three) | 20 | 0 |
| the record and the pass timestamps on (`f506`, `f507`) | 36 | 0 |
| (milestone 145, `m145a`) held 10 s / 2 s | 28 / 27 | 3 / 0 |

Too few to tell which of them matters (3 of 32 against none of 36 is p ≈ 0.1). The pass timestamps, meant to say
where the GPU stops, may themselves keep it from stopping: they make the render graph keep each pass's commands
together (a timestamp is adjacent to every command since the previous one), and the GPU drains its work at each.
Next (`m146_f508`): GPU checkpoints (`debug/gpu_checkpoints`: NVIDIA's markers before each command, without
constraining the order) and the render graph's reordering off (`debug/graph_reorder`), 20 runs each.

`m146_f508` (milestone 147): one hang in each arm — so neither the reordering (it hung with it off, at another frame,
8 485) nor markers between the commands keep it away. With the checkpoints, the report of the hung run:

* the main queue's top of pipe and bottom of pipe both at the checkpoint before command 69 of the frame — a draw list
  without a breadcrumb (none of Forward+'s passes carries one): everything before it done, the draw list begun and
  never finished;
* the bench's own viewport timestamps of the frame: the first written, the last not.

The GPU stops in one draw list, nearly idle. Next (`m147_f509`): which draw list (its region, its commands' size,
whether worker threads recorded it in secondary command buffers — now in its checkpoint — and the commands around it).
`m147_f509` (stopped after 30 of its 40 runs: the GPU paused for the owner): no hang in 15 runs as in `f504` nor in 15
with the checkpoints — the hang about one run in 20 by now (5 in 114 runs as is, without the pass timestamps, since
milestone 146). Open; next on the GPU: `f510` (40 runs with the checkpoints, prepared in `.tools/wip`).

`m147_f510` (milestone 151): 3 hangs in 40 runs (runs 7, 15 and 21), each inside command 69 of its frame, as
milestone 147's catch — both of the main queue's checkpoints before it, every command before it done: a draw list at
1920 × 1080, about 64 KB of commands, recorded on worker threads (`cpu/parallel_recording`). Forward+'s frame there:
the opaque pass (command 65, 256 KB, recorded on worker threads), the sky (66), the screen and depth textures' copies
(67, 68), then **the transparent pass** (69). Four catches, four times the transparent pass recorded in parallel, never
the opaque pass recorded the same way.

`m151_f512` — the same 40 runs with `cpu/parallel_recording` off (the lists still replayed in parallel, split by their
draws): **no hang in 40** (as is: 3 in 40; at the rate since milestone 146, one in 20, 40 runs without one happen 13 %
of the time). The frame 4.56 ms against 4.47 (the render thread records them alone), the GPU's 4.32 ms the same, p99
7.1 against 6.3 ms. Next: `f514` (parallel recording on, the frame's parallel replay off: the recorded segments replayed
into the main command buffer) — a hang there is the recorded commands', none the replay of recorded segments'.

## Decision

KEEP: the late fence report on by default (2 s; changes no frame), the record off by default (start-up), the fault
report of a lost device on fence waits.
