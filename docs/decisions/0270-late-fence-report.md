# 0270 — A late frame fence reported, and the Vulkan driver's record of its queue operations

Status: **DRAFT** · Branch `fellow/geometry` · Code: `servers/rendering/rendering_device.cpp`
(`_fellow_late_fence_report()`), `servers/rendering/rendering_device_driver.h` (`fellow_fence_wait_timeout()`,
`fellow_fence_report()`, `fellow_sync_log_enable()`, `fellow_command_queue_label()`),
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

## Checks

(pending)

## Decision

(pending)
