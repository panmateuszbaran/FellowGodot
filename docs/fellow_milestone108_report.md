# FellowEngine — milestone 108 report (parts that come off)

Status: **FINAL** (2026-11-13): the owner said "ok kontynuuj"; the laptop's job f413 done. Branch `fellow/geometry`. Decision record:
`docs/decisions/0216-detachable-parts.md`.

## 0. Result

* **Parts that come off**: `FellowVehicleDamage3D.detachable_paths` — a part dented `detach_depth` deep (added up over
  impacts, at its point nearest each) falls away as a rigid body of its own (`detach_mass`), not colliding with the
  vehicle; `part_detached(part, body)`; `repair()` puts it back. A bumper: kept after a light hit, off in a 30 km/h
  crash, lying still on the ground by the wall, back on the car after `repair()`.

## 1. Development

`fellow_vehicle_damage_3d.{h,cpp}`, the class reference, `vehicle_check.gd` (the lane `bumper`, a row of the picture
strip).

## 2. Verification

Decision 0216's table: the editor build and the release template the same; the other lanes as before.

## 3. Measurements

The laptop (job f413): the lane passes with GodotPhysics3D and Jolt (decision 0216). Nothing to time: a body made when
a part comes off.

## 4. Next

Hinged parts (doors, a bonnet); the collision shapes following the parts.
