# 0210 — Crash reports sent to a collector

Status: **KEEP** (`FellowCrashReporter.upload_reports()`; `debug/fellow/crash_reports/upload_url`, empty, and
`upload_automatically`, off) · Branch `fellow/geometry` · Code: `modules/fellow_diagnostics/fellow_crash_reporter.{h,cpp}`,
`modules/gdscript/gdscript.cpp` (marked: a fix found on the way) · Checks: `tools/fellow_crash/crash_check.py` (six
upload lanes, a collector on the machine).

## Problem

Decisions 0205 and 0206 write a report when the game crashes or hangs, and the next run finds it — then it is the
game's to send, with an `HTTPRequest` it builds itself: a multipart body, the minidump beside the report, retries when
the player is offline, what was sent and what was not. Every shipped game needs the same few hundred lines, and the
crash services studios use (Sentry, BugSplat, Backtrace, Crashpad's own servers) take minidumps one way:
`multipart/form-data` with the dump in `upload_file_minidump` and the rest as fields. The readiness assessment's
diagnostics row listed "reports kept on the machine; sending them the game's".

## Change

`FellowCrashReporter.upload_reports(url = "")` sends the reports waiting in the directory to `url` (empty:
`debug/fellow/crash_reports/upload_url`) on a thread of its own and returns at once:

* **A report a request**, oldest first: a POST of `multipart/form-data` — `report` (the JSON, as a file),
  `upload_file_minidump` (its minidump on Windows, with `upload_minidumps`, on), and the fields `kind`, `reason`,
  `project`, `version` and `commit` (what a collector indexes by without opening the JSON). HTTP or HTTPS through
  Godot's `HTTPClient`; HTTPS checks the collector's certificate against Godot's bundle (or
  `network/tls/certificate_bundle_override`).
* **Accepted** (a 2xx response): the report and its minidump move into the directory's `sent/` folder (the newest
  `max_reports` kept there too), so the next run neither finds them pending nor sends them again. **Refused or not
  answered**: they stay for the next try. The signal `report_uploaded(path, ok, response_code)` for each — the HTTP
  code, or a negative one when there was none (the URL, the host not found, no connection, the TLS handshake failed,
  no response within 30 s and a second for each 100 KB) — and `uploads_finished(sent, failed)` at the end;
  `is_uploading()`.
* **On its own**: `upload_automatically` (off) starts it two frames after the game starts. Off by default: what a
  report holds (the log's lines, the game's values, the machine) is the player's to agree to send.

Nothing runs between uploads; the thread is joined when the reporter is destroyed (an upload under way is cancelled).

## Found on the way: a lambda connected to a singleton's signal crashed the game at exit (stock GDScript)

The check's script connects lambdas to the reporter's signals; every such run ended in `malloc_consolidate(): unaligned
fastbin chunk detected` (glibc's heap corrupted) after the game's last frame. Valgrind showed a lock on a freed
`GDScript`'s mutex. At exit `ScriptServer::finish_languages()` drops the scripts' caches, so a lambda still connected to
an object destroyed later — a module's singleton (`FellowCrashReporter`, `NavigationServer3D`: both crashed in the same
way) — holds its script's last reference; destroying the lambda releases the script first and then the lambda's
function pointer (declared before the reference) unregisters itself from the freed script under its mutex. Stock Godot
4.7.2's `GDScript::~GDScript` now lets go of the lambdas still registered (`list_element` cleared), so a lambda that
outlives its script is destroyed without touching it (marked in `modules/gdscript/gdscript.cpp`). The two singletons'
runs with a lambda connected end cleanly; Valgrind: no errors.

## Checks (cloud: the CPU)

`crash_check.py`, the editor build and the release template the same: two reports written (a crash and the game's
own) and a collector on the machine (Python's HTTP server, answering 200 or 500, parsing each body):

| lane | expected | result |
|---|---|---|
| refused (500) | both tried, oldest first, both kept | codes 500, 500; `uploads_finished(0, 2)`; both still there |
| unreachable (a closed port) | both kept, no response | codes −3, −3; `(0, 2)`; both still there |
| accepted (200) | both sent, oldest first, moved to `sent/` | codes 200, 200; `(2, 0)`; none left, both in `sent/` byte for byte |
| what the collector received | the five fields, the report as a file | `kind` crash / report, `reason` SIGABRT / a soft failure, `project` Crash Check, `version` 1.2.3, `commit` the build's; `report` the file, `application/json`, its name, byte for byte |
| TLS, the certificate trusted (a self-signed one for `localhost` the project bundles) | sent | 200, `(1, 0)` |
| TLS, the same collector, the certificate not trusted | refused at the handshake, nothing received | −4, `(0, 1)`, kept; the collector received nothing |
| on their own (`upload_automatically`, `upload_minidumps` off) | the waiting report sent at the start, without its minidump | `(1, 0)`; fields and `report` only |

Each run ends with exit code 0 (the lambdas' fix). Two reports to the collector on the machine: 14-22 ms from the call
to `uploads_finished` (the thread started, two requests). The earlier lanes (seven crashes, two hangs, the game's
report, off, the cost): the same results.

## Cost

None while no upload runs (no thread, no timer); `auto_upload` a check on the second frame.

## Decision

Keep: a game's crash and hang reports reach the studio, in the form crash services take.

## Rejected / not done

* **Retries with back-off within a run**: a refused report waits for the next `upload_reports()` (the next start with
  `upload_automatically`).
* **Compression** (gzip bodies): a report is 3-4 KB; minidumps are what collectors decompress themselves.
* **Symbol upload** (the build's PDBs or debug files to the service): the build pipeline's, with the service's tools.
* **Sentry's own envelope format** (its JSON events): its minidump endpoint takes this form; the JSON report rides as
  an attachment.
