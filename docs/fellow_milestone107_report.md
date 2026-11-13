# FellowEngine — milestone 107 report (dents that reach the running gear)

Status: **FINAL** (2026-11-13): the owner said "ok kontynuuj"; the laptop's job f412 done. Branch `fellow/geometry`. Decision record:
`docs/decisions/0215-running-gear-damage.md`.

## 0. Result

* **A car hit hard drives differently**: `FellowVehicleDamage3D.wheel_bend` bends a wheel near a dent out of line (a
  side hit ahead of the front-left wheel: its toe +2.5°, the car pulls 24.5° to the right in 6 s at 40 km/h with the
  steering straight); `engine_power_loss` takes the engine's power when a dent reaches it (a front-end hit: 74 % of the
  torque, 0-100 km/h 6.27 → 7.63 s). Both 0 by default; `repair()` puts them back.
* **`FellowWheel3D.toe`**: a wheel's alignment, a setting of its own.

## 1. Development

`fellow_vehicle_damage_3d.{h,cpp}` (the reach of an impact, the wheels' bends, the engine's dent),
`fellow_vehicle_3d.{h,cpp}` (the toe in the step; the vehicle's wheels for the damage node), the class reference,
`vehicle_check.gd` (five lanes, their comparison, a row of the picture strip).

## 2. Verification

Decision 0215's table: the editor build and the release template the same, value for value; the other lanes as
before; the regression scene `vehicle` exact. The laptop (job f412): every lane with GodotPhysics3D and Jolt, the same
values; the picture strip's row of the bent car beside the unhit one.

## 3. Measurements

Nothing to time apart from decision 0213's dents (a loop over the wheels at an impact; one comparison a wheel in the
vehicle's step).

## 4. Next

Camber and suspension damage; parts coming off.
