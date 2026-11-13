# 0214 — Windows: a refused connection seen at once

Status: **KEEP** (a fix: Windows builds only) · Branch `fellow/geometry` · Code: `drivers/windows/net_socket_winsock.cpp`
(marked, a stock file) · Checks: `tools/fellow_crash/crash_check.py` (the upload lanes `unreachable` and
`localhost`) and a probe of `StreamPeerTCP` (job f411).

## Problem

Decision 0210's uploads met it first: on Windows a TCP connection to a port nothing listens on — a collector that is
down, `localhost` whose IPv6 address Windows tries first while the server listens on IPv4 — was reported only when
`network/limits/tcp/connect_timeout_seconds` ran out (30 s), not when the other side refused it. Every TCP client of
Godot's on Windows waits so: `HTTPRequest`, `HTTPClient`, `StreamPeerTCP`, a WebSocket's connection; `localhost`
servers (a game's own launcher, a local tool, a test) took 30 s to reach.

## Change

Godot polls a connection in progress by calling `connect()` again on the non-blocking socket; Winsock reports a
refused attempt as the socket's error (`select()`'s exception set, `SO_ERROR`), and another `connect()` after it starts
a new attempt instead of failing. `NetSocketWinSock::connect_to_host()` now looks at the socket's exception set first
(a zero timeout): with an error pending the connection has failed (`FAILED`, the error printed with `--verbose`).

## Checks (the laptop, Windows 11, a release template, MSVC)

| | before (jobs f403-f410) | after (job f411) |
|---|---|---|
| `StreamPeerTCP` to 127.0.0.1:9 (nothing listening) | refused at the 30 s timeout | refused (WSAECONNREFUSED, 10061) after 2.05 s (Windows' own SYN retries) |
| the same to ::1:9 | 30 s | 2.05 s |
| decision 0210's unreachable collector (two reports, the second not tried) | 30.0 s | 2.05 s |
| an upload to `http://localhost:<port>` (the collector on IPv4 only) | 30.0 s (or cut off) | sent after 2.08 s (::1 refused, then 127.0.0.1) |
| the other upload lanes (refused, accepted, TLS, on their own) | as before | as before |

Linux: untouched (its sockets report a refused attempt from `connect()`; the lanes 14-16 ms).

## Decision

Keep: Godot's TCP clients on Windows learn of a refused connection when it happens.

## Rejected / not done

* **A shorter connection timeout**: a slow network deserves its 30 s; only a refusal is known sooner.
* **Trying a host's IPv4 and IPv6 addresses at once** (Happy Eyeballs): `HTTPClient` tries them in turn, now 2 s
  apart at most on Windows.
