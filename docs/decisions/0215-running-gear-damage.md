# 0215 — Dents that reach the running gear

Status: **KEEP** (two amounts on `FellowVehicleDamage3D`, both 0 by default: the dents stay the body's) · Branch
`fellow/geometry` · Code: `modules/fellow_world/fellow_vehicle_damage_3d.{h,cpp}`, `fellow_vehicle_3d.{h,cpp}`
(`FellowWheel3D.toe`) · Checks: `benchmarks/project/bench/vehicle_check.gd` (five lanes and their comparison).

## Problem

Decision 0213 dents a car's body where it hits, but a car hit hard drives as it did: the dents are the meshes'. In the
genre's games damage is felt as well as seen — a bent wheel that pulls the car to one side (BeamNG's, Forza's
"alignment" damage), an engine that has lost power after a front-end crash (Forza's simulation damage, GTA's smoking
engine). Decision 0213's `get_damage()` left it to the game, which would have to guess where each part is.

## Change

* **`FellowWheel3D.toe`** (degrees, + the wheel's front towards the vehicle's centre line): the wheel's alignment, added
  to its steering angle in the vehicle's step (its tyre's slip, its node's turn). A setting of its own (a car's setup:
  toe-in steadies it); 0 by default, the step unchanged.
* **How far an impact reaches**: the vertices' falloff over `radius`, the distance along the push counted half — an
  impact reaches into the body twice as far as it spreads over it (a front-end hit reaches the engine behind the
  bumper, not the rear wheels; a hit by the wheel reaches it).
* **`wheel_bend`** (degrees, 0: off): a dent reaching a wheel turns it about its upright the way the push would — a
  push's arm about the wheel's centre (from above), over the wheel's radius, between −1 and 1 — times the dent's depth
  over `max_depth`, times the reach; up to `wheel_bend` in all. A push from the side ahead of the wheel's centre turns
  its front in; one from the front outside the centre turns it out.
* **`engine_power_loss`** (0-1, 0: off) at **`engine_position`** (the vehicle's space; 0, 0.5, −1.4 by default): a dent
  reaching the engine takes `engine_power_loss` × its depth over `max_depth` of `max_torque` (the dents add up to
  `max_depth`). `get_engine_power()`.
* `repair()` puts the toe and the torque back (unless the game has set them since); `get_wheel_toe_change()`,
  `get_stats()` (`engine_power`, `wheel_toe_change`).

## Checks (cloud: the CPU, GodotPhysics3D; the editor build and the release template the same)

`vehicle_check.gd`, five cars with `wheel_bend` 5° (`bent_off`: 0) and `engine_power_loss` 0.5, hit at rest by
`apply_impact()` max_depth deep (by the left 20 cm outside and 35 cm ahead of the front-left wheel's centre, or at the
front's middle), then 6 s at 40 km/h with the steering at 0 (the engine lane at full throttle):

| lane | expected | result |
|---|---|---|
| straight (unhit) | heading within 0.5° | 0.00°, 0 m sideways |
| bent | the front-left wheel's toe in by 1-5°, the others 0; turned right by more than 3° | toe +2.52° (the others 0); 24.5° to the right, 11.5 m sideways; the engine 0.96 (the hit's edge reached it) |
| bent_off (`wheel_bend` 0, the same hit) | as straight within 0.2°, toe 0 | 0.00° (the same as straight), toe 0 |
| engine (the front's middle) | power < 0.85, `max_torque` 400 × it, slower to 100 km/h than the accel lane's car by more than 10 %, straight | power 0.740, 296.1 N·m; 7.63 s against 6.27 s (+22 %); 0.00° |
| repaired (both hits, `repair()` 0.1 s later) | as straight within 0.2°, toes 0, 400 N·m | 0.00°, toes 0 (+2.52 before), 400 N·m (280.1 before) |

The other 21 lanes the same values as before (their cars' amounts 0, their toes 0); the picture strip's new row
(`bent`, from 30 m above with the unhit car beside it) shows its path pulled to the right.

## Cost

A loop over the wheels at an impact (not measured apart from the dents' 0.31 ms); the vehicle's step one comparison a
wheel.

## Decision

Keep: a hard hit can bend a wheel out of line or take the engine's power, as the game sets.

## Rejected / not done

* **Bent suspension (camber, a shorter spring), a broken steering rack, a punctured tyre, a radiator that overheats**:
  toe and power only; the game can change the rest from `impacted`.
* **Parts coming off** (a bumper, a door): the body's meshes are dented, not split.
* **Damage from the collision shapes' own deformation**: the shapes are not changed (decision 0213).
