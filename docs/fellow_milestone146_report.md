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

* Lavapipe: Streaming Lab with the record on and fences counted late after 0.5 ms (the reports read), Slice Lab and
  World Lab (no anomaly), Streaming Lab under Vulkan's core and synchronization validation with the record on (no
  message); the image regression suite with the record on in every group (`run --sync-log`: 48 of 48 identical, no
  anomaly) and without it (`terrain_holes` blessed again: the holes' rims); `hair_check.gd --scalp_mesh=1
  --sun_turn=150 --self_shadow=1` (new options: the hair from a painted mesh scalp, the sun turned — the hair's mean
  share of the light 0.498 → 0.542, the shade following it).
* The Intel GPU: the terrain-holes scene's frames 60, 90 and 120 in two runs with milestone 142's build, with discard as
  demote, and with the fix (`m146_f503`).
* The RTX: Bistro exterior streamed with releases held 10 s — the record on and off (`m146_f502`, 12 runs each), the
  record on as is, without the present and submit threads, and with FellowEngine off (`m146_f504`, 20 runs each), the
  pass timestamps on (`m146_f506`); hair_crowd_check's 24 heads with and without self-shadowing (`m146_f505`).

## 2. Next

(pending)
