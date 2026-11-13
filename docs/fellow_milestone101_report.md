# FellowEngine — milestone 101 report (wheels that meet the ground with their shape)

Status: **FINAL** (2026-11-13): the owner said "ok kontynuuj"; the laptop's job f402 done. Branch `fellow/geometry`. Decision record:
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
* The laptop (job f402, a release template, MSVC): the nineteen lanes on both engines the same, value for value (a
  curb's step 0.046/0.048 m, the trench's drop 0.015/0.020 m with cylinders; 0.120 and 0.085 m on rays); 64 cars
  upright with rays and with cylinders on both engines; the picture strip as before.

## 3. Measurements

A car's step with cylinder wheels, `--many=64`: the cloud 9 → 32 µs with GodotPhysics3D, 11.5 → 87 µs with Jolt; the
laptop 8.1 → 34.5 µs and 7.9 → 59.4 µs (64 cars' vehicles 0.52 → 2.21 ms and 0.51 → 3.80 ms a frame).

## 4. Next

Jolt's own shape cast for the wheels (one sweep instead of a bisection).
