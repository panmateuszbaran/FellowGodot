# FellowEngine — milestone 98 report (hang reports)

Status: **IN PROGRESS** (2026-11-13): the owner said "ok kontynuuj". Branch `fellow/geometry`. Decision record:
`docs/decisions/0206-hang-reports.md`.

## 0. Result

* **Hang reports** (`debug/fellow/crash_reports/hang_seconds`, 10 s): a watchdog thread writes a report when the main
  thread makes no progress — its frames (on Linux and macOS its GDScript frames too, taken by the main thread itself in
  a signal; on Windows from its suspended context, with a minidump of every thread) — without stopping the game, and
  how long the stall lasted when it goes on.
* **Checked** (Linux, the editor build and the release template): a 3.5 s stall two script functions deep (reported at
  2.0 s, recovered at 3.5 s, the game going on), a script's endless loop (reported, never recovered, the process
  killed); the crash lanes unchanged.
* **Also**: milestone 97's stack overflow on Windows fixed (the filter's frame overflowed what Windows leaves the
  thread: a 64 KiB stack guarantee, a small frame).

## 1. Development

`modules/fellow_diagnostics/fellow_crash_reporter.{h,cpp}` (the watchdog, the main thread's frames, Windows' stack
guarantee), `doc_classes/FellowCrashReporter.xml`, `doc/tools/fellow_settings_docs.py`,
`doc/classes/ProjectSettings.xml`; `tools/fellow_crash/` (two hang lanes; the symbolizer prints a hang's stall and
script frames).

## 2. Verification

* Decision 0206's table.
* The laptop (job f398, Windows): in progress.

## 3. Measurements

A thread waking ten times a second; nothing on the main thread beyond milestone 97's idle callback.

## 4. Next

Hang reports on Windows on the laptop; a per-thread view of where the CPU went in a stall (later).
