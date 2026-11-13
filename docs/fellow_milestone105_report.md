# FellowEngine — milestone 105 report (vehicles dented where they hit)

Status: **IN PROGRESS** (2026-11-13): the owner said "ok kontynuuj". Branch `fellow/geometry`. Decision record:
`docs/decisions/0213-vehicle-damage.md`.

## 0. Result

* **`FellowVehicleDamage3D`**: a vehicle's body meshes dented where its contacts' impulses exceed a threshold — into
  the body, deeper the harder, falling off over 0.7 m, at most 0.3 m; `repair()`, `get_damage()`, `impacted`.
* **Checked**: a car into a wall at 30 km/h (the front 9.3 cm dented, the rear not), at 5 km/h (nothing), a car over
  ten bumps (nothing).

## 1. Development

`modules/fellow_world/fellow_vehicle_damage_3d.{h,cpp}` (new), `register_types.cpp`, `config.py`,
`icons/FellowVehicleDamage3D.svg`, the class reference; `benchmarks/project/bench/vehicle_check.gd` (the lanes, the
crash in the picture strip); the editor check's icon list.

## 2. Verification

* Decision 0213's table (GodotPhysics3D, the editor build).
* The editor check: 37 icons, no warning.
* The laptop (job f409): GodotPhysics3D value for value; Jolt the front 9.6 cm (three impacts), nothing at 5 km/h or
  over the bumps. The picture strip's crash camera moved in front of the car (it showed its side): next job.

## 3. Measurements

The crash's four dents 0.31 ms together; nothing between impacts.

## 4. Next

Damage changing how the car drives; parts coming off.
