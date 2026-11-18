# 0275 — Two-wheelers: FellowVehicle3D balanced by its rider

Status: **KEEP** (opt-in per node) · Branch `fellow/geometry` · Code: `modules/fellow_world/fellow_vehicle_3d.{h,cpp}`
(`balance`, `max_lean`, `is_fallen()`; `get_stats()`'s `lean_deg`, `lean_target_deg`, `fallen`), the FellowEngine
menu's "Add Motorcycle" (`modules/fellow_editor/fellow_editor_plugin.cpp`) · Checks: `bench/vehicle_check.gd` (six new
lanes, `--many=32 --bike=1`) on GodotPhysics3D and Jolt, the editor check's presets.

## Problem

The readiness assessment's other vehicle gap beside tracks (decision 0274): no two-wheelers. `FellowVehicle3D` on two
wheels in line falls over as soon as anything pushes it — a bicycle or motorcycle stays up only because its rider
balances it — and its steering turns the front wheel as a car's does, where a two-wheeler turns by leaning.

## Change

With `balance` on:

* **The steering asks for a lean** to the side it steers: `steering · atan(cap)`, `cap` the least of
  `tan(max_lean)` (45°), 0.8 of the wheels' mean grip (times the ground's friction) and what the front wheel's lock
  can turn at the speed (`v²·tan(max_steer) / (wheelbase·g)`: at a walking pace hardly any lean). None going
  backwards.
* **The rider holds the body's roll there**: a torque about the body's forward axis (horizontal), a critically damped
  spring of 2 Hz on the roll's error and rate, its inertia the body's about that axis plus its mass at its height above
  the contacts' line (it rolls about the ground, not its centre of mass). Applied in every substep, the roll carried
  forward by its rate. What a rider's weight and countersteering achieve, not their dynamics.
* **The front wheel turns to the turn the body's lean balances**: `atan(wheelbase·g·tan(lean) / (v²·cos(lean)))` —
  the wheel turns about the leaned body's axis, so on the ground its angle is `cos(lean)` of it; at a walking pace
  (blended over 1-3 m/s) the steering's own angle.
* **Upright at a standstill** (the target 0: the rider's feet); **past `max_lean` by 25°** the rider lets it fall
  (`is_fallen()`) until it is back within `max_lean`.
* Two wheels are enough with balance on (the configuration warning asks for three without it).

## Result

`vehicle_check.gd`'s motorcycle (260 kg with its rider, the centre of mass 0.65 m up, wheels 1.45 m apart, 100 N·m),
the same on GodotPhysics3D and Jolt:

| Lane | Result |
|---|---|
| straight at 0.5 throttle, pushed from the side at 3 s (60 N·s, 1 m up) | leaned 1.45° at most, upright 2 s later (0.00°), heading 0.48° |
| the same push without balance | fell over (103°) |
| 60 km/h, half steering | lean 19.35° (asked 19.33°), 0.352 g (tan 0.351), radius 74.7 m |
| 40 km/h, full steering | lean 38.66° (asked 38.66°), 0.826 g (tan 0.800), radius 13.8 m |
| 5 km/h, full steering | lean 4.33°, radius 2.6 m (the lock) |
| braked from 58 km/h | 13.2 m, upright at the standstill (0.00°) |

32 motorcycles driving circles: 6.3-6.4 µs a step each (a car 12.8 µs). Each of the car's lanes and the tracked
vehicle's as before (the car's every value the same but the dents' timing).

## Decision

KEEP, opt-in (`balance` off: as before).

## Rejected / not done

* **The steering as the front wheel's angle, the lean from the turn** (a rider leaning to whatever the steering
  turns): at speed a small steering asks for more than the grip holds, and the front washes out; the lean is what a
  rider controls, the steering follows it.
* **The front wheel turned by the turn's geometry alone** (`atan(wheelbase·g·tan(lean)/v²)`): the turn fell short of
  the lean by `cos(lean)` (0.615 g at 37°, which balances 0.754 g) — the wheel turns about the leaned axis.
* **0.9 of the grip for the lean**: at 40 km/h full steering scrubbed the speed away (to 3.5 km/h) as the tyres slid;
  0.8 holds 0.83 g there.
* **Countersteering and the gyroscopic and trail dynamics of a real bicycle** (a self-stable model, Whipple's): a rider
  model that games can steer with a stick; a simulator's bike is a different node.
* **Camber thrust** (a leaned tyre's force without slip angle): the tyre model has slip only; the front wheel's angle
  makes up for it.
