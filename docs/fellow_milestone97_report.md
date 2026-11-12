# FellowEngine — milestone 97 report (crash reports)

Status: **FINAL** (2026-11-13; Windows' stack overflow checked in job f398, milestone 98): the owner said "ok
kontynuuj" (the GPU free again after a Windows update). Branch `fellow/geometry`. Decision record: `docs/decisions/0205-crash-reports.md`.

## 0. Result

* **Crash reports** (`FellowCrashReporter`, `debug/fellow/crash_reports/*`): a shipped game's crash — release templates
  included, where Godot's own handler is compiled out — writes a JSON report (the reason, the faulting thread's
  frames, the last lines of the log, the game's breadcrumbs and values, the machine and GPU, the scene, GDScript's
  backtrace) before the process ends, and a minidump beside it on Windows; the next run finds it pending.
* **Checked** (Linux): six kinds of crash on the editor build and the release template, a seventh thirty frames into a
  game in a window; the next run finding each; a report the game writes; the reports off.
* **Also**: milestones 77-96 run on the laptop (job f396: all their checks on the RTX, both physics engines); what it
  found — milestone 96's bus limits made 4 400 virtual voices cost 152-224 µs a frame instead of 62 — fixed.

## 1. Development

* `modules/fellow_diagnostics/` (new): `fellow_crash_reporter.{h,cpp}`, `register_types.{h,cpp}`, `config.py`,
  `doc_classes/FellowCrashReporter.xml`.
* `tools/fellow_crash/`: `crash_check.py` and `crash_check.gd` (the check), `symbolize.py` (a report made readable,
  its frames symbolized).
* `doc/tools/fellow_settings_docs.py`: `debug/fellow/*` described as well; `doc/classes/ProjectSettings.xml`.
* `modules/fellow_world/fellow_audio_director.cpp`: a virtual voice's tests in the order of their cost.

## 2. Verification

* Decision 0205's table: every report as expected on both Linux builds.
* The editor check: passes (34 icons; the only error ALSA's).
* The audio director's fourteen lanes under Movie Maker after the fix: the same as the laptop's before it.
* The laptop (job f397, Windows, a release template): every crash's report and minidump as expected (decision 0205's
  second table: the exception, its address and access, the frames, the script's backtrace; minidumps of 428-702 KB
  holding the exception on the faulting thread), the windowed one with the RTX in its context — but a stack overflow
  wrote nothing: the filter's own frame overflowed what Windows leaves the thread. Fixed (a 64 KiB stack guarantee, a
  small frame); job f398 checks it with milestone 98.
* The audio director's fix on the laptop: 4 447 virtual voices 46-48 µs a frame (152 before), 64 voices 7.4 µs.

## 3. Measurements

A printed line costs the same with the reporter on or off (1.9-2.4 µs, writing it out dominates); a breadcrumb from
GDScript 0.45-0.64 µs; the reporter's start-up 1.8-2.1 ms. 4 400 virtual voices: 204-224 → 57-73 µs a frame (cloud,
release template).

## 4. Milestones 77-96 on the laptop (job f396)

A release template (MSVC) of 9a49b088a1 on the RTX 5070 Ti Laptop: every check of the night's milestones passed with
GodotPhysics3D and Jolt; against the cloud's Linux build the same results (the climbing, the effects, destruction,
acoustics and faces exactly; the vehicles and the traversal within floats' rounding: 205.521 against 205.523 km/h, a
frame and a few millimetres). The reports of milestones 77-96 have their numbers.

## 5. Next

Hang reports (milestone 98).
