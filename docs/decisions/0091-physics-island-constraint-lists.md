# 0091 — GodotPhysics3D island generation from contiguous constraint lists

Status: **KEEP**, on by default (milestone 17) · Branch `fellow/geometry` · Setting `rendering/fellow/physics/island_constraint_lists` ·
Code: `modules/godot_physics_3d/godot_body_3d.h` (`fellow_constraints`), `godot_step_3d.*` (`_populate_island()`),
`fellow_settings.*`.

## Problem

Each step GodotPhysics3D gathers its islands serially: a depth-first walk from each active body over its constraint map
(`HashMap<GodotConstraint3D *, int>`), whose iteration follows a linked list of separately allocated elements — a cache
miss per constraint. Physics Lab `pile`: 3.3 ms per tick on the laptop, after decisions 0087 and 0089 the largest serial
phase but one.

## Change

Each body keeps its constraint map's entries also in a `LocalVector`, in the map's iteration order (insertion order: a
constraint added again keeps its place and takes the new index, an erased one leaves; cleared with the map), and the
walk reads that list. The same constraints, the same order, so the same islands in the same order.

## Verification

Physics Lab: the same per-tick hashes in every case (container).

## Results

Container: `pile` islands 7.1 → 2.5 ms per tick, `areas` 1.1 → 0.6 ms. Laptop: `docs/fellow_milestone17_report.md`.
