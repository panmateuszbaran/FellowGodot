# 0274 — Skid steering and tracks for FellowVehicle3D

Status: **KEEP** (opt-in per node) · Branch `fellow/geometry` · Code: `modules/fellow_world/fellow_vehicle_3d.{h,cpp}`
(`steering_mode`, `skid_turn_drive`, `tracks`; `get_stats()`'s `skid`, `skid_ratio`), the FellowEngine menu's "Add
Tracked Vehicle" (`modules/fellow_editor/fellow_editor_plugin.cpp`) · Checks: `bench/vehicle_check.gd` (six new
lanes, `--many=32 --tank=1`), the editor check's presets.

## Problem

`FellowVehicle3D` (decision 0185) steers by turning its steering wheels: a car or a truck. The readiness assessment
listed what it cannot drive: no tracks, no skid steering — a tank, a tracked carrier, a skid-steer loader, a six-wheeled
armoured car that turns on the spot. A vehicle without steering wheels could be driven only by a game's own torques on
the body.

## Change

* **`steering_mode`**: `STEERING_WHEELS` (as before) or `STEERING_SKID`. With skid steering no wheel turns (toe kept);
  a steering gear holds the inner side's mean wheel speed at `k` times the outer's (left of `X = 0` the left side; the
  steering positive to the left, the left side inside):
  * `k = 1 - |steering|·(2 - throttle)`: 1 straight (the two sides held together: the vehicle goes straight whatever
    the sides' loads), with the throttle held full steering turns about the inner side (`k = 0`), without it on the
    spot (`k = -1`);
  * at speed the steering asks for a share of the tightest turn the grip holds: a radius at least
    `v² / (0.7·μ·g·|steering|)` (μ the wheels' mean grip times the ground's friction) — full steering that turn, half
    steering twice its radius;
  * the gear is a constraint between the two sides' spins after every substep, by equal and opposite turning impulses
    through it (`λ` on the inner side, `-k·λ` on the outer: its power conserved). The engine drives `q`, the outer
    side's speed (its revolutions from the sides' best fit to `q` and `k·q`), the outer wheels getting
    `T / (n_out + k²·n_in)` each and the inner ones `k` times it;
  * the steering gives a throttle of its own, `skid_turn_drive` (0.6) at full steering, near a standstill (fading out
    by 4 m/s): the steering alone turns the vehicle on the spot. A locked differential does not apply (the sides are
    driven apart).
* **`tracks`**: each side's wheels turn together, as a track links them — their spins share their momentum (inertia
  weighted) after every substep. The wheels' tyres stand for the track's contact; road wheels without drive turn with
  the driven sprocket.
* A configuration warning when skid steering has no driven wheel on a side; the FellowEngine menu's "Add Tracked
  Vehicle" (2 000 kg, three driven wheels a side 2.4 m long and 2.3 m apart, tracks, final drive 14: about 85 km/h).

## Result

`vehicle_check.gd`'s tracked vehicle (the preset's, lavapipe's machine, headless, GodotPhysics3D):

| Lane | Result |
|---|---|
| straight, 0.7 throttle for 9 s | 83 km/h, heading changed 0.000°, each side's wheels at one speed (0 rpm apart) |
| steering alone, full left / right, 6 s | 26.1°/s on the spot (moved 1.9 cm), the sides at -21 / +21 rpm, 147.0° / -147.0° |
| 30 km/h, half / full steering | radius 35.7 / 15.3 m (asked 19.1 / 9.4 m), 0.19 / 0.43 g, sideslip 0.8° / 3.3° |
| 60 km/h, full steering | radius 72.5 m (asked 39.1 m), 0.38 g, sideslip 1.3° |

The tracks slip under the turning moment, so the vehicle turns wider than the gear asks (1.6-1.9 times), as tracked
vehicles do. Each of the car's 31 lanes as before (every value the same but the dents' timing). 32 vehicles driving
circles: a tracked one's step 15.7-17.9 µs (six wheels), a car's 12.8 µs (four).

## Decision

KEEP, opt-in (`steering_mode` `STEERING_WHEELS` by default; `tracks` off): a car is unchanged.

## Rejected / not done

* **The steering as torque alone** (the inner side driven less, then backwards; the first version): without a gear
  holding the sides' speeds, the vehicle turned on the spot at 10°/s, its sides' wheels each at their own speed — the
  torque balanced by the tyres' resistance near a standstill, whatever the steering. A skid-steered vehicle's sides
  are always held at speeds by something (a steering differential, clutches and brakes, a hydrostatic pump a side).
* **`k = 1 - 2·|steering|` at every speed, the turn capped by the whole grip** (the second): at 30 km/h half steering
  spun the vehicle out (the sideslip growing to 32° as the cap loosened with the speed it lost); the turn is now a share
  of what 0.7 of the grip holds (skid steering spends the rest on the tracks' push and drag) and the steering a share
  of it (1.0 of the grip still spun it out; 0.7 held: 0.43 g at 30 km/h).
* **A track's own geometry** (its links, its sag over the road wheels, its contact patch as a strip): the wheels'
  rays or cylinders stand for it; a visible track is the game's mesh, animated by the wheels' `get_rpm()`.
* **Two-wheelers**: not in this change (decision 0275).
