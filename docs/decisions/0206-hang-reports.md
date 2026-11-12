# 0206 — Hang reports

Status: **KEEP** (`debug/fellow/crash_reports/hang_seconds`, 10 s, with decision 0205's reports on) · Branch
`fellow/geometry` · Code: `modules/fellow_diagnostics/fellow_crash_reporter.cpp` · Checks:
`tools/fellow_crash/crash_check.py` (two hang lanes).

## Problem

A game that freezes is not a crash: nothing faults, the player waits and then kills it, and decision 0205's reports
see nothing. Freezes (a deadlock, an endless loop in a script, a load that never ends) are as common as crashes in
players' reports, and harder to reproduce. Consoles' certification requires a game never to stop responding; crash
reporters for PC watch for hangs too (Windows Error Reporting's "not responding", Sentry's app hangs).

## Change

With crash reports on and `hang_seconds` > 0, a watchdog thread looks at the frame count the reporter already keeps
(decision 0205's idle callback) ten times a second. When the main thread has made no progress for `hang_seconds`, it
writes `hang_*.json` — decision 0205's report (machine, log, breadcrumbs, values, scene) with `stalled_ms` and the
main thread's frames — without stopping the game, and when a frame comes again appends `recovered_after_ms` (a report
without it is a freeze the player ended) and adds a breadcrumb. One report a stall.

* **The main thread's frames.** Linux and macOS: the watchdog sends the main thread a real-time signal no one else
  handles (SA_RESTART: a system call it interrupts goes on); the main thread takes its own `backtrace()` and its
  GDScript frames (function, file, line: the strings referenced, copied without the heap) into static buffers, and
  the watchdog waits up to half a second for them. Windows: the main thread is suspended for the moment its context
  is read and its stack unwound (no heap, no lock it could hold), and a minidump of every thread is written beside the
  report (GDScript's frames are the thread's own: not on Windows).
* **The watchdog's report** is written with decision 0205's writer (the main thread may hold the heap's lock).
* **Not a hang**: a debugger attached (`TracerPid`, `IsDebuggerPresent`) or the game run from the editor with its
  debugger (stopped at a breakpoint); the machine asleep (a clock that stops while it sleeps, and a gap in the
  watchdog's own ticks); before the first frame; while a crash is being reported.

## Checks (cloud: the CPU, Linux)

`crash_check.py` with `hang_seconds` = 2, the editor build and the release template the same:

| | expected | result |
|---|---|---|
| the main thread asleep 3.5 s two script functions deep, then going on | one report at about 2 s, the stall's length added, the game going on | `stalled_ms` 2004, `recovered_after_ms` 3507-3508; the game went on and quit normally (exit 0) |
| — its frames | the main thread's, its script's | 23-26 C++ frames from `clock_nanosleep` and `nanosleep` (libc's symbols); `_stall_two:126`, `_stall_one:122`, `_initialize:49` |
| a script looping forever, the process killed after 9 s | one report, no recovery | `stalled_ms` 2003, no `recovered_after_ms`; `_spin_two:136`, `_spin_one:130`, `_initialize:55` |

The crash lanes (decision 0205) unchanged with the watchdog running (its default, 10 s).

## Checks on Windows (the laptop, jobs f398-f399: a release template, MSVC)

The same two lanes: the stall reported at 2009-2010 ms and recovered at 3483-3484 ms (the game going on, exit 0), the
loop reported at 2009-2010 ms and never recovered (killed); the main thread's 25-28 frames unwound from its suspended
context; a minidump of 35 threads beside each (420-431 KB). No script frames (decision's design). The crash lanes
pass with the watchdog running.

## Cost

A thread waking ten times a second to read a counter; nothing on the main thread beyond decision 0205's idle
callback.

## Decision

Keep: a freeze in players' hands leaves a report of where the main thread was, and whether it ever went on.

## Rejected / not done

* **Every thread's stack on Linux** (a signal to each): the main thread's is where a game freezes; the minidump has
  them all on Windows.
* **A script's frames on Windows**: GDScript's call stack is the thread's own; only the main thread could copy it, and
  it is stopped (a signal-like interruption, an APC, runs only at an alertable wait).
* **Ending the frozen game** (and restarting it): the player's or the platform's decision.
