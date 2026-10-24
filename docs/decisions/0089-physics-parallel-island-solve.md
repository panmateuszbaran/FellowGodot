# 0089 — GodotPhysics3D: a large island solved on every thread, each body updated in stock's order

Status: **KEEP**, on by default (milestone 17) · Branch `fellow/geometry` · Settings `rendering/fellow/physics/parallel_island_solve`,
`physics/parallel_island_solve_grab` (16) · Code: `modules/godot_physics_3d/godot_step_3d.*`, `godot_body_3d.h`
(`fellow_slot*`), `fellow_settings.*`.

## Problem

GodotPhysics3D solves its constraint islands in parallel, one island per task; an island is solved sequentially:
`iterations` (16) passes of `solve()` over its constraints in the order the island was gathered (sequential impulses,
Gauss-Seidel). Everything touching in a pile is one island: Physics Lab `pile` gathers one island of about 4 600
constraints, 74 000 `solve()` calls per step on one thread — 10-11 ms of the laptop's 27.6-29.2 ms step (`m17_f248`).

## Change

A `solve()` reads and writes the velocities of its constraint's bodies and its own impulses, nothing else (stock already
relies on that to solve islands concurrently). Two `solve()` calls on disjoint dynamic bodies therefore commute exactly
(static and kinematic bodies are only read). So the island's sequence — every iteration's pass, in stock's order — is
executed as a dependency graph: each operation waits only for the previous operation, in the sequence, on each of its
dynamic bodies. Every body then sees exactly stock's updates in stock's order; operations on disjoint bodies run on
different threads at once.

* Per island and step (on the stepping thread): the dynamic bodies get slots; one iteration's operations get dependency
  levels (one past the deepest earlier operation on their bodies) and are sorted by level, stably; each position records
  the operation it waits for on each body — in the same iteration, or the body's last one in the previous iteration.
* The worker threads and the stepping thread take operations from one queue (16 at a time; 8, 32, 64 and 128 were slower on the laptop, `m17_f251`) in that order — iteration by
  iteration, level by level — wait (spinning, then yielding) for the operations their bodies depend on, run `solve()`,
  mark the operation done. Nothing waits for a later operation, so any number of threads, even one, finishes.
* Islands of at least 256 constraints, one after another, before the other islands, which are solved as in stock (the
  order between islands, which share no body, changes nothing). Soft bodies and constraints with more than two bodies:
  stock.

## Verification

Physics Lab `pile` (and the other cases): the same per-tick hashes as stock (container and laptop); also with 8 workers on
the container's 2 cores, and with 1, 2, 4, 8, 16 and 24 workers on the laptop (`m17_f250`).

## Results

Laptop (`m17_f249`): `pile` solve 11.2 → 4.4 ms per tick (the island: 16 × 4 600 operations, about 600 dependency levels
deep); the step 29.2 → 15.8 ms with decisions 0085-0087. The solve scales to about 8 workers (3.4 ms; 16: 3.2 ms; 24:
4.4 ms — the E-cores, and the bodies' cache lines moving between cores with each operation).
