# FellowEngine — milestone 109 report (parts that hang before they come off)

Status: **IN PROGRESS** (2026-11-13): the owner said "ok kontynuuj". Branch `fellow/geometry`. Decision record:
`docs/decisions/0217-hanging-parts.md`.

## 0. Result

* **Parts that hang**: `FellowVehicleDamage3D.hang_depth` — a part dented that deep hangs from the vehicle on a pin at
  its end farthest from the hit (a bumper hit at its right end swings down from its left end onto the ground) and comes
  off at `detach_depth`; `part_hanging(part, body)`, `is_part_hanging()`; 0 by default.

## 1. Development

`fellow_vehicle_damage_3d.{h,cpp}`, the class reference, `vehicle_check.gd` (the lane `hang`; the strip's rows
`bumper` and `hang` from ahead and to the right of the car's front).

## 2. Verification

Decision 0217's table: the editor build and the release template the same; the other lanes as before.

## 3. Measurements

The laptop (Windows, GodotPhysics3D and Jolt, the pictures on the RTX): next.

## 4. Next

Hinges along a side (doors, a bonnet).
