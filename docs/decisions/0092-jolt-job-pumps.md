# 0092 — Jolt: jobs through a lock-free queue drained by a few pump tasks

Status: **REJECT** (left in, off by default; milestone 17) · Branch `fellow/geometry` · Settings
`rendering/fellow/physics/jolt_job_pumps` (false), `physics/jolt_pump_spins` (2000) · Code:
`modules/jolt_physics/spaces/jolt_job_system.*`, `fellow_settings.*`.

## Problem

The Godot integration runs each Jolt job as a WorkerThreadPool task, queued and later freed under the pool's mutex.
Jolt's step creates hundreds of small jobs per tick (barriers, per-island and per-batch jobs); with 24 workers on the
laptop the pool's lock was a candidate for the remaining overhead of small scenes after decisions 0088 and 0090.

## Change

Ready jobs go to a bounded multi-producer multi-consumer queue (Vyukov's, 4 096 cells); up to `GetMaxConcurrency() - 1`
"pump" tasks run jobs from it until it has stayed empty for `physics/jolt_pump_spins` pauses, and are started again
when jobs are queued while fewer pumps run. Jolt's barrier still runs ready jobs on the stepping thread. The jobs and
their dependencies are Jolt's own, so the results do not change.

## Verification

Physics Lab, Jolt, all five cases: the same per-tick hashes as without (container, also with 8 workers on 2 cores).

## Results

Laptop (`m17_f253`, `fellow188`, 600 ticks, median of two runs per variant), ms per tick:

| Jolt case | pumps | WorkerThreadPool tasks | pumps, 500 spins | pumps, 8 000 spins |
|---|---:|---:|---:|---:|
| `pile` | 3.52 | 3.57 | 3.34 | 3.62 |
| `stack` | 0.67 | 0.63 | 0.67 | 0.74 |
| `chars` | 0.61 | 0.62 | 0.65 | 0.70 |
| `rays` | 4.10 | 4.04 | 4.03 | 4.06 |
| `areas` | 2.08 | 2.20 | 2.06 | 2.09 |

No consistent gain (−5 % to +6 % by case, within the run-to-run spread); the pool's lock is not where Jolt's
small-scene time goes. (`chars` and `rays` were measured on the Physics Lab terrain that faced down; see milestone 17's
report.) Off by default; the code is kept for a later look at the physics thread
(`physics/3d/run_on_separate_thread`), which was not measured.
