# FellowEngine — milestone 102 report (crash reports sent to a collector)

Status: **FINAL** (2026-11-13): the owner said "ok kontynuuj"; the laptop's jobs f403-f406 done. Branch `fellow/geometry`. Decision record:
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
  own as on the cloud; the unreachable lane waited 30 s a report (Godot's Windows sockets see a refused connection only
  at the connection's timeout) — a collector not reached is now not tried again for the rest. A 10 s connection limit
  (job f404) and a 30 s one to the millisecond (f405) cut the TLS lanes off: their collector was `localhost`, whose
  IPv6 address Windows tries first; the lanes now use `127.0.0.1`. Job f406: every upload lane as on the cloud (TLS
  29-35 ms, the unreachable collector 30 s for the call).

## 3. Measurements

Two reports to a collector on the machine: 14-22 ms from the call to `uploads_finished`. Nothing while no upload runs.

## 4. Next

Retries within a run; Godot's Windows sockets seeing a refused connection at once.
