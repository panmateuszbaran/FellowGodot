# FellowEngine — milestone 106 report (Windows: a refused connection seen at once)

Status: **FINAL** (2026-11-13): the owner said "ok kontynuuj"; the laptop's job f411 done. Branch `fellow/geometry`.
Decision record: `docs/decisions/0214-windows-refused-connections.md`.

## 0. Result

* **A stock Windows bug fixed**: a TCP connection refused by the other side (nothing listening) was reported only at
  the 30 s connection timeout; now when Windows gives up on it (2.05 s, its own retries). Every Godot TCP client on
  Windows (HTTPRequest, HTTPClient, StreamPeerTCP, WebSockets) and decision 0210's crash report uploads.

## 1. Development

`drivers/windows/net_socket_winsock.cpp` (marked); `tools/fellow_crash/crash_check.py` (the unreachable collector
within 10 s, an upload to `localhost`).

## 2. Verification

Decision 0214's table (the laptop: a probe of `StreamPeerTCP` to 127.0.0.1 and ::1, the upload lanes); the cloud's
upload lanes unchanged (Linux).

## 3. Measurements

Refused: 30 s → 2.05 s; `localhost` with its IPv6 address refused: 30 s → 2.08 s.

## 4. Next

Trying a host's addresses at once (Happy Eyeballs).
