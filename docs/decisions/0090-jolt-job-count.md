# 0090 — Jolt: the number of jobs per phase by the work

Status: **KEEP**, on by default (milestone 17) · Branch `fellow/geometry` · Settings `rendering/fellow/physics/jolt_max_jobs`
(-1: by the work), `physics/jolt_bodies_per_job` (128) · Code: `modules/jolt_physics/spaces/jolt_job_system.*`,
`jolt_space_3d.cpp`, `fellow_settings.*`.

## Problem

The Godot integration's job system tells Jolt it can run as many jobs at once as the WorkerThreadPool has threads (24
on the laptop: 8 P- and 16 E-cores), and Jolt splits each phase of its step into that many jobs, each a
WorkerThreadPool task. With the pool limited (`threading/worker_pool/max_threads`, `m17_f250`) small scenes were faster
with fewer: `areas` 4.7 ms per tick with 24, 2.4 with 8, 2.1 with 4; `stack` 0.77 with 24, 0.60 with 8 — while the
2 000-body `pile` wanted more (8: 4.5, 16: 4.5, 24: 5.0).

## Change

`JoltJobSystem::GetMaxConcurrency()` returns a job per 128 active rigid bodies of the stepping space, at least 4, at
most the worker count (`physics/jolt_max_jobs` -1; 0 restores stock, a positive value caps it). Jolt's results do not
depend on the number of jobs: the Physics Lab's hashes are the same with 1, 2, 4, 8, 16 and 24 workers (`m17_f250`) and
with caps of 4, 8 and 16 (`m17_f251`).

## Results

Laptop (`m17_f251`, fixed caps, per tick): `areas` 3.05 ms with one job per worker → 2.11 with 8 (1.94 with 4); `stack`
0.75 → 0.60; `chars` 0.77 → 0.62; `pile` 3.54 → 4.23 with 8 but 3.46 with 16, hence the cap by the work (2 000 bodies:
15 jobs). The adaptive default: `docs/fellow_milestone17_report.md`.
