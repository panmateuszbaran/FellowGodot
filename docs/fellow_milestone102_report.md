# FellowEngine — milestone 102 report (crash reports sent to a collector)

Status: **IN PROGRESS** (2026-11-13): the owner said "ok kontynuuj". Branch `fellow/geometry`. Decision record:
`docs/decisions/0210-crash-report-uploads.md`.

## 0. Result

* **Uploads** (`FellowCrashReporter.upload_reports()`, `debug/fellow/crash_reports/upload_url`,
  `upload_automatically`, `upload_minidumps`): the reports waiting on the machine sent on a thread, oldest first, as
  `multipart/form-data` with the minidump in `upload_file_minidump` (the form Sentry's, BugSplat's and Backtrace's
  minidump endpoints take); accepted ones moved into `sent/`, refused or unanswered ones kept for the next try;
  `report_uploaded` and `uploads_finished`. HTTPS with the collector's certificate checked.
* **A stock GDScript bug fixed**: a lambda connected to a module singleton's signal (`FellowCrashReporter`,
  `NavigationServer3D`) corrupted the heap at exit; `GDScript::~GDScript` now lets go of the lambdas that outlive it.

## 1. Development

`modules/fellow_diagnostics/fellow_crash_reporter.{h,cpp}` (the upload thread, the multipart body, the POST, the
settings, the signals), `doc_classes/FellowCrashReporter.xml`, `doc/classes/ProjectSettings.xml` (through
`doc/tools/fellow_settings_docs.py`); `modules/gdscript/gdscript.cpp` (marked); `tools/fellow_crash/crash_check.{py,gd}`
(a collector on the machine — HTTP and TLS — six upload lanes, `--lanes`).

## 2. Verification

* Decision 0210's table: the editor build and the release template, every lane as expected; the earlier lanes the
  same.
* The lambdas' fix: the two singletons' runs with a lambda connected end cleanly; Valgrind without errors.
* The image regression suite (the GDScript change in every scene's scripts): 35 scenes exact, the release template
  the same as the editor build.
* The laptop (job f403, Windows): the minidump (438 019 bytes) sent with its report, TLS, refused, accepted, on their
  own as on the cloud; the unreachable lane waited 30 s a report (Windows reports a refused connection at the TCP
  timeout) — the connection now waits 10 s and a collector not reached is not tried again for the rest; the laptop
  again: next job.

## 3. Measurements

Two reports to a collector on the machine: 14-22 ms from the call to `uploads_finished`. Nothing while no upload runs.

## 4. Next

Retries within a run; the laptop's Windows lanes.
