# FellowEngine — milestone 146 report (a late fence reported; the terrain holes on Intel)

Status: **FINAL** (2026-11-18). Branch `fellow/geometry`. The RTX (jobs f502, f504-f507) and the Intel GPU (f503);
lavapipe in the cloud.

## 0. Result

* **The terrain holes on Intel fixed** (decision 0271): the holes' material discarded its fragments before sampling its
  textures and taking derivatives — undefined in a quad with a discarded fragment, and on Intel's GPU different every
  frame on the holes' rims (3-5 levels; milestones 136, 143, 145). Discarded at the end: every frame of every run the
  same, bit for bit (f503). On lavapipe the rims change by up to 4 levels (0.08 % of the picture; golden blessed).
* **A late frame fence reported** (decision 0270): a frame's fence not signalled within `debug/late_fence_report_s`
  (2 s, on by default; changes no frame) is reported — the frame slots, the present thread's jobs, the transfer
  workers, the driver's swap chain semaphores, with `profiling/pass_timestamps` which of the frame's timestamps the GPU
  wrote — then waited for as before; a fence wait that finds the device lost prints `VK_EXT_device_fault`'s report.
  New diagnostics beside it, all off: `debug/sync_log` (the Vulkan driver records every submission, presentation and
  acquisition in order, checks each semaphore wait against the signals before it and each fence wait against the
  submissions, traces a late fence to its submission and each wait's origin, gives each acquisition a fence that says
  whether the presentation engine released the image; on the RTX it costs nothing measurable; `regression.py run
  --sync-log` runs the suite with it), `debug/gpu_checkpoints` (NVIDIA's markers before each command of the frame, the
  last ones each queue passed in the report), `debug/graph_reorder` and `debug/graph_full_barriers` (Godot's
  compile-time switches of the render graph as settings).
* **Milestone 145's hang caught three times** (Bistro exterior streamed, releases held 10 s, the record on): always at
  the same frame — the 2 464th of the camera path's third flight — with every wait of the stuck submission signalled
  (the swap chain image released, the transfer submissions done) and no anomaly in 59 000 operations: the GPU stopped
  inside the frame's own work, nearly idle (15-20 W). 3 of 32 runs as is; none of 20 without the present and submit
  threads, none of 36 with the pass timestamps on (which also keep each pass's commands together in the render graph),
  none of 12 with the record off — too few to tell which matters. Open.
* **The hair's self-shadowing on the RTX** (decision 0268): the GPU's time the same (0.366 → 0.369 ms for 24 heads at
  1080p), the main thread's hair group +0.23 ms; on lavapipe it follows a turning sun from a painted mesh scalp.

## 1. Checks

* Lavapipe: Streaming Lab with the record on and fences counted late after 0.5 ms (the reports read), Slice Lab and
  World Lab (no anomaly), Streaming Lab under Vulkan's core and synchronization validation with the record on (no
  message); the image regression suite with the record on in every group (`run --sync-log`: 48 of 48 identical, no
  anomaly) and without it (exact after the GPU checkpoints' breadcrumbs; `terrain_holes` blessed again: the holes'
  rims); four scenes with the graph's reordering off and with full barriers (the goldens' pictures); `hair_check.gd
  --scalp_mesh=1 --sun_turn=150 --self_shadow=1` (new options: the hair from a painted mesh scalp, the sun turned — the
  hair's mean share of the light 0.498 → 0.542).
* The Intel GPU: the terrain-holes scene's frames 60, 90 and 120 in two runs with milestone 142's build, with discard as
  demote, and with the fix (`m146_f503`).
* The RTX: Bistro exterior streamed with releases held 10 s — the record on and off (`m146_f502`, 12 runs each); the
  record on as is, without the present and submit threads, and with FellowEngine off (`m146_f504`, 20 runs each); the
  pass timestamps on (`m146_f506`, 16 runs; `m146_f507`, 20 runs); `hair_crowd_check.gd`'s 24 heads with and without
  self-shadowing (`m146_f505`, 3 runs each).

## 2. Next

The hang: `m146_f508` (queued) — GPU checkpoints, which say where the GPU stopped without constraining the order of the
frame's commands, and the render graph's reordering off, 20 runs each; then the fix, and releases held 10 s.
