# 0205 — Crash reports from shipped games

Status: **KEEP** (`debug/fellow/crash_reports/enabled`, off by default) · Branch `fellow/geometry` · Code:
`modules/fellow_diagnostics/fellow_crash_reporter.{h,cpp}` (a new module), `tools/fellow_crash/` (the check, a
symbolizer) · Checks: `tools/fellow_crash/crash_check.py`.

## Problem

A crash in a player's hands left nothing behind. Godot's crash handler prints a backtrace to the console, and only in
debug builds: a release template (what a game ships) has it compiled out on Linux and Windows, so a shipped game dies
silently — no report, no minidump, nothing the next run could find. Studios ship crash reporters (Crashpad, Breakpad,
Sentry, BugSplat) for this: every crash's reason, stack and context collected, the most frequent fixed first.
Section 2 of the readiness assessment listed "no crash reporting" under quality and validation.

## Change

A singleton, `FellowCrashReporter` (module `fellow_diagnostics`, built on every platform), on with
`debug/fellow/crash_reports/enabled` (and FellowEngine on; never in the editor):

* **The fatal paths.** Linux/BSD with glibc and macOS: `sigaction` handlers for SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT
  and SIGTRAP on an alternate stack of 256 KiB (the main thread's stack overflowing still reports); Windows: an
  unhandled-exception filter, SIGABRT and the pure-virtual-call handler, the report written from a thread of its own
  (a stack overflow leaves the faulting one no stack; a minidump is best written from another thread). Installed in a
  release template as in a debug build; whatever was installed before (Godot's own handler in debug builds, the C
  runtime's filter) runs afterwards, then the default action: the process ends as it would have.
* **Without the heap.** What a handler reads is kept in static buffers as the game runs: the last
  `log_lines` lines the engine printed (a `Logger` copying each line into a ring), the breadcrumbs (64), the custom
  values (32), the frame, the current scene, the GPU's memory, and the machine as JSON built at start-up. The handler
  formats the report with its own integer and string writers into a buffer on the stack and writes it with the
  system's calls; the frames come from `backtrace()` (from the faulting instruction on) or, on Windows, from unwinding
  the exception's context with the modules' unwind tables, each described by the loader (`dladdr`,
  `GetModuleHandleEx`) as a module and an offset. What needs the heap — GDScript's backtrace of the crashing thread,
  and on Windows the minidump's size — is appended last, so a heap too broken for it leaves a complete report.
* **The report**: `crash_<UTC date>_<time>_<process>_<n>.json` — the reason (signal or exception, its code, the
  faulting address, read/write/execute), the time, the uptime, the frame, the thread (main or not), the scene, the
  memory (resident, the engine's, the GPU's), the context (engine and commit, build, OS and version, CPU and cores,
  RAM, locale, GPU, its vendor, API and driver, renderer, display, physics engine, project name and version), the
  frames, the breadcrumbs, the custom values, the log; on Windows `crash_….dmp` beside it (threads, unloaded modules,
  the memory the stacks point to). `tools/fellow_crash/symbolize.py` prints it and symbolizes the frames with the
  build's symbols (`addr2line` for ELF, `llvm-symbolizer` for a PE with its PDB).
* **The game's side**: `add_breadcrumb()`, `set_custom_value()`, `write_report(reason)` (a report without crashing:
  the calling thread's frames and its script's backtrace), `get_pending_report()` (the newest report of an earlier
  run not yet acknowledged) and `acknowledge_reports()`, `get_reports()`, `read_report()`; `max_reports` kept;
  `debug_crash(kind)` crashes on purpose to test the pipeline. Sending is the game's (with the player's consent).

## Checks (cloud: the CPU, Linux, lavapipe)

`tools/fellow_crash/crash_check.py` on the editor build and the release template, each crash in its own process, two
script functions deep after breadcrumbs, two custom values, a printed marker and a warning:

| crash | editor build | release template | the report |
|---|---|---|---|
| a null pointer written | SIGSEGV, 18 frames | SIGSEGV, 18 frames | reason, frames, breadcrumbs, values, marker and warning in the log, the script's backtrace (`_level_two`, `_level_one`), project and version |
| `abort()` | SIGABRT, 21 frames | SIGABRT, 21 frames | as above; libc's frames symbolized: `pthread_kill`, `raise`, `abort` |
| a trap (`CRASH_NOW`) | SIGILL | SIGILL | as above |
| a stack overflow (main thread) | SIGSEGV, 94 frames | SIGSEGV, 94 frames | as above, the script's backtrace too (from the alternate stack) |
| a null pointer on a worker thread | SIGSEGV, 5 frames, not the main thread | the same | as above (no script on that thread) |
| an integer divided by zero | SIGFPE | SIGFPE | as above |
| a null pointer 30 frames into a game (windowed, lavapipe) | frame 28, scene "Harbour" | the same | the GPU (`llvmpipe`), the scene's breadcrumb, 9 MB of video memory |

* Every report valid JSON; the next run found each as pending, and none after acknowledging it.
* The process still ends as before: the editor build through Godot's own handler (its backtrace printed, then
  `abort()`: exit −6), the release template by the signal's default action (−11, −6, −4, −8).
* The reports off: no report, the crash as before. A report the game writes: its reason, 15 frames, its breadcrumbs,
  its value, its script's backtrace (`_write`, `_initialize`).
* The frames' offsets symbolized with `addr2line` against libc's symbols (the engine's binaries here are stripped): the
  arithmetic a symbolized engine frame needs (both position-independent).
* The cost: a printed line 1.9-2.4 µs with the reporter on or off (within the run-to-run spread: writing to the pipe
  dominates; the ring's copy is a `vsnprintf` and a `memcpy`); a breadcrumb 0.45-0.64 µs from GDScript (0.43-0.72 µs
  for the call alone with the reporter off); the reporter's start-up 1.8-2.1 ms (the machine's description, the
  directory).

## Checks on Windows (the laptop, job f397: a release template, MSVC)

`crash_check.py` with a release template of 65dbf7997c on the laptop (Windows 11, MSVC):

| crash | the report | the minidump |
|---|---|---|
| a null pointer written | EXCEPTION_ACCESS_VIOLATION at 0x0 (write), 19 frames, the script's backtrace | 451 KB: 35 threads, 44 modules, the exception's code 0xc0000005 on the faulting thread |
| `abort()` | SIGABRT, 23 frames | 428 KB (no exception: the threads as they were) |
| a trap (`CRASH_NOW`) | EXCEPTION_BREAKPOINT, 19 frames | 451 KB, 0x80000003 |
| a null pointer on a worker thread | EXCEPTION_ACCESS_VIOLATION, 6 frames, not the main thread | 438 KB, 36 threads |
| an integer divided by zero | EXCEPTION_INT_DIVIDE_BY_ZERO, 19 frames | 453 KB, 0xc0000094 |
| a null pointer 30 frames into a game, windowed | frame 29, scene "Harbour", the GPU (NVIDIA GeForce RTX 5070 Ti Laptop GPU) | 702 KB: 63 threads, 107 modules |
| a stack overflow (main thread) | **none**: the process ended with an access violation (fixed below: EXCEPTION_STACK_OVERFLOW, 96 frames, jobs f398-f399) | (after the fix) 8.9 MB: the referenced memory of a deep stack |

Every report found pending by the next run and none after acknowledging it; the reports off: none written; a report
the game writes: as on Linux. The frames are module and offset (a release template has no PDB): the minidump, opened
with the build's PDB, is where Windows' frames are read.

The stack overflow: the filter ran on the overflowed thread with what Windows leaves it (a page or two), and its own
frame (a 4 KiB file buffer for the scripts' backtraces) overflowed again. Fixed: the main thread's stack guarantee
raised to 64 KiB (`SetThreadStackGuarantee`), the filter's frame kept small (the scripts' part in a function of its
own). Jobs f398 and f399 (milestone 98): the stack overflow reported (96 frames, its minidump with the exception);
f398 also found a worker thread's crash unwound to one frame (its unwinder asked for 512 readable bytes above the
stack pointer, past the top of a young thread's stack: 8 now), six again in f399. Every lane passes on Windows.

## Decision

Keep: a shipped game's crash leaves a report it can find and send, with a minidump on Windows.

## Rejected / not done

* **An out-of-process handler** (Crashpad's design: a second process that reads the crashed one's memory): more robust
  against a corrupted process, but a second executable to ship and start; in process, with the heap avoided on the
  fatal path, is Breakpad's in-process mode.
* **Sending the reports**: the game's (an `HTTPRequest` with the player's consent); no server here.
* **Symbol upload and server-side symbolization**: the frames are module and offset; symbolize.py with the same
  commit's symbols.
* **Worker threads' stack overflows**: no alternate stack (Linux) or stack guarantee (Windows) on threads the reporter
  did not create (the process ends as before, without a report).
* **Hangs** (the main thread stuck): decision 0206.
