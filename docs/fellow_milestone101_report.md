# FellowEngine — milestone 101 report (wheels that meet the ground with their shape)

Status: **IN PROGRESS** (2026-11-13): the owner said "ok kontynuuj". Branch `fellow/geometry`. Decision record:
`docs/decisions/0209-shape-cast-wheels.md`.

## 0. Result

* **Shape-cast wheels** (`FellowWheel3D.contact_shape`: a sphere or a cylinder of the tyre swept down its
  suspension): a curb is met by the tyre's front and climbed over several steps (the suspension's largest change in a
  step 0.12 → 0.05 m, the body's jolt 0.70 → 0.47-0.53 g), a trench narrower than the tyre bridged (a drop of 0.085 →
  0.015-0.021 m). Rays stay the default (traffic); shapes for the player's car.
* **Checked**: four lanes and their comparison on both physics engines and both builds; the earlier fifteen the same.

## 1. Development

`modules/fellow_world/fellow_vehicle_3d.{h,cpp}` (`contact_shape`, `width`, the sweep), `doc_classes/FellowWheel3D.xml`;
`benchmarks/project/bench/vehicle_check.gd` (the lanes, `--contact` with `--many`).

## 2. Verification

* Decision 0209's table.
* Found on the way: GodotPhysics3D's shape casts against the bench's 2 km ground box are erratic (steady on slabs; Jolt
  steady on both) — the lanes run on slabs, the class reference says so.
* The laptop: next job.

## 3. Measurements

A car's step with cylinder wheels (the cloud): 9 → 32 µs with GodotPhysics3D, 11.5 → 87 µs with Jolt.

## 4. Next

Jolt's own shape cast for the wheels (one sweep instead of a bisection).
