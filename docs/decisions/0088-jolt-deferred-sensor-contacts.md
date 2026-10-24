# 0088 — Jolt: sensor (Area3D) contacts recorded per thread, evaluated after the step

Status: **KEEP**, on by default (milestone 17) · Branch `fellow/geometry` · Setting `rendering/fellow/physics/jolt_deferred_sensor_contacts` ·
Code: `modules/jolt_physics/spaces/jolt_contact_listener_3d.*`, `fellow_settings.*`.

## Problem

Physics Lab `areas` (300 `Area3D` and 1 000 bouncing spheres) cost Jolt 4.4-4.7 ms per tick on the laptop with its 24
workers, more than GodotPhysics3D (3.8 ms), and less with fewer workers (`m17_f250`: 3.0 ms with one, 2.1 ms with four).
Jolt reports every sensor contact (added or persisted, each step) from the worker thread that found it, and the Godot
integration's `JoltContactListener3D` evaluates it there under one mutex shared by all of them (a lookup in the
overlap set, an insertion when new).

## Change

During the step a sensor contact is appended to the reporting thread's own list (indexed by the worker's index; the
stepping thread has its own); after `PhysicsSystem::Update()`, before the listener's flushes, the lists are evaluated
in turn by the same code. Within a step Jolt reports each sensor pair at most once and removes (`OnContactRemoved()`,
which still updates the sets at once) only pairs it did not report, so the order of evaluation does not change the
overlap sets — in stock it is whatever order the worker threads happen to report in.

A first version (a read-write lock, contacts the area already knew checked under the read lock) was slower:
`areas` 4.4 → 4.5 ms (`m17_f248`), 4.7 → 5.3 ms (`m17_f249`) — Godot's `RWLock` is a `std::shared_timed_mutex`.

## Verification

Physics Lab: the same per-tick hashes (container, laptop); `areas`: the same 8 389 `area_entered` and 7 912 `area_exited`
signals over 600 ticks (container).

## Results

Laptop (`m17_f251`, with decision 0090's job cap at 8): `areas` 2.41 → 2.11 ms per tick; with 0090's default against
stock (`m17_f252`): 3.83 → 2.16 ms (Jolt's Update 3.21 → 1.55 ms; the evaluation after it 0.08 ms).
