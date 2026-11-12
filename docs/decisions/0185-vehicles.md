# 0185 — Vehicles: wheels on rays, a tyre's curve, an engine and its gearbox

Status: **KEEP** (two new nodes; without them nothing changes) · Branch `fellow/geometry` · Code:
`modules/fellow_world/fellow_vehicle_3d.{h,cpp}` (`FellowVehicle3D`, `FellowWheel3D`),
`modules/fellow_editor/fellow_editor_plugin.{h,cpp}` (the wheel's gizmo, "Add Car") · Checks:
`benchmarks/project/bench/vehicle_check.gd` (fifteen lanes, `--many=N`), the regression scene `vehicle`, the editor
check; the laptop (job f396).

## Problem

The readiness assessment's physics row said "no vehicles beyond stock". Stock's `VehicleBody3D` (Bullet's raycast
vehicle) takes an engine force and a brake force per wheel and a friction slip: no engine with its revolutions, no
gears, differential or assists, no tyre curve combining the slips along and across the wheel; its wheels creep
sideways under a steady load and it has no hold at a standstill. A driving game builds all that on top of it.

## Change

`FellowVehicle3D` (a `RigidBody3D`) drives on its `FellowWheel3D` children, after each physics step (in the body's
state callback, as `VehicleBody3D` does), on GodotPhysics3D and Jolt alike:

* **The ground**: a ray per wheel down its suspension from its mount (the node marks the wheel's centre with the
  suspension fully extended). A physics body's velocity at the contact is taken out of the slips (a moving platform,
  a conveyor, a turntable); a rigid one is pushed back by the wheel's forces; the tyre's grip is `grip` times the
  ground's friction (its physics material's).
* **Substeps** (`substeps` 4): each reads the body's velocities once, works out every wheel's forces from them and
  applies them together (a wheel never sees another's impulse first).
* **The suspension**: a spring over the compression, a damper over its rate (compression and rebound), an anti-roll bar
  between the wheels of an axle (paired by place), a bump stop past the travel (ten times the spring).
* **The tyre**: the slip along the wheel (its rolling speed against the ground's) and across it (the slip angle's
  tangent), each over the speed (at least 1.5 m/s), combined; the force from a magic-formula curve (rising to its peak
  at 0.13, 0.8 of it at 0.5, 0.7 sliding) times the grip and the load, shared between the two directions as the slips
  are. The impulse is at most what stops the contact's slip in the substep, shared by the wheels on the ground (stiff
  tyres at walking pace would otherwise overshoot).
* **The wheel's spin**, implicitly: the w' with w' = w + h/I (drive − r·pull(w') − brake·sign w') — the left side less
  the right grows with w' (the curve falls slower past its peak than a wheel's inertia allows in a substep), so the
  root is bracketed and found by Newton's steps inside the bracket; the brake holds a stopped wheel when it can.
* **At a standstill** (under 0.3 m/s at the contact along and across) the tyre holds where it stands: a stiff spring
  (the tyre's whole grip at 3 cm) to an anchor on the ground (in the ground body's space: carried with it) across the
  wheel and, while the brakes hold the wheel against its pull, along it (a free wheel's anchor rolls along with it: a
  car without its brakes rolls down a slope); past the grip it lets go.
  Without it a steady load (a slope, a turning platform) is held only by a creeping slip.
* **The engine**: its revolutions from the driven wheels (not under `idle_rpm`), its torque from a curve over them
  (built in, or `torque_curve`), none at `max_rpm` (the limiter), its friction when the throttle lifts
  (`engine_braking`), its inertia felt through the gears at the driven wheels (`engine_inertia` 0.25 kg·m²: 22 kg·m²
  at each rear wheel in first). The tyres' rolling resistance (`rolling_resistance`, 0.012 of the load) with the
  brakes.
* **The gearbox** (`automatic`), from the road speed: up past `shift_up_rpm` at full throttle, earlier with less (from
  1.5 times `shift_down_rpm` with none, never so early that the next gear would be under the shift down), or with the
  engine there and its wheels spinning; down under `shift_down_rpm`, or at once with the throttle floored (kickdown),
  when the lower gear would not be past its shift up; `shift_time` without drive, at least 1.5 s in a gear; reverse
  after 0.5 s stopped with the brake held (not with the handbrake on), the brake then driving it backwards up to
  `max_reverse_speed`; `set_gear()` by hand. The differential open (the torque shared equally) or locked (an axle's driven wheels turning
  together).
* **The assists**: `traction_control` keeps a driven wheel's slip under 0.15 (the drive cut, the wheel braked);
  `anti_lock_brakes` keeps a braked wheel from locking past it (not with the handbrake).
* **The steering**, eased (`steer_speed`), less at speed (`steer_at_speed` of `max_steer` at 100 km/h); **the air**:
  drag and downforce over the speed squared. The body's damping is replaced by none (the project's default 0.1 a
  second would be a second drag: 2 m/s² at 70 km/h).
* The wheels' nodes placed down their suspension, steered and turned by their spin; `get_stats()` (speed, revolutions,
  gear, each wheel's load, slips, force, contact, hold), `get_global_stats()`; signal `gear_changed`. The FellowEngine
  menu's "Add Car"; the wheel's gizmo (its circle and its suspension's travel).

## Checks (cloud: the CPU, lavapipe)

`bench/vehicle_check.gd`, the editor build at 60 physics frames a second, each lane a 1 400 kg rear-driven car with the
defaults (400 N·m, six gears, 0.34 m wheels on 0.3 m of suspension, grip 1, the assists on) driven by a program;
GodotPhysics3D and Jolt:

| lane | expected | GodotPhysics3D | Jolt |
|---|---|---|---|
| full throttle from rest | 0-100 km/h in 5-12 s, up the gears | 6.27 s (90 m); 53 km/h at 3 s; up to sixth, 206 km/h at 20 s | the same |
| a stop from 100 km/h | 30-60 m, straight | 40.1 m, the heading unchanged | the same |
| a steady steer, the speed rising | 0.6-1.2 g | 0.96 g (the tyres' grip 1.0) | the same |
| a lane change at 70 km/h | roll < 10°, heading back < 15° | roll 2.0°, 6.0° off, 69 km/h | the same |
| ten 10 cm bumps at 40 km/h | within the travel, pitch < 8° | 0.19 m of 0.3, 0.6° | the same |
| a handbrake turn at 50 km/h | the rear slides | 89° (spun round), 2.1 rad/s | the same |
| reversing (the brake held at a stop) | backwards in reverse | −28.7 km/h (`max_reverse_speed`) | the same |
| a jump off a 1.5 m ramp at 60 km/h | in the air, upright | 61 frames in the air, 2.34 m up, upright | 61, 2.36 m |
| parked on a turntable (0.5 rad/s, 3 m off its axis) | carried where it stands | 0.0 mm off in 745° | the same |
| driving off a 3 000 kg slab on frictionless ice | the slab pushed back | car −2 820, slab +2 633 kg·m/s (7 %) | −2 818, +2 666 |
| parked on a 12° slope, the handbrake on, not asleep | held | 0.0 mm in 20 s | the same |
| the same slope, no brake | rolls back | 20.3 m in 5 s | 20.2 m |
| full throttle on ice (friction 0.25) | a quarter of the speed | 13.9 km/h at 3 s (dry: 53) | the same |
| coasting from 80 km/h (third) | the engine's braking, the air's | 0.55 m/s² | the same |

The release template: the same results. The first version failed five lanes; what each fix changed:

* **The wheels' forces one after another** (each saw the impulses of those before it): at 30 % throttle the car turned
  1.3° to the left in 10 s and its front tyres pushed outwards against each other (260 N). All from the same
  velocities: straight, none.
* **The spin integrated explicitly**: a tyre's slope at walking pace (17 000 N per rad/s at the contact) over a
  wheel's 1.2 kg·m² in a 4 ms substep — the spin oscillated (slip ratios +0.74 and −0.43 in successive frames), the
  gearbox shifted 2-1-2-1, full throttle reached 25 km/h, the skidpad 0.24 g. Solved implicitly: the slips smooth, the
  lanes as above.
* **The project's damping** (0.1 a second, on every rigid body): 0-100 km/h 9.5 s; replaced by none: 6.2 s.
* **The assists as a cut from the last substep's slip** switched the drive on and off each substep at walking pace;
  as limits on the solved spin: none.
* **The tyre's impulse limit alone at a standstill** lets a steady load through only with a slip: a car parked on the
  turntable crept 0.23 m in two turns. With the anchor: 0.0 mm (and 0.0 on the slope without sleeping).
* **Reverse at once** with the brake held at a stop: the car parked on the turntable drove off it backwards. Half a
  second, and not with the handbrake on.

A vehicle's step (four rays, four substeps): 8.1 µs on the cloud (editor build; 4.7 µs with one substep; the wheels'
frames on the ground worked out once a step, not each substep: 9.5 before). Many cars driving circles at once (the
release template, the CPU's main thread):

| | 64 cars | 256 cars |
|---|---|---|
| GodotPhysics3D | 0.65 ms a frame (10 µs each) | 2.9 ms (11 µs) |
| Jolt | 0.80 ms (12 µs) | 3.2 ms (12 µs) |

Lavapipe: the lanes filmed (each car followed). The regression scene `vehicle` (a car turning hard under throttle,
one parked across a slope with a wheel on a block): the same picture on every run and from the release template.

## Results on the laptop (job f396, release template)

Not run yet: the laptop was not linked to the session when the milestone was finished. Job f396 (the lanes with both
engines, many cars at once, the lanes filmed on the RTX) waits for it.

## Decision

Keep. A car that drives, brakes, corners, parks and lands like one on both physics engines, from two nodes with
defaults a game can start from; stock's `VehicleBody3D` stays as it was.

## Rejected / not done

* **Shapes cast for the wheels** (a ray per wheel: a wheel can drop into a gap narrower than itself and meets a kerb's
  edge as a step); the wheels have no colliders of their own (the body's shapes meet walls).
* **Tyre temperature and wear, surface effects** (skid marks, dust, sounds): `get_stats()` gives the slips and loads
  they would be made from.
* **Two-wheelers, tracks, a clutch by hand, damage.**
