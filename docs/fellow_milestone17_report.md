# FellowEngine — milestone 17 report (physics and collisions)

Status: **DONE** (`fellow183`-`fellow187`, laptop measurements `m17_f247`-`m17_f252`, 2026-10-24/25). Branch
`fellow/geometry`. Benchmark machine as before (Acer Predator PHN16S-71, Core Ultra 9 275HX: 8 P- and 16 E-cores, 24
workers in Godot's WorkerThreadPool; Windows 11). Decision records `docs/decisions/0085`-`0091`; user-facing summary
`docs/FELLOWENGINE_OPTIONS.md` ("Exact options", Physics).

## 0. Result

Both of Godot's 3D physics engines are faster in the fork, and **the simulation is stock's, tick by tick**: in every case
of the new Physics Lab, with both engines, the physics state hashed every 30 ticks is the same for stock Godot 4.7.2,
the fork with its physics changes off, and the fork as shipped (`m17_f252`; also with 1 to 24 workers, `m17_f250`).

Laptop, headless, one physics tick per frame, 600 ticks, median of two runs per variant in alternating order, ms per
tick (stock: the frame; the fork: the frame, which is its physics plus about 0.1-0.2 ms) — `m17_f252`:

| case | GodotPhysics3D stock | fork | | Jolt stock | fork | |
|---|---:|---:|---:|---:|---:|---:|
| `pile` — 2 000 mixed bodies falling into a bin | 25.58 | 13.09 | **× 1.95** | 3.63 | 3.40 | −6 % |
| `stack` — 25 towers of 16 boxes, knocked down by balls | 1.41 | 1.09 | −22 % | 0.74 | 0.69 | −7 % |
| `chars` — 250 `CharacterBody3D` (`move_and_slide()`) on a trimesh terrain | 22.22 | 21.10 | −5 % | 0.78 | 0.67 | −15 % |
| `rays` — 1 500 `RayCast3D` and 150 `ShapeCast3D` on moving rigs | 79.29 | 18.78 | **× 4.2** | 4.05 | 4.07 | = |
| `areas` — 300 `Area3D` monitoring 1 000 bouncing spheres | 3.88 | 2.65 | −32 % | 3.84 | 2.24 | **−42 %** |

Jolt stays the faster engine everywhere (GodotPhysics3D's `chars` is 30 × Jolt's: its `move_and_slide()` is SAT tests,
below). The changes, every one exact:

* **GodotPhysics3D queries (0085):** `cast_motion()` (ShapeCast3D) and `test_body_motion()`'s motion phase
  (`move_and_slide()`) ask GJK only yes/no questions, ten per object, and stock gives it every trimesh face in the whole
  cast's box. Faces a double-precision separating-axis test proves out of reach (1 mm + 1e-5 of the coordinates; GJK's
  tolerance is 1e-4) are not given to it; those kept go most-overlapped first. 50-66 million skipped faces were given to
  GJK anyway by `physics/verify`: none touching.
* **GodotPhysics3D SAT (0086):** a convex polygon's (or a face's) vertices transformed once per test, not once per axis.
* **GodotPhysics3D step:** the broadphase's culls on worker threads (0087), a large island solved on every thread with
  each body updated in stock's order (0089), the islands gathered from contiguous lists (0091). `pile`'s step phases:

  | ms per tick | broadphase | islands | pair setup | solve | integration | sync, scripts |
  |---|---:|---:|---:|---:|---:|---:|
  | fork, changes off | 6.65 | 2.64 | 3.56 | 9.89 | 1.45 | 1.64 |
  | fork | 2.23 | 1.34 | 2.47 | 3.87 | 1.43 | 1.60 |

* **Jolt (0088, 0090):** Area3D contacts recorded per thread instead of under one mutex from every worker; the jobs per
  phase of Jolt's step by the work (one per 128 active bodies, at least 4) instead of one per worker — small scenes were
  faster with fewer (`areas` 4.7 ms with 24, 2.1 with 4-8), the 2 000-body pile with more. Jolt's results do not depend
  on the job count.

## 1. Physics Lab and the measurement

* `benchmarks/project/scenes/physics_lab.gd` — the five cases above, deterministic (seeded; inputs depend only on the
  tick), with `--physics_case`, `--physics_scale`, `--physics_visuals=0`.
* The bench's `--mode=physics` steps `--ticks` ticks, records each tick's cost by phase and a SHA-256 of the scene's
  physics state every `--hash_every` ticks: every body's transform; the rays' hit points and the shape casts' counts and
  safe fractions. FellowEngine counters (`Main::iteration()`) time the tick's phases: sync (bodies' state to their
  nodes), `_physics_process()`, the step, the rest; GodotStep3D's and JoltSpace3D's steps are timed by phase too.
* `benchmarks/physics_bench.py` runs variants (builds, toggles, start-up overrides such as the worker count) × engines ×
  cases in alternating order and compares their hashes with a reference.

Stock's GodotPhysics3D and Jolt are deterministic in these cases (the same hashes run to run); the fork's hashes equal
stock's in all ten engine × case pairs from the first laptop job (`m17_f247`) on, and are the same with 1, 2, 4, 8, 16
and 24 workers (`m17_f250`). The
container measurements (2 cores) are within ± 10 % between runs of one binary, so the per-change numbers in the
decision records come from profiles and laptop runs.

## 2. What the profiles showed

* **`rays`, GodotPhysics3D, 84 ms per tick:** 80 % in GJK under `cast_motion()`: for a 25 m diagonal sweep of a 0.5 m
  sphere stock culls the terrain with the box of the whole cast for all ten tests (motion, rest, eight bisection steps),
  about a hundred faces each, nearly all far from the swept sphere. After 0085 the 1 500 raycasts are most of what is
  left (the broadphase's segment culls, 20-30 %).
* **`chars`, GodotPhysics3D, 22-28 ms:** 41 capsule-against-face SAT tests per character per tick (the recovery loop and
  the rest phase of each `test_body_motion()`; `move_and_slide()` calls it several times), 14 axes each on average,
  60 % of them all 19 axes (about half of those end without a contact). The motion phase that 0085
  speeds up is about a quarter. The SAT's axes must all be tested for a touching face (the deepest axis decides the
  contact), so the exact gains stop at 5 %.
* **`pile`, GodotPhysics3D:** the SAT tests with convex polygons (22 % in `project_range()`, every vertex transformed
  for every axis), the solver, the broadphase, the island walk. One island of about 4 600 constraints: stock solves it
  on one thread (74 000 `solve()` calls per tick).
* **Jolt:** its own code (the contact solver, collision detection); the Godot integration's share is small — except
  that it makes Jolt split every phase into 24 jobs on this machine, and its contact listener's mutex.

## 3. GodotPhysics3D's island solve (0089)

The constraint solver is sequential impulses: 16 passes over the island's constraints in the order they were gathered.
Reordering it changes the results; but two `solve()` calls on disjoint dynamic bodies commute exactly (each touches only
its own bodies' velocities and its own impulses — stock already relies on that to solve separate islands at once). So
the fork keeps stock's sequence as a dependency graph: each call waits only for the previous call on each of its dynamic
bodies. The island's calls are sorted by dependency level within an iteration and taken from one queue by the worker
threads and the stepping thread, 16 at a time (8-128 measured, `m17_f251`); nothing waits for a later call, so any
number of threads finishes. `pile`: one island, about 600 dependency levels deep over its 16 iterations; the solve
9.9 → 3.9 ms on the laptop (`m17_f252`). It scales to about 8 workers (`m17_f250`: 1 thread 11.7 ms, 2 workers 5.4, 4: 4.1, 8: 3.4,
16: 3.2, 24: 4.4) — the E-cores, and the bodies' state moving between cores with each call.

## 4. Tried and not kept

* A per-thread cache of transformed convex vertices inside `project_range()` (keyed by mesh and transform): its lookup
  cost more than it saved (13 % of `pile`); replaced by 0086's transform per test.
* A read-write lock in Jolt's contact listener (contacts the area knew, checked under the read lock): slower (Godot's
  `RWLock` is a `std::shared_timed_mutex`); replaced by 0088's per-thread lists.
* Jolt's motion queries with a tighter per-triangle test (a separating axis across the motion, as 0085, through a hook
  in Jolt's `CollideConvexVsTriangles`): no gain — the Godot integration already bounds each bisection step by its own
  motion, not the whole cast's — reverted, Jolt's sources left as upstream.

## 5. Open

* GodotPhysics3D's `move_and_slide()` (`chars`): the SAT tests themselves; a faster exact SAT (inlined projections,
  fewer recomputed transforms) or fewer tests (the recovery loop's iterations repeat them on a body moved by a few
  millimetres) would be the next step; anything that changes the axes' order changes contacts.
* GodotPhysics3D's raycasts: the broadphase's segment cull (an exact vectorised slab test).
* The parallel island solve's scaling beyond 8 workers (locality: a thread keeping its region of the island).
* The physics thread (`physics/3d/run_on_separate_thread`) and scenes with rendering on were not measured.

## 6. Next

The non-ray-traced look features listed in milestone 16's report (a physically based sky with aerial perspective,
volumetric clouds, histogram and local exposure, a sharper TAA, contact shadows per light, an Unreal-style bloom), or
more physics (above), as the user decides.
