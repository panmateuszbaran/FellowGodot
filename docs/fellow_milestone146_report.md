# FellowEngine — milestone 146 report (a late fence reported; the terrain holes on Intel)

Status: **DRAFT** (2026-11-18). Branch `fellow/geometry`. The RTX (jobs f502, f504, f505) and the Intel GPU (f503);
lavapipe in the cloud.

## 0. Result

* **A late frame fence reported** (decision 0270): a frame's fence not signalled within
  `debug/late_fence_report_s` (2 s, on by default; changes no frame) is reported — the frame slots, the present and
  submit thread's jobs, the transfer workers, the driver's swap chain semaphores — then waited for as before.
  `debug/sync_log` (off): the Vulkan driver records every submission, presentation and acquisition in order and checks
  each semaphore wait against the signals before it and each fence wait against the submissions; a late fence is traced
  to its submission and each wait's origin; acquisitions carry a fence that says whether the presentation engine
  released the image. A fence wait that finds the device lost prints `VK_EXT_device_fault`'s report. The regression
  suite runs with it (`run --sync-log`). On the RTX the record costs nothing measurable.
* **Milestone 145's hang caught** (f502, 1 of 12 runs with the record on): the main queue waiting on a swap chain
  acquisition's semaphore — the presentation engine had not released the image it gave the frame (IMMEDIATE present
  mode, 3 images, two frames in flight); every other wait's signal done; no anomaly in 59 407 operations.
* **The terrain holes on Intel fixed** (decision 0271): the holes' material discarded its fragments before sampling its
  textures and taking derivatives — undefined in a quad with a discarded fragment, and on Intel's GPU different every
  frame on the holes' rims (3-5 levels). Discarded at the end: every frame of every run the same, bit for bit (f503).

## 1. Checks

(pending)

## 2. Next

(pending)
